#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace ltr::temporal {

enum class HistoryResetReason : std::uint32_t {
    none = 0,
    startup,
    resize,
    resource_recreate,
    device_reset,
    sequence_discontinuity,
    scenario_change,
    manual,
};

enum class MotionDirection : std::uint32_t { current_to_previous };
enum class MotionUnits : std::uint32_t { render_pixels };
enum class DepthConvention : std::uint32_t {
    unknown = 0,
    hardware,
    linear_view_z,
};
enum class OutputOwnership : std::uint32_t {
    unknown = 0,
    game,
    backend,
    shared_transport,
};
enum class MotionProvenance : std::uint32_t {
    renderer_ground_truth,
    camera_depth_reconstruction,
    optical_flow,
};

enum MotionCoverage : std::uint32_t {
    coverage_none = 0,
    coverage_camera = 1u << 0,
    coverage_rigid_objects = 1u << 1,
    coverage_skinned_geometry = 1u << 2,
    coverage_particles = 1u << 3,
    coverage_transparency = 1u << 4,
    coverage_hud = 1u << 5,
};

struct TemporalSubrect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct DepthMetadata {
    DepthConvention convention = DepthConvention::unknown;
    std::uint32_t sample_count = 0;
    bool inverted = false;
    bool infinite_far = false;
    float near_plane = 0.0f;
    float far_plane = 0.0f;
};

struct FrameIdentity {
    // Source-renderer frame identity. Gaps here are temporal discontinuities even
    // when successful transport submissions remain sequential.
    std::uint64_t frame_index = 0;
    // Diagnostic/transport serial. This may skip independently of frame_index and
    // is deliberately not used to decide temporal history continuity.
    std::uint64_t transport_index = 0;
    std::uint32_t view_index = 0;
    std::uint32_t resource_generation = 0;
    std::uint32_t history_generation = 0;
    std::uint32_t camera_generation = 0;
    std::uint32_t config_generation = 0;
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
    TemporalSubrect color_rect{};
    TemporalSubrect depth_rect{};
    TemporalSubrect motion_rect{};
    DepthMetadata depth{};
    OutputOwnership output_ownership = OutputOwnership::unknown;
    float frame_delta_ms = 0.0f;
    float jitter_x_pixels = 0.0f;
    float jitter_y_pixels = 0.0f;
    MotionMetadata motion{};
};

[[nodiscard]] constexpr bool HasValidDimensions(
    const TemporalFrameDescription& frame) noexcept
{
    return frame.render_width != 0 && frame.render_height != 0 &&
           frame.output_width != 0 && frame.output_height != 0;
}

[[nodiscard]] constexpr bool HasValidSubrect(
    const TemporalSubrect& rect,
    std::uint32_t extent_width,
    std::uint32_t extent_height) noexcept
{
    return rect.width != 0 && rect.height != 0 &&
           rect.x <= extent_width && rect.y <= extent_height &&
           rect.width <= extent_width - rect.x &&
           rect.height <= extent_height - rect.y;
}

// Semantic identity for independently produced inputs that claim to describe
// the same renderer frame. transport_index is intentionally excluded because
// delivery can skip or retry independently of the source renderer timeline.
[[nodiscard]] constexpr bool SameTemporalFrameIdentity(
    const TemporalFrameDescription& left,
    const TemporalFrameDescription& right) noexcept
{
    return left.identity.frame_index == right.identity.frame_index &&
           left.identity.view_index == right.identity.view_index &&
           left.identity.resource_generation == right.identity.resource_generation &&
           left.identity.history_generation == right.identity.history_generation &&
           left.identity.camera_generation == right.identity.camera_generation &&
           left.identity.config_generation == right.identity.config_generation &&
           left.identity.reset_reason == right.identity.reset_reason;
}

[[nodiscard]] constexpr bool CanReuseHistory(
    const TemporalFrameDescription& previous,
    const TemporalFrameDescription& current) noexcept
{
    return current.identity.reset_reason == HistoryResetReason::none &&
           current.identity.view_index == previous.identity.view_index &&
           current.identity.resource_generation == previous.identity.resource_generation &&
           current.identity.history_generation == previous.identity.history_generation &&
           current.identity.camera_generation == previous.identity.camera_generation &&
           current.identity.config_generation == previous.identity.config_generation &&
           previous.identity.frame_index != std::numeric_limits<std::uint64_t>::max() &&
           current.identity.frame_index == previous.identity.frame_index + 1u &&
           current.render_width == previous.render_width &&
           current.render_height == previous.render_height &&
           current.output_width == previous.output_width &&
           current.output_height == previous.output_height;
}

// These checks are independent of identity and history continuity. A valid
// descriptor does not prove that any GPU resource is readable or still alive.
[[nodiscard]] inline bool HasValidTemporalMetadata(
    const TemporalFrameDescription& frame) noexcept
{
    const auto& depth = frame.depth;
    constexpr std::uint32_t known_motion_coverage =
        coverage_camera | coverage_rigid_objects | coverage_skinned_geometry |
        coverage_particles | coverage_transparency | coverage_hud;
    return HasValidDimensions(frame) &&
           HasValidSubrect(frame.color_rect, frame.render_width, frame.render_height) &&
           HasValidSubrect(frame.depth_rect, frame.render_width, frame.render_height) &&
           HasValidSubrect(frame.motion_rect, frame.render_width, frame.render_height) &&
           depth.convention != DepthConvention::unknown && depth.sample_count > 0 &&
           std::isfinite(depth.near_plane) && depth.near_plane > 0.0f &&
           std::isfinite(depth.far_plane) &&
           (depth.infinite_far ? depth.far_plane >= 0.0f
                               : depth.far_plane > depth.near_plane) &&
           std::isfinite(frame.frame_delta_ms) && frame.frame_delta_ms >= 0.0f &&
           std::isfinite(frame.jitter_x_pixels) &&
           std::isfinite(frame.jitter_y_pixels) &&
           frame.output_ownership != OutputOwnership::unknown &&
           ((frame.motion.coverage | frame.motion.known_exclusions) &
            ~known_motion_coverage) == 0u &&
           (frame.motion.coverage & frame.motion.known_exclusions) == 0u;
}

