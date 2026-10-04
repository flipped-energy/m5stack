#include "flipped/app/app.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "esp_platform.h"
#include "flipped/core/identity.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"
#include "flipped/net/http_client.h"
#include "flipped/net/sntp.h"
#include "flipped/store/config_store.h"
#include "timers.h"

namespace flipped::app {

namespace {

const char *TAG = "flipped_app";
constexpr uint32_t STACK_BYTES = 12288;
constexpr UBaseType_t PRIORITY = 5;
constexpr BaseType_t CORE = 0;
constexpr UBaseType_t QUEUE_LENGTH = 32;
constexpr uint32_t UI_MODEL_CHANGED_BIT = 1U << 0;

using MatterFacts = matter::CommissioningFacts;

enum class Action : uint8_t { openCommissioningWindow, unpair, eraseEverything, usageRefresh, republish };

struct ResponseEvent {
    core::RequestId id = 0;
    core::Response response;
};
struct TimerEvent {
    core::TimerId timer = core::TimerId::startup;
};
struct ClockEvent {};
struct NetworkEvent {
    bool up = false;
    std::string ipv4;
};
struct MatterEvent {};
struct LabelsEvent {
    std::vector<EndpointLabel> labels;
};
struct ActionEvent {
    Action action = Action::usageRefresh;
};
struct CallEvent {
    std::function<core::ConfigResult()> work;
    core::ConfigResult *result = nullptr;
    SemaphoreHandle_t done = nullptr;
};

using Event = std::variant<ResponseEvent, TimerEvent, ClockEvent, NetworkEvent, MatterEvent, LabelsEvent, ActionEvent,
                           CallEvent>;

struct Shared {
    Shared()
    {
        queue = xQueueCreate(QUEUE_LENGTH, sizeof(Event *));
        if (queue == nullptr) {
            ESP_LOGE(TAG, "xQueueCreate returned null");
            abort();
        }
        model.firmwareVersion = esp_app_get_description()->version;
    }

