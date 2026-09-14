#pragma once

#include <cstdint>

namespace ltr::temporal {

struct JitterSample {
    float x_pixels = 0.0f;
    float y_pixels = 0.0f;
};

inline float Halton(std::uint64_t index, std::uint32_t base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

inline JitterSample JitterForFrame(std::uint64_t frameIndex)
{
    const std::uint64_t jitterIndex = (frameIndex % 8u) + 1u;
    return {Halton(jitterIndex, 2) - 0.5f, Halton(jitterIndex, 3) - 0.5f};
}

} // namespace ltr::temporal
