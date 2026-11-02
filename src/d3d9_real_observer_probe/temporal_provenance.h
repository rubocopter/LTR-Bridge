#pragma once

#include <cstdint>
#include <optional>

namespace ltr::d3d9_real_observer {

struct TemporalProvenanceDrawTicket {
  std::uint64_t source_frame_index = 0;
  std::uint64_t generation_frame_index = 0;
  std::uint32_t view_id = 0;
  std::uint32_t resource_generation = 0;
  std::uint32_t sample_index = 0;
};

struct TemporalProvenancePresentTicket {
  std::uint64_t source_frame_index = 0;
  std::uint64_t generation_frame_index = 0;
  std::uint32_t view_id = 0;
  std::uint32_t resource_generation = 0;
};

struct TemporalProvenanceState {
  std::uint64_t source_frame_index = 0;
  std::uint64_t generation_frame_index = 0;
  std::uint32_t view_id = 0;

  [[nodiscard]] std::optional<TemporalProvenanceDrawTicket>
  acquire_draw_sample(std::uint32_t resource_generation,
                      std::uint64_t generation_frame_limit,
                      std::uint32_t samples_per_frame_limit) {
    if (resource_generation == 0 || generation_frame_limit == 0 ||
        samples_per_frame_limit == 0 ||
        generation_frame_index >= generation_frame_limit) {
      return std::nullopt;
    }

    if (sampled_resource_generation_ != resource_generation ||
        sampled_source_frame_index_ != source_frame_index) {
      sampled_resource_generation_ = resource_generation;
      sampled_source_frame_index_ = source_frame_index;
      samples_taken_ = 0;
    }

    if (samples_taken_ >= samples_per_frame_limit)
      return std::nullopt;

    ++samples_taken_;
    return TemporalProvenanceDrawTicket{
        source_frame_index, generation_frame_index, view_id,
        resource_generation, samples_taken_};
  }

  [[nodiscard]] std::optional<TemporalProvenancePresentTicket>
  acquire_present_sample(std::uint32_t resource_generation,
                         std::uint64_t generation_frame_limit) {
    if (resource_generation == 0 || generation_frame_limit == 0 ||
        generation_frame_index >= generation_frame_limit) {
      return std::nullopt;
    }

    if (present_sampled_ &&
        sampled_present_resource_generation_ == resource_generation &&
        sampled_present_source_frame_index_ == source_frame_index) {
      return std::nullopt;
    }

    present_sampled_ = true;
    sampled_present_resource_generation_ = resource_generation;
    sampled_present_source_frame_index_ = source_frame_index;
    return TemporalProvenancePresentTicket{
        source_frame_index, generation_frame_index, view_id,
        resource_generation};
  }

  void on_present_result(bool succeeded) {
    if (!succeeded)
      return;
    ++source_frame_index;
    ++generation_frame_index;
    sampled_source_frame_index_ = source_frame_index;
    samples_taken_ = 0;
  }

  void on_reset_result(bool succeeded) {
    if (!succeeded)
      return;
    generation_frame_index = 0;
    sampled_resource_generation_ = 0;
    sampled_source_frame_index_ = source_frame_index;
    samples_taken_ = 0;
    present_sampled_ = false;
    sampled_present_resource_generation_ = 0;
    sampled_present_source_frame_index_ = source_frame_index;
  }

private:
  std::uint32_t sampled_resource_generation_ = 0;
  std::uint64_t sampled_source_frame_index_ = 0;
  std::uint32_t samples_taken_ = 0;
  bool present_sampled_ = false;
  std::uint32_t sampled_present_resource_generation_ = 0;
  std::uint64_t sampled_present_source_frame_index_ = 0;
};

} // namespace ltr::d3d9_real_observer
