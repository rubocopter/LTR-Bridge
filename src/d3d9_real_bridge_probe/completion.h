#pragma once

#include <cstdint>

namespace ltr::d3d9_real_bridge {

inline constexpr unsigned long kCancelledChildExitCode = 90;

enum class CompletionResult {
  pass,
  cancelled,
  fail,
};

[[nodiscard]] inline constexpr CompletionResult classify_completion(
    bool cancellation_requested, unsigned long child_exit,
    std::uint32_t submitted, std::uint32_t expected_frames,
    bool done_reached) noexcept {
  if (child_exit == 0 && submitted == expected_frames && done_reached)
    return CompletionResult::pass;
  if (cancellation_requested && child_exit == kCancelledChildExitCode)
    return CompletionResult::cancelled;
  return CompletionResult::fail;
}

} // namespace ltr::d3d9_real_bridge
