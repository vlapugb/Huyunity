#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/objectlib/shapes/sphere3.h"

#include <cmath>
#include <cstddef>
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

void testShow3DObject() {
    huynity::Engine engine{huynity::EngineConfig{
        .windowSize = {800, 600},
        .forceHeadless = true,
    }};

    const huynity::objectlib::Box3::Params boxParams{.size = {4.0, 4.0, 4.0}};
    const auto boxId = engine.create(boxParams);

    CHECK(!engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());

    engine.show(boxId);

    // 1. Проверяем наличие и параметры 3D-камеры
    CHECK(engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());
    const auto* ortho = std::get_if<huynity::render_contracts::OrthographicCamera3>(&*engine.camera3());
    CHECK(ortho != nullptr);
    CHECK(ortho->width >= 4.0 * 1.2);
    CHECK(ortho->height >= 4.0 * 1.2);
    CHECK(std::abs(ortho->width / ortho->height - (800.0 / 600.0)) < 1e-4);
    CHECK(ortho->nearPlane > 0.0);
    CHECK(ortho->farPlane > ortho->nearPlane);

    // 2. Проверяем настройки освещения
    CHECK(engine.lighting().ambient.intensity > 0.0);
    CHECK(!engine.lighting().directional.empty());
    CHECK(engine.lighting().directional[0].intensity > 0.0);

    // 3. Проверяем кадровый буфер
    auto* headless = dynamic_cast<huynity::adapters::window::HeadlessWindow*>(&engine.window());
    CHECK(headless != nullptr);
    const auto* frame = headless->lastPresentedFrame();
    CHECK(frame != nullptr);
    CHECK(frame->width() == 800);
    CHECK(frame->height() == 600);

    const auto bg = readPixel(*frame, 0, 0);
    const auto center = readPixel(*frame, 400, 300);
    CHECK(center != bg);

    std::size_t objectPixels = 0;
    for (std::size_t y = 0; y < 600; ++y) {
        for (std::size_t x = 0; x < 800; ++x) {
            if (readPixel(*frame, x, y) != bg) {
                ++objectPixels;
            }
        }
    }
    CHECK(objectPixels > 1000);

    // Объект центрирован
    CHECK(readPixel(*frame, 400 - 40, 300) != bg);
    CHECK(readPixel(*frame, 400 + 40, 300) != bg);
    CHECK(readPixel(*frame, 400, 300 - 40) != bg);
    CHECK(readPixel(*frame, 400, 300 + 40) != bg);

    // Края кадра не задеты (объект не отсечён границами экрана)
    CHECK(readPixel(*frame, 10, 300) == bg);
    CHECK(readPixel(*frame, 790, 300) == bg);
    CHECK(readPixel(*frame, 400, 10) == bg);
    CHECK(readPixel(*frame, 400, 590) == bg);
}

void testShow2DObject() {
    huynity::Engine engine{huynity::EngineConfig{
        .windowSize = {800, 600},
        .forceHeadless = true,
    }};

    const huynity::objectlib::Rectangle2::Params rectParams{.size = {20.0, 10.0}};
    const auto rectId = engine.create(rectParams);

    CHECK(!engine.camera3().has_value());
    CHECK(!engine.camera2().has_value());

    engine.show(rectId);

    // 1. Проверяем наличие и параметры 2D-камеры
    CHECK(!engine.camera3().has_value());
    CHECK(engine.camera2().has_value());
    const auto& cam2 = *engine.camera2();
    CHECK(cam2.width >= 20.0 * 1.2);
    CHECK(cam2.height >= 10.0 * 1.2);
    CHECK(std::abs(cam2.width / cam2.height - (800.0 / 600.0)) < 1e-4);

    // 2. Проверяем кадровый буфер
    auto* headless = dynamic_cast<huynity::adapters::window::HeadlessWindow*>(&engine.window());
    CHECK(headless != nullptr);
    const auto* frame = headless->lastPresentedFrame();
    CHECK(frame != nullptr);
    CHECK(frame->width() == 800);
    CHECK(frame->height() == 600);

    const auto bg = readPixel(*frame, 0, 0);
    const auto center = readPixel(*frame, 400, 300);
    CHECK(center != bg);

    std::size_t objectPixels = 0;
    for (std::size_t y = 0; y < 600; ++y) {
        for (std::size_t x = 0; x < 800; ++x) {
            if (readPixel(*frame, x, y) != bg) {
                ++objectPixels;
            }
        }
    }
    CHECK(objectPixels > 1000);

    // Объект центрирован
    CHECK(readPixel(*frame, 400 - 50, 300) != bg);
    CHECK(readPixel(*frame, 400 + 50, 300) != bg);
    CHECK(readPixel(*frame, 400, 300 - 30) != bg);
    CHECK(readPixel(*frame, 400, 300 + 30) != bg);

    // Края кадра свободны
    CHECK(readPixel(*frame, 10, 300) == bg);
    CHECK(readPixel(*frame, 790, 300) == bg);
    CHECK(readPixel(*frame, 400, 10) == bg);
    CHECK(readPixel(*frame, 400, 590) == bg);
}

void testPreviewFunction() {
    // 3D Sphere preview
    auto sphere = huynity::objectlib::Sphere3::create(2.5);
    CHECK(sphere.has_value());
    huynity::Engine::preview(*sphere, huynity::EngineConfig{.forceHeadless = true});

    // 3D Box preview
    auto box = huynity::objectlib::Box3::create({3.0, 3.0, 3.0});
    CHECK(box.has_value());
    huynity::Engine::preview(*box, huynity::EngineConfig{.forceHeadless = true});

    // 2D Disk preview
    auto disk = huynity::objectlib::Disk2::create(4.0);
    CHECK(disk.has_value());
    huynity::Engine::preview(*disk, huynity::EngineConfig{.forceHeadless = true});

    // 2D Rectangle preview
    auto rect = huynity::objectlib::Rectangle2::create({12.0, 8.0});
    CHECK(rect.has_value());
    huynity::Engine::preview(*rect, huynity::EngineConfig{.forceHeadless = true});
}

void testShowInvalidObjectId() {
    huynity::Engine engine{huynity::EngineConfig{.forceHeadless = true}};
    bool threw = false;
    try {
        engine.show(huynity::core::ObjectId{99999});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        testShow3DObject();
        testShow2DObject();
        testPreviewFunction();
        testShowInvalidObjectId();
        std::cout << "All engine preview tests passed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Engine preview test failure: " << e.what() << "\n";
        return 1;
    }
}
