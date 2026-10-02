#pragma once

#include "flipped/ui/rgb.h"
#include "flipped/ui/view.h"

namespace flipped::ui::theme {

constexpr Rgb background{11, 17, 32};
constexpr Rgb surface{20, 29, 48};
constexpr Rgb surfaceHigh{31, 41, 64};
constexpr Rgb line{55, 65, 81};
constexpr Rgb text{243, 244, 246};
constexpr Rgb textDim{156, 163, 175};
constexpr Rgb primary{6, 182, 212};
constexpr Rgb primaryDark{8, 145, 178};
constexpr Rgb low{16, 185, 129};
constexpr Rgb high{245, 158, 11};
constexpr Rgb spike{239, 68, 68};
constexpr Rgb fault{239, 68, 68};
constexpr Rgb free{52, 211, 153};

constexpr Rgb toneColour(Tone tone)
{
    switch (tone) {
    case Tone::low:
        return low;
    case Tone::mid:
        return primary;
    case Tone::high:
        return high;
    case Tone::spike:
        return spike;
    case Tone::fault:
        return fault;
    case Tone::free:
        return free;
    case Tone::neutral:
        return textDim;
    }
    return textDim;
}

}
