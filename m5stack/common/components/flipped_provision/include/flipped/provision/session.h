#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "flipped/core/config_check.h"

namespace flipped::provision {

inline constexpr size_t AUTH_BYTES = 8;
inline constexpr size_t PAD_BYTES = 48;
inline constexpr size_t SECRET_BYTES = AUTH_BYTES + PAD_BYTES;
inline constexpr size_t MIN_TOKEN_BYTES = 5;
inline constexpr size_t MAX_BODY_BYTES = 160;
inline constexpr int MAX_REFUSALS = 5;
inline constexpr uint32_t SESSION_SECONDS = 600;
inline constexpr const char *PAGE_PATH = "/t";
inline constexpr const char *SAVED_TEXT = "Token saved. Look at the device.";
inline constexpr const char *CLOSED_TEXT = "Setup page closed.";

using Secret = std::array<uint8_t, SECRET_BYTES>;
using SetToken = std::function<core::ConfigResult(std::string_view token)>;

struct Request {
    std::optional<std::string_view> host;
    std::optional<std::string_view> origin;
    size_t contentLength = 0;
    std::string_view body;
};

struct Reply {
    int status = 0;
    std::string text;
};

enum class State { open, consumed, closed };

class Session {
public:
    Session(std::string_view ipv4, const Secret &secret);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    const std::string &url() const { return url_; }
    const std::string &address() const { return address_; }
    State state() const { return state_; }
    int refusals() const { return refusals_; }

    Reply post(const Request &request, const SetToken &setToken);
    void close();

private:
    std::optional<Reply> refuseRequest(const Request &request) const;
    void wipe();

    std::string address_;
    std::string origin_;
    std::string url_;
    Secret secret_{};
    State state_ = State::open;
    int refusals_ = 0;
};

void zeroize(void *data, size_t size);
std::string base64url(const uint8_t *data, size_t size);
const char *statusLine(int status);

}
