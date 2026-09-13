#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 384;
constexpr int kSampleStride = 8;
constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }

enum class Scenario { static_scene, camera_translate, camera_rotate, rigid_object, disocclusion };

const char* ScenarioName(Scenario scenario) {
    switch (scenario) {
    case Scenario::static_scene: return "static";
    case Scenario::camera_translate: return "camera-translate";
    case Scenario::camera_rotate: return "camera-rotate";
    case Scenario::rigid_object: return "rigid-object";
    case Scenario::disocclusion: return "disocclusion";
    }
    return "unknown";
}

struct Frame {
    std::vector<float> luma;
    std::vector<std::uint8_t> surface;
};

struct Level {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;
};

struct SearchResult {
    int dx = 0;
    int dy = 0;
    float best_cost = 1.0e6f;
    float second_cost = 1.0e6f;
};

struct Stats {
    std::uint64_t active = 0;
    std::uint64_t truth_valid = 0;
    std::uint64_t truth_invalid = 0;
    std::uint64_t confident = 0;
    std::uint64_t confident_valid = 0;
    std::uint64_t confident_invalid = 0;
    std::uint64_t accurate_4px = 0;
    std::uint64_t confident_accurate_4px = 0;
    double mean_error = 0.0;
    double confident_mean_error = 0.0;
    double mean_confidence_valid = 0.0;
    double mean_confidence_invalid = 0.0;
    float max_error = 0.0f;
};

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float Hash01(int x, int y) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 0x8DA6B343u;
    value ^= static_cast<std::uint32_t>(y) * 0xD8163841u;
    value ^= value >> 13u;
    value *= 0x85EBCA6Bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ValueNoise(Vec2 point, float cellSize) {
    const float gx = point.x / cellSize;
    const float gy = point.y / cellSize;
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float fx = gx - static_cast<float>(x0);
    const float fy = gy - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Hash01(x0, y0);
    const float b = Hash01(x0 + 1, y0);
    const float c = Hash01(x0, y0 + 1);
    const float d = Hash01(x0 + 1, y0 + 1);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

Vec2 RotateAround(Vec2 point, Vec2 center, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const Vec2 d = point - center;
    return {center.x + c * d.x - s * d.y, center.y + s * d.x + c * d.y};
}

Vec2 CameraToWorld(Scenario scenario, Vec2 screen, bool previous) {
    if (previous) return screen;
    if (scenario == Scenario::camera_translate) return screen + Vec2{28.0f, -10.0f};
    if (scenario == Scenario::camera_rotate) {
        return RotateAround(screen, Vec2{kWidth * 0.5f, kHeight * 0.5f}, 6.0f * kPi / 180.0f);
    }
    return screen;
}

Vec2 PreviousObjectCenter() { return {300.0f, 186.0f}; }

Vec2 CurrentObjectCenter(Scenario scenario) {
    if (scenario == Scenario::rigid_object) return {370.0f, 174.0f};
    if (scenario == Scenario::disocclusion) return {430.0f, 186.0f};
    return PreviousObjectCenter();
}

bool InsideObject(Vec2 point, Vec2 center) {
    const Vec2 d = point - center;
    return std::abs(d.x) <= 52.0f && std::abs(d.y) <= 42.0f;
}

float BackgroundLuma(Vec2 world) {
    return Clamp01(0.42f + 0.14f * std::sin(world.x * 0.041f) +
                   0.11f * std::cos(world.y * 0.063f) +
                   0.08f * std::sin((world.x + world.y) * 0.029f) +
                   0.05f * std::cos((world.x - 2.0f * world.y) * 0.017f) +
                   0.18f * (ValueNoise(world, 31.0f) - 0.5f) +
                   0.10f * (ValueNoise(world + Vec2{91.0f, -47.0f}, 13.0f) - 0.5f));
}

float ObjectLuma(Vec2 local) {
    return Clamp01(0.56f + 0.18f * std::sin(local.x * 0.18f) +
                   0.14f * std::cos(local.y * 0.23f) +
                   0.09f * std::sin((local.x - local.y) * 0.11f) +
                   0.12f * (ValueNoise(local + Vec2{37.0f, 19.0f}, 9.0f) - 0.5f));
}

Frame GenerateFrame(Scenario scenario, bool previous) {
    Frame frame{};
    frame.luma.resize(static_cast<std::size_t>(kWidth) * kHeight);
    frame.surface.resize(static_cast<std::size_t>(kWidth) * kHeight);
    const Vec2 objectCenter = previous ? PreviousObjectCenter() : CurrentObjectCenter(scenario);
    const bool objectMoves = scenario == Scenario::rigid_object || scenario == Scenario::disocclusion;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const Vec2 screen{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const Vec2 world = CameraToWorld(scenario, screen, previous);
            const Vec2 objectPoint = objectMoves ? screen : world;
            const bool object = InsideObject(objectPoint, objectCenter);
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            frame.surface[index] = object ? 2u : 1u;
            frame.luma[index] = object ? ObjectLuma(objectPoint - objectCenter) : BackgroundLuma(world);
        }
    }
    return frame;
}

