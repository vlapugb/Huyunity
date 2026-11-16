#include "huynity/core/render_preparation.h"
#include "huynity/image/image_buffer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace core = huynity::core;
namespace image = huynity::image;
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
    objects::TriangleIndices triangle;
    std::memcpy(&triangle, bytes.data() + index * sizeof(triangle), sizeof(triangle));
    return triangle.indices;
}

render::OrthographicCamera3 camera3() {
    return {.width = 100, .height = 100, .nearPlane = 0, .farPlane = 100};
}

void sharedMeshUsesOneOwnerAndIndices() {
    core::Scene scene;
    auto source = scene.createObject<objects::ObjectType::Box3>({.size = {{1, 2, 3}}});
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
    auto id = scene.createObject<objects::ObjectType::Box3>({.size = {{2, 2, 2}}});
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
    CHECK(snapshot->resources3[0].triangles.size() == 12 * sizeof(objects::TriangleIndices));
    CHECK(readTriangle(snapshot->resources3[0].triangles, 0) == std::array<std::uint32_t, 3>{0, 2, 1});
    CHECK(readTriangle(snapshot->resources3[0].triangles, 11) == std::array<std::uint32_t, 3>{3, 4, 7});
}

void hudAndWorld2dUseResourceAndMaterialTables() {
    core::Scene scene;
    auto world = scene.createObject<objects::ObjectType::Rectangle2>({.size = {{2, 2}}});
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

void snapshotRetainsTexCoordsAndShadingAfterRemoval() {
    core::Scene scene;
    auto id = scene.createObject<objects::ObjectType::TriangleMesh3>({
        .positions = {{{-1, -1, 5}}, {{0, 1, 5}}, {{1, -1, 5}}},
        .triangles = {{{0, 1, 2}}},
    });
    CHECK(id.has_value());
    CHECK(scene.setShading(*id, render::ShadingModel::Gouraud).has_value());
    CHECK(scene.setBackfaceCulling(*id, false).has_value());
    CHECK(scene.setShadowOptions(*id, false, true).has_value());
    CHECK(scene.setTexCoords(*id, {{{0.0, 0.0}}, {{0.5, 1.0}}, {{1.0, 0.0}}}).has_value());

    core::PassesConfig config;
    config.camera3 = camera3();
    config.hudEnabled = false;
    auto snapshot = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(snapshot.has_value());
    CHECK(snapshot->resources3.size() == 1);
    CHECK(snapshot->resources3[0].texCoords.size() == 3);
    CHECK(snapshot->resources3[0].texCoordsOwner != nullptr);
    const auto& item = std::get<render::World3DPass>(snapshot->passes[0]).drawItems[0];
    CHECK(item.shading == render::ShadingModel::Gouraud);
    CHECK(!item.backfaceCulling);
    CHECK(!item.castShadow);
    CHECK(item.receiveShadow);

    CHECK(scene.removeSubtree(*id).has_value());
    CHECK(snapshot->resources3[0].texCoords[1][1] == 1.0);
}

void resourceIdentityIsStableAcrossFrames() {
    core::Scene scene;
    auto id = scene.createObject<objects::ObjectType::Box3>({.size = {{1, 1, 1}}});
    CHECK(id.has_value());
    auto texture = std::make_shared<image::ImageBuffer>(*image::ImageBuffer::create({2, 2}));
    auto material = std::make_shared<render::Material>();
    material->color = render::Color{1, 0, 0, 1};
    material->texture = texture;
    CHECK(scene.setMaterial(*id, std::shared_ptr<const render::Material>(material)).has_value());
    core::PassesConfig config;
    config.camera3 = camera3();
    config.hudEnabled = false;

    auto first = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(first.has_value());
    CHECK(first->resources3.size() == 1);
    const auto firstKey = std::pair{first->resources3[0].resourceId, first->resources3[0].version};
    const auto firstTexKey = std::pair{first->materials[1]->textureId, first->materials[1]->textureVersion};

    CHECK(scene.setLocalTransform(*id, math::Transform3{}).has_value());
    auto second = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(second.has_value());
    // Трансформация между кадрами не меняет идентичность геометрии и текстуры.
    CHECK(second->resources3.size() == 1);
    CHECK(std::pair{second->resources3[0].resourceId, second->resources3[0].version} == firstKey);
    CHECK(std::pair{second->materials[1]->textureId, second->materials[1]->textureVersion} == firstTexKey);
    CHECK(first->materials[1]->textureId != 0);
}

void resourceIdentityChangesWhenContentReplaced() {
    core::Scene scene;
    auto id = scene.createObject<objects::ObjectType::Box3>({.size = {{1, 1, 1}}});
    CHECK(id.has_value());
    auto material = std::make_shared<render::Material>();
    auto texture = std::make_shared<image::ImageBuffer>(*image::ImageBuffer::create({2, 2}));
    material->texture = texture;
    CHECK(scene.setMaterial(*id, std::shared_ptr<const render::Material>(material)).has_value());
    core::PassesConfig config;
    config.camera3 = camera3();
    config.hudEnabled = false;

    auto before = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(before.has_value());
    const auto meshKey = std::pair{before->resources3[0].resourceId, before->resources3[0].version};
    const auto texKey = std::pair{before->materials[1]->textureId, before->materials[1]->textureVersion};

    const auto resized = objects::createObject<objects::ObjectType::Box3>({.size = {{2, 2, 2}}});
    CHECK(resized.has_value());
    CHECK(scene.setGeometry(*id, *resized).has_value());
    auto afterGeometry = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(afterGeometry.has_value());
    CHECK(std::pair{afterGeometry->resources3[0].resourceId, afterGeometry->resources3[0].version} != meshKey);

    auto replaced = std::make_shared<render::Material>();
    replaced->texture = std::make_shared<image::ImageBuffer>(*image::ImageBuffer::create({4, 4}));
    CHECK(scene.setMaterial(*id, std::shared_ptr<const render::Material>(replaced)).has_value());
    auto afterTexture = core::RenderPreparation::prepareSnapshot(scene, config, 64, 64);
    CHECK(afterTexture.has_value());
    CHECK(std::pair{afterTexture->materials[1]->textureId, afterTexture->materials[1]->textureVersion} != texKey);
}

int main() {
    try {
        sharedMeshUsesOneOwnerAndIndices();
        snapshotOutlivesRemovedSceneObject();
        hudAndWorld2dUseResourceAndMaterialTables();
        snapshotRetainsTexCoordsAndShadingAfterRemoval();
        resourceIdentityIsStableAcrossFrames();
        resourceIdentityChangesWhenContentReplaced();
        std::cout << "snapshot: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
