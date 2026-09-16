#pragma once
#include <cstdint>
namespace ltr::bridge_probe {
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kFramesPerGeneration = 12;
inline constexpr std::uint32_t kGenerationCount = 2;
inline constexpr std::uint32_t kControlMagic = 0x4C545242U;
struct GenerationSpec {
  std::uint32_t width;
  std::uint32_t height;
};
#pragma pack(push, 1)
struct DynamicResourceMessage {
  std::uint32_t magic;
  std::uint32_t protocol;
  std::uint32_t generation;
  std::uint32_t width;
  std::uint32_t height;
  std::uint64_t resource_handle;
};
#pragma pack(pop)
static_assert(sizeof(DynamicResourceMessage) == 28);
inline constexpr GenerationSpec kGenerations[kGenerationCount] = {{64, 64},
                                                                  {96, 72}};
[[nodiscard]] inline std::uint64_t ready_fence(std::uint32_t frame) noexcept {
  return static_cast<std::uint64_t>(frame) * 2ULL + 1ULL;
}
[[nodiscard]] inline std::uint64_t done_fence(std::uint32_t frame) noexcept {
  return static_cast<std::uint64_t>(frame) * 2ULL + 2ULL;
}
[[nodiscard]] inline std::uint8_t source_r(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t frame) noexcept {
  return static_cast<std::uint8_t>((x * 3U + y * 5U + frame * 19U) & 0xFFU);
}
[[nodiscard]] inline std::uint8_t source_g(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t frame) noexcept {
  return static_cast<std::uint8_t>((x * 7U + y * 11U + frame * 23U) & 0xFFU);
}
[[nodiscard]] inline std::uint8_t source_b(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t frame) noexcept {
  return static_cast<std::uint8_t>((x * 13U + y * 17U + frame * 29U) & 0xFFU);
}
} // namespace ltr::bridge_probe
