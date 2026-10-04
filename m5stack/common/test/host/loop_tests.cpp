#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <ArduinoJson.h>

#include "fake_platform.h"
#include "flipped/core/constants.h"
#include "flipped/core/platform.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"
#include "json_text.h"
#include "memory_source.h"
#include "suite.h"

using namespace flipped::core;

namespace {

constexpr size_t EVENT_LIMIT = 200000;

using Query = std::map<std::string, std::string>;

std::optional<std::string> stringOf(JsonVariantConst value)
{
    if (!value.is<const char *>()) {
        return std::nullopt;
    }
    return std::string(value.as<const char *>());
}

std::optional<Instant> instantOf(JsonVariantConst value)
{
    const std::optional<std::string> text = stringOf(value);
    if (!text) {
        return std::nullopt;
    }
    return parseInstant(*text).instant;
}

Query queryOf(JsonVariantConst value, std::string &problem)
{
    Query query;
    for (JsonPairConst pair : value.as<JsonObjectConst>()) {
        const std::optional<std::string> text = stringOf(pair.value());
        if (!text) {
            problem = std::string("query value of ") + pair.key().c_str() + " is not a string";
            return query;
        }
        query[pair.key().c_str()] = *text;
    }
    return query;
}

Query queryOf(const Request &request)
{
    Query query;
    for (const QueryParameter &parameter : request.query) {
        query[parameter.name] = parameter.value;
    }
    return query;
}

std::string describe(Instant at, const std::string &path, const Query &query)
{
    std::string text = formatInstant(at) + " " + path;
    char separator = '?';
    for (const auto &[name, value] : query) {
        text += separator + name + "=" + value;
        separator = '&';
    }
    return text;
}

std::string describe(const TimerRecord &record)
{
    return formatInstant(record.armedAt) + " " + record.timer + " -> " + formatInstant(record.target) + " (" +
           std::to_string(record.delaySeconds) + " s)";
}

auto timerKey(const TimerRecord &record)
{
    return std::make_tuple(record.armedAt, record.timer, record.target, record.delaySeconds);
}

class Replay {
public:
    Replay(const std::string &text, JsonVariantConst sequence) : text_(text), sequence_(sequence), scanner_(text) {}

    std::string run()
    {
        if (!prepare()) {
            return problem_;
        }
        fake_.clock = start_;
        fake_.storedAccountNumber = config_.accountNumber;
        engine_ = std::make_unique<Engine>(fake_, config_, USAGE_LOOKBACK_DAYS_MAX);
        engine_->onClockSynced();
        afterEvent();
        size_t restart = 0;
        for (size_t events = 0; problem_.empty(); ++events) {
            if (events == EVENT_LIMIT) {
                return "more than " + std::to_string(EVENT_LIMIT) + " events before the end of the sequence";
            }
            std::optional<Instant> next;
            const auto consider = [&next](std::optional<Instant> candidate) {
                if (candidate && (!next || *candidate < *next)) {
                    next = candidate;
                }
            };
            const std::optional<Instant> restartAt =
                restart < restarts_.size() ? std::optional<Instant>(restarts_[restart]) : std::nullopt;
            const auto timer = fake_.nextTimer();
            consider(restartAt);
            consider(due_ ? std::optional<Instant>(due_->at) : std::nullopt);
            consider(timer ? std::optional<Instant>(timer->second) : std::nullopt);
            if (!next || *next > end_) {
                break;
            }
            fake_.clock = *next;
            if (restartAt && *restartAt == *next) {
                ++restart;
                fake_.forgetTimers();
                due_.reset();
                config_.accountNumber = fake_.storedAccountNumber;
                engine_ = std::make_unique<Engine>(fake_, config_, USAGE_LOOKBACK_DAYS_MAX);
                engine_->onClockSynced();
            } else if (due_ && due_->at == *next) {
                Due due = std::move(*due_);
                due_.reset();
                engine_->onResponse(due.id, std::move(due.response));
            } else {
                fake_.pending[static_cast<size_t>(timer->first)].reset();
                engine_->onTimer(timer->first);
            }
            afterEvent();
        }
        if (!problem_.empty()) {
            return problem_;
        }
        return compare();
    }

