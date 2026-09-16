#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Arguments {
  std::wstring loader;
};

[[nodiscard]] bool parse(int argc, wchar_t **argv, Arguments &args) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring_view key = argv[i];
    if (key == L"--loader" && i + 1 < argc) {
      args.loader = argv[++i];
    } else {
      return false;
    }
  }
  return !args.loader.empty();
}

[[nodiscard]] std::string narrow(std::wstring_view input) {
  if (input.empty())
    return {};
  const int required = WideCharToMultiByte(CP_UTF8, 0, input.data(),
                                           static_cast<int>(input.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 0)
    return {};
  std::string output(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                      output.data(), required, nullptr, nullptr);
  return output;
}

template <typename T>
[[nodiscard]] bool load_proc(PFN_xrGetInstanceProcAddr get_instance_proc_addr,
                             XrInstance instance, const char *name, T &out) {
  PFN_xrVoidFunction function = nullptr;
  const XrResult result =
      get_instance_proc_addr(instance, name, &function);
  if (XR_FAILED(result) || function == nullptr) {
    std::cerr << "load_proc_failed name=" << name
              << " result=" << static_cast<std::int32_t>(result) << "\n";
    return false;
  }
  out = reinterpret_cast<T>(function);
  return true;
}

[[nodiscard]] bool has_extension(
    const std::vector<XrExtensionProperties> &extensions, const char *name) {
  return std::any_of(extensions.begin(), extensions.end(),
                     [name](const XrExtensionProperties &extension) {
                       return std::strcmp(extension.extensionName, name) == 0;
                     });
}

[[nodiscard]] std::string luid_string(const LUID &luid) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setfill('0') << std::setw(8)
         << static_cast<std::uint32_t>(luid.HighPart) << std::setw(8)
         << luid.LowPart;
  return stream.str();
}