    QueueHandle_t queue = nullptr;
    std::mutex modelMutex;
    UiModel model;
    std::atomic<TaskHandle_t> uiTask{nullptr};
    std::atomic<TaskHandle_t> appTask{nullptr};
    std::atomic<TaskHandle_t> httpTask{nullptr};
    std::atomic<bool> clockSynced{false};
    std::atomic<bool> networkUp{false};
    std::mutex handlerMutex;
    std::function<void(uint16_t, bool)> identify;
    std::function<void(uint16_t, uint8_t, uint8_t)> identifyEffect;
    std::mutex factsMutex;
    MatterFacts facts;
    bool factsPending = false;
};

Shared &shared()
{
    static Shared instance;
    return instance;
}

void post(Event event)
{
    Event *item = new Event(std::move(event));
    if (xQueueSend(shared().queue, &item, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "xQueueSend to flipped_app failed");
        abort();
    }
}

std::string userAgent()
{
    static_assert(sizeof(CONFIG_FLIPPED_BOARD_NAME) > 1, "CONFIG_FLIPPED_BOARD_NAME is empty");
    return std::string("flipped-m5stack-") + CONFIG_FLIPPED_BOARD_NAME + "/" + esp_app_get_description()->version;
}

core::Config configOf(const store::Settings &settings)
{
    core::Config config;
    config.accountNumber = settings.accountNumber;
    config.nmi = settings.nmi;
    if (settings.token) {
        config.tokenPreview = core::tokenPreview(*settings.token);
    }
    config.priceHighThresholdCentsPerKwh = settings.priceHighThresholdCentsPerKwh;
    config.priceLowThresholdCentsPerKwh = settings.priceLowThresholdCentsPerKwh;
    return config;
}

bool eligible(const core::Account &account)
{
    return account.accountNumber.present() && !account.accountNumber.value.empty() && account.product.present();
}

std::vector<AccountChoice> eligibleAccounts(const core::Snapshot<core::AccountBody> &snapshot)
{
    std::vector<AccountChoice> choices;
    if (!snapshot.body || !snapshot.body->accounts.present()) {
        return choices;
    }
    for (const core::Account &account : snapshot.body->accounts.value) {
        if (eligible(account)) {
            choices.push_back(AccountChoice{account.accountNumber.value,
                                            account.siteAddress.present() ? account.siteAddress.value : std::string()});
        }
    }
    return choices;
}

std::vector<std::string> candidateNmis(const std::optional<std::string> &pinned, const core::Snapshots &snapshots)
{
    std::vector<std::string> nmis;
    if (!snapshots.account.body || !snapshots.account.body->accounts.present() || !snapshots.meters.body ||
        !snapshots.meters.body->meters.present()) {
        return nmis;
    }
    std::vector<const core::Account *> accounts;
    for (const core::Account &account : snapshots.account.body->accounts.value) {
        if (eligible(account)) {
            accounts.push_back(&account);
        }
    }
    const core::Account *selected = nullptr;
    for (const core::Account *account : accounts) {
        if (pinned && account->accountNumber.value == *pinned) {
            selected = account;
        }
    }
    if (!pinned && accounts.size() == 1) {
        selected = accounts.front();
    }
    if (selected == nullptr) {
        return nmis;
    }
    const auto add = [&nmis](const std::string &nmi) {
        if (std::find(nmis.begin(), nmis.end(), nmi) == nmis.end()) {
            nmis.push_back(nmi);
        }
    };
    const std::vector<core::Meter> &meters = snapshots.meters.body->meters.value;
    for (const core::Meter &meter : meters) {
        if (meter.nmi.present() && meter.address.present() && selected->siteAddress.present() &&
            meter.address.value == selected->siteAddress.value) {
            add(meter.nmi.value);
        }
    }
    if (nmis.empty()) {
        for (const core::Meter &meter : meters) {
            if (meter.nmi.present()) {
                add(meter.nmi.value);
            }
        }
    }
    return nmis;
}

void notifyCommissioning(const MatterFacts &facts)
{
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(shared().factsMutex);
        shared().facts = facts;
        wake = !shared().factsPending;
        shared().factsPending = true;
    }
    if (wake) {
        post(MatterEvent{});
    }
}

void onClockSynced()
{
    post(ClockEvent{});
}

void onTimerFired(core::TimerId timer)
{
    post(TimerEvent{timer});
}

void onHttpResponse(core::RequestId id, core::Response response)
{
    post(ResponseEvent{id, std::move(response)});
}

void onNetworkEvent(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *got = static_cast<const ip_event_got_ip_t *>(data);
        char text[16];
        snprintf(text, sizeof text, IPSTR, IP2STR(&got->ip_info.ip));
        shared().networkUp = true;
        post(NetworkEvent{true, text});
        return;
    }
    if (shared().networkUp.exchange(false)) {
        post(NetworkEvent{false, std::string()});
    }
}

std::string stackText(const char *name, TaskHandle_t task)
{
    if (task == nullptr) {
        return std::string(name) + " not running";
    }
    return std::string(name) + " " + std::to_string(uxTaskGetStackHighWaterMark(task)) + " B";
}

class App {
public:
    explicit App(std::function<void(const Publication &)> publish)
        : timers_(onTimerFired), http_(userAgent(), onHttpResponse), platform_(timers_, http_, store_),
          publish_(std::move(publish))
    {
        if (std::optional<std::variant<core::Ledger, core::Invalid>> stored = store_.ledger()) {
            if (const core::Invalid *invalid = std::get_if<core::Invalid>(&*stored)) {
                ESP_LOGE(TAG, "%s/ledger: %s", store::NAMESPACE, invalid->message.c_str());
                abort();
            }
            ledger_ = std::get<core::Ledger>(*stored);
        }
        engine_ = std::make_unique<core::Engine>(platform_, configOf(store_.settings()),
                                                 CONFIG_FLIPPED_USAGE_LOOKBACK_DAYS);
    }

    void startHttp()
    {
        http_.start();
        shared().httpTask = http_.task();
    }

    void run()
    {
        readNetworkState();
        logResources("after start-up");
        publishAll(std::nullopt);
        for (;;) {
            Event *received = nullptr;
            if (xQueueReceive(shared().queue, &received, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "xQueueReceive on flipped_app failed");
                abort();
            }
            std::unique_ptr<Event> event(received);
            std::visit([this](auto &item) { handle(item); }, *event);
            afterEvent();
        }
    }