    size_t requests() const { return fake_.sent.size(); }
    size_t timers() const { return fake_.timers.size(); }

private:
    struct Due {
        RequestId id = 0;
        Instant at = 0;
        Response response;
    };

    bool prepare()
    {
        const std::optional<Instant> start = instantOf(sequence_["start"]);
        const std::optional<Instant> end = instantOf(sequence_["end"]);
        if (!start || !end) {
            problem_ = "start or end is not an instant";
            return false;
        }
        start_ = *start;
        end_ = *end;
        for (JsonVariantConst restart : sequence_["restarts"].as<JsonArrayConst>()) {
            const std::optional<Instant> at = instantOf(restart);
            if (!at) {
                problem_ = "a restart is not an instant";
                return false;
            }
            restarts_.push_back(*at);
        }
        JsonVariantConst config = sequence_["config"];
        const std::optional<std::string> token = stringOf(config["token"]);
        if (!token) {
            problem_ = "config.token is not a string";
            return false;
        }
        config_.tokenPreview = tokenPreview(*token);
        config_.accountNumber = stringOf(config["accountNumber"]);
        config_.nmi = stringOf(config["nmi"]);
        if (!config["priceHighThresholdCentsPerKwh"].isNull()) {
            config_.priceHighThresholdCentsPerKwh = config["priceHighThresholdCentsPerKwh"].as<double>();
        }
        if (!config["priceLowThresholdCentsPerKwh"].isNull()) {
            config_.priceLowThresholdCentsPerKwh = config["priceLowThresholdCentsPerKwh"].as<double>();
        }
        const std::optional<Span> root = scanner_.root();
        responsesSpan_ = root ? scanner_.member(*root, "responses") : std::nullopt;
        if (!responsesSpan_) {
            problem_ = "no raw text for responses";
            return false;
        }
        return true;
    }

    void afterEvent()
    {
        states_.push_back(engine_->requestsState());
        while (problem_.empty() && answered_ < fake_.sent.size()) {
            schedule(fake_.sent[answered_++]);
        }
    }

    void schedule(const SentRequest &sent)
    {
        const std::string path = endpointPath(sent.request.endpoint);
        const Query query = queryOf(sent.request);
        if (due_) {
            problem_ = "request " + describe(sent.sendAt, path, query) + " sent while another is in flight";
            return;
        }
        JsonArrayConst responses = sequence_["responses"].as<JsonArrayConst>();
        if (script_ >= responses.size()) {
            problem_ = "request " + describe(sent.sendAt, path, query) + " is not in the script";
            return;
        }
        JsonVariantConst entry = responses[script_];
        std::string why;
        const Query scripted = queryOf(entry["query"], why);
        if (!why.empty()) {
            problem_ = "response " + std::to_string(script_) + ": " + why;
            return;
        }
        if (stringOf(entry["method"]) != std::optional<std::string>("GET") || stringOf(entry["path"]) != path ||
            scripted != query) {
            problem_ = "request " + std::to_string(script_) + " is " + describe(sent.sendAt, path, query) +
                       ", the script expects " + stringOf(entry["path"]).value_or("(no path)") + " " +
                       describe(sent.sendAt, "", scripted);
            return;
        }
        std::optional<Response> response = responseOf(sent.request, entry);
        if (!response) {
            return;
        }
        const Instant hold = entry["holdSeconds"].isNull() ? 0 : entry["holdSeconds"].as<int64_t>();
        due_ = Due{sent.id, sent.sendAt + hold, std::move(*response)};
        ++script_;
    }