[[nodiscard]] bool same_luid(const LUID &left, const LUID &right) {
  return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

[[nodiscard]] std::string adapter_name(const LUID &luid) {
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    return {};
  ComPtr<IDXGIAdapter1> adapter;
  if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
    return {};
  DXGI_ADAPTER_DESC1 desc{};
  if (FAILED(adapter->GetDesc1(&desc)))
    return {};
  return narrow(desc.Description);
}

[[nodiscard]] std::string version_string(XrVersion version) {
  return std::to_string(XR_VERSION_MAJOR(version)) + "." +
         std::to_string(XR_VERSION_MINOR(version)) + "." +
         std::to_string(XR_VERSION_PATCH(version));
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  Arguments args;
  if (!parse(argc, argv, args)) {
    std::cerr << "usage: ltr_openxr_runtime_probe --loader <openxr_loader.dll>\n";
    return 2;
  }

  HMODULE loader = LoadLibraryExW(
      args.loader.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (loader == nullptr) {
    std::cerr << "loader_open_failed path=" << narrow(args.loader)
              << " win32_error=" << GetLastError() << "\nRESULT FAIL\n";
    return 3;
  }

  const auto get_instance_proc_addr =
      reinterpret_cast<PFN_xrGetInstanceProcAddr>(
          GetProcAddress(loader, "xrGetInstanceProcAddr"));
  if (get_instance_proc_addr == nullptr) {
    std::cerr << "loader_missing_export=xrGetInstanceProcAddr\nRESULT FAIL\n";
    FreeLibrary(loader);
    return 4;
  }

  PFN_xrEnumerateInstanceExtensionProperties enumerate_extensions = nullptr;
  PFN_xrCreateInstance create_instance = nullptr;
  if (!load_proc(get_instance_proc_addr, XR_NULL_HANDLE,
                 "xrEnumerateInstanceExtensionProperties",
                 enumerate_extensions) ||
      !load_proc(get_instance_proc_addr, XR_NULL_HANDLE, "xrCreateInstance",
                 create_instance)) {
    FreeLibrary(loader);
    return 5;
  }

  std::uint32_t extension_count = 0;
  XrResult result = enumerate_extensions(nullptr, 0, &extension_count, nullptr);
  if (XR_FAILED(result)) {
    std::cerr << "enumerate_extensions_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    FreeLibrary(loader);
    return 6;
  }
  std::vector<XrExtensionProperties> extensions(
      extension_count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
  result = enumerate_extensions(nullptr, extension_count, &extension_count,
                                extensions.data());
  if (XR_FAILED(result)) {
    std::cerr << "enumerate_extensions_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    FreeLibrary(loader);
    return 6;
  }

  const bool has_d3d11 = has_extension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
  const bool has_d3d12 = has_extension(extensions, XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
  std::vector<const char *> enabled_extensions;
  if (has_d3d11)
    enabled_extensions.push_back(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
  if (has_d3d12)
    enabled_extensions.push_back(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);

  XrInstanceCreateInfo create_info{XR_TYPE_INSTANCE_CREATE_INFO};
  strncpy_s(create_info.applicationInfo.applicationName,
            "LTR Bridge OpenXR Probe", _TRUNCATE);
  create_info.applicationInfo.applicationVersion = 1;
  strncpy_s(create_info.applicationInfo.engineName, "LTR Bridge", _TRUNCATE);
  create_info.applicationInfo.engineVersion = 1;
  create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  create_info.enabledExtensionCount =
      static_cast<std::uint32_t>(enabled_extensions.size());
  create_info.enabledExtensionNames = enabled_extensions.data();

  XrInstance instance = XR_NULL_HANDLE;
  const XrResult initial_create_result = create_instance(&create_info, &instance);
  result = initial_create_result;
  bool api_fallback = false;
  if (initial_create_result == XR_ERROR_API_VERSION_UNSUPPORTED) {
    api_fallback = true;
    create_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    result = create_instance(&create_info, &instance);
  }
  if (XR_FAILED(result)) {
    std::cerr << "create_instance_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    FreeLibrary(loader);
    return 7;
  }

  PFN_xrDestroyInstance destroy_instance = nullptr;
  PFN_xrGetInstanceProperties get_instance_properties = nullptr;
  PFN_xrGetSystem get_system = nullptr;
  PFN_xrGetSystemProperties get_system_properties = nullptr;
  PFN_xrEnumerateViewConfigurations enumerate_view_configurations = nullptr;
  PFN_xrEnumerateViewConfigurationViews enumerate_view_configuration_views =
      nullptr;
  if (!load_proc(get_instance_proc_addr, instance, "xrDestroyInstance",
                 destroy_instance) ||
      !load_proc(get_instance_proc_addr, instance, "xrGetInstanceProperties",
                 get_instance_properties) ||
      !load_proc(get_instance_proc_addr, instance, "xrGetSystem", get_system) ||
      !load_proc(get_instance_proc_addr, instance, "xrGetSystemProperties",
                 get_system_properties) ||
      !load_proc(get_instance_proc_addr, instance, "xrEnumerateViewConfigurations",
                 enumerate_view_configurations) ||
      !load_proc(get_instance_proc_addr, instance,
                 "xrEnumerateViewConfigurationViews",
                 enumerate_view_configuration_views)) {
    destroy_instance(instance);
    FreeLibrary(loader);
    return 8;
  }

  XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
  result = get_instance_properties(instance, &instance_properties);
  if (XR_FAILED(result)) {
    std::cerr << "get_instance_properties_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 9;
  }

  std::cout << "loader=" << narrow(args.loader) << "\n"
            << "header_api_version=" << version_string(XR_CURRENT_API_VERSION)
            << " initial_create_result="
            << static_cast<std::int32_t>(initial_create_result)
            << " api_fallback=" << (api_fallback ? 1 : 0)
            << " negotiated_api_version="
            << version_string(create_info.applicationInfo.apiVersion) << "\n"
            << "runtime_name=" << instance_properties.runtimeName
            << " runtime_version="
            << version_string(instance_properties.runtimeVersion) << "\n"
            << "instance_extensions=" << extension_count
            << " d3d11_enable=" << (has_d3d11 ? 1 : 0)
            << " d3d12_enable=" << (has_d3d12 ? 1 : 0) << "\n";

  XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrSystemId system_id = XR_NULL_SYSTEM_ID;
  result = get_system(instance, &system_info, &system_id);
  if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
    std::cout << "system_available=0 form_factor=hmd result="
              << static_cast<std::int32_t>(result)
              << "\nprobe_scope=runtime_bootstrap\nRESULT PASS\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 0;
  }
  if (XR_FAILED(result)) {
    std::cerr << "get_system_failed result=" << static_cast<std::int32_t>(result)
              << "\nRESULT FAIL\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 10;
  }

  XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
  result = get_system_properties(instance, system_id, &system_properties);
  if (XR_FAILED(result)) {
    std::cerr << "get_system_properties_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 11;
  }
  std::cout << "system_available=1 system_name=" << system_properties.systemName
            << " vendor_id=" << system_properties.vendorId << "\n";

  std::uint32_t view_configuration_count = 0;
  result = enumerate_view_configurations(instance, system_id, 0,
                                         &view_configuration_count, nullptr);
  if (XR_FAILED(result)) {
    std::cerr << "enumerate_view_configurations_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 12;
  }
  std::vector<XrViewConfigurationType> view_configurations(
      view_configuration_count);
  result = enumerate_view_configurations(
      instance, system_id, view_configuration_count, &view_configuration_count,
      view_configurations.data());
  if (XR_FAILED(result)) {
    std::cerr << "enumerate_view_configurations_failed result="
              << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
    destroy_instance(instance);
    FreeLibrary(loader);
    return 12;
  }

  const bool primary_stereo =
      std::find(view_configurations.begin(), view_configurations.end(),
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) !=
      view_configurations.end();
  std::cout << "view_configuration_count=" << view_configuration_count
            << " primary_stereo=" << (primary_stereo ? 1 : 0) << "\n";
  if (primary_stereo) {
    std::uint32_t view_count = 0;
    result = enumerate_view_configuration_views(
        instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
        &view_count, nullptr);
    if (XR_FAILED(result)) {
      std::cerr << "enumerate_stereo_views_failed result="
                << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
      destroy_instance(instance);
      FreeLibrary(loader);
      return 13;
    }
    std::vector<XrViewConfigurationView> views(
        view_count, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    result = enumerate_view_configuration_views(
        instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views.data());
    if (XR_FAILED(result)) {
      std::cerr << "enumerate_stereo_views_failed result="
                << static_cast<std::int32_t>(result) << "\nRESULT FAIL\n";
      destroy_instance(instance);
      FreeLibrary(loader);
      return 13;
    }
    std::cout << "stereo_view_count=" << view_count << "\n";
    for (std::uint32_t i = 0; i < view_count; ++i) {
      std::cout << "view=" << i
                << " recommended_extent=" << views[i].recommendedImageRectWidth
                << "x" << views[i].recommendedImageRectHeight
                << " recommended_samples="
                << views[i].recommendedSwapchainSampleCount << "\n";
    }
  }

  LUID d3d11_luid{};
  LUID d3d12_luid{};
  bool have_d3d11_luid = false;
  bool have_d3d12_luid = false;
  if (has_d3d11) {
    PFN_xrGetD3D11GraphicsRequirementsKHR requirements = nullptr;
    if (load_proc(get_instance_proc_addr, instance,
                  "xrGetD3D11GraphicsRequirementsKHR", requirements)) {
      XrGraphicsRequirementsD3D11KHR graphics{
          XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
      result = requirements(instance, system_id, &graphics);
      if (XR_SUCCEEDED(result)) {
        d3d11_luid = graphics.adapterLuid;
        have_d3d11_luid = true;
        std::cout << "d3d11_adapter_luid=" << luid_string(d3d11_luid)
                  << " min_feature_level=0x" << std::hex
                  << static_cast<std::uint32_t>(graphics.minFeatureLevel)
                  << std::dec << " adapter=" << adapter_name(d3d11_luid)
                  << "\n";
      }
    }
  }
  if (has_d3d12) {
    PFN_xrGetD3D12GraphicsRequirementsKHR requirements = nullptr;
    if (load_proc(get_instance_proc_addr, instance,
                  "xrGetD3D12GraphicsRequirementsKHR", requirements)) {
      XrGraphicsRequirementsD3D12KHR graphics{
          XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
      result = requirements(instance, system_id, &graphics);
      if (XR_SUCCEEDED(result)) {
        d3d12_luid = graphics.adapterLuid;
        have_d3d12_luid = true;
        std::cout << "d3d12_adapter_luid=" << luid_string(d3d12_luid)
                  << " min_feature_level=0x" << std::hex
                  << static_cast<std::uint32_t>(graphics.minFeatureLevel)
                  << std::dec << " adapter=" << adapter_name(d3d12_luid)
                  << "\n";
      }
    }
  }
  if (have_d3d11_luid && have_d3d12_luid) {
    std::cout << "graphics_adapter_identity_match="
              << (same_luid(d3d11_luid, d3d12_luid) ? 1 : 0) << "\n";
  }

  std::cout << "probe_scope=runtime_system_view_graphics_requirements\n"
               "RESULT PASS\n";
  destroy_instance(instance);
  FreeLibrary(loader);
  return 0;
}
