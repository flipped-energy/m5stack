#include "flipped/net/http_client.h"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "flipped/core/constants.h"

namespace flipped::net {

namespace {

const char *TAG = "flipped_http";
constexpr UBaseType_t PRIORITY = 1;
constexpr BaseType_t CORE = 0;
constexpr UBaseType_t QUEUE_LENGTH = 8;
constexpr size_t BODY_LIMIT_BYTES = 1048576;
constexpr size_t CHUNK_BYTES = 512;

TaskMemory &taskMemory()
{
    static TaskMemory memory{10240, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT};
    return memory;
}

struct Headers {
    std::optional<std::string> retryAfter;
    std::optional<std::string> location;
    std::optional<std::string> dailyLimitRemaining;
};

esp_err_t onEvent(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }
    auto *headers = static_cast<Headers *>(event->user_data);
    if (strcasecmp(event->header_key, "Retry-After") == 0) {
        headers->retryAfter = std::string(event->header_value);
    } else if (strcasecmp(event->header_key, "Location") == 0) {
        headers->location = std::string(event->header_value);
    } else if (strcasecmp(event->header_key, "X-DailyLimit-Remaining") == 0) {
        headers->dailyLimitRemaining = std::string(event->header_value);
    }
    return ESP_OK;
}

std::string errnoText(esp_http_client *client)
{
    const int number = esp_http_client_get_errno(client);
    return "errno " + std::to_string(number) + " " + std::strerror(number);
}

std::string tlsText(esp_http_client *client)
{
    int code = 0;
    int flags = 0;
    const esp_err_t last = esp_http_client_get_and_clear_last_tls_error(client, &code, &flags);
    return std::string("esp-tls ") + esp_err_to_name(last) + ", tls code " + std::to_string(code) + ", tls flags " +
           std::to_string(flags);
}

class BodySource : public core::ByteSource {
public:
    explicit BodySource(esp_http_client *client) : client_(client) {}

    size_t read(uint8_t *buffer, size_t capacity) override
    {
        if (ended_) {
            return 0;
        }
        const size_t room = BODY_LIMIT_BYTES + 1 - total_;
        const int wanted = static_cast<int>(std::min({capacity, room, CHUNK_BYTES}));
        const int n = esp_http_client_read(client_, reinterpret_cast<char *>(buffer), wanted);
        if (n < 0) {
            failure_ = n;
            failureErrno_ = errnoText(client_);
            ended_ = true;
            return 0;
        }
        if (n == 0) {
            ended_ = true;
            return 0;
        }
        total_ += static_cast<size_t>(n);
        if (total_ > BODY_LIMIT_BYTES) {
            exceeded_ = true;
            ended_ = true;
            return 0;
        }
        return static_cast<size_t>(n);
    }

    void drain()
    {
        uint8_t scratch[CHUNK_BYTES];
        while (read(scratch, sizeof scratch) > 0) {
        }
    }

    bool exceeded() const { return exceeded_; }
    std::optional<int> failure() const { return failure_; }
    const std::string &failureErrno() const { return failureErrno_; }
    size_t total() const { return total_; }

private:
    esp_http_client *client_;
    size_t total_ = 0;
    bool ended_ = false;
    bool exceeded_ = false;
    std::optional<int> failure_;
    std::string failureErrno_;
};

}

struct HttpClient::Job {
    core::RequestId id = 0;
    core::Request request;
    std::string token;
};

void setTaskMemory(TaskMemory memory)
{
    taskMemory() = memory;
}

HttpClient::HttpClient(std::string userAgent, Sink sink) : userAgent_(std::move(userAgent)), sink_(std::move(sink))
{
    queue_ = xQueueCreate(QUEUE_LENGTH, sizeof(Job *));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "xQueueCreate returned null");
        abort();
    }
}

void HttpClient::start()
{
    if (task_ != nullptr) {
        ESP_LOGE(TAG, "started twice");
        abort();
    }
    const TaskMemory memory = taskMemory();
    if (xTaskCreatePinnedToCoreWithCaps(run, "flipped_http", memory.stackBytes, this, PRIORITY, &task_, CORE, memory.caps) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCoreWithCaps flipped_http failed: %" PRIu32 " bytes, caps 0x%" PRIx32, memory.stackBytes, memory.caps);
        abort();
    }
}

void HttpClient::get(core::RequestId id, const core::Request &request, const std::string &token)
{
    Job *job = new Job{id, request, token};
    if (xQueueSend(queue_, &job, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "xQueueSend to flipped_http failed");
        abort();
    }
}

void HttpClient::run(void *self)
{
    static_cast<HttpClient *>(self)->loop();
}

void HttpClient::loop()
{
    for (;;) {
        Job *received = nullptr;
        if (xQueueReceive(queue_, &received, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "xQueueReceive on flipped_http failed");
            abort();
        }
        std::unique_ptr<Job> job(received);
        core::Response response = perform(*job);
        closeConnection();
        sink_(job->id, std::move(response));
    }
}

