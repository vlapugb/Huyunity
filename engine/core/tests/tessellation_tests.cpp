#include "huynity/core/render_preparation.h"
#include "huynity/core/scene.h"

#include <iostream>
#include <limits>
#include <memory>
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

core::ObjectId createSphere(core::Scene& scene, double radius = 2) {
    auto result = scene.createObject<objects::ObjectType::Sphere3>({.radius = radius});
    CHECK(result.has_value());
    return *result;
}

void reuseAcrossMovementAndSharedInstances() {
    core::Scene scene;
    const auto first = createSphere(scene);
    const auto second = scene.createInstance(first);
    CHECK(second.has_value());
    auto initial = scene.tessellation(first, {.maxDeviation = 0.05, .maxPrimitiveCount = 10000});
    CHECK(initial.has_value());
    CHECK(scene.tessellationCalculations() == 1);

    CHECK(scene.setLocalTransform(first, math::translation(math::Vector3{{4, 5, 6}})).has_value());
    auto moved = scene.tessellation(first, {.maxDeviation = 0.05, .maxPrimitiveCount = 10000});
    auto shared = scene.tessellation(*second, {.maxDeviation = 0.05, .maxPrimitiveCount = 10000});
    CHECK(moved.has_value() && shared.has_value());
    CHECK(*moved == *initial);
    CHECK(*shared == *initial);
    CHECK(scene.tessellationCalculations() == 1);
    CHECK(scene.tessellationCacheHits() == 2);
}

void qualityKeyIncludesEveryOption() {
    core::Scene scene;
    const auto id = createSphere(scene);
    auto initial = scene.tessellation(id, {.maxDeviation = 0.1, .maxPrimitiveCount = 10000});
    auto deviation = scene.tessellation(id, {.maxDeviation = 0.05, .maxPrimitiveCount = 10000});
    auto primitiveLimit = scene.tessellation(id, {.maxDeviation = 0.1, .maxPrimitiveCount = 9000});
    CHECK(initial.has_value() && deviation.has_value() && primitiveLimit.has_value());
    CHECK(*deviation != *initial);
    CHECK(*primitiveLimit != *initial);
    CHECK(scene.tessellationCalculations() == 3);
    auto reused = scene.tessellation(id, {.maxDeviation = 0.05, .maxPrimitiveCount = 10000});
    CHECK(reused.has_value() && *reused == *deviation);
    CHECK(scene.tessellationCalculations() == 3);
}

void geometryChangeInvalidatesOnlyNewVersion() {
    core::Scene scene;
    const auto id = createSphere(scene);
    auto initial = scene.tessellation(id);
    CHECK(initial.has_value());
    auto replacement = objects::createObject<objects::ObjectType::Sphere3>({.radius = 3});
    CHECK(replacement.has_value());
    CHECK(scene.setGeometry(id, objects::Geometry{std::move(*replacement)}).has_value());
    CHECK(scene.find(id)->dirty.tessellation);
    auto changed = scene.tessellation(id);
    CHECK(changed.has_value());
    CHECK(*changed != *initial);
    CHECK(scene.tessellationCalculations() == 2);
    CHECK(!scene.find(id)->dirty.tessellation);
}

void cacheReleasesGeometryAndDistinguishesVersions() {
    const auto sphere = objects::createObject<objects::ObjectType::Sphere3>({.radius = 2});
    CHECK(sphere.has_value());
    const auto copy = std::make_shared<const objects::Geometry>(*sphere);
    auto geometry = std::make_shared<const objects::Geometry>(*sphere);
    const std::weak_ptr<const objects::Geometry> owner{geometry};
    core::TessellationCache cache;
    const auto initial = cache.get(geometry, 1, {});
    const auto nextVersion = cache.get(geometry, 2, {});
    const auto distinctGeometry = cache.get(copy, 1, {});
    CHECK(initial.has_value() && nextVersion.has_value() && distinctGeometry.has_value());
    CHECK(*nextVersion != *initial && *distinctGeometry != *initial);
    CHECK(cache.size() == 3);

    CHECK(cache.get(geometry, 1, {}) == initial);
    CHECK(cache.calculations() == 3);
    CHECK(cache.cacheHits() == 1);
    cache.markSourceDetached(geometry);
    geometry.reset();
    CHECK(owner.expired());
    cache.collectDetachedSources();
    CHECK(cache.size() == 1);
}

void sourceAddressReuseDoesNotReturnOldMesh() {
    auto firstSphere = objects::createObject<objects::ObjectType::Sphere3>({.radius = 2});
    auto nextSphere = objects::createObject<objects::ObjectType::Sphere3>({.radius = 3});
    CHECK(firstSphere.has_value() && nextSphere.has_value());
    objects::Geometry storage{std::move(*firstSphere)};
    core::TessellationCache cache;
    auto firstOwner = std::shared_ptr<const objects::Geometry>{&storage, [](const auto*) {}};
    const auto initial = cache.get(firstOwner, 1, {});
    CHECK(initial.has_value());
    firstOwner.reset();

    storage = std::move(*nextSphere);
    const auto nextOwner = std::shared_ptr<const objects::Geometry>{&storage, [](const auto*) {}};
    const auto next = cache.get(nextOwner, 1, {});
    CHECK(next.has_value() && *next != *initial);
    CHECK(cache.size() == 1);
    CHECK(cache.calculations() == 2);
    CHECK(cache.cacheHits() == 0);
}