    void applyLabels(std::vector<EndpointLabel> labels) { labels_ = std::move(labels); }

    std::optional<std::string> storedInstanceKey() const { return instanceKey(); }

    core::Instant tariffPublishedAt(uint32_t hash, core::Instant at)
    {
        if (!tariffMarkLoaded_) {
            tariffMark_ = store_.tariffMark();
            tariffMarkLoaded_ = true;
        }
        if (tariffMark_ && tariffMark_->hash == hash) {
            return tariffMark_->publishedAt;
        }
        tariffMark_ = store::TariffMark{hash, at};
        store_.setTariffMark(*tariffMark_);
        ESP_LOGI(TAG, "%s/tariff_h %08" PRIx32 " first published at %lld", store::NAMESPACE, hash,
                 static_cast<long long>(at));
        return at;
    }

    core::ConfigResult applyToken(const std::string &token) { return applied(store_.setToken(token)); }
    core::ConfigResult applyAccountNumber(const std::string &number) { return applied(store_.setAccountNumber(number)); }
    core::ConfigResult applyNmi(const std::string &nmi) { return applied(store_.setNmi(nmi)); }
    core::ConfigResult applyThresholds(std::optional<double> high, std::optional<double> low)
    {
        return applied(store_.setThresholds(high, low));
    }

private:
    core::ConfigResult applied(core::ConfigResult result)
    {
        if (result.accepted) {
            engine_->onConfigChanged(configOf(store_.settings()));
            startEngine();
        }
        return result;
    }

    void startEngine()
    {
        if (!clockSynced_ || engineStarted_ || !store_.settings().token) {
            return;
        }
        engineStarted_ = true;
        engine_->onClockSynced();
    }

    void readNetworkState()
    {
        esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (station == nullptr || !esp_netif_is_netif_up(station)) {
            return;
        }
        esp_netif_ip_info_t info = {};
        ESP_ERROR_CHECK(esp_netif_get_ip_info(station, &info));
        if (info.ip.addr == 0) {
            return;
        }
        char text[16];
        snprintf(text, sizeof text, IPSTR, IP2STR(&info.ip));
        shared().networkUp = true;
        NetworkEvent event{true, text};
        handle(event);
    }

    void handle(ResponseEvent &event) { engine_->onResponse(event.id, std::move(event.response)); }

    void handle(TimerEvent &event) { engine_->onTimer(event.timer); }

    void handle(ClockEvent &)
    {
        if (clockSynced_) {
            return;
        }
        clockSynced_ = true;
        shared().clockSynced = true;
        ESP_LOGI(TAG, "clock synchronised by SNTP: %s", core::formatInstant(*now()).c_str());
        if (!store_.settings().token) {
            ESP_LOGI(TAG, "no token stored: nothing is requested until one is set");
            engine_->onConfigChanged(configOf(store_.settings()));
            return;
        }
        startEngine();
    }

    void handle(NetworkEvent &event)
    {
        networkUp_ = event.up;
        ipv4_ = event.ipv4;
        ESP_LOGI(TAG, "network %s %s", event.up ? "up" : "down", event.ipv4.c_str());
        if (event.up && !sntpStarted_) {
            sntpStarted_ = true;
            net::startSntp(onClockSynced);
        }
    }

    void handle(MatterEvent &)
    {
        const bool wasCommissioned = matter_.fabricCount > 0;
        {
            std::lock_guard<std::mutex> lock(shared().factsMutex);
            matter_ = shared().facts;
            shared().factsPending = false;
        }
        if (!wasCommissioned && matter_.fabricCount > 0) {
            logResources("after commissioning");
        }
    }

    void handle(LabelsEvent &event) { applyLabels(std::move(event.labels)); }

    void handle(ActionEvent &event)
    {
        switch (event.action) {
        case Action::openCommissioningWindow:
            matter::openCommissioningWindow();
            return;
        case Action::unpair:
            matter::unpair();
            suspendUntilRestart();
            return;
        case Action::eraseEverything:
            ESP_LOGW(TAG, "erase everything: erasing %s", store::NAMESPACE);
            store_.eraseAll();
            matter::eraseEverything();
            suspendUntilRestart();
            return;
        case Action::usageRefresh:
            engine_->onUsageRefresh();
            return;
        case Action::republish:
            return;
        }
        abort();
    }