    std::optional<Response> responseOf(const Request &request, JsonVariantConst entry)
    {
        JsonVariantConst answer = entry["answer"];
        if (const std::optional<std::string> network = stringOf(answer["network"])) {
            return networkResponse(*network);
        }
        if (!answer["status"].is<int>()) {
            problem_ = "response " + std::to_string(script_) + " has no status";
            return std::nullopt;
        }
        const int status = answer["status"].as<int>();
        Response response;
        if (status >= 200 && status < 300) {
            if (answer["body"].isNull()) {
                MemorySource empty(std::string_view(), 1);
                response = parsedResponse(request, status, empty);
            } else {
                const std::optional<Span> element = scanner_.element(*responsesSpan_, script_);
                const std::optional<Span> answerSpan = element ? scanner_.member(*element, "answer") : std::nullopt;
                const std::optional<Span> bodySpan = answerSpan ? scanner_.member(*answerSpan, "body") : std::nullopt;
                if (!bodySpan) {
                    problem_ = "response " + std::to_string(script_) + ": no raw body text";
                    return std::nullopt;
                }
                const std::string_view raw(text_.data() + bodySpan->begin, bodySpan->end - bodySpan->begin);
                MemorySource source(raw, raw.size());
                response = parsedResponse(request, status, source);
            }
        } else {
            const std::optional<std::string> body = stringOf(answer["body"]);
            if (!body) {
                problem_ = "response " + std::to_string(script_) + ": a non-2xx body that is not a string";
                return std::nullopt;
            }
            response = errorResponse(status, keptErrorBody(*body), body->size());
        }
        for (JsonPairConst header : answer["headers"].as<JsonObjectConst>()) {
            const std::string name = header.key().c_str();
            const std::optional<std::string> value = stringOf(header.value());
            if (name == "Retry-After") {
                response.retryAfter = value;
            } else if (name == "Location") {
                response.location = value;
            } else if (name == "X-DailyLimit-Remaining") {
                response.dailyLimitRemaining = value;
            }
        }
        return response;
    }

    std::string compare()
    {
        JsonVariantConst expected = sequence_["expected"];
        JsonArrayConst responses = sequence_["responses"].as<JsonArrayConst>();
        if (script_ != responses.size()) {
            return std::to_string(responses.size() - script_) + " scripted response(s) never requested, the first is " +
                   stringOf(responses[script_]["path"]).value_or("(no path)");
        }
        std::string why = compareRequests(expected["requests"].as<JsonArrayConst>());
        if (why.empty()) {
            why = compareTimers(expected["timers"].as<JsonArrayConst>());
        }
        if (why.empty()) {
            why = compareGroups(expected["groups"]);
        }
        if (why.empty() && stringOf(expected["storedAccountNumber"]) != fake_.storedAccountNumber) {
            why = "stored account number is " + fake_.storedAccountNumber.value_or("null") + ", expected " +
                  stringOf(expected["storedAccountNumber"]).value_or("null");
        }
        if (why.empty()) {
            why = checkBounds();
        }
        if (why.empty()) {
            why = checkRequestsState(responses);
        }
        return why;
    }

    std::string compareRequests(JsonArrayConst expected)
    {
        const size_t count = std::min(expected.size(), fake_.sent.size());
        for (size_t i = 0; i < count; ++i) {
            JsonVariantConst want = expected[i];
            const SentRequest &sent = fake_.sent[i];
            std::string problem;
            const Query query = queryOf(want["query"], problem);
            const std::optional<Instant> at = instantOf(want["sendAt"]);
            const std::string path = endpointPath(sent.request.endpoint);
            if (!problem.empty() || !at || *at != sent.sendAt || stringOf(want["path"]) != path ||
                query != queryOf(sent.request)) {
                return "request " + std::to_string(i) + " was " + describe(sent.sendAt, path, queryOf(sent.request)) +
                       ", expected " + describe(at.value_or(0), stringOf(want["path"]).value_or("(no path)"), query);
            }
        }
        if (expected.size() != fake_.sent.size()) {
            return "sent " + std::to_string(fake_.sent.size()) + " requests, expected " + std::to_string(expected.size());
        }
        return "";
    }

