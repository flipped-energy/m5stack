#include "knob.h"

#include <atomic>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"

namespace dial {

namespace {

constexpr gpio_num_t kPinA = GPIO_NUM_40;
constexpr gpio_num_t kPinB = GPIO_NUM_41;
constexpr int kCountsPerStep = 4;

TaskHandle_t wake = nullptr;
std::atomic<int32_t> counts{0};
uint8_t state = 0;

constexpr int8_t kTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR onEdge(void*)
{
    uint8_t now = static_cast<uint8_t>((gpio_get_level(kPinA) << 1) | gpio_get_level(kPinB));
    state = static_cast<uint8_t>(((state << 2) | now) & 0x0F);
    int8_t delta = kTable[state];
    if (delta != 0) {
        counts.fetch_add(delta);
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(wake, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

}

void knobBegin(TaskHandle_t notify)
{
    wake = notify;
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << kPinA) | (1ULL << kPinB);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));
    state = static_cast<uint8_t>((gpio_get_level(kPinA) << 1) | gpio_get_level(kPinB));
    ESP_ERROR_CHECK(gpio_isr_handler_add(kPinA, onEdge, nullptr));
    ESP_ERROR_CHECK(gpio_isr_handler_add(kPinB, onEdge, nullptr));
    ESP_LOGI("knob", "encoder on GPIO %d / %d, %d counts per step", kPinA, kPinB, kCountsPerStep);
}

int32_t knobTakeSteps()
{
    int32_t total = counts.load();
    int32_t steps = total / kCountsPerStep;
    counts.fetch_sub(steps * kCountsPerStep);
    return steps;
}

}
