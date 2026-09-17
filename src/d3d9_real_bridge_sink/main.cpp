#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "../d3d9_real_bridge_probe/protocol.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Options {
  HANDLE shared_handle = nullptr;
  HANDLE bootstrap_write = nullptr;
  std::uint32_t parent_pid = 0;
  std::wstring ready_fence_name;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t frames = 0;
  std::uint32_t generation = 0;
  std::uint32_t initial_stall_ms = 0;
  std::uint32_t protocol_version = 0;
  std::uint64_t adapter_luid = 0;
  bool have_adapter_luid = false;
  bool validate_synthetic_pattern = false;
  std::array<std::uint32_t, 5> expected{};
  bool have_expected = false;
  std::wstring log_path;
};

[[nodiscard]] bool parse_u64(const std::wstring &text, std::uint64_t &out) {
  try {
    out = std::stoull(text, nullptr, 0);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] std::uint64_t pack_luid(const LUID &luid) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart))
          << 32U) |
         static_cast<std::uint64_t>(luid.LowPart);
}

[[nodiscard]] bool parse_u32(const std::wstring &text, std::uint32_t &out) {
  try {
    const unsigned long long value = std::stoull(text, nullptr, 0);
    if (value > std::numeric_limits<std::uint32_t>::max())
      return false;
    out = static_cast<std::uint32_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool parse(int argc, wchar_t **argv, Options &out) {
  if ((argc - 1) % 2 != 0)
    return false;
  std::array<bool, 5> have_expected{};
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::wstring_view key(argv[i]);
    const std::wstring value(argv[i + 1]);
    if (key == L"--handle") {
      try {
        out.shared_handle = reinterpret_cast<HANDLE>(
            static_cast<std::uintptr_t>(std::stoull(value, nullptr, 0)));
      } catch (...) {
        return false;
      }
    } else if (key == L"--bootstrap-write") {
      try {
        out.bootstrap_write = reinterpret_cast<HANDLE>(
            static_cast<std::uintptr_t>(std::stoull(value, nullptr, 0)));
      } catch (...) {
        return false;
      }
    } else if (key == L"--parent-pid") {
      if (!parse_u32(value, out.parent_pid))
        return false;
    } else if (key == L"--ready-fence-name") {
      out.ready_fence_name = value;
    } else if (key == L"--width") {
      if (!parse_u32(value, out.width))
        return false;
    } else if (key == L"--height") {
      if (!parse_u32(value, out.height))
        return false;
    } else if (key == L"--frames") {
      if (!parse_u32(value, out.frames))
        return false;
    } else if (key == L"--generation") {
      if (!parse_u32(value, out.generation))
        return false;
    } else if (key == L"--protocol-version") {
      if (!parse_u32(value, out.protocol_version))
        return false;
    } else if (key == L"--adapter-luid") {
      if (!parse_u64(value, out.adapter_luid))
        return false;
      out.have_adapter_luid = true;
    } else if (key == L"--validate-synthetic-pattern") {
      std::uint32_t enabled = 0;
      if (!parse_u32(value, enabled) || enabled > 1)
        return false;
      out.validate_synthetic_pattern = enabled != 0;
    } else if (key == L"--initial-stall-ms") {
      if (!parse_u32(value, out.initial_stall_ms))
        return false;
    } else if (key == L"--log") {
      out.log_path = value;
    } else if (key.size() == 11 && key.starts_with(L"--expected")) {
      const wchar_t suffix = key.back();
      if (suffix < L'0' || suffix > L'4')
        return false;
      const std::size_t index = static_cast<std::size_t>(suffix - L'0');
      if (!parse_u32(value, out.expected[index]))
        return false;
      have_expected[index] = true;
    } else {
      return false;
    }
  }
  out.have_expected = true;
  for (bool present : have_expected)
    out.have_expected = out.have_expected && present;
  const bool multiframe = out.bootstrap_write || out.frames;
  if (multiframe)
    return out.bootstrap_write && out.parent_pid && !out.ready_fence_name.empty() &&
           out.width && out.height && out.frames >= 2 &&
           out.protocol_version == ltr::d3d9_real_bridge::kProtocolVersion &&
           !out.have_expected && !out.log_path.empty();
  return out.shared_handle && out.width && out.height && out.have_expected &&
         !out.log_path.empty();
}

void log_line(const std::wstring &path, const std::string &line) {
  std::ofstream out(path, std::ios::app | std::ios::binary);
  if (out)
    out << line << "\r\n";
}

[[nodiscard]] bool wait_for_fence(ID3D12Fence *fence, std::uint64_t value) {
  if (fence->GetCompletedValue() >= value)
    return true;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event)
    return false;
  const HRESULT hr = fence->SetEventOnCompletion(value, event);
  const DWORD wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
  CloseHandle(event);
  return SUCCEEDED(hr) && wait == WAIT_OBJECT_0;
}

