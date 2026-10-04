#include "coreaws/device.h"

#include <M5Unified.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <mutex>

#include "board.h"
#include "coreaws/settings.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flipped/ui/led_frame.h"
#include "flipped/ui/navigator.h"
#include "flipped/ui/theme.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "renderer.h"

extern const uint8_t wordmarkStart[] asm("_binary_flipped_wordmark_png_start");
extern const uint8_t wordmarkEnd[] asm("_binary_flipped_wordmark_png_end");
extern const uint8_t boltStart[] asm("_binary_flipped_bolt_png_start");
extern const uint8_t boltEnd[] asm("_binary_flipped_bolt_png_end");
extern const uint8_t headerStart[] asm("_binary_flipped_header_png_start");
extern const uint8_t headerEnd[] asm("_binary_flipped_header_png_end");
extern const uint8_t headerBoltStart[] asm("_binary_flipped_header_bolt_png_start");
extern const uint8_t headerBoltEnd[] asm("_binary_flipped_header_bolt_png_end");

namespace coreaws {

namespace ui = flipped::ui;

namespace {

constexpr const char* kTag = "device";
constexpr uint32_t kFrameMs = 20;
constexpr uint32_t kDimAfterMs = 120000;
constexpr uint32_t kHoldMs = 1000;
constexpr uint32_t kResetHoldMs = 3000;
constexpr int16_t kSwipeMin = 45;
constexpr int16_t kTapSlop = 14;
constexpr uint32_t kStackBytes = 10240;

struct Injected {
    ui::Input input;
    int16_t x;
    int16_t y;
};

ModelSource* source = nullptr;
TaskHandle_t task = nullptr;
Board board;
LGFX_Sprite* front = nullptr;
LGFX_Sprite* back = nullptr;
std::mutex frameMutex;
std::atomic<bool> modelDirty{true};
std::atomic<bool> hasInjected{false};
Injected injected{};
std::atomic<bool> shotRequested{false};
SemaphoreHandle_t shotDone = nullptr;
std::string shotData;

void captureScreen(LGFX_Sprite* sprite)
{
    const uint16_t* pixels = static_cast<const uint16_t*>(sprite->getBuffer());
    const std::size_t count = static_cast<std::size_t>(sprite->width()) * sprite->height();
    std::string runs;
    runs.reserve(32768);
    std::size_t i = 0;
    while (i < count) {
        uint16_t value = pixels[i];
        uint16_t length = 1;
        while (i + length < count && length < 0xFFFF && pixels[i + length] == value) {
            ++length;
        }
        runs.push_back(static_cast<char>(length & 0xFF));
        runs.push_back(static_cast<char>(length >> 8));
        runs.push_back(static_cast<char>(value & 0xFF));
        runs.push_back(static_cast<char>(value >> 8));
        i += length;
    }
    size_t needed = 0;
    mbedtls_base64_encode(nullptr, 0, &needed, reinterpret_cast<const unsigned char*>(runs.data()), runs.size());
    std::string out(needed, static_cast<char>(0));
    size_t written = 0;
    int rc = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &written,
                                   reinterpret_cast<const unsigned char*>(runs.data()), runs.size());
    if (rc != 0) {
        ESP_LOGE(kTag, "base64 encode failed: %d", rc);
        out.clear();
    } else {
        out.resize(written);
    }
    shotData = std::move(out);
}

UiSettings loadSettings()
{
    UiSettings settings;
    nvs_handle_t handle;
    if (nvs_open("flipped_ui", NVS_READONLY, &handle) != ESP_OK) {
        return settings;
    }
    uint8_t value = 0;
    if (nvs_get_u8(handle, "bright", &value) == ESP_OK) {
        settings.brightness = value;
    }
    if (nvs_get_u8(handle, "leds", &value) == ESP_OK) {
        settings.leds = value != 0;
    }
    if (nvs_get_u8(handle, "led_bright", &value) == ESP_OK) {
        settings.ledBrightness = value;
    }
    if (nvs_get_u8(handle, "night", &value) == ESP_OK) {
        settings.dimAtNight = value != 0;
    }
    if (nvs_get_u8(handle, "beep", &value) == ESP_OK) {
        settings.beep = value != 0;
    }
    nvs_close(handle);
    return settings;
}

void saveSettings(const UiSettings& settings)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("flipped_ui", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "bright", settings.brightness));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "leds", settings.leds ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "led_bright", settings.ledBrightness));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "night", settings.dimAtNight ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "beep", settings.beep ? 1 : 0));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

