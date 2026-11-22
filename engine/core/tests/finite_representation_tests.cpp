#include "huynity/core/render_preparation.h"
#include "huynity/objectlib/shapes/rectangle2.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace core = huynity::core;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

core::PassesConfig config2() {
    core::PassesConfig config;
    config.camera2 = render::Camera2{.width = 100, .height = 100};
    return config;
}

core::PassesConfig config3() {
    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{.width = 100, .height = 100, .nearPlane = 0, .farPlane = 100};
    return config;
}

void infiniteGeometryWithoutRepresentationIsRejectedAtomically() {
    core::Scene scene;
    auto valid = scene.createObject(objects::Rectangle2::Params{.size = {2, 2}});
    auto line = scene.createObject(objects::Line2::Params{.origin = {0, 0}, .direction = {1, 0}});
    CHECK(valid.has_value() && line.has_value());
    const auto size = scene.size();
    const auto geometry = scene.find(*line)->geometry;
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config2(), 100, 100);
    CHECK(!snapshot.has_value());
    CHECK(snapshot.error().code == objects::GeometryErrorCode::UnsupportedOperation);
    CHECK(scene.size() == size);
    CHECK(scene.find(*line)->geometry == geometry);
    CHECK(scene.find(*valid) != nullptr);
}

void lineAndRayUseExplicitFiniteRepresentations() {
    core::Scene scene;
    auto line = scene.createObject(objects::Line2::Params{.origin = {0, 0}, .direction = {1, 0}});
    auto ray = scene.createObject(objects::Ray2::Params{.origin = {0, 0}, .direction = {0, 1}});
    CHECK(line.has_value() && ray.has_value());
    auto lineGeometry = scene.find(*line)->geometry;
    auto lineRepresentation = objects::createObject(objects::Rectangle2::Params{.size = {10, 1}});
    auto rayRepresentation = objects::createObject(objects::Disk2::Params{.radius = 1});
    CHECK(lineRepresentation.has_value() && rayRepresentation.has_value());
    CHECK(scene.setFiniteRepresentation(*line, objects::Geometry{std::move(*lineRepresentation)}).has_value());
    CHECK(scene.setFiniteRepresentation(*ray, objects::Geometry{std::move(*rayRepresentation)}).has_value());
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config2(), 100, 100);
    CHECK(snapshot.has_value());
    CHECK(scene.find(*line)->geometry == lineGeometry);
    const auto& pass = std::get<render::World2DPass>(snapshot->passes[0]);
    CHECK(pass.drawItems.size() == 2);
    CHECK(snapshot->resources2.size() == 2);
}

void planeUsesFiniteRepresentation() {
    core::Scene scene;
    auto plane = scene.createObject(objects::Plane3::Params{.normal = {0, 1, 0}, .offset = 0});
    CHECK(plane.has_value());
    auto original = scene.find(*plane)->geometry;
    auto representation = objects::createObject(objects::Box3::Params{.size = {10, 0.1, 10}});
    CHECK(representation.has_value());
    CHECK(scene.setFiniteRepresentation(*plane, objects::Geometry{std::move(*representation)}).has_value());
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config3(), 100, 100);
    CHECK(snapshot.has_value());
    CHECK(scene.find(*plane)->geometry == original);
    CHECK(std::get<render::World3DPass>(snapshot->passes[0]).drawItems.size() == 1);
}

void hiddenInfiniteGeometryDoesNotFailPreparation() {
    core::Scene scene;
    auto line = scene.createObject(objects::Line2::Params{.origin = {0, 0}, .direction = {1, 0}});
    CHECK(line.has_value());
    CHECK(scene.setVisibility(*line, false).has_value());
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config2(), 100, 100);
    CHECK(snapshot.has_value());
    CHECK(std::get<render::World2DPass>(snapshot->passes[0]).drawItems.empty());
}

void invalidRepresentationDoesNotMutateInstance() {
    core::Scene scene;
    auto line = scene.createObject(objects::Line2::Params{.origin = {0, 0}, .direction = {1, 0}});
    CHECK(line.has_value());
    auto invalid = objects::createObject(objects::Ray2::Params{.origin = {0, 0}, .direction = {1, 0}});
    CHECK(invalid.has_value());
    auto result = scene.setFiniteRepresentation(*line, objects::Geometry{std::move(*invalid)});
    CHECK(!result.has_value());
    CHECK(result.error() == core::SceneError::InvalidFiniteRepresentation);
    CHECK(scene.find(*line)->finiteRepresentation == nullptr);
}

int main() {
    try {
        infiniteGeometryWithoutRepresentationIsRejectedAtomically();
        lineAndRayUseExplicitFiniteRepresentations();
        planeUsesFiniteRepresentation();
        hiddenInfiniteGeometryDoesNotFailPreparation();
        invalidRepresentationDoesNotMutateInstance();
        std::cout << "finite_representation: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
