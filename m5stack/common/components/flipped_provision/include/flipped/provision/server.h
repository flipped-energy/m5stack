#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "esp_err.h"

#include "flipped/provision/session.h"

struct httpd_req;

namespace flipped::provision {

struct Outcome {
    int status = 0;
    std::string text;
    State state = State::open;
};

struct Start {
    bool started = false;
    std::string url;
    std::string error;
};

class Server {
public:
    using OnOutcome = std::function<void(const Outcome &outcome)>;

    explicit Server(OnOutcome onOutcome);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    Start start(std::string_view ipv4);
    void stop();
    bool running() const { return handle_ != nullptr; }

private:
    static esp_err_t submit(httpd_req *request);
    esp_err_t handleSubmit(httpd_req *request);

    OnOutcome onOutcome_;
    void *handle_ = nullptr;
    std::unique_ptr<Session> session_;
};

}
