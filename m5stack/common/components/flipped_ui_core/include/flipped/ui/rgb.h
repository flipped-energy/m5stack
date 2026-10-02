#pragma once

#include <cstdint>

namespace flipped::ui {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

}