    std::string compareTimers(JsonArrayConst expected)
    {
        std::vector<TimerRecord> want;
        for (JsonVariantConst timer : expected) {
            const std::optional<Instant> armedAt = instantOf(timer["armedAt"]);
            const std::optional<Instant> target = instantOf(timer["target"]);
            const std::optional<std::string> name = stringOf(timer["timer"]);
            if (!armedAt || !target || !name || !timer["delaySeconds"].is<int64_t>()) {
                return "an expected timer is malformed";
            }
            want.push_back(TimerRecord{*armedAt, *name, *target, timer["delaySeconds"].as<int64_t>()});
        }
        std::vector<TimerRecord> got = fake_.timers;
        const auto order = [](const TimerRecord &a, const TimerRecord &b) { return timerKey(a) < timerKey(b); };
        std::sort(want.begin(), want.end(), order);
        std::sort(got.begin(), got.end(), order);
        const size_t count = std::min(want.size(), got.size());
        for (size_t i = 0; i < count; ++i) {
            if (timerKey(want[i]) != timerKey(got[i])) {
                return "timer " + std::to_string(i) + " was " + describe(got[i]) + ", expected " + describe(want[i]);
            }
        }
        if (want.size() != got.size()) {
            return "armed " + std::to_string(got.size()) + " timers, expected " + std::to_string(want.size()) + "; next " +
                   (got.size() > count ? describe(got[count]) : "expected " + describe(want[count]));
        }
        return "";
    }

    static std::string compareFault(const char *group, const std::optional<Fault> &fault, JsonVariantConst want)
    {
        const std::optional<std::string> status = stringOf(want["status"]);
        const std::string actual = fault ? "faulted" : "ok";
        if (status != actual) {
            return std::string(group) + " is " + actual + (fault ? " (" + fault->code + ")" : "") + ", expected " +
                   status.value_or("(no status)");
        }
        JsonVariantConst wantFault = want["fault"];
        if (wantFault.isNull()) {
            return "";
        }
        if (stringOf(wantFault["code"]) != fault->code) {
            return std::string(group) + " fault is " + fault->code + ", expected " +
                   stringOf(wantFault["code"]).value_or("(no code)");
        }
        if (!wantFault["httpStatus"].isNull() && fault->httpStatus != wantFault["httpStatus"].as<int>()) {
            return std::string(group) + " httpStatus is " + (fault->httpStatus ? std::to_string(*fault->httpStatus) : "null") +
                   ", expected " + std::to_string(wantFault["httpStatus"].as<int>());
        }
        if (!wantFault["body"].isNull() && fault->body != stringOf(wantFault["body"])) {
            return std::string(group) + " body is \"" + fault->body.value_or("null") + "\", expected \"" +
                   stringOf(wantFault["body"]).value_or("(not a string)") + "\"";
        }
        if (!wantFault["bodyBytes"].isNull() &&
            fault->bodyBytes != std::optional<size_t>(wantFault["bodyBytes"].as<size_t>())) {
            return std::string(group) + " bodyBytes is " + (fault->bodyBytes ? std::to_string(*fault->bodyBytes) : "null") +
                   ", expected " + std::to_string(wantFault["bodyBytes"].as<size_t>());
        }
        return "";
    }

    std::string compareGroups(JsonVariantConst groups)
    {
        const Signals &signals = engine_->signals();
        std::string why = compareFault("account", signals.account.fault, groups["account"]);
        if (why.empty()) {
            why = compareFault("tariff", signals.tariff.fault, groups["tariff"]);
        }
        if (why.empty()) {
            why = compareFault("price", signals.price.fault, groups["price"]);
        }
        if (why.empty()) {
            why = compareFault("energy", signals.energy.fault, groups["energy"]);
        }
        return why;
    }

    std::string checkBounds()
    {
        for (const TimerRecord &record : fake_.timers) {
            if (record.delaySeconds < 0 || record.delaySeconds > TIMER_MAX_AHEAD_S) {
                return "timer delay out of range: " + describe(record);
            }
        }
        std::map<Instant, int> holds;
        for (const SentRequest &sent : fake_.sent) {
            if (sent.request.endpoint == Endpoint::wait &&
                ++holds[dispatchBoundary(sent.sendAt)] > WAIT_HOLDS_PER_INTERVAL) {
                return "more than " + std::to_string(WAIT_HOLDS_PER_INTERVAL) + " holds started in the interval of " +
                       formatInstant(dispatchBoundary(sent.sendAt));
            }
        }
        return "";
    }

