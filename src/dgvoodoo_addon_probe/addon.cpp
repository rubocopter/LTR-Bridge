#include <dxgi.h>

#include <Addon/AddonDefs.hpp>
#include <Addon/IAddonMainCallback.hpp>
#include <Addon/ID3DObserver.hpp>
#include <Addon/ID3D12RootObserver.hpp>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {

constexpr const char *kLogPath = "ltr_dgvoodoo_addon_probe.log";

struct ProbeState {
  std::mutex mutex;
  dgVoodoo::IAddonMainCallback *callback = nullptr;
  ID3D12Root *root = nullptr;
  bool registered = false;
  bool d3d_registered = false;
  std::uint32_t d3d_created = 0;
  std::uint32_t d3d_released = 0;
  std::uint32_t root_created = 0;
  std::uint32_t root_released = 0;
  std::uint32_t adapter_begin = 0;
  std::uint32_t adapter_end = 0;
  std::uint32_t device_nonnull = 0;
  std::uint32_t swapchain_hook = 0;
  std::uint32_t swapchain_created = 0;
  std::uint32_t swapchain_changed = 0;
  std::uint32_t swapchain_released = 0;
  std::uint32_t present_begin = 0;
  std::uint32_t present_end = 0;
  std::uint32_t src_nonnull = 0;
  std::uint32_t dst_nonnull = 0;
};

ProbeState g_state;

void write_line(const std::string &line) {
  std::ofstream out(kLogPath, std::ios::out | std::ios::app);
  if (out)
    out << line << '\n';
}

std::string resource_desc(const char *name, ID3D12Resource *resource,
                          UINT state) {
  std::ostringstream out;
  out << name << "_ptr=" << resource << " " << name << "_state=" << state;
  if (!resource)
    return out.str();
  const D3D12_RESOURCE_DESC desc = resource->GetDesc();
  out << " " << name << "_dimension=" << static_cast<unsigned>(desc.Dimension)
      << " " << name << "_width=" << desc.Width
      << " " << name << "_height=" << desc.Height
      << " " << name << "_format=" << static_cast<unsigned>(desc.Format)
      << " " << name << "_flags=" << static_cast<unsigned>(desc.Flags);
  return out.str();
}