Vec2 GroundTruthMotion(Scenario scenario, Vec2 currentScreen, std::uint8_t currentSurface) {
    if (scenario == Scenario::camera_translate || scenario == Scenario::camera_rotate) {
        return CameraToWorld(scenario, currentScreen, false) - currentScreen;
    }
    if (currentSurface == 2u && (scenario == Scenario::rigid_object || scenario == Scenario::disocclusion)) {
        return PreviousObjectCenter() - CurrentObjectCenter(scenario);
    }
    return {};
}

Level Downsample(const Level& source) {
    Level result{};
    result.width = (source.width + 1) / 2;
    result.height = (source.height + 1) / 2;
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int oy = 0; oy < 2; ++oy) {
                for (int ox = 0; ox < 2; ++ox) {
                    const int sx = x * 2 + ox;
                    const int sy = y * 2 + oy;
                    if (sx < source.width && sy < source.height) {
                        sum += source.pixels[static_cast<std::size_t>(sy) * source.width + sx];
                        ++count;
                    }
                }
            }
            result.pixels[static_cast<std::size_t>(y) * result.width + x] = sum / static_cast<float>(count);
        }
    }
    return result;
}

std::vector<Level> BuildPyramid(const Frame& frame) {
    std::vector<Level> levels;
    levels.push_back({kWidth, kHeight, frame.luma});
    for (int level = 1; level <= 6; ++level) levels.push_back(Downsample(levels.back()));
    return levels;
}

float PatchCost(const Level& current, const Level& previous, int x, int y,
                int dx, int dy, int radius) {
    float cost = 0.0f;
    int samples = 0;
    for (int oy = -radius; oy <= radius; ++oy) {
        for (int ox = -radius; ox <= radius; ++ox) {
            const int cx = x + ox;
            const int cy = y + oy;
            const int px = x + dx + ox;
            const int py = y + dy + oy;
            if (cx < 0 || cy < 0 || px < 0 || py < 0 ||
                cx >= current.width || cy >= current.height || px >= previous.width || py >= previous.height) {
                return 1.0e6f;
            }
            const float a = current.pixels[static_cast<std::size_t>(cy) * current.width + cx];
            const float b = previous.pixels[static_cast<std::size_t>(py) * previous.width + px];
            cost += std::abs(a - b);
            ++samples;
        }
    }
    return samples > 0 ? cost / static_cast<float>(samples) : 1.0e6f;
}

SearchResult SearchPatch(const Level& current, const Level& previous, int x, int y,
                         int seedX, int seedY, int searchRadius, int patchRadius) {
    SearchResult result{};
    for (int oy = -searchRadius; oy <= searchRadius; ++oy) {
        for (int ox = -searchRadius; ox <= searchRadius; ++ox) {
            const int dx = seedX + ox;
            const int dy = seedY + oy;
            const float cost = PatchCost(current, previous, x, y, dx, dy, patchRadius);
            if (cost < result.best_cost) {
                result.second_cost = result.best_cost;
                result.best_cost = cost;
                result.dx = dx;
                result.dy = dy;
            } else if (cost < result.second_cost) {
                result.second_cost = cost;
            }
        }
    }
    return result;
}

float PatchVariance(const Level& image, int x, int y, int radius) {
    float sum = 0.0f;
    float sum2 = 0.0f;
    int samples = 0;
    for (int oy = -radius; oy <= radius; ++oy) {
        for (int ox = -radius; ox <= radius; ++ox) {
            const int sx = x + ox;
            const int sy = y + oy;
            if (sx < 0 || sy < 0 || sx >= image.width || sy >= image.height) continue;
            const float value = image.pixels[static_cast<std::size_t>(sy) * image.width + sx];
            sum += value;
            sum2 += value * value;
            ++samples;
        }
    }
    if (samples <= 1) return 0.0f;
    const float mean = sum / static_cast<float>(samples);
    return std::max(0.0f, sum2 / static_cast<float>(samples) - mean * mean);
}