// Independently captured inputs must agree on the temporal interpretation of
// a single source sample. Changing jitter across *consecutive* frames is fine.
[[nodiscard]] constexpr bool SameTemporalSampleMetadata(
    const TemporalFrameDescription& a,
    const TemporalFrameDescription& b) noexcept
{
    return a.frame_delta_ms == b.frame_delta_ms &&
           a.jitter_x_pixels == b.jitter_x_pixels &&
           a.jitter_y_pixels == b.jitter_y_pixels &&
           a.depth.convention == b.depth.convention &&
           a.depth.sample_count == b.depth.sample_count &&
           a.depth.inverted == b.depth.inverted &&
           a.depth.infinite_far == b.depth.infinite_far &&
           a.depth.near_plane == b.depth.near_plane &&
           a.depth.far_plane == b.depth.far_plane &&
           a.motion.direction == b.motion.direction &&
           a.motion.units == b.motion.units &&
           a.motion.provenance == b.motion.provenance &&
           a.motion.coverage == b.motion.coverage &&
           a.motion.known_exclusions == b.motion.known_exclusions &&
           a.motion.jitter_included == b.motion.jitter_included &&
           a.motion.dilated == b.motion.dilated &&
           a.motion.validity_is_explicit == b.motion.validity_is_explicit;
}

[[nodiscard]] constexpr bool SameTemporalGeometry(
    const TemporalFrameDescription& a,
    const TemporalFrameDescription& b) noexcept
{
    const auto rect_equal = [](const TemporalSubrect& x, const TemporalSubrect& y) {
        return x.x == y.x && x.y == y.y &&
               x.width == y.width && x.height == y.height;
    };
    return a.render_width == b.render_width && a.render_height == b.render_height &&
           a.output_width == b.output_width && a.output_height == b.output_height &&
           rect_equal(a.color_rect, b.color_rect) &&
           rect_equal(a.depth_rect, b.depth_rect) &&
           rect_equal(a.motion_rect, b.motion_rect);
}

// The API adapter supplies these borrowed, API-local references. Presence and
// device ownership can be checked here; SRV capability and lifetime cannot.
struct TemporalInputSource {
    const TemporalFrameDescription* description = nullptr;
    const void* resource = nullptr;
    const void* source_device = nullptr;
};

[[nodiscard]] inline bool CanJoinTemporalInputs(
    const TemporalInputSource& color,
    const TemporalInputSource& depth,
    const TemporalInputSource& motion) noexcept
{
    if (!color.description || !depth.description || !motion.description ||
        !color.resource || !depth.resource || !motion.resource ||
        !color.source_device || color.source_device != depth.source_device ||
        color.source_device != motion.source_device)
        return false;
    const auto& a = *color.description;
    const auto& b = *depth.description;
    const auto& c = *motion.description;
    return HasValidTemporalMetadata(a) && HasValidTemporalMetadata(b) &&
           HasValidTemporalMetadata(c) &&
           SameTemporalFrameIdentity(a, b) && SameTemporalFrameIdentity(a, c) &&
           SameTemporalGeometry(a, b) && SameTemporalGeometry(a, c) &&
           SameTemporalSampleMetadata(a, b) && SameTemporalSampleMetadata(a, c) &&
           a.output_ownership == b.output_ownership &&
           a.output_ownership == c.output_ownership;
}

// A new source frame can arrive after a skipped frame, but its previous history
// must then be rejected by CanReuseValidatedHistory. Ignore transport_index.
[[nodiscard]] constexpr bool IsNewerSourceFrame(
    const TemporalFrameDescription& last,
    const TemporalFrameDescription& candidate) noexcept
{
    return candidate.identity.view_index == last.identity.view_index &&
           candidate.identity.frame_index > last.identity.frame_index &&
           candidate.identity.resource_generation >= last.identity.resource_generation &&
           candidate.identity.history_generation >= last.identity.history_generation &&
           candidate.identity.camera_generation >= last.identity.camera_generation &&
           candidate.identity.config_generation >= last.identity.config_generation;
}

[[nodiscard]] inline bool CanReuseValidatedHistory(
    const TemporalFrameDescription& previous,
    const TemporalFrameDescription& current) noexcept
{
    return HasValidTemporalMetadata(previous) && HasValidTemporalMetadata(current) &&
           current.frame_delta_ms > 0.0f &&
           CanReuseHistory(previous, current) && SameTemporalGeometry(previous, current) &&
           previous.output_ownership == current.output_ownership &&
           previous.depth.convention == current.depth.convention &&
           previous.depth.sample_count == current.depth.sample_count &&
           previous.depth.inverted == current.depth.inverted &&
           previous.depth.infinite_far == current.depth.infinite_far &&
           previous.depth.near_plane == current.depth.near_plane &&
           previous.depth.far_plane == current.depth.far_plane &&
           previous.motion.direction == current.motion.direction &&
           previous.motion.units == current.motion.units &&
           previous.motion.provenance == current.motion.provenance &&
           previous.motion.coverage == current.motion.coverage &&
           previous.motion.known_exclusions == current.motion.known_exclusions &&
           previous.motion.jitter_included == current.motion.jitter_included &&
           previous.motion.dilated == current.motion.dilated &&
           previous.motion.validity_is_explicit == current.motion.validity_is_explicit;
}

} // namespace ltr::temporal
