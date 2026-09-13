#pragma once

#include <cstdint>

namespace ltr::harness {

enum class HistoryResetReason : std::uint32_t { none = 0, startup, resize, scenario_change, manual };
enum class MotionDirection : std::uint32_t { current_to_previous };
enum class MotionUnits : std::uint32_t { render_pixels };
enum class MotionProvenance : std::uint32_t { renderer_ground_truth, camera_depth_reconstruction, optical_flow };

enum MotionCoverage : std::uint32_t {
    coverage_none = 0,
    coverage_camera = 1u << 0,
    coverage_rigid_objects = 1u << 1,
    coverage_skinned_geometry = 1u << 2,
    coverage_particles = 1u << 3,
    coverage_transparency = 1u << 4,
    coverage_hud = 1u << 5,
};

struct FrameIdentity {
    std::uint64_t frame_index = 0;
    std::uint32_t view_index = 0;
    std::uint32_t history_generation = 0;
    HistoryResetReason reset_reason = HistoryResetReason::none;
};

struct MotionMetadata {
    MotionDirection direction = MotionDirection::current_to_previous;
    MotionUnits units = MotionUnits::render_pixels;
    MotionProvenance provenance = MotionProvenance::renderer_ground_truth;
    std::uint32_t coverage = coverage_none;
    std::uint32_t known_exclusions = coverage_none;
    bool jitter_included = false;
    bool dilated = false;
    bool validity_is_explicit = false;
};

struct TemporalFrameDescription {
    FrameIdentity identity{};
    std::uint32_t render_width = 0;
    std::uint32_t render_height = 0;
    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;
    float jitter_x_pixels = 0.0f;
    float jitter_y_pixels = 0.0f;
    MotionMetadata motion{};
};

} // namespace ltr::harness