void WriteLe16(std::ofstream& out, std::uint16_t value) {
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    out.write(reinterpret_cast<const char*>(bytes.data()), 2);
}

void WriteLe32(std::ofstream& out, std::uint32_t value) {
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    out.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

std::uint32_t Rgb(float red, float green, float blue) {
    const auto byte = [](float value) {
        return static_cast<std::uint32_t>(std::lround(Clamp01(value) * 255.0f));
    };
    return (byte(red) << 16u) | (byte(green) << 8u) | byte(blue);
}

void WriteBmp(const std::string& path, const std::vector<std::uint32_t>& rgb) {
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(((kWidth * 3) + 3) & ~3);
    const std::uint32_t dataBytes = rowBytes * kHeight;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    WriteLe16(out, 0x4D42u);
    WriteLe32(out, 54u + dataBytes);
    WriteLe16(out, 0u);
    WriteLe16(out, 0u);
    WriteLe32(out, 54u);
    WriteLe32(out, 40u);
    WriteLe32(out, static_cast<std::uint32_t>(kWidth));
    WriteLe32(out, static_cast<std::uint32_t>(kHeight));
    WriteLe16(out, 1u);
    WriteLe16(out, 24u);
    WriteLe32(out, 0u);
    WriteLe32(out, dataBytes);
    WriteLe32(out, 2835u);
    WriteLe32(out, 2835u);
    WriteLe32(out, 0u);
    WriteLe32(out, 0u);
    const std::array<unsigned char, 3> zero{};
    const std::uint32_t padding = rowBytes - static_cast<std::uint32_t>(kWidth * 3);
    for (int y = kHeight - 1; y >= 0; --y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::uint32_t value = rgb[static_cast<std::size_t>(y) * kWidth + x];
            const std::array<unsigned char, 3> bgr{
                static_cast<unsigned char>(value),
                static_cast<unsigned char>(value >> 8),
                static_cast<unsigned char>(value >> 16),
            };
            out.write(reinterpret_cast<const char*>(bgr.data()), 3);
        }
        out.write(reinterpret_cast<const char*>(zero.data()), padding);
    }
}

