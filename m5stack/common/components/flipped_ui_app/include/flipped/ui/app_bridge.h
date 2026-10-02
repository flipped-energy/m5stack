#pragma once

#include "flipped/app/ui_model.h"
#include "flipped/ui/view.h"

namespace flipped::ui {

ScreenModel screenFromApp();
ScreenModel fromModel(const app::UiModel& model);

}