    void handle(CallEvent &event)
    {
        *event.result = event.work();
        xSemaphoreGive(event.done);
    }

    void suspendUntilRestart()
    {
        ESP_LOGW(TAG, "factory reset scheduled: flipped_app stops handling events until the restart");
        vTaskSuspend(nullptr);
    }

    void afterEvent()
    {
        std::optional<core::LedgerStep> step;
        if (engine_->usageSyncs() != usageSyncsSeen_) {
            usageSyncsSeen_ = engine_->usageSyncs();
            step = advanceLedger();
            logResources("after usage sync");
        }
        publishAll(step);
    }

    std::optional<std::string> instanceKey() const
    {
        const store::Settings &settings = store_.settings();
        if (!settings.accountNumber) {
            return std::nullopt;
        }
        return core::instanceKey(*settings.accountNumber, settings.nmi);
    }

    std::optional<core::LedgerStep> advanceLedger()
    {
        const core::Signals &signals = engine_->signals();
        if (signals.energy.fault) {
            return std::nullopt;
        }
        const std::optional<std::string> key = instanceKey();
        if (!key) {
            ESP_LOGE(TAG, "ledger not advanced: the energy group is ok but no account number is stored");
            return std::nullopt;
        }
        core::Ledger ledger = core::ledgerFor(core::instanceHash(*key), ledger_);
        if (const std::optional<std::string> refusal = core::ledgerRefusal(ledger, signals.energy.intervals)) {
            ESP_LOGE(TAG, "ledger not advanced: %s", refusal->c_str());
            ledger_ = ledger;
            return std::nullopt;
        }
        const core::LedgerStep step = core::advance(ledger, signals.energy.intervals);
        if (step.took > 0) {
            store_.setLedger(ledger);
        }
        ledger_ = ledger;
        return step;
    }

    void publishAll(const std::optional<core::LedgerStep> &step)
    {
        const Publication publication{engine_->signals(), instanceKey(), ledger_, step, engine_->dailyLimitRemaining(),
                                      now()};
        publish_(publication);
        publishModel();
    }

