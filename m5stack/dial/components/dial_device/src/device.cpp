#include "nvs.h"
#include "dial/device.h"

#include <M5Unified.h>

#include <atomic>
#include <ctime>
#include <mutex>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "knob.h"
#include "mbedtls/base64.h"
#include "flipped/ui/theme.h"
#include "round.h"

extern const uint8_t wordmarkStart[] asm("_binary_flipped_wordmark_png_start");
extern const uint8_t wordmarkEnd[] asm("_binary_flipped_wordmark_png_end");
extern const uint8_t boltStart[] asm("_binary_flipped_bolt_png_start");
extern const uint8_t boltEnd[] asm("_binary_flipped_bolt_png_end");
extern const uint8_t logoStart[] asm("_binary_flipped_logo_png_start");
extern const uint8_t logoEnd[] asm("_binary_flipped_logo_png_end");

namespace dial {

namespace ui = flipped::ui;

namespace {

constexpr const char* kTag = "dial";
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_14;
constexpr gpio_num_t kButton = GPIO_NUM_42;
constexpr uint32_t kFrameMs = 20;
constexpr uint32_t kHoldMs = 1000;
constexpr uint32_t kDimAfterMs = 60000;
constexpr uint8_t kBrightness = 160;
constexpr uint32_t kStackBytes = 10240;

ModelSource* source = nullptr;
TaskHandle_t task = nullptr;
LGFX_Sprite* canvas = nullptr;
std::atomic<bool> modelDirty{true};
std::atomic<bool> hasInjected{false};
std::atomic<bool> inputWake{false};
Input injectedInput = Input::press;
std::atomic<bool> shotRequested{false};
SemaphoreHandle_t shotDone = nullptr;

void IRAM_ATTR onInputEdge(void*)
{
    inputWake = true;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    portYIELD_FROM_ISR(woken);
}

void wakeOn(gpio_num_t pin)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, onInputEdge, nullptr));
}

uint32_t msUntilNextMinute()
{
    time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);
    return static_cast<uint32_t>(60 - parts.tm_sec) * 1000 + 50;
}

class Base64Lines {
public:
    void add(const uint8_t* bytes, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            pending_[size_++] = bytes[i];
            if (size_ == sizeof pending_) {
                flush();
            }
        }
    }

    void finish()
    {
        if (size_ > 0) {
            flush();
        }
    }

private:
    void flush()
    {
        unsigned char line[80];
        size_t written = 0;
        const int rc = mbedtls_base64_encode(line, sizeof line, &written, pending_, size_);
        if (rc != 0) {
            ESP_LOGE(kTag, "mbedtls_base64_encode of %u bytes returned %d", static_cast<unsigned>(size_), rc);
            abort();
        }
        fwrite(line, 1, written, stdout);
        fputc('\n', stdout);
        size_ = 0;
    }

    uint8_t pending_[57];
    std::size_t size_ = 0;
};

