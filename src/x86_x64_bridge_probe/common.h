#pragma once

#include <cstdint>

namespace ltr::bridge_probe {

inline constexpr std::uint32_t kWidth = 64;
inline constexpr std::uint32_t kHeight = 64;
inline constexpr std::uint64_t kProducerReadyFence = 1;
inline constexpr std::uint64_t kConsumerDoneFence = 2;

[[nodiscard]] inline std::uint8_t source_r(std::uint32_t x, std::uint32_t y) noexcept {
    return static_cast<std::uint8_t>((x * 3U + y * 5U) & 0xFFU);
}

[[nodiscard]] inline std::uint8_t source_g(std::uint32_t x, std::uint32_t y) noexcept {
    return static_cast<std::uint8_t>((x * 7U + y * 11U) & 0xFFU);
}

[[nodiscard]] inline std::uint8_t source_b(std::uint32_t x, std::uint32_t y) noexcept {
    return static_cast<std::uint8_t>((x * 13U + y * 17U) & 0xFFU);
}

}  // namespace ltr::bridge_probe