void HttpClient::closeConnection()
{
    if (client_ == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(esp_http_client_cleanup(client_));
    client_ = nullptr;
}

core::Response HttpClient::perform(const Job &job)
{
    const std::string target = core::requestTarget(job.request);
    const std::string url = std::string(core::BASE_URL) + target;
    if (job.request.timeoutSeconds <= 0) {
        ESP_LOGE(TAG, "GET %s has timeout %lld s", target.c_str(), static_cast<long long>(job.request.timeoutSeconds));
        abort();
    }
    const int timeoutMs = static_cast<int>(job.request.timeoutSeconds * 1000);
    if (client_ == nullptr) {
        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = timeoutMs;
        config.disable_auto_redirect = true;
        config.user_agent = userAgent_.c_str();
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.event_handler = onEvent;
        client_ = esp_http_client_init(&config);
        if (client_ == nullptr) {
            ESP_LOGE(TAG, "esp_http_client_init returned null for GET %s", target.c_str());
            abort();
        }
    } else {
        ESP_ERROR_CHECK(esp_http_client_set_url(client_, url.c_str()));
        ESP_ERROR_CHECK(esp_http_client_set_timeout_ms(client_, timeoutMs));
    }
    const std::string authorization = "Bearer " + job.token;
    ESP_ERROR_CHECK(esp_http_client_set_header(client_, "Authorization", authorization.c_str()));
    ESP_ERROR_CHECK(esp_http_client_set_header(client_, "Accept", "*/*"));
    Headers headers;
    ESP_ERROR_CHECK(esp_http_client_set_user_data(client_, &headers));

    const esp_err_t opened = esp_http_client_open(client_, 0);
    if (opened != ESP_OK) {
        const std::string message =
            "GET " + target + ": esp_http_client_open " + esp_err_to_name(opened) + ", " + tlsText(client_);
        ESP_LOGE(TAG, "%s", message.c_str());
        return core::networkResponse(message);
    }
    const int64_t length = esp_http_client_fetch_headers(client_);
    if (length < 0) {
        const std::string message = "GET " + target + ": esp_http_client_fetch_headers returned " +
                                    std::to_string(length) + ", " + errnoText(client_) + ", " + tlsText(client_);
        ESP_LOGE(TAG, "%s", message.c_str());
        return core::networkResponse(message);
    }
    const int status = esp_http_client_get_status_code(client_);

    core::Response response;
    BodySource source(client_);
    if (status >= 200 && status < 300) {
        response = core::parsedResponse(job.request, status, source);
        source.drain();
    } else {
        std::string kept;
        uint8_t chunk[CHUNK_BYTES];
        for (;;) {
            const size_t before = source.total();
            const size_t n = source.read(chunk, sizeof chunk);
            if (n == 0) {
                break;
            }
            ESP_LOGW(TAG, "GET %s %d body bytes %zu..%zu: %.*s", target.c_str(), status, before, before + n,
                     static_cast<int>(n), reinterpret_cast<const char *>(chunk));
            if (kept.size() <= core::FIRMWARE_ERROR_BODY_MAX_BYTES) {
                kept.append(reinterpret_cast<const char *>(chunk),
                            std::min(n, core::FIRMWARE_ERROR_BODY_MAX_BYTES + 1 - kept.size()));
            }
        }
        response = core::errorResponse(status, core::keptErrorBody(kept), source.total());
    }

    if (source.exceeded()) {
        response = core::Response{};
        response.kind = core::ResponseKind::invalid;
        response.status = status;
        response.message =
            std::string(core::endpointPath(job.request.endpoint)) + " body exceeds " + std::to_string(BODY_LIMIT_BYTES) + " bytes";
        ESP_LOGE(TAG, "GET %s: %s", target.c_str(), response.message.c_str());
        return response;
    }
    if (source.failure()) {
        const std::string message = "GET " + target + ": answered " + std::to_string(status) +
                                    ", then esp_http_client_read returned " + std::to_string(*source.failure()) +
                                    " after " + std::to_string(source.total()) + " bytes, " + source.failureErrno();
        ESP_LOGE(TAG, "%s", message.c_str());
        return core::networkResponse(message);
    }
    if (!esp_http_client_is_complete_data_received(client_)) {
        const std::string message = "GET " + target + ": answered " + std::to_string(status) +
                                    ", then the connection closed after " + std::to_string(source.total()) +
                                    " bytes, before the end of the body";
        ESP_LOGE(TAG, "%s", message.c_str());
        return core::networkResponse(message);
    }
    response.retryAfter = headers.retryAfter;
    response.location = headers.location;
    response.dailyLimitRemaining = headers.dailyLimitRemaining;
    ESP_LOGI(TAG, "GET %s %d, %zu bytes", target.c_str(), status, source.total());
    return response;
}

}
