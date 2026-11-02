#include "../src/temporal/temporal_frame.h"
#include <limits>

using namespace ltr::temporal;

#define CHECK(expression)        \
    do {                         \
        if (!(expression)) {     \
            return __LINE__;     \
        }                        \
    } while (false)

static TemporalFrameDescription MakeFrame(std::uint64_t source_frame,
                                          std::uint64_t transport_index) {
    TemporalFrameDescription frame{};
    frame.identity.frame_index = source_frame;
    frame.identity.transport_index = transport_index;
    frame.identity.view_index = 0;
    frame.identity.resource_generation = 3;
    frame.identity.history_generation = 7;
    frame.identity.camera_generation = 11;
    frame.identity.config_generation = 13;
    frame.render_width = 1920;
    frame.render_height = 1080;
    frame.output_width = 1920;
    frame.output_height = 1080;
    frame.color_rect = {0, 0, 1920, 1080};
    frame.depth_rect = {0, 0, 1920, 1080};
    frame.motion_rect = {0, 0, 1920, 1080};
    frame.depth.convention = DepthConvention::hardware;
    frame.depth.sample_count = 1;
    frame.depth.near_plane = 0.1f;
    frame.depth.far_plane = 100.0f;
    frame.output_ownership = OutputOwnership::backend;
    frame.frame_delta_ms = 1000.0f / 60.0f;
    return frame;
}

