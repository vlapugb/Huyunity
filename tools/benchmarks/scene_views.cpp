#include "huynity/core/render_preparation.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;

#ifdef HUYNITY_COUNT_ALLOCATIONS
namespace {

struct AllocationCounter {
    std::size_t count{};
    std::size_t bytes{};
};

AllocationCounter* measurement{};

} // namespace

void* operator new(std::size_t size) {
    if (auto* storage = std::malloc(size == 0 ? 1 : size)) {
        if (measurement != nullptr) {
            ++measurement->count;
            measurement->bytes += size;
        }
        return storage;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* storage) noexcept {
    std::free(storage);
}

void operator delete[](void* storage) noexcept {
    std::free(storage);
}

void operator delete(void* storage, std::size_t) noexcept {
    std::free(storage);
}

void operator delete[](void* storage, std::size_t) noexcept {
    std::free(storage);
}
#endif

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t warmupFrames = 20;
constexpr std::size_t measuredFrames = 101;
constexpr std::size_t width = 1280;
constexpr std::size_t height = 720;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Result>
auto value(Result result, std::string_view message) {
    check(result.has_value(), message);
    return *std::move(result);
}

struct Fixture {
    core::Scene scene;
    core::PassesConfig config;

    explicit Fixture(std::size_t groups) {
        config.camera3 = render::OrthographicCamera3{
            .worldToView = math::Transform3{}, .width = 260, .height = 260, .nearPlane = 1, .farPlane = 50};
        config.camera2 = render::Camera2{.worldToView = math::Transform2{}, .width = 260, .height = 260};
        config.world2DFirst = true;

        const auto root3 = value(scene.createObject<objects::ObjectType::Box3>({.size = {{1, 1, 1}}}), "make box");
        const auto root2 =
            value(scene.createObject<objects::ObjectType::Rectangle2>({.size = {{1, 1}}}), "make rectangle");
        for (std::size_t index = 0; index < groups; ++index) {
            const auto parent3 = index == 0 ? root3 : value(scene.createInstance(root3), "3D parent");
            const auto child3 = value(scene.createInstance(root3), "3D child");
            const auto parent2 = index == 0 ? root2 : value(scene.createInstance(root2), "2D parent");
            const auto child2 = value(scene.createInstance(root2), "2D child");
            const auto parentHud = value(scene.createInstance(root2), "HUD parent");
            const auto childHud = value(scene.createInstance(root2), "HUD child");
            const auto x = static_cast<double>(index % 32) * 3 - 60;
            const auto y = static_cast<double>(index / 32) * 3 - 60;
            check(scene.setLocalTransform(parent3, math::translation(math::Vector3{{x, y, 10}})).has_value(),
                  "3D position");
            check(scene.setLocalTransform(child3, math::translation(math::Vector3{{1, 1, 0}})).has_value(),
                  "3D child position");
            check(scene.setLocalTransform(parent2, math::translation(math::Vector2{{x, y}})).has_value(),
                  "2D position");
            check(scene.setLocalTransform(child2, math::translation(math::Vector2{{1, 1}})).has_value(),
                  "2D child position");
            check(scene.setLocalTransform(parentHud, math::translation(math::Vector2{{x + 60, y + 60}})).has_value(),
                  "HUD position");
            check(scene.setLocalTransform(childHud, math::translation(math::Vector2{{1, 1}})).has_value(),
                  "HUD child position");
            check(scene.setParent(child3, parent3).has_value(), "3D hierarchy");
            check(scene.setParent(child2, parent2).has_value(), "2D hierarchy");
            check(scene.setParent(childHud, parentHud).has_value(), "HUD hierarchy");
            check(scene.setRenderProperties(parent2, core::RenderSpace::World, static_cast<std::int32_t>(index % 3), 0)
                      .has_value(),
                  "2D layer");
            check(scene.setRenderProperties(child2, core::RenderSpace::World, static_cast<std::int32_t>(index % 3), 1)
                      .has_value(),
                  "2D child layer");
            check(scene.setRenderProperties(parentHud, core::RenderSpace::Hud, static_cast<std::int32_t>(index % 4), 0)
                      .has_value(),
                  "HUD layer");
            check(scene.setRenderProperties(childHud, core::RenderSpace::Hud, static_cast<std::int32_t>(index % 4), 1)
                      .has_value(),
                  "HUD child layer");
        }
    }
};

void run(std::size_t groups) {
    Fixture fixture(groups);
    std::array<std::uint64_t, measuredFrames> samples{};
#ifdef HUYNITY_COUNT_ALLOCATIONS
    std::array<AllocationCounter, measuredFrames> allocations{};
#endif
    bool valid = true;
    for (std::size_t frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
#ifdef HUYNITY_COUNT_ALLOCATIONS
        if (frame >= warmupFrames) {
            measurement = &allocations[frame - warmupFrames];
        }
#endif
        const auto start = Clock::now();
        {
            const auto snapshot =
                core::RenderPreparation::prepareSnapshot(fixture.scene, fixture.config, width, height);
            if (snapshot && snapshot->resources3.size() == 1 && snapshot->resources2.size() == 1 &&
                snapshot->passes.size() == 3) {
                valid &= std::get<render::World2DPass>(snapshot->passes[0]).drawItems.size() == groups * 2;
                valid &= std::get<render::World3DPass>(snapshot->passes[1]).drawItems.size() == groups * 2;
                valid &= std::get<render::Hud2DPass>(snapshot->passes[2]).drawItems.size() == groups * 2;
            } else {
                valid = false;
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
#ifdef HUYNITY_COUNT_ALLOCATIONS
        measurement = nullptr;
#endif
        if (frame >= warmupFrames) {
            samples[frame - warmupFrames] = static_cast<std::uint64_t>(elapsed);
        }
    }
    check(valid, "unexpected pass order, draw counts, or resource sharing");
    check(fixture.scene.tessellationCalculations() == 2, "unexpected cache misses");
    check(fixture.scene.tessellationCacheHits() == groups * 6 * (warmupFrames + measuredFrames) - 2,
          "unexpected cache hits");

    for (std::size_t frame = 0; frame < measuredFrames; ++frame) {
        std::cout << "mixed," << groups * 6 << ",2," << frame << ',' << samples[frame];
#ifdef HUYNITY_COUNT_ALLOCATIONS
        std::cout << ',' << allocations[frame].count << ',' << allocations[frame].bytes;
#endif
        std::cout << '\n';
    }
}

} // namespace

int main() {
    try {
        std::cout << "phase,objects,distinct_geometry,frame,nanoseconds";
#ifdef HUYNITY_COUNT_ALLOCATIONS
        std::cout << ",new_calls,requested_bytes";
#endif
        std::cout << '\n';
        run(32);
        run(256);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
