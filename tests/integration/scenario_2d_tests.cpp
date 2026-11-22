#include "huynity/core/render_preparation.h"
#include "huynity/core/scene.h"
#include "huynity/image/image_buffer.h"
#include "huynity/math/transform.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/software_renderer.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace core = huynity::core;
namespace image = huynity::image;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace renderer = huynity::renderer;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

struct Rgba {
    int red;
    int green;
    int blue;
    int alpha;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

constexpr Rgba BLACK{0, 0, 0, 255};
constexpr Rgba BLUE{0, 0, 255, 255};
constexpr Rgba RED{255, 0, 0, 255};
constexpr Rgba YELLOW{255, 255, 0, 255};

constexpr std::size_t IMAGE_WIDTH = 64;
constexpr std::size_t IMAGE_HEIGHT = 64;

render::Color makeColor(double red, double green, double blue, double alpha = 1.0) {
    return {red, green, blue, alpha};
}

std::shared_ptr<const render::Material> makeMaterial(double red, double green, double blue, double alpha = 1.0) {
    return std::make_shared<const render::Material>(makeColor(red, green, blue, alpha));
}

image::ImageBuffer makeFramebuffer(std::size_t width, std::size_t height) {
    auto created = image::ImageBuffer::create({width, height});
    if (!created) {
        throw std::runtime_error("failed to create framebuffer");
    }
    return std::move(*created);
}

Rgba pixel(const image::ImageBuffer& framebuffer, std::size_t x, std::size_t y) {
    const auto bytes = framebuffer.bytes();
    const std::size_t offset = y * framebuffer.stride() + x * 4;
    return {std::to_integer<int>(bytes[offset]), std::to_integer<int>(bytes[offset + 1]),
            std::to_integer<int>(bytes[offset + 2]), std::to_integer<int>(bytes[offset + 3])};
}

core::PassesConfig make2DPassesConfig() {
    core::PassesConfig config;
    config.camera2 = render::Camera2{.worldToView = math::Transform2{}, .width = 20.0, .height = 20.0};
    config.hudEnabled = true;
    return config;
}

void scenario2DMovementAndPassOrder() {
    core::Scene scene;

    // 1. Статическое 2D-окружение: прямоугольный фон (16x16 в мировых координатах)
    auto staticBackground = scene.createObject(objects::Rectangle2::Params{.size = {16.0, 16.0}});
    CHECK(staticBackground.has_value());
    const auto staticId = *staticBackground;
    CHECK(scene.setMobility(staticId, core::Mobility::Static).has_value());
    CHECK(scene.setRenderProperties(staticId, core::RenderSpace::World, 0, 0).has_value());
    CHECK(scene.setMaterial(staticId, makeMaterial(0.0, 0.0, 1.0)).has_value());

    // 2. Динамический 2D-объект: диск радиуса 2.0, начальная позиция (-3, 0)
    auto dynamicDisk = scene.createObject(objects::Disk2::Params{.radius = 2.0});
    CHECK(dynamicDisk.has_value());
    const auto dynamicId = *dynamicDisk;
    CHECK(scene.setMobility(dynamicId, core::Mobility::Dynamic).has_value());
    CHECK(scene.setLocalTransform(dynamicId, math::translation(math::Vector2{{-3.0, 0.0}})).has_value());
    CHECK(scene.setRenderProperties(dynamicId, core::RenderSpace::World, 1, 0).has_value());
    CHECK(scene.setMaterial(dynamicId, makeMaterial(1.0, 0.0, 0.0)).has_value());

    // 3. Элемент HUD в экранных пикселях: прямоугольник 20x10 с центром в (15, 12)
    auto hudElement = scene.createObject(objects::Rectangle2::Params{.size = {20.0, 10.0}});
    CHECK(hudElement.has_value());
    const auto hudId = *hudElement;
    CHECK(scene.setLocalTransform(hudId, math::translation(math::Vector2{{15.0, 12.0}})).has_value());
    CHECK(scene.setRenderProperties(hudId, core::RenderSpace::Hud, 0, 0).has_value());
    CHECK(scene.setMaterial(hudId, makeMaterial(1.0, 1.0, 0.0)).has_value());

    // Проверка состояния сцены через view
    const auto sceneView = scene.view();
    CHECK(sceneView.instances.size() == 3);
    CHECK(scene.find(staticId)->mobility == core::Mobility::Static);
    CHECK(scene.find(dynamicId)->mobility == core::Mobility::Dynamic);
    CHECK(scene.find(hudId)->renderSpace == core::RenderSpace::Hud);

    const auto config = make2DPassesConfig();

    // Первый снимок: начальное состояние
    auto snapshot1 = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshot1.has_value());
    snapshot1->clearColor = makeColor(0.0, 0.0, 0.0);

