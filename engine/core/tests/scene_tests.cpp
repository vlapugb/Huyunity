#include "huynity/core/scene.h"
#include "huynity/objectlib/geometry.h"
#include "huynity/objectlib/shapes/circle2.h"
#include "huynity/objectlib/shapes/sphere3.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace core = huynity::core;
namespace objects = huynity::objectlib;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

void typedCreationAndAtomicErrors() {
    core::Scene scene;
    auto created = scene.createObject(objects::Circle2::Params{.radius = 2});
    CHECK(created.has_value());
    CHECK(scene.size() == 1);
    const auto* instance = scene.find(*created);
    CHECK(instance != nullptr);
    CHECK(instance->geometry != nullptr);
    CHECK(std::get<objects::Circle2>(*instance->geometry).radius() == 2);

    auto rejected = scene.createObject(objects::Circle2::Params{.radius = 0});
    CHECK(!rejected.has_value());
    CHECK(rejected.error().code == objects::CreateErrorCode::InvalidRadius);
    CHECK(scene.size() == 1);
    CHECK(scene.find(*created) == instance);
    CHECK(std::get<objects::Circle2>(*instance->geometry).radius() == 2);
}

void runtimeCreationAndAtomicErrors() {
    core::Scene scene;
    auto spec = objects::GeometrySpec{objects::Sphere3::Params{.radius = 3}};
    auto created = scene.createObject(spec);
    CHECK(created.has_value());
    CHECK(scene.size() == 1);
    CHECK(std::get<objects::Sphere3>(*scene.find(*created)->geometry).radius() == 3);

    auto rejected = scene.createObject(objects::GeometrySpec{objects::Sphere3::Params{.radius = -1}});
    CHECK(!rejected.has_value());
    CHECK(rejected.error().code == objects::CreateErrorCode::InvalidRadius);
    CHECK(scene.size() == 1);
    CHECK(std::get<objects::Sphere3>(*scene.find(*created)->geometry).radius() == 3);
}

void sharedGeometry() {
    core::Scene scene;
    const auto source = scene.createObject(objects::Box3::Params{.size = {{1, 2, 3}}});
    CHECK(source.has_value());
    const auto* original = scene.find(*source);
    const auto resource = original->geometry;
    const auto geometryVersion = original->geometryVersion;
    auto second = scene.createInstance(*source);
    CHECK(second.has_value());
    CHECK(*second != *source);
    CHECK(scene.size() == 2);
    CHECK(scene.find(*second)->geometry == resource);
    CHECK(scene.find(*second)->geometryVersion == geometryVersion);
    CHECK(resource.use_count() == 3);

    auto rejected = scene.createInstance(core::ObjectId{1000, 1});
    CHECK(!rejected.has_value());
    CHECK(scene.size() == 2);
}

int main() {
    try {
        typedCreationAndAtomicErrors();
        runtimeCreationAndAtomicErrors();
        sharedGeometry();
        std::cout << "scene: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