    std::string storedSsid() const
    {
        wifi_config_t config = {};
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &config));
        const auto *ssid = reinterpret_cast<const char *>(config.sta.ssid);
        return std::string(ssid, strnlen(ssid, sizeof config.sta.ssid));
    }

    void refreshRssi()
    {
        wifi_ap_record_t record = {};
        const esp_err_t err = esp_wifi_sta_get_ap_info(&record);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_sta_get_ap_info: %s", esp_err_to_name(err));
            return;
        }
        rssi_ = record.rssi;
    }

    std::optional<uint16_t> dailyLimitRemaining() const
    {
        const std::optional<int64_t> remaining = engine_->dailyLimitRemaining();
        if (!remaining) {
            return std::nullopt;
        }
        if (*remaining < 0 || *remaining > UINT16_MAX) {
            ESP_LOGE(TAG, "X-DailyLimit-Remaining %lld does not fit the UI model", static_cast<long long>(*remaining));
            return std::nullopt;
        }
        return static_cast<uint16_t>(*remaining);
    }

    void publishModel()
    {
        auto next = std::make_unique<UiModel>();
        next->signals = engine_->signals();
        next->wifiProvisioned = matter_.wifiProvisioned;
        next->wifiConnected = networkUp_;
        next->wifi.ssid = storedSsid();
        if (networkUp_) {
            refreshRssi();
        }
        next->wifi.rssi = rssi_;
        next->wifi.ipv4 = ipv4_;
        next->clockSynced = clockSynced_;
        next->fabricCount = matter_.fabricCount;
        next->bleAvailable = matter_.bleAvailable;
        next->commissioningWindowOpen = matter_.commissioningWindowOpen;
        next->qrPayload = matter_.qrPayload;
        next->manualCode = matter_.manualCode;
        const store::Settings &settings = store_.settings();
        next->hasToken = settings.token.has_value();
        next->tokenRejected = engine_->requestsState() == core::RequestsState::stoppedTokenRejected;
        if (settings.token) {
            if (std::optional<std::string> preview = core::tokenPreview(*settings.token)) {
                next->tokenPreview = *preview;
            }
        }
        next->eligibleAccounts = eligibleAccounts(engine_->snapshots().account);
        next->candidateNmis = candidateNmis(settings.accountNumber, engine_->snapshots());
        next->priceHighThresholdCentsPerKwh = settings.priceHighThresholdCentsPerKwh;
        next->priceLowThresholdCentsPerKwh = settings.priceLowThresholdCentsPerKwh;
        next->dailyLimitRemaining = dailyLimitRemaining();
        next->identifyLabels = labels_;
        next->firmwareVersion = esp_app_get_description()->version;
        {
            std::lock_guard<std::mutex> lock(shared().modelMutex);
            std::swap(shared().model, *next);
        }
        if (TaskHandle_t ui = shared().uiTask.load()) {
            xTaskNotify(ui, UI_MODEL_CHANGED_BIT, eSetBits);
        }
    }

    store::ConfigStore store_;
    EspTimers timers_;
    net::HttpClient http_;
    EspPlatform platform_;
    std::unique_ptr<core::Engine> engine_;
    std::function<void(const Publication &)> publish_;
    bool clockSynced_ = false;
    bool engineStarted_ = false;
    bool sntpStarted_ = false;
    bool networkUp_ = false;
    std::string ipv4_;
    int8_t rssi_ = 0;
    MatterFacts matter_;
    std::vector<EndpointLabel> labels_;
    uint32_t usageSyncsSeen_ = 0;
    std::optional<core::Ledger> ledger_;
    bool tariffMarkLoaded_ = false;
    std::optional<store::TariffMark> tariffMark_;
};

std::atomic<App *> instance{nullptr};
std::atomic<bool> started{false};

App &app()
{
    App *running = instance.load();
    if (running == nullptr) {
        ESP_LOGE(TAG, "flipped_app is not started");
        abort();
    }
    return *running;
}

void runApp(void *)
{
    shared().appTask = xTaskGetCurrentTaskHandle();
    app().run();
}

core::ConfigResult call(std::function<core::ConfigResult()> work)
{
    if (xTaskGetCurrentTaskHandle() == shared().appTask.load()) {
        ESP_LOGE(TAG, "a configuration call on the flipped_app task would wait for itself");
        abort();
    }
    StaticSemaphore_t buffer;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&buffer);
    core::ConfigResult result;
    post(CallEvent{std::move(work), &result, done});
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return result;
}

}

UiModel snapshot()
{
    Shared &state = shared();
    std::lock_guard<std::mutex> lock(state.modelMutex);
    return state.model;
}

void visitModel(const std::function<void(const UiModel &)> &visit)
{
    Shared &state = shared();
    std::lock_guard<std::mutex> lock(state.modelMutex);
    visit(state.model);
}

void onUiModelChanged(TaskHandle_t notify)
{
    shared().uiTask = notify;
}

std::optional<core::Instant> now()
{
    if (!shared().clockSynced.load()) {
        return std::nullopt;
    }
    return static_cast<core::Instant>(std::time(nullptr));
}

void openCommissioningWindow()
{
    post(ActionEvent{Action::openCommissioningWindow});
}

void unpair()
{
    post(ActionEvent{Action::unpair});
}

void eraseEverything()
{
    post(ActionEvent{Action::eraseEverything});
}

void requestUsageRefresh()
{
    post(ActionEvent{Action::usageRefresh});
}

core::ConfigResult setToken(std::string_view token)
{
    std::string value(token);
    return call([value]() { return app().applyToken(value); });
}

core::ConfigResult setAccountNumber(std::string_view accountNumber)
{
    std::string value(accountNumber);
    return call([value]() { return app().applyAccountNumber(value); });
}

core::ConfigResult setNmi(std::string_view nmi)
{
    std::string value(nmi);
    return call([value]() { return app().applyNmi(value); });
}