    std::string checkRequestsState(JsonArrayConst responses)
    {
        bool gateway = false;
        bool core = false;
        for (JsonVariantConst entry : responses) {
            if (entry["answer"]["status"].as<int>() != 401) {
                continue;
            }
            const std::optional<std::string> body = stringOf(entry["answer"]["body"]);
            if (body && gatewayErrorCode(*body) == std::optional<std::string>("unauthorized")) {
                gateway = true;
            } else {
                core = true;
            }
        }
        if (gateway && engine_->requestsState() != RequestsState::stoppedTokenRejected) {
            return std::string("requestsState is ") + requestsStateName(engine_->requestsState()) +
                   " after the gateway 401, expected stoppedTokenRejected";
        }
        if (core && std::find(states_.begin(), states_.end(), RequestsState::stoppedUntilAccountSync) == states_.end()) {
            return "requestsState was never stoppedUntilAccountSync after the non-gateway 401";
        }
        return "";
    }

    const std::string &text_;
    JsonVariantConst sequence_;
    RawScanner scanner_;
    std::optional<Span> responsesSpan_;
    FakePlatform fake_;
    Config config_;
    std::unique_ptr<Engine> engine_;
    Instant start_ = 0;
    Instant end_ = 0;
    std::vector<Instant> restarts_;
    size_t script_ = 0;
    size_t answered_ = 0;
    std::optional<Due> due_;
    std::vector<RequestsState> states_;
    std::string problem_;
};

}

int runSequences(const char *directory)
{
    const std::string base(directory);
    const std::optional<std::string> indexText = readFile(base + "/index.json");
    if (!indexText) {
        std::fprintf(stderr, "cannot read %s/index.json\n", directory);
        return 1;
    }
    JsonDocument index;
    const DeserializationError indexError = deserializeJson(index, *indexText);
    if (indexError) {
        std::fprintf(stderr, "%s/index.json: %s\n", directory, indexError.c_str());
        return 1;
    }
    size_t passed = 0;
    size_t failed = 0;
    for (JsonVariantConst entry : index.as<JsonArrayConst>()) {
        const std::string path = entry["path"].as<const char *>();
        const std::optional<std::string> text = readFile(base + "/" + path);
        std::string why;
        JsonDocument sequence;
        if (!text) {
            why = "cannot read the file";
        } else if (const DeserializationError error = deserializeJson(sequence, *text, DeserializationOption::NestingLimit(32))) {
            why = std::string("sequence JSON: ") + error.c_str();
        }
        size_t requests = 0;
        size_t timers = 0;
        if (why.empty()) {
            Replay replay(*text, sequence.as<JsonVariantConst>());
            why = replay.run();
            requests = replay.requests();
            timers = replay.timers();
        }
        if (why.empty()) {
            ++passed;
            std::printf("ok   loop_tests %s (%zu requests, %zu timers)\n", path.c_str(), requests, timers);
        } else {
            ++failed;
            std::printf("FAIL loop_tests %s: %s\n", path.c_str(), why.c_str());
        }
    }
    std::printf("loop_tests: %zu sequences, %zu passed, %zu failed\n", passed + failed, passed, failed);
    std::fflush(stdout);
    return failed == 0 && passed > 0 ? 0 : 1;
}

namespace {

std::string accountBody(const std::string &gridType)
{
    return R"({"accounts":[{"accountNumber":"36200000000001","siteAddress":"1 Example St, Sampletown NSW 2000",)"
           R"("accountState":"ACTIVE","productName":"Anytime Flat",)"
           R"("product":{"gridType":")" +
           gridType + R"(","timeZone":"Australia/Sydney","currentPlan":null,"upcomingPlan":null}}]})";
}

std::string outlookBody(const std::string &time)
{
    return R"({"region":"NSW1","now":{"time":")" + time +
           R"(","region":"NSW1","averageCentsPerKwh":9.1,"minCentsPerKwh":9.1,"maxCentsPerKwh":9.1,"intervals":1}})";
}

