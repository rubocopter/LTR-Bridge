#include "../src/d3d9_real_observer_probe/temporal_provenance.h"

#include <iostream>

// These checks must execute in Release builds, including when NDEBUG is set.
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "check_failed=" << #condition << '\n';                     \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  unsigned checks = 0;
  using ltr::d3d9_real_observer::TemporalProvenanceState;

  TemporalProvenanceState state{};
  CHECK(state.source_frame_index == 0);
  CHECK(state.view_id == 0);
  CHECK(state.generation_frame_index == 0);

  const auto first = state.acquire_draw_sample(1, 2, 2);
  CHECK(first.has_value());
  CHECK(first->source_frame_index == 0);
  CHECK(first->generation_frame_index == 0);
  CHECK(first->view_id == 0);
  CHECK(first->resource_generation == 1);
  CHECK(first->sample_index == 1);

  const auto first_present = state.acquire_present_sample(1, 2);
  CHECK(first_present.has_value());
  CHECK(first_present->source_frame_index == 0);
  CHECK(first_present->generation_frame_index == 0);
  CHECK(first_present->view_id == 0);
  CHECK(first_present->resource_generation == 1);
  CHECK(!state.acquire_present_sample(1, 2).has_value());

  const auto second = state.acquire_draw_sample(1, 2, 2);
  CHECK(second.has_value());
  CHECK(second->sample_index == 2);
  CHECK(!state.acquire_draw_sample(1, 2, 2).has_value());

  state.on_present_result(false);
  CHECK(state.source_frame_index == 0);
  CHECK(state.generation_frame_index == 0);
  CHECK(!state.acquire_draw_sample(1, 2, 2).has_value());
  CHECK(!state.acquire_present_sample(1, 2).has_value());

  state.on_present_result(true);
  CHECK(state.source_frame_index == 1);
  CHECK(state.generation_frame_index == 1);
  const auto second_present = state.acquire_present_sample(1, 2);
  CHECK(second_present.has_value());
  CHECK(second_present->source_frame_index == 1);
  CHECK(second_present->generation_frame_index == 1);
  CHECK(second_present->resource_generation == 1);
  const auto after_present = state.acquire_draw_sample(1, 2, 2);
  CHECK(after_present.has_value());
  CHECK(after_present->source_frame_index == 1);
  CHECK(after_present->generation_frame_index == 1);
  CHECK(after_present->resource_generation == 1);
  CHECK(after_present->sample_index == 1);

  state.on_reset_result(false);
  CHECK(state.generation_frame_index == 1);
  state.on_reset_result(true);
  CHECK(state.source_frame_index == 1);
  CHECK(state.generation_frame_index == 0);
  const auto after_reset = state.acquire_draw_sample(2, 2, 2);
  CHECK(after_reset.has_value());
  CHECK(after_reset->source_frame_index == 1);
  CHECK(after_reset->generation_frame_index == 0);
  CHECK(after_reset->resource_generation == 2);
  CHECK(after_reset->sample_index == 1);
  const auto after_reset_present = state.acquire_present_sample(2, 2);
  CHECK(after_reset_present.has_value());
  CHECK(after_reset_present->source_frame_index == 1);
  CHECK(after_reset_present->generation_frame_index == 0);
  CHECK(after_reset_present->resource_generation == 2);

  state.on_present_result(true);
  state.on_present_result(true);
  CHECK(state.generation_frame_index == 2);
  CHECK(!state.acquire_draw_sample(2, 2, 2).has_value());
  CHECK(!state.acquire_draw_sample(2, 2, 0).has_value());
  CHECK(!state.acquire_present_sample(2, 2).has_value());
  CHECK(!state.acquire_present_sample(2, 0).has_value());

  std::cout << "temporal_provenance_checks=" << checks << " RESULT PASS\n";
  return 0;
}