    // Проверка структуры снимка: ровно 2 пасса в порядке World2DPass, затем Hud2DPass
    CHECK(snapshot1->passes.size() == 2);
    CHECK(std::holds_alternative<render::World2DPass>(snapshot1->passes[0]));
    CHECK(std::holds_alternative<render::Hud2DPass>(snapshot1->passes[1]));

    const auto& worldPass = std::get<render::World2DPass>(snapshot1->passes[0]);
    const auto& hudPass = std::get<render::Hud2DPass>(snapshot1->passes[1]);
    CHECK(worldPass.drawItems.size() == 2);
    CHECK(hudPass.drawItems.size() == 1);

    // Headless-рендеринг кадра 1
    renderer::SoftwareRenderer softwareRenderer;
    auto framebuffer1 = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(softwareRenderer.render(*snapshot1, framebuffer1).has_value());

    // Проверка пикселей кадра 1:
    // (2, 2): фон за пределами мира и HUD — черный
    CHECK(pixel(framebuffer1, 2, 2) == BLACK);
    // (42, 32): статический синий фон (динамический объект пока слева на x = -3)
    CHECK(pixel(framebuffer1, 42, 32) == BLUE);
    // (32, 50): неизменная часть статического синего фона
    CHECK(pixel(framebuffer1, 32, 50) == BLUE);
    // (22, 32): динамический красный диск на позиции x = -3
    CHECK(pixel(framebuffer1, 22, 32) == RED);
    // (15, 12): желтый HUD перекрывает статический фон
    CHECK(pixel(framebuffer1, 15, 12) == YELLOW);

    // Фиксация счетчиков тесселяции до перемещения
    const std::size_t calculationsBefore = scene.tessellationCalculations();
    const std::size_t cacheHitsBefore = scene.tessellationCacheHits();
    CHECK(calculationsBefore > 0);

    // Перемещение динамического объекта на новые координаты (x = +3.0) через setLocalTransform
    CHECK(scene.setLocalTransform(dynamicId, math::translation(math::Vector2{{3.0, 0.0}})).has_value());

    // Второй снимок кадра
    auto snapshot2 = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshot2.has_value());
    snapshot2->clearColor = makeColor(0.0, 0.0, 0.0);

    const std::size_t calculationsAfter = scene.tessellationCalculations();
    const std::size_t cacheHitsAfter = scene.tessellationCacheHits();

    // Проверка отсутствия повторной тесселяции при одном только движении
    CHECK(calculationsAfter == calculationsBefore);
    CHECK(cacheHitsAfter > cacheHitsBefore);
    CHECK(cacheHitsAfter == cacheHitsBefore + 3);

    // Headless-рендеринг кадра 2
    auto framebuffer2 = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(softwareRenderer.render(*snapshot2, framebuffer2).has_value());

    // Проверка пикселей кадра 2:
    // Старое место динамического объекта (22, 32) снова занято статическим синим фоном
    CHECK(pixel(framebuffer2, 22, 32) == BLUE);
    // Новое место динамического объекта (42, 32) теперь красное
    CHECK(pixel(framebuffer2, 42, 32) == RED);
    // HUD остался на месте поверх мира
    CHECK(pixel(framebuffer2, 15, 12) == YELLOW);
    // Статический фон в неизменной области остался на месте
    CHECK(pixel(framebuffer2, 32, 50) == BLUE);
    // Фон очистки остался черным
    CHECK(pixel(framebuffer2, 2, 2) == BLACK);
}

