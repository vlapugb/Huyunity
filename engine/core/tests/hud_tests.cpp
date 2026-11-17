#include "huynity/core/render_preparation.h"

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

core::ObjectId createRectangle(core::Scene& scene, core::RenderSpace space, int layer, int order) {
    auto id = scene.createObject(objects::Rectangle2::Params{.size = {2, 2}});
    CHECK(id.has_value());
    CHECK(scene.setRenderProperties(*id, space, layer, order).has_value());
    return *id;
}

core::ObjectId createBox(core::Scene& scene) {
    auto id = scene.createObject(objects::Box3::Params{.size = {1, 1, 1}});
    CHECK(id.has_value());
    return *id;
}

render::Camera2 camera2() {
    return {.width = 100, .height = 100};
}

render::OrthographicCamera3 camera3() {
    return {.width = 100, .height = 100, .nearPlane = 0, .farPlane = 100};
}

void hudUsesScreenSpaceLayersOrderAndAlpha() {
    core::Scene scene;
    createRectangle(scene, core::RenderSpace::Hud, 2, 0);
    const auto early = createRectangle(scene, core::RenderSpace::Hud, 1, 5);
    const auto hidden = createRectangle(scene, core::RenderSpace::Hud, 0, 0);
    CHECK(scene.setVisibility(hidden, false).has_value());
    auto material = std::make_shared<const render::Material>(render::Material{.color = {.alpha = 0.4}});
    CHECK(scene.setMaterial(early, material).has_value());
    CHECK(scene.setLocalTransform(early, math::translation(math::Vector2{{30, 40}})).has_value());

    auto hud = core::RenderPreparation::prepareHud2D(scene);
    CHECK(hud.has_value());
    CHECK(hud->drawItems.size() == 2);
    CHECK(hud->drawItems[0].layer == 1 && hud->drawItems[0].order == 5);
    CHECK(hud->drawItems[1].layer == 2);
    CHECK(hud->drawItems[0].alpha == 0.4);
    CHECK(hud->drawItems[0].localToWorld.translation(0) == 30);
    CHECK(hud->drawItems[0].localToWorld.translation(1) == 40);
}

void twoDimensionalOnlyActivatesWorldAndHud() {
    core::Scene scene;
    const auto world = createRectangle(scene, core::RenderSpace::World, 3, 7);
    createRectangle(scene, core::RenderSpace::Hud, 0, 0);
    auto material = std::make_shared<const render::Material>(render::Material{.color = {.alpha = 0.65}});
    CHECK(scene.setMaterial(world, material).has_value());
    core::PassesConfig config;
    config.camera2 = camera2();
    auto prepared = core::RenderPreparation::preparePasses(scene, config);
    CHECK(prepared.has_value());
    CHECK(prepared->passes.size() == 2);
    CHECK(std::holds_alternative<core::WorldDrawList2>(prepared->passes[0]));
    CHECK(std::holds_alternative<core::HudDrawList2>(prepared->passes[1]));
    CHECK(std::get<core::WorldDrawList2>(prepared->passes[0]).drawItems.size() == 1);
    const auto& draw = std::get<core::WorldDrawList2>(prepared->passes[0]).drawItems[0];
    CHECK(draw.layer == 3 && draw.order == 7);
    CHECK(draw.alpha == 0.65);
    CHECK(std::get<core::HudDrawList2>(prepared->passes[1]).drawItems.size() == 1);
}

void mixedSceneOrdersAllActivePasses() {
    core::Scene scene;
    createBox(scene);
    createRectangle(scene, core::RenderSpace::World, 0, 0);
    createRectangle(scene, core::RenderSpace::Hud, 0, 0);
    core::PassesConfig config;
    config.camera3 = camera3();
    config.camera2 = camera2();
    auto prepared = core::RenderPreparation::preparePasses(scene, config);
    CHECK(prepared.has_value());
    CHECK(prepared->passes.size() == 3);
    CHECK(std::holds_alternative<core::WorldDrawList3>(prepared->passes[0]));
    CHECK(std::holds_alternative<core::WorldDrawList2>(prepared->passes[1]));
    CHECK(std::holds_alternative<core::HudDrawList2>(prepared->passes[2]));
    CHECK(std::get<core::WorldDrawList3>(prepared->passes[0]).drawItems.size() == 1);
    CHECK(std::get<core::WorldDrawList2>(prepared->passes[1]).drawItems.size() == 1);
    CHECK(std::get<core::HudDrawList2>(prepared->passes[2]).drawItems.size() == 1);

    config.world2DFirst = true;
    auto reversed = core::RenderPreparation::preparePasses(scene, config);
    CHECK(reversed.has_value());
    CHECK(std::holds_alternative<core::WorldDrawList2>(reversed->passes[0]));
    CHECK(std::holds_alternative<core::WorldDrawList3>(reversed->passes[1]));
    CHECK(std::holds_alternative<core::HudDrawList2>(reversed->passes[2]));
}

void threeDimensionalWorldAndHudWithoutWorld2d() {
    core::Scene scene;
    createBox(scene);
    createRectangle(scene, core::RenderSpace::Hud, 0, 0);
    core::PassesConfig config;
    config.camera3 = camera3();
    auto prepared = core::RenderPreparation::preparePasses(scene, config);
    CHECK(prepared.has_value());
    CHECK(prepared->passes.size() == 2);
    CHECK(std::holds_alternative<core::WorldDrawList3>(prepared->passes[0]));
    CHECK(std::holds_alternative<core::HudDrawList2>(prepared->passes[1]));
}

void hudCanBeDisabledIndependently() {
    core::Scene scene;
    createRectangle(scene, core::RenderSpace::Hud, 0, 0);
    core::PassesConfig config;
    config.hudEnabled = false;
    auto prepared = core::RenderPreparation::preparePasses(scene, config);
    CHECK(prepared.has_value());
    CHECK(prepared->passes.empty());
}

int main() {
    try {
        hudUsesScreenSpaceLayersOrderAndAlpha();
        twoDimensionalOnlyActivatesWorldAndHud();
        mixedSceneOrdersAllActivePasses();
        threeDimensionalWorldAndHudWithoutWorld2d();
        hudCanBeDisabledIndependently();
        std::cout << "hud: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