LGFX_Sprite* makeCanvas()
{
    auto* sprite = new LGFX_Sprite(&M5.Display);
    sprite->setColorDepth(16);
    sprite->setPsram(true);
    if (!sprite->createSprite(320, 240)) {
        ESP_LOGE(kTag, "createSprite 320x240 failed; free PSRAM %u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        abort();
    }
    return sprite;
}

bool isNight(const ui::ScreenModel& model)
{
    return model.localMinute && (*model.localMinute >= 22 * 60 || *model.localMinute < 6 * 60);
}

ui::Tone planTone(const ui::ScreenModel& model)
{
    const auto& chips = model.home.chips;
    if (!chips[ui::kPeakChip].on || !chips[ui::kOffPeakChip].on) {
        return model.configured ? ui::Tone::fault : ui::Tone::neutral;
    }
    if (*chips[ui::kPeakChip].on) {
        return ui::Tone::high;
    }
    if (*chips[ui::kOffPeakChip].on) {
        return model.home.rate.tone == ui::Tone::free ? ui::Tone::free : ui::Tone::low;
    }
    return ui::Tone::mid;
}

ui::Tone wholesaleTone(const ui::ScreenModel& model)
{
    if (!model.showSpotPrices) return ui::Tone::neutral;
    const auto& chips = model.home.chips;
    if (!chips[ui::kPriceHighChip].on || !chips[ui::kPriceLowChip].on) {
        return model.configured ? ui::Tone::fault : ui::Tone::neutral;
    }
    if (model.home.spot.tone == ui::Tone::spike) {
        return ui::Tone::spike;
    }
    if (*chips[ui::kPriceHighChip].on) {
        return ui::Tone::high;
    }
    if (*chips[ui::kPriceLowChip].on) {
        return ui::Tone::low;
    }
    return ui::Tone::mid;
}

float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

void present(LGFX_Sprite* sprite)
{
    std::lock_guard<std::mutex> lock(frameMutex);
    sprite->pushSprite(0, 0);
}

void slide(int direction)
{
    constexpr int frames = 9;
    M5.Display.setClipRect(0, 25, 320, 179);
    for (int i = 1; i <= frames; ++i) {
        int offset = static_cast<int>(std::lround(320.0f * easeOut(static_cast<float>(i) / frames)));
        std::lock_guard<std::mutex> lock(frameMutex);
        front->pushSprite(-direction * offset, 0);
        back->pushSprite(direction * (320 - offset), 0);
    }
    M5.Display.clearClipRect();
}

uint32_t msUntilNextMinute()
{
    time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);
    return static_cast<uint32_t>(60 - parts.tm_sec) * 1000 + 50;
}

struct Gesture {
    bool active = false;
    int16_t startX = 0;
    int16_t startY = 0;
    int16_t lastX = 0;
    int16_t lastY = 0;
    int64_t startUs = 0;
};

