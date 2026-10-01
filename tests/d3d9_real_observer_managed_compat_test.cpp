#include "../src/d3d9_real_observer_probe/managed_compat.h"

#include <iostream>

// These checks must execute in Release builds, including when NDEBUG is set.
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "check_failed=" << #condition << '\n';                        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  unsigned checks = 0;
  using ltr::d3d9_real_observer::ManagedResourceClass;
  using ltr::d3d9_real_observer::plan_managed_resource_adaptation;
  using ltr::d3d9_real_observer::should_sample_post_reset_draw_state;
  using ltr::d3d9_real_observer::should_trace_stale_managed_binding;

  CHECK(!should_sample_post_reset_draw_state(1, 0, 128));
  CHECK(should_sample_post_reset_draw_state(2, 0, 128));
  CHECK(should_sample_post_reset_draw_state(2, 127, 128));
  CHECK(!should_sample_post_reset_draw_state(2, 128, 128));
  CHECK(!should_sample_post_reset_draw_state(2, 0, 0));

  CHECK(!should_trace_stale_managed_binding(1, 1));
  CHECK(should_trace_stale_managed_binding(1, 2));
  CHECK(should_trace_stale_managed_binding(2, 3));
  CHECK(!should_trace_stale_managed_binding(2, 1));
  CHECK(!should_trace_stale_managed_binding(0, 2));
  CHECK(!should_trace_stale_managed_binding(1, 0));

  {
    const auto plan = plan_managed_resource_adaptation(
        ManagedResourceClass::texture2d, 0, D3DPOOL_MANAGED, false,
        D3DERR_INVALIDCALL);
    CHECK(plan.retry);
    CHECK(plan.pool == D3DPOOL_DEFAULT);
    CHECK(plan.usage == D3DUSAGE_DYNAMIC);
  }

  {
    const auto plan = plan_managed_resource_adaptation(
        ManagedResourceClass::cube_texture, 0, D3DPOOL_MANAGED, false,
        D3DERR_INVALIDCALL);
    CHECK(plan.retry);
    CHECK(plan.pool == D3DPOOL_DEFAULT);
    CHECK(plan.usage == D3DUSAGE_DYNAMIC);
  }

  {
    const auto plan = plan_managed_resource_adaptation(
        ManagedResourceClass::vertex_buffer, D3DUSAGE_WRITEONLY,
        D3DPOOL_MANAGED, false, D3DERR_INVALIDCALL);
    CHECK(plan.retry);
    CHECK(plan.pool == D3DPOOL_DEFAULT);
    CHECK(plan.usage == D3DUSAGE_WRITEONLY);
  }

  {
    const auto plan = plan_managed_resource_adaptation(
        ManagedResourceClass::index_buffer, D3DUSAGE_WRITEONLY,
        D3DPOOL_MANAGED, false, D3DERR_INVALIDCALL);
    CHECK(plan.retry);
    CHECK(plan.pool == D3DPOOL_DEFAULT);
    CHECK(plan.usage == D3DUSAGE_WRITEONLY);
  }

  CHECK(!plan_managed_resource_adaptation(
              ManagedResourceClass::volume_texture, 0, D3DPOOL_MANAGED, false,
              D3DERR_INVALIDCALL)
              .retry);
  CHECK(!plan_managed_resource_adaptation(
              ManagedResourceClass::texture2d, D3DUSAGE_RENDERTARGET,
              D3DPOOL_MANAGED, false, D3DERR_INVALIDCALL)
              .retry);
  CHECK(!plan_managed_resource_adaptation(
              ManagedResourceClass::texture2d, 0, D3DPOOL_DEFAULT, false,
              D3DERR_INVALIDCALL)
              .retry);
  CHECK(!plan_managed_resource_adaptation(
              ManagedResourceClass::texture2d, 0, D3DPOOL_MANAGED, true,
              D3DERR_INVALIDCALL)
              .retry);
  CHECK(!plan_managed_resource_adaptation(
              ManagedResourceClass::texture2d, 0, D3DPOOL_MANAGED, false,
              D3D_OK)
              .retry);

  std::cout << "managed_compat_checks=" << checks << " RESULT PASS\n";
  return 0;
}