core::ConfigResult setThresholds(std::optional<double> high, std::optional<double> low)
{
    return call([high, low]() { return app().applyThresholds(high, low); });
}

void onIdentify(std::function<void(uint16_t endpoint, bool active)> handler)
{
    std::lock_guard<std::mutex> lock(shared().handlerMutex);
    shared().identify = std::move(handler);
}

void onIdentifyEffect(std::function<void(uint16_t endpoint, uint8_t effectId, uint8_t effectVariant)> handler)
{
    std::lock_guard<std::mutex> lock(shared().handlerMutex);
    shared().identifyEffect = std::move(handler);
}

void notifyIdentify(uint16_t endpoint, bool active)
{
    std::function<void(uint16_t, bool)> handler;
    {
        std::lock_guard<std::mutex> lock(shared().handlerMutex);
        handler = shared().identify;
    }
    if (handler) {
        handler(endpoint, active);
    }
}

void notifyIdentifyEffect(uint16_t endpoint, uint8_t effectId, uint8_t effectVariant)
{
    std::function<void(uint16_t, uint8_t, uint8_t)> handler;
    {
        std::lock_guard<std::mutex> lock(shared().handlerMutex);
        handler = shared().identifyEffect;
    }
    if (handler) {
        handler(endpoint, effectId, effectVariant);
    }
}

void setIdentifyLabels(std::vector<EndpointLabel> labels)
{
    post(LabelsEvent{std::move(labels)});
}

void prepare(std::function<void(const Publication &)> publish)
{
    if (!publish) {
        ESP_LOGE(TAG, "prepare without a publish hook");
        abort();
    }
    if (instance.load() != nullptr) {
        ESP_LOGE(TAG, "prepared twice");
        abort();
    }
    instance = new App(std::move(publish));
}

std::optional<std::string> storedInstanceKey()
{
    if (started.load()) {
        ESP_LOGE(TAG, "storedInstanceKey read after start: the stored settings belong to the flipped_app task");
        abort();
    }
    return app().storedInstanceKey();
}

void start()
{
    if (started.exchange(true)) {
        ESP_LOGE(TAG, "started twice");
        abort();
    }
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onNetworkEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, onNetworkEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onNetworkEvent, nullptr));
    app().startHttp();
    if (xTaskCreatePinnedToCore(runApp, "flipped_app", STACK_BYTES, nullptr, PRIORITY, nullptr, CORE) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore flipped_app failed");
        abort();
    }
}

matter::Hooks matterHooks()
{
    matter::Hooks hooks;
    hooks.identify = notifyIdentify;
    hooks.identifyEffect = notifyIdentifyEffect;
    hooks.commissioning = notifyCommissioning;
    hooks.labels = [](std::vector<matter::EndpointName> names) {
        std::vector<EndpointLabel> labels;
        for (matter::EndpointName &name : names) {
            labels.push_back(EndpointLabel{name.endpoint, std::move(name.label)});
        }
        if (xTaskGetCurrentTaskHandle() == shared().appTask.load()) {
            app().applyLabels(std::move(labels));
            return;
        }
        setIdentifyLabels(std::move(labels));
    };
    hooks.refresh = []() { post(ActionEvent{Action::republish}); };
    hooks.tariffPublishedAt = [](uint32_t hash, core::Instant at) {
        if (xTaskGetCurrentTaskHandle() != shared().appTask.load()) {
            ESP_LOGE(TAG, "tariffPublishedAt called outside the flipped_app task");
            abort();
        }
        return app().tariffPublishedAt(hash, at);
    };
    return hooks;
}

void logResources(const char *when)
{
    const size_t internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const size_t byteCapable = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const size_t byteCapableInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    const std::string stacks = stackText("flipped_app", shared().appTask.load()) + ", " +
                               stackText("flipped_http", shared().httpTask.load()) + ", " +
                               stackText("ui", shared().uiTask.load()) + ", " +
                               stackText("console", xTaskGetHandle("console"));
    ESP_LOGI(TAG, "resources %s: minimum free heap internal %u B, external %u B; stack left %s", when,
             static_cast<unsigned>(internal), static_cast<unsigned>(byteCapable - byteCapableInternal), stacks.c_str());
}

}
