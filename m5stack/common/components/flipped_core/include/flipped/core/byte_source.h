#pragma once

#include <cstddef>
#include <cstdint>

namespace flipped::core {

struct ByteSource {
    virtual ~ByteSource() = default;
    virtual size_t read(uint8_t *buffer, size_t capacity) = 0;
};

}