[[nodiscard]] bool validate_contract(ID3D12Resource *resource,
                                     std::uint32_t width,
                                     std::uint32_t height) {
  if (!resource)
    return false;
  const D3D12_RESOURCE_DESC desc = resource->GetDesc();
  return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         desc.Width == width && desc.Height == height &&
         desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && desc.SampleDesc.Count == 1;
}

[[nodiscard]] int run_multiframe(const Options &options,
                                 ID3D12Device *device) {
  const std::uint64_t device_luid = pack_luid(device->GetAdapterLuid());
  if (options.have_adapter_luid && options.adapter_luid != device_luid) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=adapter_identity expected=" +
                 std::to_string(options.adapter_luid) + " actual=" +
                 std::to_string(device_luid) + " RESULT FAIL");
    return 19;
  }
  D3D12_HEAP_PROPERTIES heap_properties{};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC resource_desc{};
  resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Width = options.width;
  resource_desc.Height = options.height;
  resource_desc.DepthOrArraySize = 1;
  resource_desc.MipLevels = 1;
  resource_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  resource_desc.SampleDesc.Count = 1;
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

  std::array<ComPtr<ID3D12Resource>, ltr::d3d9_real_bridge::kRingDepth>
      resources;
  std::array<ComPtr<ID3D12Resource>, ltr::d3d9_real_bridge::kRingDepth>
      consumer_resources;
  std::array<HANDLE, ltr::d3d9_real_bridge::kRingDepth> local_handles{};
  for (std::uint32_t slot = 0; slot < ltr::d3d9_real_bridge::kRingDepth; ++slot) {
    HRESULT hr = device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_SHARED, &resource_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resources[slot]));
    if (SUCCEEDED(hr))
      hr = device->CreateSharedHandle(resources[slot].Get(), nullptr, GENERIC_ALL,
                                      nullptr, &local_handles[slot]);
    if (FAILED(hr) || !local_handles[slot]) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=create_resource slot=" +
                   std::to_string(slot) + " RESULT FAIL");
      for (HANDLE handle : local_handles)
        if (handle)
          CloseHandle(handle);
      return 20;
    }
    D3D12_RESOURCE_DESC consumer_desc = resource_desc;
    consumer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &consumer_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&consumer_resources[slot]));
    if (FAILED(hr) || !consumer_resources[slot]) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=create_consumer_resource slot=" +
                   std::to_string(slot) + " RESULT FAIL");
      for (HANDLE handle : local_handles)
        if (handle)
          CloseHandle(handle);
      return 34;
    }
  }

  ComPtr<ID3D12Fence> done_fence;
  HANDLE local_done_handle = nullptr;
  HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                   IID_PPV_ARGS(&done_fence));
  if (SUCCEEDED(hr))
    hr = device->CreateSharedHandle(done_fence.Get(), nullptr, GENERIC_ALL,
                                    nullptr, &local_done_handle);
  if (FAILED(hr) || !local_done_handle) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=create_done_fence RESULT FAIL");
    for (HANDLE handle : local_handles)
      if (handle)
        CloseHandle(handle);
    return 21;
  }

  HANDLE parent = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE,
                              options.parent_pid);
  if (!parent) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=open_parent RESULT FAIL");
    for (HANDLE handle : local_handles)
      CloseHandle(handle);
    CloseHandle(local_done_handle);
    return 22;
  }

  const auto duplicate_to_parent = [&](HANDLE source, std::uint64_t &target) {
    HANDLE remote = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, parent, &remote, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
      return false;
    target = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(remote));
    return true;
  };

  ltr::d3d9_real_bridge::BootstrapMessage bootstrap{};
  bootstrap.magic = ltr::d3d9_real_bridge::kBootstrapMagic;
  bootstrap.protocol = ltr::d3d9_real_bridge::kProtocolVersion;
  bootstrap.width = options.width;
  bootstrap.height = options.height;
  bootstrap.frames = options.frames;
  bootstrap.ring_depth = ltr::d3d9_real_bridge::kRingDepth;
  bootstrap.adapter_luid = device_luid;
  const bool duplicated =
      duplicate_to_parent(local_handles[0], bootstrap.resource0_handle) &&
      duplicate_to_parent(local_handles[1], bootstrap.resource1_handle) &&
      duplicate_to_parent(local_done_handle, bootstrap.done_fence_handle);
  for (HANDLE handle : local_handles)
    CloseHandle(handle);
  CloseHandle(local_done_handle);
  CloseHandle(parent);
  if (!duplicated) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=duplicate_handles RESULT FAIL");
    return 23;
  }

  DWORD written = 0;
  const BOOL bootstrap_written =
      WriteFile(options.bootstrap_write, &bootstrap, sizeof(bootstrap), &written,
                nullptr);
  CloseHandle(options.bootstrap_write);
  if (!bootstrap_written || written != sizeof(bootstrap)) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=bootstrap_write RESULT FAIL");
    return 24;
  }

  HANDLE ready_handle = nullptr;
  HRESULT open_ready = E_FAIL;
  for (std::uint32_t attempt = 0; attempt < 5000; ++attempt) {
    open_ready = device->OpenSharedHandleByName(options.ready_fence_name.c_str(),
                                                GENERIC_ALL, &ready_handle);
    if (SUCCEEDED(open_ready))
      break;
    Sleep(1);
  }
  ComPtr<ID3D12Fence> ready_fence;
  if (FAILED(open_ready) || !ready_handle ||
      FAILED(device->OpenSharedHandle(ready_handle,
                                      IID_PPV_ARGS(&ready_fence)))) {
    if (ready_handle)
      CloseHandle(ready_handle);
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=open_ready_fence RESULT FAIL");
    return 25;
  }
  CloseHandle(ready_handle);

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=create_queue RESULT FAIL");
    return 26;
  }

  log_line(options.log_path,
           "event=x64_real_bridge_multiframe stage=initialized protocol=" +
               std::to_string(ltr::d3d9_real_bridge::kProtocolVersion) +
               " generation=" + std::to_string(options.generation) +
               " ring_depth=" +
               std::to_string(ltr::d3d9_real_bridge::kRingDepth) + " frames=" +
               std::to_string(options.frames) + " adapter_luid=" +
               std::to_string(device_luid) + " consumer_copy=1");
  if (options.initial_stall_ms)
    Sleep(options.initial_stall_ms);

  D3D12_RESOURCE_DESC shared_desc = resources[0]->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  if (options.validate_synthetic_pattern)
    device->GetCopyableFootprints(&shared_desc, 0, 1, 0, &footprint, &rows,
                                  &row_size, &total_size);
  std::vector<ComPtr<ID3D12Resource>> readbacks(options.frames);
  std::vector<ComPtr<ID3D12CommandAllocator>> allocators(options.frames);
  std::vector<ComPtr<ID3D12GraphicsCommandList>> lists(options.frames);

  for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
    const std::uint32_t slot = frame % ltr::d3d9_real_bridge::kRingDepth;
    const std::uint64_t ready = ltr::d3d9_real_bridge::ready_value(frame);
    const std::uint64_t done = ltr::d3d9_real_bridge::done_value(frame);
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=queue frame=" +
                 std::to_string(frame) + " slot=" + std::to_string(slot) +
                 " ready_target=" + std::to_string(ready) +
                 " ready_completed=" +
                 std::to_string(ready_fence->GetCompletedValue()) +
                 " done_before=" + std::to_string(done_fence->GetCompletedValue()));
    if (FAILED(queue->Wait(ready_fence.Get(), ready))) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=queue_sync frame=" +
                   std::to_string(frame) + " RESULT FAIL");
      return 27;
    }
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[frame]))) ||
        FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[frame].Get(), nullptr,
            IID_PPV_ARGS(&lists[frame])))) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=consumer_setup frame=" +
                   std::to_string(frame) + " RESULT FAIL");
      return 35;
    }

    D3D12_RESOURCE_BARRIER shared_to_copy{};
    shared_to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    shared_to_copy.Transition.pResource = resources[slot].Get();
    shared_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    shared_to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    shared_to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    lists[frame]->ResourceBarrier(1, &shared_to_copy);
    lists[frame]->CopyResource(consumer_resources[slot].Get(), resources[slot].Get());

    if (options.validate_synthetic_pattern) {
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_READBACK;
      D3D12_RESOURCE_DESC buffer{};
      buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buffer.Width = total_size;
      buffer.Height = 1;
      buffer.DepthOrArraySize = 1;
      buffer.MipLevels = 1;
      buffer.SampleDesc.Count = 1;
      buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (FAILED(device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &buffer,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&readbacks[frame])))) {
        log_line(options.log_path,
                 "event=x64_real_bridge_multiframe stage=validation_setup frame=" +
                     std::to_string(frame) + " RESULT FAIL");
        return 28;
      }
      D3D12_RESOURCE_BARRIER consumer_to_source{};
      consumer_to_source.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      consumer_to_source.Transition.pResource = consumer_resources[slot].Get();
      consumer_to_source.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      consumer_to_source.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      consumer_to_source.Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      lists[frame]->ResourceBarrier(1, &consumer_to_source);
      D3D12_TEXTURE_COPY_LOCATION dst{};
      dst.pResource = readbacks[frame].Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      dst.PlacedFootprint = footprint;
      D3D12_TEXTURE_COPY_LOCATION src{};
      src.pResource = consumer_resources[slot].Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      src.SubresourceIndex = 0;
      lists[frame]->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      std::swap(consumer_to_source.Transition.StateBefore,
                consumer_to_source.Transition.StateAfter);
      lists[frame]->ResourceBarrier(1, &consumer_to_source);
    }
    std::swap(shared_to_copy.Transition.StateBefore,
              shared_to_copy.Transition.StateAfter);
    lists[frame]->ResourceBarrier(1, &shared_to_copy);
    if (FAILED(lists[frame]->Close())) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=consumer_close frame=" +
                   std::to_string(frame) + " RESULT FAIL");
      return 29;
    }
    ID3D12CommandList *execute[] = {lists[frame].Get()};
    queue->ExecuteCommandLists(1, execute);
    if (FAILED(queue->Signal(done_fence.Get(), done))) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=signal_done frame=" +
                   std::to_string(frame) + " RESULT FAIL");
      return 30;
    }
  }

  const std::uint64_t final_done =
      ltr::d3d9_real_bridge::done_value(options.frames - 1U);
  if (!wait_for_fence(done_fence.Get(), final_done)) {
    log_line(options.log_path,
             "event=x64_real_bridge_multiframe stage=completion final_done=" +
                 std::to_string(done_fence->GetCompletedValue()) +
                 " target=" + std::to_string(final_done) + " RESULT FAIL");
    return 31;
  }
  std::uint32_t mismatches = 0;
  if (options.validate_synthetic_pattern) {
    for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
      const std::uint8_t *mapped = nullptr;
      D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
      if (FAILED(readbacks[frame]->Map(
              0, &read_range,
              reinterpret_cast<void **>(const_cast<std::uint8_t **>(&mapped)))) ||
          !mapped) {
        log_line(options.log_path,
                 "event=x64_real_bridge_multiframe stage=validation_map frame=" +
                     std::to_string(frame) + " RESULT FAIL");
        return 32;
      }
      std::uint32_t pixel = 0;
      std::memcpy(&pixel, mapped + footprint.Offset, sizeof(pixel));
      D3D12_RANGE written_range{0, 0};
      readbacks[frame]->Unmap(0, &written_range);
      const std::uint32_t expected =
          ltr::d3d9_real_bridge::synthetic_pixel(frame);
      if (pixel != expected) {
        if (mismatches < 3) {
          log_line(options.log_path,
                   "event=x64_real_bridge_multiframe stage=validation_sample frame=" +
                       std::to_string(frame) + " actual=" +
                       std::to_string(pixel) + " expected=" +
                       std::to_string(expected));
        }
        ++mismatches;
      }
    }
    if (mismatches) {
      log_line(options.log_path,
               "event=x64_real_bridge_multiframe stage=validation mismatches=" +
                   std::to_string(mismatches) + " RESULT FAIL");
      return 33;
    }
  }
  log_line(options.log_path,
           "event=x64_real_bridge_multiframe stage=completion final_ready=" +
               std::to_string(ready_fence->GetCompletedValue()) +
               " final_done=" + std::to_string(done_fence->GetCompletedValue()) +
               " generation=" + std::to_string(options.generation) +
               " frames=" + std::to_string(options.frames) +
               " consumer_copies=" + std::to_string(options.frames) +
               " validation_mismatches=" + std::to_string(mismatches) +
               " RESULT PASS");
  return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  static_assert(sizeof(void *) == 8, "real bridge sink must be x64");
  Options options{};
  if (!parse(argc, argv, options))
    return 2;

  ComPtr<ID3D12Device> device;
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device));
  if (FAILED(hr)) {
    log_line(options.log_path, "event=x64_real_bridge_sink stage=device RESULT FAIL");
    return 3;
  }

  if (options.frames)
    return run_multiframe(options, device.Get());

  ComPtr<ID3D12Resource> source;
  hr = device->OpenSharedHandle(options.shared_handle, IID_PPV_ARGS(&source));
  if (FAILED(hr) || !source) {
    log_line(options.log_path, "event=x64_real_bridge_sink stage=open_shared RESULT FAIL");
    return 4;
  }

  const D3D12_RESOURCE_DESC desc = source->GetDesc();
  if (!validate_contract(source.Get(), options.width, options.height)) {
    log_line(options.log_path,
             "event=x64_real_bridge_sink stage=resource_contract RESULT FAIL");
    return 5;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
    return 6;
  ComPtr<ID3D12CommandAllocator> allocator;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&allocator))))
    return 7;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&list))))
    return 8;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size,
                                &total_size);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = total_size;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr, IID_PPV_ARGS(&readback))))
    return 9;

  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = source.Get();
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &to_copy);

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = source.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  std::swap(to_copy.Transition.StateBefore, to_copy.Transition.StateAfter);
  list->ResourceBarrier(1, &to_copy);
  if (FAILED(list->Close()))
    return 10;
  ID3D12CommandList *lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&fence))) ||
      FAILED(queue->Signal(fence.Get(), 1)) || !wait_for_fence(fence.Get(), 1))
    return 11;

  const std::array<std::array<std::uint32_t, 2>, 5> points{{
      {0, 0},
      {options.width / 4, options.height / 4},
      {options.width / 2, options.height / 2},
      {(options.width * 3) / 4, (options.height * 3) / 4},
      {options.width - 1, options.height - 1},
  }};
  const std::uint8_t *mapped = nullptr;
  D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
  if (FAILED(readback->Map(0, &read_range,
                           reinterpret_cast<void **>(const_cast<std::uint8_t **>(&mapped)))) ||
      !mapped)
    return 12;

  std::uint32_t mismatches = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto *pixel = mapped + footprint.Offset +
                        static_cast<std::size_t>(points[i][1]) *
                            footprint.Footprint.RowPitch +
                        static_cast<std::size_t>(points[i][0]) * 4U;
    std::uint32_t packed = 0;
    std::memcpy(&packed, pixel, sizeof(packed));
    if (packed != options.expected[i])
      ++mismatches;
  }
  D3D12_RANGE written_range{0, 0};
  readback->Unmap(0, &written_range);

  std::string result = "event=x64_real_bridge_sink width=" +
                       std::to_string(options.width) + " height=" +
                       std::to_string(options.height) + " format=" +
                       std::to_string(static_cast<unsigned>(desc.Format)) +
                       " samples=5 mismatches=" + std::to_string(mismatches) +
                       " RESULT " + (mismatches == 0 ? "PASS" : "FAIL");
  log_line(options.log_path, result);
  return mismatches == 0 ? 0 : 13;
}