void run(void*)
{
    board.begin(task);
    UiSettings settings = loadSettings();
    front = makeCanvas();
    back = makeCanvas();
    Assets assets{wordmarkStart, static_cast<size_t>(wordmarkEnd - wordmarkStart), boltStart, static_cast<size_t>(boltEnd - boltStart)};
    auto* headerLogo = new LGFX_Sprite(&M5.Display);
    headerLogo->setColorDepth(16);
    headerLogo->setPsram(true);
    headerLogo->createSprite(96, 24);
    headerLogo->fillScreen(lgfx::rgb888_t(flipped::ui::theme::background.r, flipped::ui::theme::background.g, flipped::ui::theme::background.b));
    headerLogo->drawPng(headerStart, static_cast<size_t>(headerEnd - headerStart), 0, 0);
    auto* headerBolt = new LGFX_Sprite(&M5.Display);
    headerBolt->setColorDepth(16);
    headerBolt->setPsram(true);
    headerBolt->createSprite(22, 24);
    headerBolt->fillScreen(lgfx::rgb888_t(flipped::ui::theme::background.r, flipped::ui::theme::background.g, flipped::ui::theme::background.b));
    headerBolt->drawPng(headerBoltStart, static_cast<size_t>(headerBoltEnd - headerBoltStart), 0, 0);
    assets.headerLogo = headerLogo;
    assets.headerBolt = headerBolt;
    Renderer rendererA(*front, assets);
    Renderer rendererB(*back, assets);
    Renderer* frontRenderer = &rendererA;
    Renderer* backRenderer = &rendererB;

    constexpr uint8_t bootSteps = 24;
    for (uint8_t step = 0; step <= bootSteps; ++step) {
        frontRenderer->drawBoot(step, bootSteps, "Starting");
        present(front);
        if (step == 1) {
            board.setBrightness(settings.brightness);
        }
        ui::LedInput sweep;
        sweep.pairing = true;
        sweep.brightness = 40;
        sweep.phaseMs = step * 60u;
        board.setLeds(ui::ledFrame(sweep));
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ui::Navigator navigator;
    ui::ScreenModel model;
    Gesture gesture;
    bool dimmed = false;
    bool redraw = true;
    int64_t lastInputUs = esp_timer_get_time();
    int64_t ledEpochUs = esp_timer_get_time();
    bool holdFired = false;
    uint32_t resetHeldMs = 0;
    int64_t nextMinuteUs = 0;
    UBaseType_t lowestStack = UINT32_MAX;

    for (;;) {
        bool interacting = gesture.active || resetHeldMs > 0;
        ui::LedInput leds;
        leds.plan = planTone(model);
        leds.wholesale = wholesaleTone(model);
        leds.pairing = navigator.state().overlay == ui::Overlay::pairing && !model.configured;
        leds.enabled = settings.leds && !(settings.dimAtNight && isNight(model));
        leds.brightness = settings.ledBrightness;
        leds.phaseMs = static_cast<uint32_t>((esp_timer_get_time() - ledEpochUs) / 1000);
        bool animating = ui::ledAnimated(leds);
        int64_t untilMinuteMs = std::max<int64_t>(0, (nextMinuteUs - esp_timer_get_time()) / 1000);
        uint32_t waitMs = interacting ? kFrameMs : (animating ? 40 : static_cast<uint32_t>(untilMinuteMs));
        uint32_t sinceInput = static_cast<uint32_t>((esp_timer_get_time() - lastInputUs) / 1000);
        if (!dimmed && sinceInput < kDimAfterMs) {
            waitMs = std::min<uint32_t>(waitMs, kDimAfterMs - sinceInput + 10);
        }
        uint32_t woke = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));

        if (shotRequested.exchange(false)) {
            captureScreen(front);
            xSemaphoreGive(shotDone);
        }

        TouchState touch = board.read();
        bool anyDown = touch.screenDown || touch.buttonDown[0] || touch.buttonDown[1] || touch.buttonDown[2];
        std::optional<ui::Input> input;
        ui::Hit hit;

        if (touch.screenDown) {
            if (!gesture.active) {
                gesture = Gesture{true, touch.x, touch.y, touch.x, touch.y, esp_timer_get_time()};
            } else {
                gesture.lastX = touch.x;
                gesture.lastY = touch.y;
            }
        } else if (gesture.active) {
            int16_t dx = static_cast<int16_t>(gesture.lastX - gesture.startX);
            int16_t dy = static_cast<int16_t>(gesture.lastY - gesture.startY);
            const int settingsRow = (gesture.startY - Renderer::kSettingsRowTop) / Renderer::kSettingsRowHeight + std::min(navigator.state().scroll, 2);
            if (navigator.state().overlay == ui::Overlay::settings && gesture.startY >= Renderer::kSettingsRowTop && settingsRow == 1 && gesture.startX >= 200 && gesture.startX <= 306) {
                input = ui::Input::tap;
                hit = ui::Hit{static_cast<int16_t>(std::clamp<int>(gesture.lastX, 200, 306)), gesture.startY};
            } else if (std::abs(dx) >= kSwipeMin && std::abs(dx) > std::abs(dy)) {
                input = dx < 0 ? ui::Input::swipeLeft : ui::Input::swipeRight;
            } else if (std::abs(dy) >= kSwipeMin) {
                input = dy < 0 ? ui::Input::swipeUp : ui::Input::swipeDown;
            } else if (std::abs(dx) <= kTapSlop && std::abs(dy) <= kTapSlop) {
                input = ui::Input::tap;
                hit = ui::Hit{gesture.startX, gesture.startY};
            }
            gesture.active = false;
        }
        if (touch.buttonClicked[0]) {
            input = ui::Input::buttonA;
        }
        if (touch.buttonClicked[2]) {
            input = ui::Input::buttonC;
        }
        if (navigator.state().overlay == ui::Overlay::reset && touch.buttonDown[1]) {
            uint32_t held = touch.buttonHeldMs[1];
            if (held != resetHeldMs) {
                resetHeldMs = held;
                redraw = true;
            }
            if (held >= kResetHoldMs && !holdFired) {
                holdFired = true;
                board.haptic();
                ESP_LOGW(kTag, "factory reset confirmed on the device");
                source->act(Action::eraseEverything);
            }
        } else {
            if (resetHeldMs > 0) {
                redraw = true;
            }
            resetHeldMs = 0;
            if (touch.buttonDown[1] && touch.buttonHeldMs[1] >= kHoldMs && !holdFired) {
                holdFired = true;
                input = ui::Input::holdB;
            } else if (touch.buttonClicked[1] && !holdFired) {
                input = ui::Input::buttonB;
            }
        }
        if (!touch.buttonDown[1]) {
            holdFired = false;
        }
        if (hasInjected.exchange(false)) {
            input = injected.input;
            hit = ui::Hit{injected.x, injected.y};
        }

        if (anyDown || input) {
            lastInputUs = esp_timer_get_time();
            if (dimmed) {
                dimmed = false;
                board.setBrightness(settings.brightness);
                input.reset();
            }
        }

        bool touchWoke = board.takeTouchWake();
        bool released = !touch.screenDown && !anyDown && input.has_value();
        bool minutePassed = esp_timer_get_time() >= nextMinuteUs;
        if (modelDirty.exchange(false) || (woke > 0 && !touchWoke) || released || minutePassed) {
            const auto previous = model.home.chips;
            model = source->model();
            bool alert = false;
            for (std::size_t i : {ui::kPeakChip, ui::kOffPeakChip, ui::kPriceHighChip, ui::kPriceLowChip}) {
                alert = alert || ((i < ui::kPriceHighChip || model.showSpotPrices) && previous[i].on.has_value() && previous[i].on != model.home.chips[i].on &&
                                  model.home.chips[i].on == true);
            }
            if (settings.beep && alert) {
                M5.Speaker.tone(1800, 150);
            }
            navigator.sync(model);
            redraw = true;
            nextMinuteUs = esp_timer_get_time() + static_cast<int64_t>(msUntilNextMinute()) * 1000;
        }

        int direction = 0;
        if (input) {
            bool changed = false;
            if (*input == ui::Input::tap && navigator.state().overlay == ui::Overlay::settings) {
                int row = (hit.y - Renderer::kSettingsRowTop) / Renderer::kSettingsRowHeight + std::min(navigator.state().scroll, 2);
                if (hit.y >= Renderer::kSettingsRowTop && row == 0 && hit.x >= 196 && hit.x < 232) {
                    settings.brightness = static_cast<uint8_t>(std::max(30, settings.brightness - 25));
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 0 && hit.x >= 276) {
                    settings.brightness = static_cast<uint8_t>(std::min(255, settings.brightness + 25));
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 1 && hit.x >= 200 && hit.x <= 306) {
                    settings.ledBrightness = static_cast<uint8_t>((hit.x - 200) * 255 / 106);
                    settings.leds = settings.ledBrightness > 0;
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 2 && hit.x >= 240) {
                    settings.dimAtNight = !settings.dimAtNight;
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 3 && hit.x >= 240) {
                    settings.beep = !settings.beep;
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 4 && hit.x >= 240) {
                    source->act(Action::toggleVirtualDevices);
                    model = source->model();
                    changed = true;
                } else if (hit.y >= Renderer::kSettingsRowTop && row == 5 && hit.x >= 240) {
                    source->act(Action::toggleSpotPrices);
                    model = source->model();
                    navigator.sync(model);
                    changed = true;
                }
                if (changed) {
                    saveSettings(settings);
                    board.setBrightness(settings.brightness);
                }
            }
            if (!changed) {
                ui::Overlay before = navigator.state().overlay;
                changed = navigator.apply(*input, hit, model);
                if (changed && navigator.state().overlay == ui::Overlay::pairing && before != ui::Overlay::pairing) {
                    source->act(Action::openCommissioningWindow);
                }
                direction = navigator.direction();
            }
            if (changed) {
                board.haptic();
                redraw = true;
            }
        }

        if (redraw) {
            uint8_t holdPercent = static_cast<uint8_t>(std::min<uint32_t>(100, resetHeldMs * 100 / kResetHoldMs));
            backRenderer->draw(model, navigator.state(), settings, holdPercent);
            if (direction != 0) {
                slide(direction);
            }
            std::swap(front, back);
            std::swap(frontRenderer, backRenderer);
            present(front);
            redraw = false;
            UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(nullptr);
            if (stackLeft < lowestStack) {
                lowestStack = stackLeft;
                ESP_LOGI(kTag, "ui stack high-water %u bytes free, internal heap minimum %u", static_cast<unsigned>(stackLeft),
                         static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
            }
        }

        board.setLeds(ui::ledFrame(leds));

        sinceInput = static_cast<uint32_t>((esp_timer_get_time() - lastInputUs) / 1000);
        bool night = settings.dimAtNight && isNight(model);
        if (!dimmed && sinceInput >= kDimAfterMs && !anyDown) {
            dimmed = true;
            board.setBrightness(night ? 8 : settings.brightness / 5);
        }
    }
}

}

void startDevice(ModelSource& modelSource)
{
    source = &modelSource;
    xTaskCreatePinnedToCore(run, "ui", kStackBytes, nullptr, 4, &task, 1);
}

TaskHandle_t deviceTask() { return task; }

void notifyModelChanged()
{
    modelDirty = true;
    if (task) {
        xTaskNotifyGive(task);
    }
}

void injectInput(ui::Input input, int16_t x, int16_t y)
{
    injected = Injected{input, x, y};
    hasInjected = true;
    if (task) {
        xTaskNotifyGive(task);
    }
}

std::string screenshotBase64()
{
    shotDone = xSemaphoreCreateBinary();
    shotRequested = true;
    xTaskNotifyGive(task);
    bool done = xSemaphoreTake(shotDone, pdMS_TO_TICKS(10000)) == pdTRUE;
    vSemaphoreDelete(shotDone);
    shotDone = nullptr;
    if (!done) {
        ESP_LOGE(kTag, "screenshot timed out waiting for the ui task");
        return {};
    }
    return std::move(shotData);
}

}
