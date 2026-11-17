#include "huynity/core/scene.h"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

core::ObjectId createObject(core::Scene& scene) {
    auto result = scene.createObject(objects::Point3::Params{.coordinates = {1, 2, 3}});
    CHECK(result.has_value());
    return *result;
}

void staticObjectCanMoveWithoutInvalidatingTessellation() {
    core::Scene scene;
    const auto id = createObject(scene);
    CHECK(scene.setMobility(id, core::Mobility::Static).has_value());
    const auto* before = scene.find(id);
    CHECK(before->mobility == core::Mobility::Static);
    const auto geometry = before->geometry;
    const auto geometryVersion = before->geometryVersion;
    const auto revisions = before->revisions;
    const auto dirty = before->dirty;

    CHECK(scene.setLocalTransform(id, math::translation(math::Vector3{{4, 5, 6}})).has_value());
    const auto* after = scene.find(id);
    CHECK(after->mobility == core::Mobility::Static);
    CHECK(after->geometry == geometry);
    CHECK(after->geometryVersion == geometryVersion);
    CHECK(after->revisions.localTransform == revisions.localTransform + 1);
    CHECK(after->revisions.worldTransform == revisions.worldTransform + 1);
    CHECK(after->revisions.bounds == revisions.bounds + 1);
    CHECK(after->revisions.material == revisions.material);
    CHECK(after->revisions.visibility == revisions.visibility);
    CHECK(!after->dirty.worldTransform);
    CHECK(after->dirty.bounds);
    CHECK(after->dirty.material == dirty.material);
    CHECK(after->dirty.visibility == dirty.visibility);
    CHECK(!after->dirty.tessellation);
}

void staticObjectCanReplaceGeometry() {
    core::Scene scene;
    const auto id = createObject(scene);
    CHECK(scene.setMobility(id, core::Mobility::Static).has_value());
    const auto oldGeometry = scene.find(id)->geometry;
    const auto oldVersion = scene.find(id)->geometryVersion;
    const auto before = scene.find(id)->revisions;

    CHECK(scene.setGeometry(id, *createGeometry(objects::Point3::Params{.coordinates = {7, 8, 9}})).has_value());
    const auto* after = scene.find(id);
    CHECK(after->geometry != oldGeometry);
    CHECK(after->geometryVersion == oldVersion + 1);
    CHECK(after->dirty.bounds);
    CHECK(after->dirty.tessellation);
    CHECK(after->revisions.bounds == before.bounds + 1);
    CHECK(after->revisions.localTransform == before.localTransform);
    CHECK(after->revisions.worldTransform == before.worldTransform);
    CHECK(after->revisions.material == before.material);
    CHECK(after->revisions.visibility == before.visibility);
    CHECK(std::get<objects::Point3>(*oldGeometry).coordinates() == math::Point3{1, 2, 3});
}

void visualChangesHaveIndependentRevisions() {
    core::Scene scene;
    const auto id = createObject(scene);
    const auto geometry = scene.find(id)->geometry;
    const auto geometryVersion = scene.find(id)->geometryVersion;
    const auto before = scene.find(id)->revisions;

    CHECK(scene.setVisibility(id, false).has_value());
    CHECK(!scene.find(id)->isVisible);
    CHECK(scene.find(id)->dirty.visibility);
    CHECK(scene.find(id)->revisions.visibility == before.visibility + 1);
    CHECK(scene.find(id)->revisions.localTransform == before.localTransform);
    CHECK(scene.find(id)->revisions.worldTransform == before.worldTransform);
    CHECK(scene.find(id)->revisions.bounds == before.bounds);
    CHECK(scene.find(id)->revisions.material == before.material);
    CHECK(scene.find(id)->geometry == geometry);
    CHECK(scene.find(id)->geometryVersion == geometryVersion);
    CHECK(!scene.find(id)->dirty.tessellation);

    auto material = std::make_shared<const render::Material>(render::Material{.color = {.red = 0.25}});
    CHECK(scene.setMaterial(id, material).has_value());
    CHECK(scene.find(id)->material == material);
    CHECK(scene.find(id)->dirty.material);
    CHECK(scene.find(id)->revisions.material == before.material + 1);
    CHECK(scene.find(id)->revisions.localTransform == before.localTransform);
    CHECK(scene.find(id)->revisions.worldTransform == before.worldTransform);
    CHECK(scene.find(id)->revisions.bounds == before.bounds);
    CHECK(scene.find(id)->revisions.visibility == before.visibility + 1);
    CHECK(scene.find(id)->geometry == geometry);
    CHECK(scene.find(id)->geometryVersion == geometryVersion);
    CHECK(!scene.find(id)->dirty.tessellation);
}

void parentMovementMarksOnlyDependentWorldState() {
    core::Scene scene;
    const auto parent = createObject(scene);
    const auto child = createObject(scene);
    const auto unrelated = createObject(scene);
    CHECK(scene.setParent(child, parent).has_value());
    const auto childRevisions = scene.find(child)->revisions;
    const auto unrelatedRevisions = scene.find(unrelated)->revisions;
    const auto childGeometry = scene.find(child)->geometry;
    const auto childGeometryVersion = scene.find(child)->geometryVersion;

    CHECK(scene.setLocalTransform(parent, math::translation(math::Vector3{{3, 0, 0}})).has_value());
    CHECK(scene.find(child)->revisions.worldTransform == childRevisions.worldTransform + 1);
    CHECK(scene.find(child)->revisions.bounds == childRevisions.bounds + 1);
    CHECK(scene.find(child)->revisions.localTransform == childRevisions.localTransform);
    CHECK(scene.find(child)->revisions.material == childRevisions.material);
    CHECK(scene.find(child)->revisions.visibility == childRevisions.visibility);
    CHECK(scene.find(child)->dirty.bounds);
    CHECK(!scene.find(child)->dirty.worldTransform);
    CHECK(!scene.find(child)->dirty.tessellation);
    CHECK(scene.find(child)->geometry == childGeometry);
    CHECK(scene.find(child)->geometryVersion == childGeometryVersion);
    CHECK(scene.find(unrelated)->revisions.worldTransform == unrelatedRevisions.worldTransform);
    CHECK(scene.find(unrelated)->revisions.bounds == unrelatedRevisions.bounds);
    CHECK(scene.find(unrelated)->revisions.localTransform == unrelatedRevisions.localTransform);
    CHECK(scene.find(unrelated)->revisions.material == unrelatedRevisions.material);
    CHECK(scene.find(unrelated)->revisions.visibility == unrelatedRevisions.visibility);
}

void invalidCommandsDoNotMutateState() {
    core::Scene scene;
    const auto id = createObject(scene);
    const core::ObjectId missing{1000, 1};
    CHECK(!scene.setMobility(missing, core::Mobility::Static).has_value());
    CHECK(!scene.setVisibility(missing, false).has_value());
    CHECK(!scene.setMaterial(missing, std::make_shared<const render::Material>()).has_value());
    CHECK(scene.find(id)->mobility == core::Mobility::Dynamic);
    CHECK(scene.find(id)->isVisible);
    CHECK(scene.find(id)->material == nullptr);
}

int main() {
    try {
        staticObjectCanMoveWithoutInvalidatingTessellation();
        staticObjectCanReplaceGeometry();
        visualChangesHaveIndependentRevisions();
        parentMovementMarksOnlyDependentWorldState();
        invalidCommandsDoNotMutateState();
        std::cout << "state: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
