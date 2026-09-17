#include "client.h"
#include "protocol.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Options {
  std::wstring sink_path;
  std::uint32_t width = 64;
  std::uint32_t height = 64;
  std::uint32_t frames = ltr::d3d9_real_bridge::kDefaultFrames;
  std::uint32_t initial_stall_ms = 50;
};

[[nodiscard]] bool parse_u32(const std::wstring &text, std::uint32_t &out,
                             bool allow_zero = false) {
  try {
    const unsigned long long value = std::stoull(text, nullptr, 0);
    if ((!allow_zero && !value) ||
        value > std::numeric_limits<std::uint32_t>::max())
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
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::wstring_view key(argv[i]);
    const std::wstring value(argv[i + 1]);
    if (key == L"--sink")
      out.sink_path = value;
    else if (key == L"--width") {
      if (!parse_u32(value, out.width))
        return false;
    } else if (key == L"--height") {
      if (!parse_u32(value, out.height))
        return false;
    } else if (key == L"--frames") {
      if (!parse_u32(value, out.frames))
        return false;
    } else if (key == L"--initial-stall-ms") {
      if (!parse_u32(value, out.initial_stall_ms, true))
        return false;
    } else {
      return false;
    }
  }
  return !out.sink_path.empty() &&
         out.frames >= ltr::d3d9_real_bridge::kRingDepth;
}

[[nodiscard]] std::wstring make_log_path() {
  wchar_t temp[MAX_PATH]{};
  const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
  if (!length || length >= std::size(temp))
    return L"ltr_d3d9_real_bridge_transport.log";
  std::wostringstream out;
  out << temp << L"ltr_d3d9_real_bridge_transport_" << GetCurrentProcessId()
      << L".log";
  return out.str();
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  static_assert(sizeof(void *) == 4, "real bridge producer probe must be x86");
  Options options{};
  if (!parse(argc, argv, options)) {
    std::cerr << "usage: --sink <x64-exe> [--width N --height N --frames N "
                 "--initial-stall-ms N]\n";
    return 2;
  }

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature_level{};
  const HRESULT create = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
      &feature_level, &context);
  if (FAILED(create)) {
    std::cerr << "stage=device hr=0x" << std::hex
              << static_cast<unsigned long>(create) << "\nRESULT FAIL\n";
    return 3;
  }

  D3D11_TEXTURE2D_DESC source_desc{};
  source_desc.Width = options.width;
  source_desc.Height = options.height;
  source_desc.MipLevels = 1;
  source_desc.ArraySize = 1;
  source_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  source_desc.SampleDesc.Count = 1;
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  std::array<ComPtr<ID3D11Texture2D>, ltr::d3d9_real_bridge::kRingDepth>
      local_sources;
  for (auto &source : local_sources) {
    if (FAILED(device->CreateTexture2D(&source_desc, nullptr, &source))) {
      std::cerr << "stage=create_local_source\nRESULT FAIL\n";
      return 4;
    }
  }

  ltr::d3d9_real_bridge::ClientOptions client_options{};
  client_options.sink_path = options.sink_path;
  client_options.log_path = make_log_path();
  client_options.width = options.width;
  client_options.height = options.height;
  client_options.frames = options.frames;
  client_options.initial_stall_ms = options.initial_stall_ms;
  client_options.validate_synthetic_pattern = true;

  ltr::d3d9_real_bridge::Client client;
  if (!client.Start(device.Get(), context.Get(), client_options,
                    [](const std::string &line) { std::cout << line << '\n'; })) {
    std::cerr << "stage=client_start\nRESULT FAIL\n";
    return 5;
  }

  std::vector<std::uint32_t> pixels(
      static_cast<std::size_t>(options.width) * options.height);
  std::uint32_t next_frame = 0;
  while (next_frame < options.frames) {
    const auto availability = client.QuerySubmitStatus();
    if (availability == ltr::d3d9_real_bridge::SubmitStatus::backpressure) {
      Sleep(1);
      continue;
    }
    if (availability != ltr::d3d9_real_bridge::SubmitStatus::submitted) {
      std::cerr << "stage=availability frame=" << next_frame
                << "\nRESULT FAIL\n";
      return 6;
    }

    std::fill(pixels.begin(), pixels.end(),
              ltr::d3d9_real_bridge::synthetic_pixel(next_frame));
    const std::uint32_t slot =
        next_frame % ltr::d3d9_real_bridge::kRingDepth;
    context->UpdateSubresource(local_sources[slot].Get(), 0, nullptr,
                               pixels.data(), options.width * 4U, 0);
    const auto status = client.TrySubmit(local_sources[slot].Get());
    if (status == ltr::d3d9_real_bridge::SubmitStatus::submitted) {
      ++next_frame;
      continue;
    }
    std::cerr << "stage=submit frame=" << next_frame << "\nRESULT FAIL\n";
    return 6;
  }

  const ULONGLONG deadline = GetTickCount64() + 10000;
  while (!client.Poll() && GetTickCount64() < deadline)
    Sleep(1);
  const auto snapshot = client.Snapshot();
  const bool pressure_ok =
      options.initial_stall_ms == 0 || snapshot.backpressure_checks > 0;
  const bool passed = snapshot.finished && snapshot.passed && pressure_ok;
  std::cout << "event=producer_complete frames=" << snapshot.submitted
            << " final_done=" << snapshot.done_completed
            << " backpressure_checks=" << snapshot.backpressure_checks
            << " child_liveness_checks=" << snapshot.child_liveness_checks
            << " adapter_luid=" << snapshot.adapter_luid
            << " child_exit=" << snapshot.child_exit << " RESULT "
            << (passed ? "PASS" : "FAIL") << '\n';
  return passed ? 0 : 7;
}
