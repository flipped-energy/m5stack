#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace dial {

void knobBegin(TaskHandle_t notify);
int32_t knobTakeSteps();

}