class Observer final : public dgVoodoo::ID3D12RootObserver {
public:
  bool D3D12RootCreated(HMODULE h_d3d12, ID3D12Root *root) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.root_created;
    g_state.root = root;
    std::ostringstream out;
    out << "event=root_created d3d12_module=" << h_d3d12 << " root=" << root;
    write_line(out.str());
    return root != nullptr;
  }

  void D3D12RootReleased(const ID3D12Root *root) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.root_released;
    std::ostringstream out;
    out << "event=root_released root=" << root;
    write_line(out.str());
    if (g_state.root == root)
      g_state.root = nullptr;
  }

  bool D3D12BeginUsingAdapter(UInt32 adapter_id) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.adapter_begin;
    ID3D12Device *device = g_state.root ? g_state.root->GetDevice(adapter_id) : nullptr;
    if (device)
      ++g_state.device_nonnull;
    std::ostringstream out;
    out << "event=adapter_begin adapter_id=" << adapter_id << " device=" << device;
    if (device) {
      const LUID luid = device->GetAdapterLuid();
      out << " adapter_luid_high=" << luid.HighPart
          << " adapter_luid_low=" << luid.LowPart;
    }
    write_line(out.str());
    return device != nullptr;
  }

  void D3D12EndUsingAdapter(UInt32 adapter_id) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.adapter_end;
    write_line("event=adapter_end adapter_id=" + std::to_string(adapter_id));
  }

  bool D3D12CreateSwapchainHook(UInt32 adapter_id, IDXGIFactory1 *, IUnknown *,
                                const DXGI_SWAP_CHAIN_DESC &desc,
                                IDXGISwapChain **) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.swapchain_hook;
    std::ostringstream out;
    out << "event=swapchain_hook adapter_id=" << adapter_id
        << " width=" << desc.BufferDesc.Width
        << " height=" << desc.BufferDesc.Height
        << " format=" << static_cast<unsigned>(desc.BufferDesc.Format)
        << " buffer_count=" << desc.BufferCount;
    write_line(out.str());
    return false;
  }

  void D3D12SwapchainCreated(UInt32 adapter_id, ID3D12Swapchain *swapchain,
                             const ID3D12Root::SwapchainData &data) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.swapchain_created;
    log_swapchain("swapchain_created", adapter_id, swapchain, data);
  }

  void D3D12SwapchainChanged(UInt32 adapter_id, ID3D12Swapchain *swapchain,
                             const ID3D12Root::SwapchainData &data) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.swapchain_changed;
    log_swapchain("swapchain_changed", adapter_id, swapchain, data);
  }

  void D3D12SwapchainReleased(UInt32 adapter_id,
                              ID3D12Swapchain *swapchain) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.swapchain_released;
    std::ostringstream out;
    out << "event=swapchain_released adapter_id=" << adapter_id
        << " swapchain=" << swapchain;
    write_line(out.str());
  }

  bool D3D12SwapchainPresentBegin(
      UInt32 adapter_id, const PresentBeginContextInput &input,
      PresentBeginContextOutput &output) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.present_begin;
    if (input.pSrcTexture)
      ++g_state.src_nonnull;
    if (input.drawingTarget.pDstTexture)
      ++g_state.dst_nonnull;

    output.pOutputTexture = nullptr;
    output.outputTexSRVCPUHandle = {};
    output.outputTextureExpectedState = static_cast<UINT>(-1);

    std::ostringstream out;
    out << "event=present_begin adapter_id=" << adapter_id
        << " swapchain=" << input.pSwapchain
        << " src_rect=" << input.srcRect.left << ',' << input.srcRect.top << ','
        << input.srcRect.right << ',' << input.srcRect.bottom
        << " dst_rect=" << input.drawingTarget.dstRect.left << ','
        << input.drawingTarget.dstRect.top << ','
        << input.drawingTarget.dstRect.right << ','
        << input.drawingTarget.dstRect.bottom << ' '
        << resource_desc("src", input.pSrcTexture, input.srcTextureState) << ' '
        << resource_desc("dst", input.drawingTarget.pDstTexture,
                         input.drawingTarget.dstTextureState);
    write_line(out.str());
    return false;
  }

  void D3D12SwapchainPresentEnd(UInt32 adapter_id,
                                const PresentEndContextInput &input) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.present_end;
    std::ostringstream out;
    out << "event=present_end adapter_id=" << adapter_id
        << " swapchain=" << input.pSwapchain;
    write_line(out.str());
  }

private:
  static void log_swapchain(const char *event, UInt32 adapter_id,
                            ID3D12Swapchain *swapchain,
                            const ID3D12Root::SwapchainData &data) {
    std::ostringstream out;
    out << "event=" << event << " adapter_id=" << adapter_id
        << " swapchain=" << swapchain << " image=" << data.imageSize.cx << 'x'
        << data.imageSize.cy << " presentation="
        << data.imagePresentationSize.cx << 'x' << data.imagePresentationSize.cy
        << " format=" << static_cast<unsigned>(data.format)
        << " max_override=" << data.maxOverriddenInputTextureSize.cx << 'x'
        << data.maxOverriddenInputTextureSize.cy;
    write_line(out.str());
  }
};

Observer g_observer;

class D3DObserver final : public dgVoodoo::ID3DObserver {
public:
  bool D3DObjectCreated(dgVoodoo::ID3D *d3d) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.d3d_created;
    std::ostringstream out;
    out << "event=d3d_created object=" << d3d;
    write_line(out.str());
    return d3d != nullptr;
  }

  void D3DObjectReleased(const dgVoodoo::ID3D *d3d) override {
    std::lock_guard lock(g_state.mutex);
    ++g_state.d3d_released;
    std::ostringstream out;
    out << "event=d3d_released object=" << d3d;
    write_line(out.str());
  }
};

