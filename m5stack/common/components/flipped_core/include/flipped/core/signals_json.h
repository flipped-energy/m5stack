#pragma once

#include <string>

#include "flipped/core/signals.h"

namespace flipped::core {

std::string signalsJson(const Signals &signals);
const char *bandName(Band band);
const char *structureName(Structure structure);

}
