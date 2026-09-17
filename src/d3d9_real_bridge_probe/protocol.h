#pragma once

#include <cstdint>

namespace ltr::d3d9_real_bridge {

inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kRingDepth = 2;
inline constexpr std::uint32_t kDefaultFrames = 12;
inline constexpr std::uint32_t kBootstrapMagic = 0x4C545239U;

#pragma pack(push, 1)
struct BootstrapMessage {
  std::uint32_t magic;
  std::uint32_t protocol;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t frames;
  std::uint32_t ring_depth;
  std::uint64_t adapter_luid;
  std::uint64_t resource0_handle;
  std::uint64_t resource1_handle;
  std::uint64_t done_fence_handle;
};
#pragma pack(pop)

static_assert(sizeof(BootstrapMessage) == 56);

[[nodiscard]] inline constexpr std::uint64_t ready_value(
    std::uint32_t frame_index) noexcept {
  return static_cast<std::uint64_t>(frame_index) + 1ULL;
}

[[nodiscard]] inline constexpr std::uint64_t done_value(
    std::uint32_t frame_index) noexcept {
  return static_cast<std::uint64_t>(frame_index) + 1ULL;
}

[[nodiscard]] inline constexpr std::uint64_t reuse_done_value(
    std::uint32_t submitted_frames) noexcept {
  return submitted_frames < kRingDepth
             ? 0ULL
             : done_value(submitted_frames - kRingDepth);
}

[[nodiscard]] inline constexpr std::uint32_t synthetic_pixel(
    std::uint32_t frame_index) noexcept {
  return 0xFF000000U | ((frame_index * 37U) & 0xFFU) |
         (((frame_index * 53U) & 0xFFU) << 8U) |
         (((frame_index * 71U) & 0xFFU) << 16U);
}

} // namespace ltr::d3d9_real_bridge