D3DObserver g_d3d_observer;

} // namespace

extern "C" bool API_EXPORT AddOnInit(
    dgVoodoo::IAddonMainCallback *callback) {
  {
    std::lock_guard lock(g_state.mutex);
    g_state.callback = callback;
    std::ostringstream out;
    out << "event=addon_init callback=" << callback;
    if (callback)
      out << " api_version=" << callback->GetVersion();
    write_line(out.str());
  }
  if (!callback)
    return false;
  const bool d3d_registered = callback->RegisterForCallback(
      IID_D3DObserver, static_cast<dgVoodoo::ID3DObserver *>(&g_d3d_observer));
  const bool registered = callback->RegisterForCallback(
      IID_D3D12RootObserver,
      static_cast<dgVoodoo::ID3D12RootObserver *>(&g_observer));
  {
    std::lock_guard lock(g_state.mutex);
    g_state.d3d_registered = d3d_registered;
    g_state.registered = registered;
    write_line(std::string("event=observer_registration d3d=") +
               (d3d_registered ? "1" : "0") + " d3d12=" +
               (registered ? "1" : "0"));
  }
  return d3d_registered && registered;
}

extern "C" void API_EXPORT AddOnExit() {
  dgVoodoo::IAddonMainCallback *callback = nullptr;
  bool registered = false;
  bool d3d_registered = false;
  {
    std::lock_guard lock(g_state.mutex);
    callback = g_state.callback;
    registered = g_state.registered;
    d3d_registered = g_state.d3d_registered;
  }
  if (callback && registered) {
    callback->UnregisterForCallback(
        IID_D3D12RootObserver,
        static_cast<dgVoodoo::ID3D12RootObserver *>(&g_observer));
  }
  if (callback && d3d_registered) {
    callback->UnregisterForCallback(
        IID_D3DObserver, static_cast<dgVoodoo::ID3DObserver *>(&g_d3d_observer));
  }

  std::lock_guard lock(g_state.mutex);
  const bool pass = g_state.registered && g_state.d3d_registered &&
                    g_state.d3d_created > 0 && g_state.root_created > 0 &&
                    g_state.adapter_begin > 0 && g_state.device_nonnull > 0 &&
                    g_state.swapchain_created > 0 && g_state.present_begin > 0 &&
                    g_state.present_begin == g_state.present_end &&
                    g_state.src_nonnull == g_state.present_begin &&
                    g_state.dst_nonnull == g_state.present_begin;
  const bool inactive = g_state.registered && g_state.d3d_registered &&
                        g_state.d3d_created == 0 && g_state.root_created == 0 &&
                        g_state.adapter_begin == 0 && g_state.present_begin == 0;
  std::ostringstream out;
  out << "event=addon_exit d3d_created=" << g_state.d3d_created
      << " d3d_released=" << g_state.d3d_released
      << " root_created=" << g_state.root_created
      << " root_released=" << g_state.root_released
      << " adapter_begin=" << g_state.adapter_begin
      << " adapter_end=" << g_state.adapter_end
      << " device_nonnull=" << g_state.device_nonnull
      << " swapchain_hook=" << g_state.swapchain_hook
      << " swapchain_created=" << g_state.swapchain_created
      << " swapchain_changed=" << g_state.swapchain_changed
      << " swapchain_released=" << g_state.swapchain_released
      << " present_begin=" << g_state.present_begin
      << " present_end=" << g_state.present_end
      << " src_nonnull=" << g_state.src_nonnull
      << " dst_nonnull=" << g_state.dst_nonnull << " RESULT "
      << (pass ? "PASS" : (inactive ? "INACTIVE" : "FAIL"));
  write_line(out.str());
  g_state.callback = nullptr;
  g_state.root = nullptr;
  g_state.d3d_registered = false;
  g_state.registered = false;
}