Stats RunScenario(Scenario scenario) {
    const Frame previous = GenerateFrame(scenario, true);
    const Frame current = GenerateFrame(scenario, false);
    const auto previousPyramid = BuildPyramid(previous);
    const auto currentPyramid = BuildPyramid(current);
    Stats stats{};
    double errorSum = 0.0;
    double confidentErrorSum = 0.0;
    double confidenceValidSum = 0.0;
    double confidenceInvalidSum = 0.0;
    std::vector<std::uint32_t> flowImage(static_cast<std::size_t>(kWidth) * kHeight, Rgb(0.0f, 0.0f, 0.0f));
    std::vector<std::uint32_t> confidenceImage(flowImage.size(), Rgb(0.0f, 0.0f, 0.0f));
    std::vector<std::uint32_t> errorImage(flowImage.size(), Rgb(0.0f, 0.0f, 0.0f));

    auto fillBlock = [](std::vector<std::uint32_t>& image, int centerX, int centerY, std::uint32_t value) {
        for (int oy = -kSampleStride / 2; oy < kSampleStride / 2; ++oy) {
            for (int ox = -kSampleStride / 2; ox < kSampleStride / 2; ++ox) {
                const int x = centerX + ox;
                const int y = centerY + oy;
                if (x >= 0 && y >= 0 && x < kWidth && y < kHeight) {
                    image[static_cast<std::size_t>(y) * kWidth + x] = value;
                }
            }
        }
    };

    for (int y = kSampleStride / 2; y < kHeight; y += kSampleStride) {
        for (int x = kSampleStride / 2; x < kWidth; x += kSampleStride) {
            const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
            const std::uint8_t surface = current.surface[index];
            ++stats.active;

            int flowX = 0;
            int flowY = 0;
            SearchResult search{};
            for (int level = 6; level >= 0; --level) {
                if (level < 6) {
                    flowX *= 2;
                    flowY *= 2;
                }
                const int levelX = x >> level;
                const int levelY = y >> level;
                const int searchRadius = level == 6 ? 4 : (level >= 3 ? 2 : 1);
                const int patchRadius = level >= 3 ? 1 : 2;
                search = SearchPatch(currentPyramid[static_cast<std::size_t>(level)],
                                     previousPyramid[static_cast<std::size_t>(level)],
                                     levelX, levelY, flowX, flowY, searchRadius, patchRadius);
                flowX = search.dx;
                flowY = search.dy;
            }

            int wideFlowX = 0;
            int wideFlowY = 0;
            SearchResult wideSearch = SearchPatch(currentPyramid[3], previousPyramid[3],
                                                   x >> 3, y >> 3, 0, 0, 18, 2);
            wideFlowX = wideSearch.dx;
            wideFlowY = wideSearch.dy;
            for (int level = 2; level >= 0; --level) {
                wideFlowX *= 2;
                wideFlowY *= 2;
                wideSearch = SearchPatch(currentPyramid[static_cast<std::size_t>(level)],
                                         previousPyramid[static_cast<std::size_t>(level)],
                                         x >> level, y >> level, wideFlowX, wideFlowY, 1, 2);
                wideFlowX = wideSearch.dx;
                wideFlowY = wideSearch.dy;
            }
            if (wideSearch.best_cost < search.best_cost) {
                flowX = wideFlowX;
                flowY = wideFlowY;
                search = wideSearch;
            }

            const Vec2 screen{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const Vec2 truth = GroundTruthMotion(scenario, screen, surface);
            const Vec2 previousPos = screen + truth;
            const int previousX = static_cast<int>(std::floor(previousPos.x));
            const int previousY = static_cast<int>(std::floor(previousPos.y));
            const bool inBounds = previousX >= 0 && previousY >= 0 && previousX < kWidth && previousY < kHeight;
            const bool truthValid = inBounds &&
                previous.surface[static_cast<std::size_t>(previousY) * kWidth + previousX] == surface;
            const float error = std::hypot(static_cast<float>(flowX) - truth.x,
                                           static_cast<float>(flowY) - truth.y);
            const float variance = PatchVariance(currentPyramid[0], x, y, 2);
            const float textureConfidence = Clamp01(std::sqrt(variance) * 24.0f);
            const float costConfidence = search.best_cost < 1.0e5f ? std::exp(-search.best_cost * 40.0f) : 0.0f;
            const float uniqueness = search.second_cost < 1.0e5f
                ? Clamp01((search.second_cost - search.best_cost) / (search.second_cost + 1.0e-5f) * 4.0f)
                : 0.0f;
            const float confidence = costConfidence * (0.25f + 0.75f * textureConfidence) *
                                     (0.25f + 0.75f * uniqueness);
            const bool confident = confidence >= 0.35f;
            if (confident) ++stats.confident;

            if (truthValid) {
                ++stats.truth_valid;
                errorSum += error;
                confidenceValidSum += confidence;
                stats.max_error = std::max(stats.max_error, error);
                if (error <= 4.0f) ++stats.accurate_4px;
                if (confident) {
                    ++stats.confident_valid;
                    confidentErrorSum += error;
                    if (error <= 4.0f) ++stats.confident_accurate_4px;
                }
            } else {
                ++stats.truth_invalid;
                confidenceInvalidSum += confidence;
                if (confident) ++stats.confident_invalid;
            }

            const float nx = std::clamp(static_cast<float>(flowX) / 128.0f, -1.0f, 1.0f);
            const float ny = std::clamp(static_cast<float>(flowY) / 128.0f, -1.0f, 1.0f);
            const float magnitude = std::hypot(static_cast<float>(flowX), static_cast<float>(flowY));
            fillBlock(flowImage, x, y,
                      Rgb(0.5f + nx * 0.5f, 0.5f + ny * 0.5f, std::min(magnitude / 128.0f, 1.0f)));
            fillBlock(confidenceImage, x, y, Rgb(1.0f - confidence, confidence, 0.0f));
            const float errorVisual = std::min(error / 16.0f, 1.0f);
            fillBlock(errorImage, x, y,
                      truthValid ? Rgb(errorVisual, 0.0f, 1.0f - errorVisual) : Rgb(1.0f, 0.0f, 1.0f));
        }
    }

    if (stats.truth_valid != 0) {
        stats.mean_error = errorSum / static_cast<double>(stats.truth_valid);
        stats.mean_confidence_valid = confidenceValidSum / static_cast<double>(stats.truth_valid);
    }
    if (stats.confident_valid != 0) {
        stats.confident_mean_error = confidentErrorSum / static_cast<double>(stats.confident_valid);
    }
    if (stats.truth_invalid != 0) {
        stats.mean_confidence_invalid = confidenceInvalidSum / static_cast<double>(stats.truth_invalid);
    }
    const std::string prefix = std::string("ltr_diag_optical_") + ScenarioName(scenario);
    WriteBmp(prefix + "_flow.bmp", flowImage);
    WriteBmp(prefix + "_confidence.bmp", confidenceImage);
    WriteBmp(prefix + "_error.bmp", errorImage);
    return stats;
}

bool ValidateStats(Scenario scenario, const Stats& stats) {
    if (stats.active < 3000 || stats.truth_valid < 3000) return false;
    const double accurateShare = static_cast<double>(stats.accurate_4px) / static_cast<double>(stats.truth_valid);
    const double confidentAccurateShare = stats.confident_valid != 0
        ? static_cast<double>(stats.confident_accurate_4px) / static_cast<double>(stats.confident_valid)
        : 0.0;
    if (stats.confident_valid < stats.truth_valid / 3u) return false;
    if (scenario == Scenario::static_scene) {
        return accurateShare >= 0.99 && stats.mean_error <= 0.25 && confidentAccurateShare >= 0.99;
    }
    const double minimumAccurateShare = scenario == Scenario::camera_translate ? 0.70 :
                                        scenario == Scenario::camera_rotate ? 0.75 : 0.90;
    const double maximumConfidentMeanError = scenario == Scenario::camera_translate ? 6.0 : 4.0;
    if (accurateShare < minimumAccurateShare || confidentAccurateShare < 0.85 ||
        stats.confident_mean_error > maximumConfidentMeanError) {
        return false;
    }
    if (stats.truth_invalid != 0 && stats.mean_confidence_valid <= stats.mean_confidence_invalid * 1.5) return false;
    if (scenario == Scenario::disocclusion && stats.truth_invalid < 100) return false;
    return true;
}

void AppendStats(std::ostream& out, Scenario scenario, const Stats& stats, bool ok) {
    const double accurateShare = stats.truth_valid != 0
        ? static_cast<double>(stats.accurate_4px) / static_cast<double>(stats.truth_valid)
        : 0.0;
    const double confidentAccurateShare = stats.confident_valid != 0
        ? static_cast<double>(stats.confident_accurate_4px) / static_cast<double>(stats.confident_valid)
        : 0.0;
    out << (ok ? "PASS " : "FAIL ") << ScenarioName(scenario)
        << " active=" << stats.active
        << " truth_valid=" << stats.truth_valid
        << " truth_invalid=" << stats.truth_invalid
        << " accurate_4px=" << stats.accurate_4px
        << " accurate_share=" << accurateShare
        << " confident=" << stats.confident
        << " confident_valid=" << stats.confident_valid
        << " confident_invalid=" << stats.confident_invalid
        << " confident_accurate_4px=" << stats.confident_accurate_4px
        << " confident_accurate_share=" << confidentAccurateShare
        << " mean_error=" << stats.mean_error
        << " confident_mean_error=" << stats.confident_mean_error
        << " mean_confidence_valid=" << stats.mean_confidence_valid
        << " mean_confidence_invalid=" << stats.mean_confidence_invalid
        << " max_error=" << stats.max_error << '\n';
}

} // namespace

int main() {
    const std::array scenarios{
        Scenario::static_scene,
        Scenario::camera_translate,
        Scenario::camera_rotate,
        Scenario::rigid_object,
        Scenario::disocclusion,
    };
    std::ofstream report("ltr_optical_flow_probe.txt", std::ios::trunc);
    report << std::fixed << std::setprecision(6);
    report << "LTR Bridge independent optical-flow baseline\n";
    report << "extent=" << kWidth << 'x' << kHeight
           << " grid=1/8 search=1/64-to-1px direction=current-to-previous\n";
    bool ok = true;
    for (const Scenario scenario : scenarios) {
        const Stats stats = RunScenario(scenario);
        const bool scenarioOk = ValidateStats(scenario, stats);
        AppendStats(report, scenario, stats, scenarioOk);
        ok = ok && scenarioOk;
    }
    report << (ok ? "RESULT PASS\n" : "RESULT FAIL\n");
    report.close();

    std::ifstream readback("ltr_optical_flow_probe.txt");
    std::cout << readback.rdbuf();
    return ok ? 0 : 2;
}
