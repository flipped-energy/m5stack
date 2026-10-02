#include "board.h"

#include <M5Unified.h>

#include <atomic>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

namespace coreaws {

namespace {

constexpr const char* kTag = "board";
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_39;
constexpr gpio_num_t kLedData = GPIO_NUM_25;

led_strip_handle_t strip = nullptr;
TaskHandle_t wakeTask = nullptr;
std::atomic<bool> touchWake{false};

void IRAM_ATTR onTouchInterrupt(void*)
{
    BaseType_t woken = pdFALSE;
    touchWake = true;
    if (wakeTask) {
        vTaskNotifyGiveFromISR(wakeTask, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

}

void Board::begin(TaskHandle_t notify)
{
    wakeTask = notify;
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_mic = false;
    cfg.internal_spk = true;
    cfg.output_power = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(0);

    led_strip_config_t stripConfig = {};
    stripConfig.strip_gpio_num = kLedData;
    stripConfig.max_leds = flipped::ui::kLedCount;
    stripConfig.led_model = LED_MODEL_SK6812;
    stripConfig.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmtConfig = {};
    rmtConfig.resolution_hz = 10 * 1000 * 1000;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&stripConfig, &rmtConfig, &strip));
    ESP_ERROR_CHECK(led_strip_clear(strip));

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << kTouchInterrupt;
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));
    esp_err_t installed = gpio_install_isr_service(0);
    if (installed != ESP_OK && installed != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(installed);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(kTouchInterrupt, onTouchInterrupt, nullptr));
    ESP_LOGI(kTag, "Core2 for AWS ready: display %dx%d, touch interrupt GPIO %d, %d LEDs on GPIO %d", M5.Display.width(),
             M5.Display.height(), kTouchInterrupt, static_cast<int>(flipped::ui::kLedCount), kLedData);
}

TouchState Board::read()
{
    M5.update();
    TouchState state;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.isPressed() && detail.y < 240) {
            state.screenDown = true;
            state.x = detail.x;
            state.y = detail.y;
        }
    }
    m5::Button_Class* buttons[3] = {&M5.BtnA, &M5.BtnB, &M5.BtnC};
    for (int i = 0; i < 3; ++i) {
        state.buttonDown[i] = buttons[i]->isPressed();
        state.buttonClicked[i] = buttons[i]->wasClicked();
        state.buttonHeldMs[i] = buttons[i]->isPressed() ? buttons[i]->getUpdateMsec() - buttons[i]->lastChange() : 0;
    }
    return state;
}

bool Board::takeTouchWake() { return touchWake.exchange(false); }

void Board::setBrightness(uint8_t level) { M5.Display.setBrightness(level); }

void Board::setLeds(const std::array<flipped::ui::Rgb, flipped::ui::kLedCount>& frame)
{
    for (std::size_t i = 0; i < frame.size(); ++i) {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, static_cast<uint32_t>(i), frame[i].r, frame[i].g, frame[i].b));
    }
    ESP_ERROR_CHECK(led_strip_refresh(strip));
}

void Board::haptic()
{
    M5.Power.setVibration(160);
    vTaskDelay(pdMS_TO_TICKS(35));
    M5.Power.setVibration(0);
}

}
