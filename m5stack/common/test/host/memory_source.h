#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "flipped/core/byte_source.h"

class MemorySource : public flipped::core::ByteSource {
public:
    MemorySource(std::string_view data, size_t chunk) : data_(data), chunk_(chunk) {}

    size_t read(uint8_t *buffer, size_t capacity) override
    {
        const size_t count = std::min({capacity, chunk_, data_.size() - position_});
        std::memcpy(buffer, data_.data() + position_, count);
        position_ += count;
        return count;
    }

private:
    std::string_view data_;
    size_t chunk_;
    size_t position_ = 0;
};
