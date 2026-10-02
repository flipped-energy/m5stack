#include "flipped/provision/server.h"

#include <array>
#include <cstdlib>
#include <optional>
#include <utility>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"

#include "flipped/app/app.h"

extern const char token_html_start[] asm("_binary_token_html_start");
extern const char token_html_end[] asm("_binary_token_html_end");

namespace flipped::provision {

namespace {

constexpr const char *TAG = "flipped_provision";
constexpr const char *CONTENT_SECURITY_POLICY = "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                                                "connect-src 'self'; base-uri 'none'; form-action 'none'";

void setCommonHeaders(httpd_req_t *request)
{
    ESP_ERROR_CHECK(httpd_resp_set_hdr(request, "Cache-Control", "no-store"));
    ESP_ERROR_CHECK(httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer"));
    ESP_ERROR_CHECK(httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff"));
}

std::optional<std::string> header(httpd_req_t *request, const char *field)
{
    const size_t size = httpd_req_get_hdr_value_len(request, field);
    if (size == 0) {
        return std::nullopt;
    }
    std::string value(size + 1, '\0');
    ESP_ERROR_CHECK(httpd_req_get_hdr_value_str(request, field, value.data(), value.size()));
    value.resize(size);
    return value;
}

esp_err_t page(httpd_req_t *request)
{
    setCommonHeaders(request);
    ESP_ERROR_CHECK(httpd_resp_set_hdr(request, "Content-Security-Policy", CONTENT_SECURITY_POLICY));
    ESP_ERROR_CHECK(httpd_resp_set_type(request, "text/html; charset=utf-8"));
    const esp_err_t sent = httpd_resp_send(request, token_html_start, token_html_end - token_html_start - 1);
    if (sent != ESP_OK) {
        ESP_LOGE(TAG, "GET %s: httpd_resp_send %s", PAGE_PATH, esp_err_to_name(sent));
    }
    return sent;
}

esp_err_t notFound(httpd_req_t *request, httpd_err_code_t error)
{
    ESP_LOGI(TAG, "%s: httpd error %d answered 404", request->uri, static_cast<int>(error));
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, nullptr);
}

}

Server::Server(OnOutcome onOutcome) : onOutcome_(std::move(onOutcome)) {}

Server::~Server() { stop(); }

Start Server::start(std::string_view ipv4)
{
    if (handle_ != nullptr) {
        ESP_LOGE(TAG, "start while a session is running");
        abort();
    }
    Secret secret;
    esp_fill_random(secret.data(), secret.size());
    session_ = std::make_unique<Session>(ipv4, secret);
    mbedtls_platform_zeroize(secret.data(), secret.size());

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 2;
    httpd_handle_t handle = nullptr;
    const esp_err_t started = httpd_start(&handle, &config);
    if (started != ESP_OK) {
        session_.reset();
        return Start{false, {}, std::string("httpd_start ") + esp_err_to_name(started)};
    }
    const httpd_uri_t get = {.uri = PAGE_PATH, .method = HTTP_GET, .handler = page, .user_ctx = nullptr};
    const httpd_uri_t post = {.uri = PAGE_PATH, .method = HTTP_POST, .handler = submit, .user_ctx = this};
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(handle, &post));
    ESP_ERROR_CHECK(httpd_register_err_handler(handle, HTTPD_405_METHOD_NOT_ALLOWED, notFound));
    handle_ = handle;
    ESP_LOGI(TAG, "session started on %s port %u", session_->address().c_str(), static_cast<unsigned>(config.server_port));
    return Start{true, session_->url(), {}};
}

void Server::stop()
{
    if (handle_ == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(httpd_stop(handle_));
    handle_ = nullptr;
    session_.reset();
    ESP_LOGI(TAG, "session stopped");
}

esp_err_t Server::submit(httpd_req *request)
{
    return static_cast<Server *>(request->user_ctx)->handleSubmit(request);
}

esp_err_t Server::handleSubmit(httpd_req *request)
{
    const std::optional<std::string> host = header(request, "Host");
    const std::optional<std::string> origin = header(request, "Origin");
    std::array<char, MAX_BODY_BYTES> body{};
    size_t received = 0;
    if (request->content_len <= body.size()) {
        while (received < request->content_len) {
            const int got = httpd_req_recv(request, body.data() + received, request->content_len - received);
            if (got <= 0) {
                ESP_LOGE(TAG, "POST %s: httpd_req_recv returned %d after %u of %u bytes", PAGE_PATH, got,
                         static_cast<unsigned>(received), static_cast<unsigned>(request->content_len));
                mbedtls_platform_zeroize(body.data(), body.size());
                return ESP_FAIL;
            }
            received += static_cast<size_t>(got);
        }
    }
    Request input;
    if (host) {
        input.host = *host;
    }
    if (origin) {
        input.origin = *origin;
    }
    input.contentLength = request->content_len;
    input.body = std::string_view(body.data(), received);
    const Reply reply = session_->post(input, [](std::string_view token) { return flipped::app::setToken(token); });
    mbedtls_platform_zeroize(body.data(), body.size());

    setCommonHeaders(request);
    ESP_ERROR_CHECK(httpd_resp_set_status(request, statusLine(reply.status)));
    ESP_ERROR_CHECK(httpd_resp_set_type(request, "text/plain; charset=utf-8"));
    const esp_err_t sent = httpd_resp_send(request, reply.text.data(), reply.text.size());
    if (sent != ESP_OK) {
        ESP_LOGE(TAG, "POST %s: httpd_resp_send %s", PAGE_PATH, esp_err_to_name(sent));
    }
    ESP_LOGI(TAG, "POST %s: %d %s", PAGE_PATH, reply.status, reply.text.c_str());
    onOutcome_(Outcome{reply.status, reply.text, session_->state()});
    return sent;
}

}
