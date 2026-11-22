#include "huynity/core/render_preparation.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;

using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t warmupFrames = 20;
constexpr std::size_t measuredFrames = 101;
constexpr std::size_t width = 1280;
constexpr std::size_t height = 720;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Fixture {
    core::Scene scene;
    core::PassesConfig config;
    std::vector<core::ObjectId> ids;

    Fixture(std::size_t count, bool shared) {
        config.camera3 = render::OrthographicCamera3{
            .worldToView = math::Transform3{}, .width = 260, .height = 260, .nearPlane = 1, .farPlane = 50};
        config.hudEnabled = false;

        ids.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto id = [&] {
                if (shared && index > 0) {
                    const auto instance = scene.createInstance(ids.front());
                    check(instance.has_value(), "create instance");
                    return *instance;
                }
                const auto box = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{1, 1, 1}}});
                check(box.has_value(), "create box");
                return *box;
            }();
            ids.push_back(id);
            const auto x = static_cast<double>(index % 64) * 2 - 63;
            const auto y = static_cast<double>(index / 64) * 2 - 63;
            check(scene.setLocalTransform(id, math::translation(math::Vector3{{x, y, 10}})).has_value(),
                  "position box");
            check(scene.setMobility(id, core::Mobility::Static).has_value(), "set box mobility");
        }
    }
};

void run(std::size_t count, bool shared, bool snapshot) {
    Fixture fixture(count, shared);
    const auto resources = shared ? std::size_t{1} : count;
    std::array<std::int64_t, measuredFrames> samples{};
    std::size_t checksum = 0;
    bool valid = true;
    for (std::size_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
        const auto start = Clock::now();
        if (snapshot) {
            const auto result = core::RenderPreparation::prepareSnapshot(fixture.scene, fixture.config, width, height);
            if (result && result->passes.size() == 1) {
                const auto drawCount = std::get<render::World3DPass>(result->passes.front()).drawItems.size();
                checksum += drawCount;
                valid &= result->resources3.size() == resources;
            } else {
                valid = false;
            }
        } else {
            for (auto id : fixture.ids) {
                const auto result = fixture.scene.tessellation(id);
                checksum += result.has_value() && std::holds_alternative<objects::TriangleMesh3>(**result);
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        if (frame >= warmupFrames) {
            samples[frame - warmupFrames] = elapsed;
        }
    }
    check(valid && checksum == count * (warmupFrames + measuredFrames), "unexpected geometry/draw count");
    check(fixture.scene.tessellationCalculations() == resources, "unexpected cache miss");
    check(fixture.scene.tessellationCacheHits() == checksum - resources, "unexpected cache hit count");

    for (std::size_t frame = 0; frame < measuredFrames; ++frame) {
        std::cout << (snapshot ? "prepare" : "lookup") << ',' << count << ',' << resources << ',' << frame << ','
                  << samples[frame] << '\n';
    }
}

} // namespace

int main() {
    try {
        std::cout << "phase,objects,distinct_geometry,frame,nanoseconds\n";
        for (auto count : {std::size_t{128}, std::size_t{4096}}) {
            for (const bool shared : {true, false}) {
                run(count, shared, false);
                run(count, shared, true);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
