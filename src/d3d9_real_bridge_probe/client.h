#pragma once

#include <d3d11.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ltr::d3d9_real_bridge {

struct ClientOptions {
  std::wstring sink_path;
  std::wstring log_path;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t frames = 0;
  std::uint32_t generation = 0;
  std::uint32_t initial_stall_ms = 0;
  std::uint32_t bootstrap_timeout_ms = 5000;
  bool validate_synthetic_pattern = false;
};

enum class SubmitStatus {
  submitted,
  backpressure,
  waiting_for_completion,
  finished,
  failed,
};

struct ClientSnapshot {
  std::uint32_t submitted = 0;
  std::uint32_t backpressure_checks = 0;
  std::uint32_t child_liveness_checks = 0;
  std::uint64_t ready_completed = 0;
  std::uint64_t done_completed = 0;
  std::uint64_t adapter_luid = 0;
  unsigned long child_exit = 0;
  bool active = false;
  bool finished = false;
  bool passed = false;
};

class Client {
public:
  using Logger = std::function<void(const std::string &)>;

  Client();
  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  [[nodiscard]] bool Start(ID3D11Device *device,
                           ID3D11DeviceContext *context,
                           const ClientOptions &options, Logger logger);
  [[nodiscard]] SubmitStatus QuerySubmitStatus();
  [[nodiscard]] SubmitStatus TrySubmit(ID3D11Texture2D *source);
  [[nodiscard]] bool Poll();
  void Shutdown(bool terminate_child);
  [[nodiscard]] ClientSnapshot Snapshot() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ltr::d3d9_real_bridge
