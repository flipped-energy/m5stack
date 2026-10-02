#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "flipped/core/platform.h"

struct esp_http_client;

namespace flipped::net {

struct TaskMemory {
    uint32_t stackBytes;
    uint32_t caps;
};

void setTaskMemory(TaskMemory memory);

class HttpClient {
public:
    using Sink = std::function<void(core::RequestId id, core::Response response)>;

    HttpClient(std::string userAgent, Sink sink);
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    void start();
    void get(core::RequestId id, const core::Request &request, const std::string &token);
    TaskHandle_t task() const { return task_; }

private:
    struct Job;

    static void run(void *self);
    void loop();
    core::Response perform(const Job &job);
    void closeConnection();

    std::string userAgent_;
    Sink sink_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    esp_http_client *client_ = nullptr;
};

}
