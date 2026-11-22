#include "huynity/engine.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/objectlib/shapes/sphere3.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

void testDefaultConfig() {
    huynity::Engine engine;
    CHECK(engine.config().windowSize == huynity::adapters::window::WindowSize{800, 600});
    CHECK(engine.config().windowTitle == "Huynity Game");
    CHECK(engine.config().fixedTimeStep == 1.0 / 60.0);
    CHECK(!engine.config().forceHeadless);

    CHECK(engine.window().isOpen());
    CHECK(engine.window().size().width >= 800 && engine.window().size().height >= 600);
    CHECK(engine.scene().view().instances.empty());
    CHECK(!engine.input().isKeyPressed(huynity::adapters::window::KeyCode::Space));
}

void testCustomConfig() {
    huynity::EngineConfig customConfig{
        .windowSize = {1024, 768},
        .windowTitle = "Custom Huynity Window",
        .fixedTimeStep = 0.02,
        .forceHeadless = true,
    };
    huynity::Engine engine{customConfig};

    CHECK(engine.config().windowSize.width == 1024);
    CHECK(engine.config().windowSize.height == 768);
    CHECK(engine.config().windowTitle == "Custom Huynity Window");
    CHECK(engine.config().fixedTimeStep == 0.02);
    CHECK(engine.config().forceHeadless);

    CHECK(engine.window().size().width == 1024);
    CHECK(engine.window().size().height == 768);
}

void testCreate3DObjects() {
    huynity::Engine engine{huynity::EngineConfig{.forceHeadless = true}};

    // 1. Создание Box3 со статической подвижностью по умолчанию
    const huynity::objectlib::Box3::Params boxParams{.size = {2.0, 4.0, 6.0}};
    const auto boxId = engine.create(boxParams);
    const auto view1 = engine.scene().view();
    CHECK(view1.instances.size() == 1);
    CHECK(view1.instances[0].id == boxId);
    CHECK(view1.instances[0].mobility == huynity::core::Mobility::Static);

    // 2. Создание Sphere3 с явной динамической подвижностью
    const huynity::objectlib::Sphere3::Params sphereParams{.radius = 2.5};
    const auto sphereId = engine.create(sphereParams, huynity::core::Mobility::Dynamic);
    const auto view2 = engine.scene().view();
    CHECK(view2.instances.size() == 2);
    CHECK(view2.instances[1].id == sphereId);
    CHECK(view2.instances[1].mobility == huynity::core::Mobility::Dynamic);

    // 3. Проверка обработки ошибок при недопустимых геометрических параметрах
    bool threw = false;
    try {
        (void)engine.create(huynity::objectlib::Sphere3::Params{.radius = -1.0});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void testCreate2DObjects() {
    huynity::Engine engine{huynity::EngineConfig{.forceHeadless = true}};

    // 1. Создание 2D-прямоугольника через типизированный метод со статической подвижностью
    const huynity::objectlib::Rectangle2::Params rectParams{.size = {10.0, 20.0}};
    const auto rectId = engine.create(rectParams, huynity::core::Mobility::Static);
    const auto view1 = engine.scene().view();
    CHECK(view1.instances.size() == 1);
    CHECK(view1.instances[0].id == rectId);
    CHECK(view1.instances[0].mobility == huynity::core::Mobility::Static);

    // 2. Создание 2D-диска через ObjectSpec с динамической подвижностью
    const huynity::objectlib::Disk2::Params diskParams{.radius = 7.5};
    const huynity::objectlib::GeometrySpec spec{diskParams};
    const auto diskId = engine.create(spec, huynity::core::Mobility::Dynamic);
    const auto view2 = engine.scene().view();
    CHECK(view2.instances.size() == 2);
    CHECK(view2.instances[1].id == diskId);
    CHECK(view2.instances[1].mobility == huynity::core::Mobility::Dynamic);

    // 3. Проверка обработки ошибок при некорректной спецификации
    bool threw = false;
    try {
        const huynity::objectlib::GeometrySpec invalidSpec{huynity::objectlib::Disk2::Params{.radius = -5.0}};
        (void)engine.create(invalidSpec);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void testCameraAndLighting() {
    huynity::Engine engine{huynity::EngineConfig{.forceHeadless = true}};

    CHECK(!engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());
    CHECK(engine.lighting().directional.empty());

    // 1. Установка Camera3
    const huynity::render_contracts::PerspectiveCamera3 cam3{
        .worldToView = huynity::math::Transform3{},
        .verticalFieldOfView = 1.05,
        .nearPlane = 0.1,
        .farPlane = 500.0,
    };
    engine.setCamera(cam3);
    CHECK(engine.camera3().has_value());
    const auto* persp = std::get_if<huynity::render_contracts::PerspectiveCamera3>(&*engine.camera3());
    CHECK(persp != nullptr);
    CHECK(persp->verticalFieldOfView == 1.05);

    // 2. Установка Camera2
    const huynity::render_contracts::Camera2 cam2{
        .worldToView = huynity::math::Transform2{},
        .width = 1280.0,
        .height = 720.0,
    };
    engine.setCamera(cam2);
    CHECK(engine.camera2().has_value());
    CHECK(engine.camera2()->width == 1280.0);
    CHECK(engine.camera2()->height == 720.0);

    // 3. Установка Lighting3
    const huynity::render_contracts::Lighting3 lighting{
        .ambient = {.color = {0.2, 0.2, 0.2, 1.0}, .intensity = 0.5},
        .directional = {{.direction = {0.0, -1.0, 0.0}, .color = {1.0, 1.0, 0.9, 1.0}, .intensity = 1.0}},
    };
    engine.setLighting(lighting);
    CHECK(engine.lighting().ambient.intensity == 0.5);
    CHECK(engine.lighting().directional.size() == 1);
    CHECK(engine.lighting().directional[0].intensity == 1.0);
}

} // namespace

int main() {
    try {
        testDefaultConfig();
        testCustomConfig();
        testCreate3DObjects();
        testCreate2DObjects();
        testCameraAndLighting();
        std::cout << "engine: OK\n";
    } catch (const std::exception& error) {
        std::cerr << "Engine test failed: " << error.what() << '\n';
        return 1;
    }
}
