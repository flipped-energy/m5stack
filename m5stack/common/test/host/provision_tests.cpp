#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "flipped/provision/session.h"
#include "suite.h"

namespace provision = flipped::provision;

namespace {

struct Vector {
    std::string ipv4;
    provision::Secret secret{};
    std::string url;
    std::string token;
    std::string body;
};

struct Recorder {
    std::vector<std::string> tokens;
    flipped::core::ConfigResult result{true, "token stored"};

    provision::SetToken setter()
    {
        return [this](std::string_view token) {
            tokens.emplace_back(token);
            return result;
        };
    }
};

std::optional<Vector> loadVector(const char *path, std::string &problem)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        problem = std::string("cannot open ") + path;
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, text.str());
    if (error) {
        problem = std::string(path) + ": " + error.c_str();
        return std::nullopt;
    }
    Vector vector;
    vector.ipv4 = document["ipv4"].as<std::string>();
    vector.url = document["url"].as<std::string>();
    vector.token = document["token"].as<std::string>();
    vector.body = document["body"].as<std::string>();
    const std::string secret = document["secret"].as<std::string>();
    if (secret.size() != 2 * provision::SECRET_BYTES) {
        problem = "secret holds " + std::to_string(secret.size()) + " hex digits";
        return std::nullopt;
    }
    for (size_t i = 0; i < provision::SECRET_BYTES; ++i) {
        vector.secret[i] = static_cast<uint8_t>(std::stoul(secret.substr(2 * i, 2), nullptr, 16));
    }
    return vector;
}

provision::Request request(const Vector &vector, std::string_view body)
{
    static const std::string origin = "http://" + vector.ipv4;
    provision::Request out;
    out.host = vector.ipv4;
    out.origin = origin;
    out.contentLength = body.size();
    out.body = body;
    return out;
}

std::string hexOf(std::string_view bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (const char ch : bytes) {
        const auto value = static_cast<unsigned char>(ch);
        out += digits[value >> 4];
        out += digits[value & 0x0F];
    }
    return out;
}

std::string bodyWith(std::string_view auth, size_t tokenBytes)
{
    return "a=" + std::string(auth) + "&c=" + hexOf(std::string(tokenBytes, 'x'));
}

std::string expect(bool ok, const std::string &what)
{
    return ok ? std::string() : what;
}

std::string replyIs(const provision::Reply &reply, int status, const std::string &text)
{
    if (reply.status == status && reply.text == text) {
        return {};
    }
    return "reply " + std::to_string(reply.status) + " '" + reply.text + "', want " + std::to_string(status) + " '" + text + "'";
}