int main() {
    const auto previous = MakeFrame(100, 20);

    auto peer = MakeFrame(100, 99);
    CHECK(SameTemporalFrameIdentity(previous, peer));

    // Transport serials describe delivery, not renderer-frame identity.
    peer.identity.transport_index = 1000;
    CHECK(SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(99, 20);
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    ++peer.identity.view_index;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    ++peer.identity.resource_generation;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    ++peer.identity.history_generation;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    ++peer.identity.camera_generation;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    ++peer.identity.config_generation;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    peer = MakeFrame(100, 20);
    peer.identity.reset_reason = HistoryResetReason::manual;
    CHECK(!SameTemporalFrameIdentity(previous, peer));

    auto current = MakeFrame(101, 21);
    CHECK(CanReuseHistory(previous, current));
    CHECK(HasValidTemporalMetadata(previous));
    CHECK(CanReuseValidatedHistory(previous, current));
    CHECK(IsNewerSourceFrame(previous, current));
    CHECK(!IsNewerSourceFrame(current, previous));

    // Three independently supplied resources must belong to one source frame,
    // view, generation, geometry and API device, regardless of delivery serial.
    int color_resource = 1, depth_resource = 2, motion_resource = 3;
    int device = 4, foreign_device = 5;
    auto depth_frame = MakeFrame(100, 200);
    auto motion_frame = MakeFrame(100, 300);
    const TemporalInputSource color_input{&previous, &color_resource, &device};
    TemporalInputSource depth_input{&depth_frame, &depth_resource, &device};
    TemporalInputSource motion_input{&motion_frame, &motion_resource, &device};
    CHECK(CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_input.resource = nullptr;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_input.resource = &depth_resource;
    motion_input.source_device = &foreign_device;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    motion_input.source_device = &device;
    depth_frame.identity.view_index = 1;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(99, 200);
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    ++depth_frame.identity.resource_generation;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    depth_frame.output_ownership = OutputOwnership::shared_transport;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    depth_frame.depth_rect.y = 1;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    depth_frame.depth.inverted = true;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    depth_frame.jitter_x_pixels = 0.25f;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    depth_frame.frame_delta_ms += 1.0f;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    depth_frame = MakeFrame(100, 200);
    motion_frame.motion.provenance = MotionProvenance::optical_flow;
    CHECK(!CanJoinTemporalInputs(color_input, depth_input, motion_input));
    motion_frame = MakeFrame(100, 300);
    depth_frame = MakeFrame(100, 200);
    CHECK(CanJoinTemporalInputs(color_input, depth_input, motion_input));

    // Transport sequence is diagnostic/ownership identity. Temporal continuity
    // follows the source renderer frame, so omitted source frames reset history.
    current = MakeFrame(102, 21);
    CHECK(!CanReuseHistory(previous, current));

    // A transport serial gap alone does not invent a source-frame discontinuity.
    current = MakeFrame(101, 25);
    CHECK(CanReuseHistory(previous, current));

    current = MakeFrame(101, 21);
    ++current.identity.camera_generation;
    CHECK(!CanReuseHistory(previous, current));

    current = MakeFrame(101, 21);
    ++current.identity.resource_generation;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    ++current.identity.history_generation;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    ++current.identity.view_index;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.identity.reset_reason = HistoryResetReason::device_reset;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(100, 22);
    CHECK(!CanReuseValidatedHistory(previous, current));
    CHECK(!IsNewerSourceFrame(previous, current));

    current = MakeFrame(99, 22);
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(102, 21);
    CHECK(IsNewerSourceFrame(previous, current));
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.identity.resource_generation = 2;
    CHECK(!IsNewerSourceFrame(previous, current));

    current = MakeFrame(101, 21);
    ++current.identity.config_generation;
    CHECK(!CanReuseHistory(previous, current));

    current = MakeFrame(101, 21);
    current.render_width = 1280;
    CHECK(!CanReuseHistory(previous, current));

    current = MakeFrame(101, 21);
    current.depth_rect = {0, 0, 1920, 1079};
    CHECK(CanReuseHistory(previous, current)); // identity alone cannot detect it
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.depth.inverted = true;
    CHECK(!CanReuseValidatedHistory(previous, current));
    current = MakeFrame(101, 21);
    current.frame_delta_ms = 0.0f;
    CHECK(HasValidTemporalMetadata(current));
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.motion.jitter_included = true;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.motion.coverage = coverage_camera;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.output_ownership = OutputOwnership::shared_transport;
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.render_width = 0;
    current.output_width = 0;
    CHECK(!HasValidTemporalMetadata(current));
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.motion_rect.width = 0;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.depth_rect.y = 1080;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.color_rect.x = std::numeric_limits<std::uint32_t>::max();
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.depth.sample_count = 0;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.depth.convention = DepthConvention::unknown;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.depth.far_plane = current.depth.near_plane;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.depth.infinite_far = true;
    current.depth.far_plane = 0.0f;
    CHECK(HasValidTemporalMetadata(current));
    CHECK(!CanReuseValidatedHistory(previous, current));

    current = MakeFrame(101, 21);
    current.frame_delta_ms = std::numeric_limits<float>::quiet_NaN();
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.jitter_x_pixels = std::numeric_limits<float>::infinity();
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.motion.coverage = coverage_camera;
    current.motion.known_exclusions = coverage_camera;
    CHECK(!HasValidTemporalMetadata(current));
    current = MakeFrame(101, 21);
    current.motion.coverage = 1u << 31;
    CHECK(!HasValidTemporalMetadata(current));

    current = MakeFrame(101, 21);
    current.output_ownership = OutputOwnership::unknown;
    CHECK(!HasValidTemporalMetadata(current));

    CHECK(HasValidSubrect(previous.color_rect, previous.render_width,
                          previous.render_height));
    auto invalid_rect = previous.depth_rect;
    invalid_rect.x = 1900;
    invalid_rect.width = 64;
    CHECK(!HasValidSubrect(invalid_rect, previous.render_width,
                           previous.render_height));
    auto overflow_frame = MakeFrame(std::numeric_limits<std::uint64_t>::max(), 2);
    auto wrapped_frame = MakeFrame(0, 3);
    CHECK(!CanReuseHistory(overflow_frame, wrapped_frame));
    CHECK(!IsNewerSourceFrame(overflow_frame, wrapped_frame));

    return 0;
}
