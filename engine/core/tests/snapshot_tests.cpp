#include "huynity/core/render_preparation.h"
#include "huynity/math/vector_unsigned.h"

#include <array>
#include <cstdint>
#include <cstring>
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

std::array<std::uint32_t, 3> readTriangle(std::span<const std::byte> bytes, std::size_t index) {
    math::VectorUnsigned3 triangle;
    std::memcpy(&triangle, bytes.data() + index * sizeof(triangle), sizeof(triangle));
    return {triangle[0], triangle[1], triangle[2]};
}

render::OrthographicCamera3 camera3() {
    return {.width = 100, .height = 100, .nearPlane = 0, .farPlane = 100};
}

void sharedMeshUsesOneOwnerAndIndices() {
    core::Scene scene;
    auto source = scene.createObject(objects::Box3::Params{.size = {1, 2, 3}});
    CHECK(source.has_value());
    auto instance = scene.createInstance(*source);
    CHECK(instance.has_value());
    auto material = std::make_shared<const render::Material>();
    CHECK(scene.setMaterial(*source, material).has_value());
    CHECK(scene.setMaterial(*instance, material).has_value());
    core::PassesConfig config;
    config.camera3 = camera3();
    config.hudEnabled = false;

    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config, 640, 480);
    CHECK(snapshot.has_value());
    CHECK(snapshot->resources3.size() == 1);
    CHECK(snapshot->materials.size() == 2);
    const auto& pass = std::get<render::World3DPass>(snapshot->passes[0]);
    CHECK(pass.drawItems.size() == 2);
    CHECK(pass.drawItems[0].resource == 0 && pass.drawItems[1].resource == 0);
    CHECK(pass.drawItems[0].material == 1 && pass.drawItems[1].material == 1);
    CHECK(snapshot->resources3[0].positions.size() == 8);

    auto second = core::RenderPreparation::prepareSnapshot(scene, config, 640, 480);
    CHECK(second.has_value());
    CHECK(second->resources3[0].owner == snapshot->resources3[0].owner);
    CHECK(second->resources3[0].positions.data() == snapshot->resources3[0].positions.data());
    CHECK(second->resources3[0].triangles.data() == snapshot->resources3[0].triangles.data());
}

void snapshotOutlivesRemovedSceneObject() {
    core::Scene scene;
    auto id = scene.createObject(objects::Box3::Params{.size = {{2, 2, 2}}});
    CHECK(id.has_value());
    core::PassesConfig config;
    config.camera3 = camera3();
    config.hudEnabled = false;
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config, 32, 32);
    CHECK(snapshot.has_value());
    const auto* positions = snapshot->resources3[0].positions.data();
    const auto owner = snapshot->resources3[0].owner;
    CHECK(scene.removeSubtree(*id).has_value());
    CHECK(scene.size() == 0);
    CHECK(snapshot->resources3[0].owner == owner);
    CHECK(snapshot->resources3[0].positions.data() == positions);
    CHECK(snapshot->resources3[0].positions.size() == 8);
    CHECK(snapshot->resources3[0].triangles.size() == 12 * sizeof(math::VectorUnsigned3));
    CHECK(readTriangle(snapshot->resources3[0].triangles, 0) == std::array<std::uint32_t, 3>{0, 2, 1});
    CHECK(readTriangle(snapshot->resources3[0].triangles, 11) == std::array<std::uint32_t, 3>{3, 4, 7});
}

void hudAndWorld2dUseResourceAndMaterialTables() {
    core::Scene scene;
    auto world = scene.createObject(objects::Rectangle2::Params{.size = {2, 2}});
    CHECK(world.has_value());
    auto hud = scene.createInstance(*world);
    CHECK(hud.has_value());
    CHECK(scene.setRenderProperties(*hud, core::RenderSpace::Hud, 4, 2).has_value());
    core::PassesConfig config;
    config.camera2 = render::Camera2{.width = 100, .height = 100};
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config, 100, 100);
    CHECK(snapshot.has_value());
    CHECK(snapshot->resources2.size() == 1);
    const auto& worldPass = std::get<render::World2DPass>(snapshot->passes[0]);
    const auto& hudPass = std::get<render::Hud2DPass>(snapshot->passes[1]);
    CHECK(worldPass.drawItems[0].resource == 0);
    CHECK(hudPass.drawItems[0].resource == 0);
    CHECK(hudPass.drawItems[0].layer == 4 && hudPass.drawItems[0].order == 2);
}

int main() {
    try {
        sharedMeshUsesOneOwnerAndIndices();
        snapshotOutlivesRemovedSceneObject();
        hudAndWorld2dUseResourceAndMaterialTables();
        std::cout << "snapshot: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