std::string regionChangeAtAccountSync()
{
    FakePlatform fake;
    fake.clock = *parseInstant("2026-10-01T23:58:00+10:00").instant;
    const Instant midnight = *parseInstant("2026-10-02T00:00:00+10:00").instant;
    const Instant end = *parseInstant("2026-10-02T00:04:00+10:00").instant;
    Config config;
    config.tokenPreview = tokenPreview("fdk_SEQUENCEFIXTURE000000000000000000wXyZ");
    config.accountNumber = std::string("36200000000001");
    Engine engine(fake, config, USAGE_LOOKBACK_DAYS_MAX);
    engine.onClockSynced();
    struct Due {
        RequestId id = 0;
        Instant at = 0;
        Response response;
    };
    std::optional<Due> due;
    size_t answered = 0;
    size_t accountCalls = 0;
    std::vector<std::string> requests;
    for (size_t events = 0; events < EVENT_LIMIT; ++events) {
        while (answered < fake.sent.size()) {
            const SentRequest &sent = fake.sent[answered++];
            const Query query = queryOf(sent.request);
            const auto region = query.find("region");
            requests.push_back(std::string(endpointPath(sent.request.endpoint)) +
                               (region == query.end() ? std::string() : " " + region->second));
            if (due) {
                return "request " + requests.back() + " sent while another is in flight";
            }
            std::string body;
            int status = 200;
            Instant hold = 0;
            switch (sent.request.endpoint) {
            case Endpoint::account:
                body = accountBody(++accountCalls == 1 ? "Ausgrid" : "Endeavour");
                break;
            case Endpoint::meters:
                body = R"({"meters":[{"nmi":"4102000000","address":"1 Example St, Sampletown NSW 2000"}]})";
                break;
            case Endpoint::tokens:
                body = R"({"tokens":[]})";
                break;
            case Endpoint::outlook:
                body = outlookBody(fake.clock < midnight ? "2026-10-01T23:55:00+10:00" : "2026-10-02T00:00:00+10:00");
                break;
            case Endpoint::wait:
                body = R"([{"time":"2026-10-02T00:00:00+10:00","averageCentsPerKwh":9.1}])";
                hold = 70;
                break;
            case Endpoint::usageHalfHourly:
            case Endpoint::usageDaily:
                body = "[]";
                break;
            }
            MemorySource source(body, body.size());
            due = Due{sent.id, sent.sendAt + hold, parsedResponse(sent.request, status, source)};
        }
        std::optional<Instant> next = due ? std::optional<Instant>(due->at) : std::nullopt;
        const auto timer = fake.nextTimer();
        if (timer && (!next || timer->second < *next)) {
            next = timer->second;
        }
        if (!next || *next > end) {
            break;
        }
        fake.clock = *next;
        if (due && due->at == *next) {
            Due done = std::move(*due);
            due.reset();
            engine.onResponse(done.id, std::move(done.response));
        } else {
            fake.pending[static_cast<size_t>(timer->first)].reset();
            engine.onTimer(timer->first);
        }
    }
    const std::vector<std::string> expected = {
        "/api/MyAccount/GetAccountData",
        "/api/Billing/meters",
        "/tokens",
        "/api/Live/nempricing/outlook Ausgrid",
        "/api/Usage/usage/projectreads/halfhourly",
        "/api/Usage/usage/projectreads/daily",
        "/api/Live/nempricing/wait Ausgrid",
        "/api/MyAccount/GetAccountData",
        "/api/Live/nempricing/outlook Endeavour",
        "/api/Billing/meters",
        "/tokens",
        "/api/Usage/usage/projectreads/halfhourly",
        "/api/Usage/usage/projectreads/daily",
    };
    if (requests != expected) {
        std::string sent;
        for (const std::string &request : requests) {
            sent += "\n  " + request;
        }
        return "requests sent:" + sent;
    }
    if (engine.priceLoop().region() != "Endeavour" || engine.priceLoop().state() != PriceLoop::State::armed) {
        return "the price loop is not armed on Endeavour: region " + engine.priceLoop().region() + ", state " +
               std::to_string(static_cast<int>(engine.priceLoop().state()));
    }
    return "";
}

}

bool runRegionChangeTests()
{
    Suite suite("loop_region_tests");
    suite.run("a region change at ACCOUNT_SYNC drops the outlook queued for the old region", regionChangeAtAccountSync);
    return suite.finish();
}
