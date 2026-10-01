#include "../src/d3d9_real_bridge_probe/owned_handles.h"
#include "../src/d3d9_real_bridge_probe/protocol.h"

#include <Windows.h>

#include <array>
#include <cstdint>

namespace {

[[nodiscard]] bool handle_is_open(HANDLE handle) {
  DWORD flags = 0;
  SetLastError(ERROR_SUCCESS);
  return GetHandleInformation(handle, &flags) != FALSE;
}

} // namespace

int main() {
  std::array<HANDLE, 3> raw = {
      CreateEventW(nullptr, FALSE, FALSE, nullptr),
      CreateEventW(nullptr, FALSE, FALSE, nullptr),
      CreateEventW(nullptr, FALSE, FALSE, nullptr),
  };
  for (HANDLE handle : raw) {
    if (!handle)
      return 1;
  }

  ltr::d3d9_real_bridge::BootstrapMessage bootstrap{};
  bootstrap.resource0_handle = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(raw[0]));
  bootstrap.resource1_handle = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(raw[1]));
  bootstrap.done_fence_handle = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(raw[2]));

  {
    ltr::d3d9_real_bridge::OwnedBootstrapHandles handles(bootstrap);
    if (handles.resource(0) != raw[0] || handles.resource(1) != raw[1] ||
        handles.done_fence() != raw[2])
      return 2;
  }

  for (HANDLE handle : raw) {
    if (handle_is_open(handle)) {
      CloseHandle(handle);
      return 3;
    }
    if (GetLastError() != ERROR_INVALID_HANDLE)
      return 4;
  }
  return 0;
}