void scenario2DCommandMovement() {
    core::Scene scene;

    auto staticBackground = scene.createObject(objects::Rectangle2::Params{.size = {16.0, 16.0}});
    CHECK(staticBackground.has_value());
    const auto staticId = *staticBackground;
    CHECK(scene.setMobility(staticId, core::Mobility::Static).has_value());
    CHECK(scene.setRenderProperties(staticId, core::RenderSpace::World, 0, 0).has_value());
    CHECK(scene.setMaterial(staticId, makeMaterial(0.0, 0.0, 1.0)).has_value());

    auto dynamicDisk = scene.createObject(objects::Disk2::Params{.radius = 2.0});
    CHECK(dynamicDisk.has_value());
    const auto dynamicId = *dynamicDisk;
    CHECK(scene.setMobility(dynamicId, core::Mobility::Dynamic).has_value());
    CHECK(scene.setLocalTransform(dynamicId, math::translation(math::Vector2{{-3.0, 0.0}})).has_value());
    CHECK(scene.setRenderProperties(dynamicId, core::RenderSpace::World, 1, 0).has_value());
    CHECK(scene.setMaterial(dynamicId, makeMaterial(1.0, 0.0, 0.0)).has_value());

    auto hudElement = scene.createObject(objects::Rectangle2::Params{.size = {20.0, 10.0}});
    CHECK(hudElement.has_value());
    const auto hudId = *hudElement;
    CHECK(scene.setLocalTransform(hudId, math::translation(math::Vector2{{15.0, 12.0}})).has_value());
    CHECK(scene.setRenderProperties(hudId, core::RenderSpace::Hud, 0, 0).has_value());
    CHECK(scene.setMaterial(hudId, makeMaterial(1.0, 1.0, 0.0)).has_value());

    const auto config = make2DPassesConfig();

    auto snapshot1 = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshot1.has_value());
    snapshot1->clearColor = makeColor(0.0, 0.0, 0.0);

    renderer::SoftwareRenderer rendererInstance;
    auto framebuffer1 = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(rendererInstance.render(*snapshot1, framebuffer1).has_value());
    CHECK(pixel(framebuffer1, 22, 32) == RED);
    CHECK(pixel(framebuffer1, 42, 32) == BLUE);

    const std::size_t calculationsBefore = scene.tessellationCalculations();
    const std::size_t cacheHitsBefore = scene.tessellationCacheHits();

    // Перемещение динамического объекта через команду SetLocalTransformCommand
    const std::vector<core::CommandRequest> requests{
        {1, core::SetLocalTransformCommand{dynamicId, math::translation(math::Vector2{{3.0, 0.0}})}}};
    const auto results = scene.apply(requests);
    CHECK(results.size() == 1);
    CHECK(results[0].status.has_value());

    auto snapshot2 = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshot2.has_value());
    snapshot2->clearColor = makeColor(0.0, 0.0, 0.0);

    // Проверка сохранения кеша тесселяции при перемещении через команду
    CHECK(scene.tessellationCalculations() == calculationsBefore);
    CHECK(scene.tessellationCacheHits() > cacheHitsBefore);

    auto framebuffer2 = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(rendererInstance.render(*snapshot2, framebuffer2).has_value());
    CHECK(pixel(framebuffer2, 22, 32) == BLUE);
    CHECK(pixel(framebuffer2, 42, 32) == RED);
    CHECK(pixel(framebuffer2, 15, 12) == YELLOW);
}

int main() {
    try {
        scenario2DMovementAndPassOrder();
        scenario2DCommandMovement();
        std::cout << "scenario_2d_tests: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