void sceneReleasesMeshAfterLastSharedInstance() {
    core::Scene scene;
    const auto source = createSphere(scene);
    const auto instance = scene.createInstance(source);
    CHECK(instance.has_value());
    const auto* entry = scene.find(source);
    CHECK(entry != nullptr);
    const std::weak_ptr<const objects::Geometry> geometry{entry->geometry};
    std::weak_ptr<const objects::TessellatedGeometry> mesh;
    {
        const auto cached = scene.tessellation(source);
        CHECK(cached.has_value());
        mesh = *cached;
    }
    CHECK(scene.removeSubtree(source).has_value());
    CHECK(!geometry.expired());
    CHECK(scene.tessellation(*instance) == mesh.lock());
    CHECK(scene.tessellationCalculations() == 1);
    CHECK(scene.removeSubtree(*instance).has_value());
    core::PassesConfig config;
    config.hudEnabled = false;
    CHECK(core::RenderPreparation::preparePasses(scene, config).has_value());
    CHECK(geometry.expired() && mesh.expired());
}

void sceneReleasesMeshAfterViewDropsSource() {
    core::Scene scene;
    const auto source = createSphere(scene);
    auto view = scene.view();
    const std::weak_ptr<const objects::Geometry> geometry{view.instances.front().geometry};
    std::weak_ptr<const objects::TessellatedGeometry> mesh;
    {
        const auto cached = scene.tessellation(source);
        CHECK(cached.has_value());
        mesh = *cached;
    }
    CHECK(scene.removeSubtree(source).has_value());
    core::PassesConfig config;
    config.hudEnabled = false;
    CHECK(core::RenderPreparation::preparePasses(scene, config).has_value());
    CHECK(!geometry.expired() && !mesh.expired());
    view.instances.clear();
    CHECK(geometry.expired());
    for (std::size_t frame = 0; frame < 120; ++frame) {
        CHECK(core::RenderPreparation::preparePasses(scene, config).has_value());
    }
    CHECK(mesh.expired());
}

void invalidDeviationDoesNotEnterCache() {
    const auto sphere = objects::createObject<objects::ObjectType::Sphere3>({.radius = 2});
    CHECK(sphere.has_value());
    const auto geometry = std::make_shared<const objects::Geometry>(*sphere);
    const auto unsupported = std::make_shared<const objects::Geometry>(math::Point3{});
    const objects::TessellationOptions invalidOptions{.maxDeviation = std::numeric_limits<double>::quiet_NaN()};
    core::TessellationCache cache;
    const auto valid = cache.get(geometry, 1, {});
    CHECK(valid.has_value());
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto invalid = cache.get(geometry, 1, invalidOptions);
        CHECK(!invalid.has_value() && invalid.error().code == objects::GeometryErrorCode::InvalidOptions);
    }
    const auto unsupportedResult = cache.get(unsupported, 1, invalidOptions);
    CHECK(!unsupportedResult.has_value() &&
          unsupportedResult.error().code == objects::GeometryErrorCode::UnsupportedOperation);
    CHECK(cache.size() == 1);
    CHECK(cache.calculations() == 4);
    CHECK(cache.cacheHits() == 0);
    CHECK(cache.get(geometry, 1, {}) == valid);
}

void geometryErrorLeavesSceneAndCacheConsistent() {
    core::Scene scene;
    const auto id = createSphere(scene);
    auto initial = scene.tessellation(id);
    CHECK(initial.has_value());
    const auto geometry = scene.find(id)->geometry;
    const auto version = scene.find(id)->geometryVersion;
    const auto calculations = scene.tessellationCalculations();

    auto failed = scene.tessellation(id, {.maxDeviation = 0.01, .maxPrimitiveCount = 0});
    CHECK(!failed.has_value());
    CHECK(scene.size() == 1);
    CHECK(scene.find(id)->geometry == geometry);
    CHECK(scene.find(id)->geometryVersion == version);
    CHECK(scene.find(id)->dirty.tessellation == false);
    CHECK(scene.tessellationCalculations() == calculations + 1);

    auto valid = scene.tessellation(id);
    CHECK(valid.has_value());
    CHECK(*valid == *initial);
    CHECK(scene.tessellationCalculations() == calculations + 1);
}

int main() {
    try {
        reuseAcrossMovementAndSharedInstances();
        qualityKeyIncludesEveryOption();
        geometryChangeInvalidatesOnlyNewVersion();
        cacheReleasesGeometryAndDistinguishesVersions();
        sourceAddressReuseDoesNotReturnOldMesh();
        sceneReleasesMeshAfterLastSharedInstance();
        sceneReleasesMeshAfterViewDropsSource();
        invalidDeviationDoesNotEnterCache();
        geometryErrorLeavesSceneAndCacheConsistent();
        std::cout << "tessellation_cache: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