void printRuns(Base64Lines& out)
{
    const uint16_t* pixels = static_cast<const uint16_t*>(canvas->getBuffer());
    const std::size_t count = static_cast<std::size_t>(canvas->width()) * canvas->height();
    std::size_t i = 0;
    while (i < count) {
        const uint16_t value = pixels[i];
        uint16_t length = 1;
        while (i + length < count && length < 0xFFFF && pixels[i + length] == value) {
            ++length;
        }
        const uint8_t run[4] = {static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>(length >> 8),
                                static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
        out.add(run, sizeof run);
        i += length;
    }
}

void printScreen(RoundRenderer& renderer, const ui::ScreenModel& model, const DialState& state)
{
    printf("\nRLE565-BEGIN\n");
    Base64Lines out;
    for (int16_t top = 0; top < 240; top += RoundRenderer::kStrip) {
        renderer.draw(model, state, top);
        printRuns(out);
        vTaskDelay(1);
    }
    out.finish();
    printf("RLE565-END\n");
    fflush(stdout);
}

std::size_t newestDayWithData(const ui::UsageView& usage)
{
    for (std::size_t i = usage.days.size(); i > 0; --i) {
        if (usage.days[i - 1].hasData) {
            return i - 1;
        }
    }
    return 0;
}

bool apply(DialState& state, Input input, const ui::ScreenModel& model)
{
    switch (input) {
    case Input::turnRight:
    case Input::turnLeft: {
        int delta = input == Input::turnRight ? 1 : -1;
        int next = (static_cast<int>(state.screen) + delta + static_cast<int>(kScreenCount)) % static_cast<int>(kScreenCount);
        if (!model.showSpotPrices && static_cast<Screen>(next) == Screen::spot) {
            next = (next + delta + static_cast<int>(kScreenCount)) % static_cast<int>(kScreenCount);
        }
        state.screen = static_cast<Screen>(next);
        return true;
    }
    case Input::hold:
        state.screen = Screen::pair;
        source->act(Action::openCommissioningWindow);
        return true;
    case Input::press:
    case Input::tap:
        if (state.screen == Screen::virtualDevices || state.screen == Screen::spotSetting) {
            source->act(state.screen == Screen::virtualDevices ? Action::toggleVirtualDevices : Action::toggleSpotPrices);
            return true;
        }
        if (state.screen == Screen::beep) {
            state.beep = !state.beep;
            nvs_handle_t handle = 0;
            ESP_ERROR_CHECK(nvs_open("flipped_dial", NVS_READWRITE, &handle));
            ESP_ERROR_CHECK(nvs_set_u8(handle, "beep", state.beep ? 1 : 0));
            ESP_ERROR_CHECK(nvs_commit(handle));
            nvs_close(handle);
            return true;
        }
        if (state.screen == Screen::spot) {
            state.spotAhead = !state.spotAhead;
            return true;
        }
        if (state.screen == Screen::usage && !model.usage.days.empty()) {
            state.usageDay = state.usageDay == 0 ? newestDayWithData(model.usage) : state.usageDay - 1;
            return true;
        }
        return false;
    }
    return false;
}

void run(void*)
{
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_mic = false;
    M5.begin(cfg);
    M5.Display.setBrightness(0);
    canvas = new LGFX_Sprite(&M5.Display);
    canvas->setColorDepth(16);
    if (!canvas->createSprite(240, RoundRenderer::kStrip)) {
        ESP_LOGE(kTag, "createSprite 240x24 failed; internal free %u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        abort();
    }
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    knobBegin(task);
    wakeOn(kTouchInterrupt);
    wakeOn(kButton);
    ESP_LOGI(kTag, "M5Stack Dial ready: display %dx%d, internal free %u", M5.Display.width(), M5.Display.height(),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    Assets assets{boltStart, static_cast<size_t>(boltEnd - boltStart), wordmarkStart, static_cast<size_t>(wordmarkEnd - wordmarkStart)};
    auto* logo = new LGFX_Sprite(&M5.Display);
    logo->setColorDepth(16);
    if (!logo->createSprite(70, 28)) {
        ESP_LOGE(kTag, "createSprite 70x28 failed; internal free %u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        abort();
    }
    logo->fillScreen(lgfx::rgb888_t(flipped::ui::theme::background.r, flipped::ui::theme::background.g, flipped::ui::theme::background.b));
    logo->drawPng(logoStart, static_cast<size_t>(logoEnd - logoStart), 0, 0);
    assets.logo = logo;
    RoundRenderer renderer(*canvas, assets);
    for (uint8_t step = 0; step <= 20; ++step) {
        for (int16_t top = 0; top < 240; top += RoundRenderer::kStrip) {
            renderer.drawBoot(step, 20, top);
            canvas->pushSprite(0, top);
            M5.Display.waitDMA();
        }
        if (step == 1) {
            M5.Display.setBrightness(kBrightness);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    DialState state;
    nvs_handle_t settingsHandle = 0;
    ESP_ERROR_CHECK(nvs_open("flipped_dial", NVS_READWRITE, &settingsHandle));
    uint8_t beep = 0;
    const auto beepError = nvs_get_u8(settingsHandle, "beep", &beep);
    if (beepError != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(beepError);
        state.beep = beep != 0;
    }
    nvs_close(settingsHandle);
    ui::ScreenModel model;
    bool redraw = true;
    bool touching = false;
    bool holdFired = false;
    bool dimmed = false;
    bool navigated = false;
    int64_t lastInputUs = esp_timer_get_time();
    int64_t nextMinuteUs = 0;
    UBaseType_t lowestStack = UINT32_MAX;

    for (;;) {
        bool pressed = M5.BtnA.isPressed() || touching;
        int64_t untilMinute = std::max<int64_t>(0, (nextMinuteUs - esp_timer_get_time()) / 1000);
        uint32_t wait = pressed ? kFrameMs : static_cast<uint32_t>(untilMinute);
        uint32_t idle = static_cast<uint32_t>((esp_timer_get_time() - lastInputUs) / 1000);
        if (!dimmed && idle < kDimAfterMs) {
            wait = std::min<uint32_t>(wait, kDimAfterMs - idle + 10);
        }
        uint32_t woke = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));

        if (shotRequested.exchange(false)) {
            printScreen(renderer, model, state);
            xSemaphoreGive(shotDone);
        }

        bool fromInput = inputWake.exchange(false);
        M5.update();
        std::optional<Input> input;
        int32_t steps = knobTakeSteps();
        if (M5.BtnA.isPressed() && M5.BtnA.getUpdateMsec() - M5.BtnA.lastChange() >= kHoldMs && !holdFired) {
            holdFired = true;
            input = Input::hold;
        } else if (M5.BtnA.wasReleased() && !holdFired) {
            input = Input::press;
        }
        if (!M5.BtnA.isPressed()) {
            holdFired = false;
        }
        touching = M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).isPressed();
        if (M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).wasClicked()) {
            input = Input::tap;
        }
        if (hasInjected.exchange(false)) {
            input = injectedInput;
        }

        if (input || steps != 0) {
            navigated = true;
            lastInputUs = esp_timer_get_time();
            if (dimmed) {
                dimmed = false;
                M5.Display.setBrightness(kBrightness);
            }
        }
        if (modelDirty.exchange(false) || (woke > 0 && !fromInput && steps == 0) || esp_timer_get_time() >= nextMinuteUs) {
            const auto previous = model.home.chips;
            model = source->model();
            bool alert = false;
            for (std::size_t i : {ui::kPeakChip, ui::kOffPeakChip, ui::kPriceHighChip, ui::kPriceLowChip}) {
                alert = alert || ((i < ui::kPriceHighChip || model.showSpotPrices) && previous[i].on.has_value() &&
                                  previous[i].on != model.home.chips[i].on && model.home.chips[i].on == true);
            }
            if (state.beep && alert) {
                M5.Speaker.tone(1800, 150);
            }
            if (!model.showSpotPrices && state.screen == Screen::spot) {
                state.screen = Screen::now;
            }
            if (!state.usageChosen && !model.usage.days.empty()) {
                state.usageDay = newestDayWithData(model.usage);
                state.usageChosen = true;
            }
            if (!navigated) {
                state.screen = model.configured ? Screen::now : Screen::pair;
            }
            redraw = true;
            nextMinuteUs = esp_timer_get_time() + static_cast<int64_t>(msUntilNextMinute()) * 1000;
        }
        if (input && apply(state, *input, model)) {
            model = source->model();
            redraw = true;
        }
        for (int32_t i = 0; i < (steps > 0 ? steps : -steps); ++i) {
            apply(state, steps > 0 ? Input::turnRight : Input::turnLeft, model);
            redraw = true;
        }
        if (redraw) {
            M5.Display.startWrite();
            for (int16_t top = 0; top < 240; top += RoundRenderer::kStrip) {
                renderer.draw(model, state, top);
                canvas->pushSprite(0, top);
                M5.Display.waitDMA();
            }
            M5.Display.endWrite();
            redraw = false;
            UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(nullptr);
            if (stackLeft < lowestStack) {
                lowestStack = stackLeft;
                ESP_LOGI(kTag, "ui stack high-water %u bytes free, internal heap minimum %u", static_cast<unsigned>(stackLeft),
                         static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
            }
        }
        idle = static_cast<uint32_t>((esp_timer_get_time() - lastInputUs) / 1000);
        if (!dimmed && idle >= kDimAfterMs && !pressed) {
            dimmed = true;
            M5.Display.setBrightness(kBrightness / 5);
        }
    }
}

}

void startDevice(ModelSource& modelSource)
{
    source = &modelSource;
    xTaskCreatePinnedToCore(run, "ui", kStackBytes, nullptr, 4, &task, 1);
}

void* deviceTask() { return task; }

void notifyModelChanged()
{
    modelDirty = true;
    if (task) {
        xTaskNotifyGive(task);
    }
}

void injectInput(Input input, int16_t, int16_t)
{
    injectedInput = input;
    hasInjected = true;
    if (task) {
        xTaskNotifyGive(task);
    }
}

void printScreenshot()
{
    if (shotDone == nullptr) {
        shotDone = xSemaphoreCreateBinary();
    }
    shotRequested = true;
    xTaskNotifyGive(task);
    xSemaphoreTake(shotDone, portMAX_DELAY);
}

}
