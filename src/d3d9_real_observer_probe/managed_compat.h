#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstdint>

namespace ltr::d3d9_real_observer {

enum class ManagedResourceClass {
  texture2d,
  volume_texture,
  cube_texture,
  vertex_buffer,
  index_buffer,
};

struct ManagedResourceAdaptationPlan {
  bool retry = false;
  DWORD usage = 0;
  D3DPOOL pool = D3DPOOL_MANAGED;
};

[[nodiscard]] constexpr bool
should_sample_post_reset_draw_state(std::uint32_t current_generation,
                                    std::uint32_t samples_taken,
                                    std::uint32_t sample_limit) {
  return current_generation > 1 && samples_taken < sample_limit;
}

[[nodiscard]] constexpr bool
should_trace_stale_managed_binding(std::uint32_t creation_generation,
                                   std::uint32_t current_generation) {
  return creation_generation != 0 && current_generation != 0 &&
         creation_generation < current_generation;
}

[[nodiscard]] constexpr ManagedResourceAdaptationPlan
plan_managed_resource_adaptation(ManagedResourceClass resource_class,
                                 DWORD usage, D3DPOOL pool,
                                 bool shared_handle_present,
                                 HRESULT original_hr) {
  ManagedResourceAdaptationPlan plan{false, usage, pool};
  if (pool != D3DPOOL_MANAGED || shared_handle_present ||
      original_hr != D3DERR_INVALIDCALL) {
    return plan;
  }

  switch (resource_class) {
  case ManagedResourceClass::texture2d:
  case ManagedResourceClass::cube_texture:
    if (usage != 0)
      return plan;
    plan.retry = true;
    plan.usage = usage | D3DUSAGE_DYNAMIC;
    plan.pool = D3DPOOL_DEFAULT;
    return plan;

  case ManagedResourceClass::vertex_buffer:
  case ManagedResourceClass::index_buffer:
    if (usage != D3DUSAGE_WRITEONLY)
      return plan;
    plan.retry = true;
    plan.pool = D3DPOOL_DEFAULT;
    return plan;

  case ManagedResourceClass::volume_texture:
    return plan;
  }

  return plan;
}

} // namespace ltr::d3d9_real_observer
