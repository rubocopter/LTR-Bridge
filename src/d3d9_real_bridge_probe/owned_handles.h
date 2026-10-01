#pragma once

#include "protocol.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ltr::d3d9_real_bridge {

class OwnedBootstrapHandles {
public:
  explicit OwnedBootstrapHandles(const BootstrapMessage &bootstrap) noexcept
      : handles_{to_handle(bootstrap.resource0_handle),
                 to_handle(bootstrap.resource1_handle),
                 to_handle(bootstrap.done_fence_handle)} {}

  OwnedBootstrapHandles(const OwnedBootstrapHandles &) = delete;
  OwnedBootstrapHandles &operator=(const OwnedBootstrapHandles &) = delete;

  ~OwnedBootstrapHandles() {
    for (HANDLE handle : handles_) {
      if (handle)
        CloseHandle(handle);
    }
  }

  [[nodiscard]] HANDLE resource(std::size_t slot) const noexcept {
    return handles_[slot];
  }

  [[nodiscard]] HANDLE done_fence() const noexcept { return handles_[2]; }

private:
  [[nodiscard]] static HANDLE to_handle(std::uint64_t value) noexcept {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
  }

  std::array<HANDLE, 3> handles_{};
};

} // namespace ltr::d3d9_real_bridge
