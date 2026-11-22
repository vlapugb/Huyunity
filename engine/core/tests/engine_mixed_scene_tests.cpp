#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"
#include "huynity/math/transform.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

struct Rgba {
    int r{};
    int g{};
    int b{};
    int a{};

    bool operator==(const Rgba&) const = default;
};

Rgba readPixel(const huynity::image::ImageBuffer& buffer, std::size_t x, std::size_t y) {
    const auto bytes = buffer.bytes();
    const std::size_t offset = y * buffer.stride() + x * 4;
    return {
        std::to_integer<int>(bytes[offset]),
        std::to_integer<int>(bytes[offset + 1]),
        std::to_integer<int>(bytes[offset + 2]),
        std::to_integer<int>(bytes[offset + 3]),
    };
}

void testEngineMixedSceneHelpers() {
    huynity::Engine engine{huynity::EngineConfig{.forceHeadless = true}};

    CHECK(engine.isHudEnabled());
    CHECK(!engine.isWorld2DFirst());
    CHECK(!engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());

    const huynity::render_contracts::OrthographicCamera3 cam3{
        .worldToView = huynity::math::Transform3{},
        .width = 20.0,
        .height = 20.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    const huynity::render_contracts::Camera2 cam2{
        .worldToView = huynity::math::Transform2{},
        .width = 20.0,
        .height = 20.0,
    };

    engine.setCamera(cam3);
    CHECK(engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());

    // одновременная настройка 2D и 3D камер
    engine.setCamera(cam2);
    CHECK(engine.camera3().has_value());
    CHECK(engine.camera2().has_value());

    // сброс отдельной камеры
    engine.clearCamera3();
    CHECK(!engine.camera3().has_value());
    CHECK(engine.camera2().has_value());

    engine.clearCamera2();
    CHECK(!engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());

    // управление цветом очистки
    engine.setClearColor(huynity::render_contracts::Color{0.1, 0.2, 0.3, 1.0});
    CHECK(engine.clearColor().red == 0.1);
    CHECK(engine.clearColor().green == 0.2);
    CHECK(engine.clearColor().blue == 0.3);

    // управление HUD и порядком проходов
    engine.setHudEnabled(false);
    CHECK(!engine.isHudEnabled());
    engine.setHudEnabled(true);
    CHECK(engine.isHudEnabled());

    engine.setWorld2DFirst(true);
    CHECK(engine.isWorld2DFirst());
    engine.setWorld2DFirst(false);
    CHECK(!engine.isWorld2DFirst());
}

void testMixedSceneRenderingAndHudToggle() {
    huynity::Engine engine{huynity::EngineConfig{
        .windowSize = {100, 100},
        .forceHeadless = true,
    }};

    engine.setClearColor(huynity::render_contracts::Color{0.0, 0.0, 0.0, 1.0});

    // 1. Настройка освещения
    huynity::render_contracts::Lighting3 lighting{
        .ambient = {.color = huynity::render_contracts::Color{1.0, 1.0, 1.0, 1.0}, .intensity = 0.5},
        .directional =
            {
                {
                    .direction = huynity::math::Vector3{{1.0, 1.0, 1.0}},
                    .color = huynity::render_contracts::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.5,
                },
            },
    };
    engine.setLighting(std::move(lighting));

    // 2. Настройка обеих камер одновременно
    const huynity::render_contracts::OrthographicCamera3 cam3{
        .worldToView = huynity::math::Transform3{},
        .width = 20.0,
        .height = 20.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    const huynity::render_contracts::Camera2 cam2{
        .worldToView = huynity::math::Transform2{},
        .width = 20.0,
        .height = 20.0,
    };
    engine.setCamera(cam3);
    engine.setCamera(cam2);

    // 3. Создание 3D Box3 в мировом пространстве (красный материал)
    const auto boxId = engine.create(huynity::objectlib::Box3::Params{.size = {4.0, 4.0, 4.0}});
    (void)engine.scene().setLocalTransform(boxId,
                                           huynity::math::translation(huynity::math::Vector3{{-4.0, 0.0, 10.0}}));
    (void)engine.scene().setMaterial(boxId, std::make_shared<huynity::render_contracts::Material>(
                                                huynity::render_contracts::Color{1.0, 0.0, 0.0, 1.0}));

    // 4. Создание 2D Disk2 в мировом пространстве (зелёный материал)
    const auto diskId = engine.create(huynity::objectlib::Disk2::Params{.radius = 2.0});
    (void)engine.scene().setLocalTransform(diskId, huynity::math::translation(huynity::math::Vector2{{4.0, 0.0}}));
    (void)engine.scene().setMaterial(diskId, std::make_shared<huynity::render_contracts::Material>(
                                                 huynity::render_contracts::Color{0.0, 1.0, 0.0, 1.0}));

    // 5. Создание HUD Rectangle2 (жёлтый материал)
    const auto hudId = engine.create(huynity::objectlib::Rectangle2::Params{.size = {20.0, 20.0}});
    (void)engine.scene().setLocalTransform(hudId, huynity::math::translation(huynity::math::Vector2{{40.0, 50.0}}));
    (void)engine.scene().setRenderProperties(hudId, huynity::core::RenderSpace::Hud, 0, 0);
    (void)engine.scene().setMaterial(hudId, std::make_shared<huynity::render_contracts::Material>(
                                                huynity::render_contracts::Color{1.0, 1.0, 0.0, 1.0}));

    // Шаг кадра
    engine.step(0.0);

    auto* headless = dynamic_cast<huynity::adapters::window::HeadlessWindow*>(&engine.window());
    CHECK(headless != nullptr);
    const auto* frame = headless->lastPresentedFrame();
    CHECK(frame != nullptr);

    // Контрольные пиксели слоёв:
    // 3D-объект (красный) на (24, 50)
    const auto pixel3D = readPixel(*frame, 24, 50);
    CHECK(pixel3D.r > 50);
    CHECK(pixel3D.g == 0);
    CHECK(pixel3D.b == 0);

    // 2D-объект мира (зелёный) на (70, 50)
    const auto pixel2D = readPixel(*frame, 70, 50);
    CHECK(pixel2D.r == 0);
    CHECK(pixel2D.g > 200);
    CHECK(pixel2D.b == 0);

    // HUD (жёлтый) поверх фона на (45, 50)
    const auto pixelHud = readPixel(*frame, 45, 50);
    CHECK(pixelHud.r > 200);
    CHECK(pixelHud.g > 200);
    CHECK(pixelHud.b == 0);

    // HUD (жёлтый) поверх 3D-объекта на (36, 50)
    const auto pixelHudOver3D = readPixel(*frame, 36, 50);
    CHECK(pixelHudOver3D.r > 200);
    CHECK(pixelHudOver3D.g > 200);
    CHECK(pixelHudOver3D.b == 0);

    // Фон (чёрный) на (5, 5)
    const auto pixelBg = readPixel(*frame, 5, 5);
    CHECK(pixelBg.r == 0);
    CHECK(pixelBg.g == 0);
    CHECK(pixelBg.b == 0);

    // --- Тест отключения HUD ---
    engine.setHudEnabled(false);
    CHECK(!engine.isHudEnabled());

    engine.step(0.0);
    frame = headless->lastPresentedFrame();
    CHECK(frame != nullptr);

    // 3D и 2D объекты по-прежнему на месте
    const auto pixel3DAfter = readPixel(*frame, 24, 50);
    CHECK(pixel3DAfter.r > 50);
    CHECK(pixel3DAfter.g == 0);

    const auto pixel2DAfter = readPixel(*frame, 70, 50);
    CHECK(pixel2DAfter.r == 0);
    CHECK(pixel2DAfter.g > 200);

    // Пиксель (45, 50), где был HUD поверх фона, теперь чёрный фон
    const auto pixelHudGone = readPixel(*frame, 45, 50);
    CHECK(pixelHudGone.r == 0);
    CHECK(pixelHudGone.g == 0);
    CHECK(pixelHudGone.b == 0);

    // Пиксель (36, 50), где HUD перекрывал 3D-объект, теперь стал красным (виден 3D-объект под ним)
    const auto pixelUnderlying3D = readPixel(*frame, 36, 50);
    CHECK(pixelUnderlying3D.r > 50);
    CHECK(pixelUnderlying3D.g == 0);
    CHECK(pixelUnderlying3D.b == 0);
}

void testPassOrder() {
    huynity::Engine engine{huynity::EngineConfig{
        .windowSize = {100, 100},
        .forceHeadless = true,
    }};

    engine.setClearColor(huynity::render_contracts::Color{0.0, 0.0, 0.0, 1.0});

    huynity::render_contracts::Lighting3 lighting{
        .ambient = {.color = huynity::render_contracts::Color{1.0, 1.0, 1.0, 1.0}, .intensity = 0.5},
        .directional =
            {
                {
                    .direction = huynity::math::Vector3{{1.0, 1.0, 1.0}},
                    .color = huynity::render_contracts::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.5,
                },
            },
    };
    engine.setLighting(std::move(lighting));

    const huynity::render_contracts::OrthographicCamera3 cam3{
        .worldToView = huynity::math::Transform3{},
        .width = 20.0,
        .height = 20.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    const huynity::render_contracts::Camera2 cam2{
        .worldToView = huynity::math::Transform2{},
        .width = 20.0,
        .height = 20.0,
    };
    engine.setCamera(cam3);
    engine.setCamera(cam2);

    // 3D Box3 в центре мира (красный)
    const auto boxId = engine.create(huynity::objectlib::Box3::Params{.size = {4.0, 4.0, 4.0}});
    (void)engine.scene().setLocalTransform(boxId, huynity::math::translation(huynity::math::Vector3{{0.0, 0.0, 10.0}}));
    (void)engine.scene().setMaterial(boxId, std::make_shared<huynity::render_contracts::Material>(
                                                huynity::render_contracts::Color{1.0, 0.0, 0.0, 1.0}));

    // 2D Disk2 в центре мира (зелёный), перекрывающий 3D Box
    const auto diskId = engine.create(huynity::objectlib::Disk2::Params{.radius = 2.0});
    (void)engine.scene().setLocalTransform(diskId, huynity::math::translation(huynity::math::Vector2{{0.0, 0.0}}));
    (void)engine.scene().setMaterial(diskId, std::make_shared<huynity::render_contracts::Material>(
                                                 huynity::render_contracts::Color{0.0, 1.0, 0.0, 1.0}));

    auto* headless = dynamic_cast<huynity::adapters::window::HeadlessWindow*>(&engine.window());
    CHECK(headless != nullptr);

    // 1. По умолчанию world2DFirst == false -> 3D рисуется первым, затем 2D рисуется поверх него.
    CHECK(!engine.isWorld2DFirst());
    engine.step(0.0);
    const auto* frameDefault = headless->lastPresentedFrame();
    CHECK(frameDefault != nullptr);

    // В центре (50, 50) виден 2D-диск (зелёный), так как 2D-проход идёт после 3D
    const auto centerDefault = readPixel(*frameDefault, 50, 50);
    CHECK(centerDefault.g > 200);
    CHECK(centerDefault.r == 0);

    // 2. Переключаем на world2DFirst == true -> 2D рисуется первым, затем 3D рисуется поверх него.
    engine.setWorld2DFirst(true);
    CHECK(engine.isWorld2DFirst());
    engine.step(0.0);
    const auto* frame2DFirst = headless->lastPresentedFrame();
    CHECK(frame2DFirst != nullptr);

    // В центре (50, 50) теперь виден 3D-куб (красный), так как 3D-проход идёт после 2D
    const auto center2DFirst = readPixel(*frame2DFirst, 50, 50);
    CHECK(center2DFirst.r > 50);
    CHECK(center2DFirst.g == 0);
}

} // namespace

int main() {
    try {
        testEngineMixedSceneHelpers();
        testMixedSceneRenderingAndHudToggle();
        testPassOrder();
        std::cout << "All engine mixed scene tests passed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Engine mixed scene test failure: " << e.what() << "\n";
        return 1;
    }
}
