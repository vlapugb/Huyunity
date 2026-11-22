#include "huynity/core/scene.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

core::ObjectId createBox(core::Scene& scene, const math::Vector3& size) {
    auto result = scene.createObject(objects::Box3::Params{.size = size});
    CHECK(result.has_value());
    return *result;
}

void checkBounds(const core::Scene& scene, core::ObjectId id, const math::Point3& min, const math::Point3& max) {
    const auto& bounds = scene.find(id)->worldBounds;
    CHECK(bounds.has_value());
    const auto& box = std::get<objects::Aabb3>(*bounds);
    CHECK(box.min() == min);
    CHECK(box.max() == max);
}

void movementReusesLocalBoundsAndMatchesFullRecompute() {
    core::Scene scene;
    const auto parent = createBox(scene, {2, 4, 6});
    const auto child = createBox(scene, {1, 1, 1});
    const auto unrelated = createBox(scene, {2, 2, 2});
    CHECK(scene.setParent(child, parent).has_value());
    CHECK(scene.setLocalTransform(child, math::translation(math::Vector3{1, 2, 3})).has_value());
    CHECK(scene.refreshSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 3);

    CHECK(scene.setLocalTransform(parent, math::translation(math::Vector3{10, 20, 30})).has_value());
    CHECK(scene.refreshSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 3);
    CHECK(!scene.find(parent)->dirty.bounds);
    CHECK(!scene.find(child)->dirty.bounds);
    CHECK(!scene.find(unrelated)->dirty.bounds);
    checkBounds(scene, parent, math::Point3{9, 18, 27}, math::Point3{11, 22, 33});
    checkBounds(scene, child, math::Point3{10.5, 21.5, 32.5}, math::Point3{11.5, 22.5, 33.5});
    checkBounds(scene, unrelated, math::Point3{-1, -1, -1}, math::Point3{1, 1, 1});

    CHECK(scene.recomputeSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 6);
    checkBounds(scene, parent, math::Point3{9, 18, 27}, math::Point3{11, 22, 33});
    checkBounds(scene, child, math::Point3{10.5, 21.5, 32.5}, math::Point3{11.5, 22.5, 33.5});
    checkBounds(scene, unrelated, math::Point3{-1, -1, -1}, math::Point3{1, 1, 1});
}

void geometryVersionControlsLocalCache() {
    core::Scene scene;
    const auto id = createBox(scene, {2, 2, 2});
    CHECK(scene.refreshSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 1);
    CHECK(scene.refreshSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 1);

    auto replacement = objects::createObject(objects::Box3::Params{.size = {4, 6, 8}});
    CHECK(replacement.has_value());
    CHECK(scene.setGeometry(id, objects::Geometry{std::move(*replacement)}).has_value());
    CHECK(scene.refreshSpatialState().has_value());
    CHECK(scene.localBoundsCalculations() == 2);
    checkBounds(scene, id, math::Point3{-2, -3, -4}, math::Point3{2, 3, 4});
}

void rotatedBoundsMatchFullRecompute() {
    core::Scene scene;
    const auto id = createBox(scene, {2, 4, 2});
    auto transform =
        math::compose(math::translation(math::Vector3{3, 4, 5}), math::rotation(math::RotationAxis::Z, 0.5));
    CHECK(scene.setLocalTransform(id, transform).has_value());
    CHECK(scene.refreshSpatialState().has_value());
    const auto incremental = scene.find(id)->worldBounds;
    CHECK(scene.recomputeSpatialState().has_value());
    const auto& full = scene.find(id)->worldBounds;
    CHECK(std::get<objects::Aabb3>(*incremental).min() == std::get<objects::Aabb3>(*full).min());
    CHECK(std::get<objects::Aabb3>(*incremental).max() == std::get<objects::Aabb3>(*full).max());
}

int main() {
    try {
        movementReusesLocalBoundsAndMatchesFullRecompute();
        geometryVersionControlsLocalCache();
        rotatedBoundsMatchFullRecompute();
        std::cout << "spatial: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