const std::string WRONG_AUTH = "0000000000000000";

}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <token_vector.json>\n", argv[0]);
        return 2;
    }
    std::string problem;
    const std::optional<Vector> loaded = loadVector(argv[1], problem);
    if (!loaded) {
        std::printf("provision_tests: %s\n", problem.c_str());
        return 1;
    }
    const Vector &vector = *loaded;
    const std::string auth = vector.body.substr(2, 16);
    Suite suite("provision_tests");

    suite.run("url of the fixed vector", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        return expect(session.url() == vector.url, "url '" + session.url() + "', want '" + vector.url + "'");
    });

    suite.run("url length and alphabet", [&]() {
        for (const char *address : {"1.2.3.4", "192.168.100.200"}) {
            provision::Session session(address, vector.secret);
            const std::string &url = session.url();
            const size_t hash = url.find('#');
            const size_t want = std::string_view(address).size() == 7 ? 92 : 100;
            if (url.size() != want) {
                return "url of " + std::string(address) + " is " + std::to_string(url.size()) + " characters, want " +
                       std::to_string(want);
            }
            if (url.compare(0, hash, "http://" + std::string(address) + "/t") != 0 || url.size() - hash - 1 != 75) {
                return "url '" + url + "' is not http://<address>/t# and 75 characters";
            }
            for (size_t i = hash + 1; i < url.size(); ++i) {
                const char ch = url[i];
                const bool allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                                     ch == '-' || ch == '_';
                if (!allowed) {
                    return "url '" + url + "' has '" + std::string(1, ch) + "' after the '#'";
                }
            }
        }
        return std::string();
    });

    suite.run("decrypt of the fixed vector", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        const provision::Reply reply = session.post(request(vector, vector.body), recorder.setter());
        if (std::string why = replyIs(reply, 200, provision::SAVED_TEXT); !why.empty()) {
            return why;
        }
        if (recorder.tokens != std::vector<std::string>{vector.token}) {
            return "setToken got " + std::to_string(recorder.tokens.size()) + " values, want '" + vector.token + "'";
        }
        return expect(session.state() == provision::State::consumed && session.url().empty(),
                      "the session is not consumed and wiped");
    });

    suite.run("a token refused by setToken answers 422 with its text verbatim", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        recorder.result = {false, "token contains byte 0x20 at position 7, outside 0x21..0x7E"};
        const provision::Reply reply = session.post(request(vector, vector.body), recorder.setter());
        if (std::string why = replyIs(reply, 422, recorder.result.text); !why.empty()) {
            return why;
        }
        return expect(session.state() == provision::State::consumed, "the session is not consumed");
    });

    suite.run("wrong auth leaves the pad intact and counts", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        const std::string wrong = "a=" + WRONG_AUTH + vector.body.substr(18);
        const provision::Reply refused = session.post(request(vector, wrong), recorder.setter());
        if (std::string why = replyIs(refused, 403, "Wrong code, refusal 1 of 5."); !why.empty()) {
            return why;
        }
        if (session.refusals() != 1 || session.state() != provision::State::open || session.url() != vector.url) {
            return std::string("the refusal was not counted or changed the session");
        }
        const provision::Reply accepted = session.post(request(vector, vector.body), recorder.setter());
        if (std::string why = replyIs(accepted, 200, provision::SAVED_TEXT); !why.empty()) {
            return why;
        }
        return expect(recorder.tokens == std::vector<std::string>{vector.token}, "the pad did not decrypt after a refusal");
    });

    suite.run("five refusals end the session", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        const std::string wrong = "a=" + WRONG_AUTH + vector.body.substr(18);
        for (int i = 1; i <= 4; ++i) {
            const provision::Reply reply = session.post(request(vector, wrong), recorder.setter());
            if (std::string why = replyIs(reply, 403, "Wrong code, refusal " + std::to_string(i) + " of 5."); !why.empty()) {
                return why;
            }
        }
        const provision::Reply last = session.post(request(vector, wrong), recorder.setter());
        if (std::string why = replyIs(last, 403, "Wrong code, refusal 5 of 5. Setup page closed."); !why.empty()) {
            return why;
        }
        if (session.state() != provision::State::closed || !session.url().empty()) {
            return std::string("the session is not closed and wiped");
        }
        const provision::Reply after = session.post(request(vector, vector.body), recorder.setter());
        if (std::string why = replyIs(after, 403, provision::CLOSED_TEXT); !why.empty()) {
            return why;
        }
        return expect(recorder.tokens.empty(), "setToken was called on a closed session");
    });

    suite.run("a second submission after success is refused", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        session.post(request(vector, vector.body), recorder.setter());
        const provision::Reply second = session.post(request(vector, vector.body), recorder.setter());
        if (std::string why = replyIs(second, 403, provision::CLOSED_TEXT); !why.empty()) {
            return why;
        }
        return expect(recorder.tokens.size() == 1, "setToken was called twice");
    });

    suite.run("a token shorter than 5 or longer than 48 bytes is refused", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        const std::string shortBody = bodyWith(auth, 4);
        const std::string longBody = bodyWith(auth, 49);
        if (std::string why = replyIs(session.post(request(vector, shortBody), recorder.setter()), 400,
                                      "Token is 4 bytes, not 5 to 48.");
            !why.empty()) {
            return why;
        }
        if (std::string why = replyIs(session.post(request(vector, longBody), recorder.setter()), 400,
                                      "Token is 49 bytes, not 5 to 48.");
            !why.empty()) {
            return why;
        }
        const std::string five = bodyWith(auth, 5);
        if (std::string why = replyIs(session.post(request(vector, five), recorder.setter()), 200, provision::SAVED_TEXT);
            !why.empty()) {
            return "five bytes: " + why;
        }
        return expect(recorder.tokens.size() == 1 && recorder.tokens[0].size() == 5, "setToken did not get the 5-byte token");
    });

    suite.run("a body that is too long or malformed is refused without a count", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        provision::Request oversized = request(vector, vector.body);
        oversized.contentLength = 161;
        if (std::string why = replyIs(session.post(oversized, recorder.setter()), 400, "Body is 161 bytes, more than 160.");
            !why.empty()) {
            return why;
        }
        const std::string malformed = "Body is not a=<16 hex digits>&c=<hex digits, two per byte>.";
        for (const std::string &body : {std::string("c=00&a=00"), "a=" + auth + "&c=0", "a=" + auth.substr(1) + "&c=0000000000",
                                        "a=" + auth + "&c=zz00000000", "a=" + auth + "&d=0000000000", std::string()}) {
            if (std::string why = replyIs(session.post(request(vector, body), recorder.setter()), 400, malformed);
                !why.empty()) {
                return "'" + body + "': " + why;
            }
        }
        return expect(session.refusals() == 0 && session.state() == provision::State::open && recorder.tokens.empty(),
                      "a malformed body changed the session");
    });

    suite.run("Host must equal the device address", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        provision::Request missing = request(vector, vector.body);
        missing.host.reset();
        if (std::string why = replyIs(session.post(missing, recorder.setter()), 400, "Host is missing, not '192.168.1.23'.");
            !why.empty()) {
            return why;
        }
        provision::Request rebound = request(vector, vector.body);
        rebound.host = std::string_view("attacker.example");
        if (std::string why = replyIs(session.post(rebound, recorder.setter()), 400,
                                      "Host is 'attacker.example', not '192.168.1.23'.");
            !why.empty()) {
            return why;
        }
        return expect(session.refusals() == 0 && session.state() == provision::State::open && recorder.tokens.empty(),
                      "a Host refusal changed the session");
    });

    suite.run("Origin, when present, must be http://<address>", [&]() {
        provision::Session session(vector.ipv4, vector.secret);
        Recorder recorder;
        provision::Request foreign = request(vector, vector.body);
        foreign.origin = std::string_view("https://attacker.example");
        if (std::string why = replyIs(session.post(foreign, recorder.setter()), 403,
                                      "Origin is 'https://attacker.example', not 'http://192.168.1.23'.");
            !why.empty()) {
            return why;
        }
        if (session.refusals() != 0 || session.state() != provision::State::open) {
            return std::string("an Origin refusal changed the session");
        }
        provision::Request without = request(vector, vector.body);
        without.origin.reset();
        if (std::string why = replyIs(session.post(without, recorder.setter()), 200, provision::SAVED_TEXT); !why.empty()) {
            return "without Origin: " + why;
        }
        return expect(recorder.tokens == std::vector<std::string>{vector.token}, "setToken did not get the token");
    });

    suite.run("status lines", [&]() {
        for (const auto &[status, line] : std::vector<std::pair<int, std::string>>{
                 {200, "200 OK"}, {400, "400 Bad Request"}, {403, "403 Forbidden"}, {404, "404 Not Found"},
                 {422, "422 Unprocessable Content"}}) {
            if (provision::statusLine(status) != line) {
                return "status " + std::to_string(status) + " gives '" + provision::statusLine(status) + "'";
            }
        }
        return std::string();
    });

    suite.run("base64url without padding", [&]() {
        const uint8_t bytes[] = {0xFB, 0xFF, 0xBF, 0x00};
        const std::string got = provision::base64url(bytes, 1) + "," + provision::base64url(bytes, 2) + "," +
                                provision::base64url(bytes, 3) + "," + provision::base64url(bytes, 4);
        return expect(got == "-w,-_8,-_-_,-_-_AA", "got '" + got + "'");
    });

    const bool passed = suite.finish();
    std::printf("provision_tests: %s\n", passed ? "passed" : "FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
