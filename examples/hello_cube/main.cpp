#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"
#include "huynity/math/transform.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>

namespace {

void savePpm(const huynity::image::ImageBuffer& buffer, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    file << "P6\n" << buffer.width() << " " << buffer.height() << "\n255\n";
    for (std::size_t y = 0; y < buffer.height(); ++y) {
        const auto row = buffer.row(y);
        for (std::size_t x = 0; x < buffer.width(); ++x) {
            file.put(static_cast<char>(row[x * 4 + 0]));
            file.put(static_cast<char>(row[x * 4 + 1]));
            file.put(static_cast<char>(row[x * 4 + 2]));
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    bool continuous = false;
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--continuous" || arg == "-c") {
            continuous = true;
        } else if ((arg == "--screenshot" || arg == "-s") && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
    }

    std::cout << "Starting Huynity: Hello, Cube (" << (continuous ? "continuous mode" : "10 CI steps") << ")...\n";

    huynity::Engine engine({.windowTitle = "Huynity: Hello, Cube", .forceHeadless = !continuous});

    using huynity::core::Mobility;
    using huynity::math::RotationAxis;
    using huynity::math::Vector3;
    namespace objects = huynity::objectlib;
    namespace render = huynity::render_contracts;

    const auto cube = engine.create(objects::Box3::Params{.size = {3.0, 3.0, 3.0}}, Mobility::Dynamic);
    (void)engine.scene().setMaterial(cube, std::make_shared<const render::Material>(render::Material{
                                               .color = {0.95, 0.38, 0.12, 1.0},
                                           }));
    engine.setClearColor({0.035, 0.05, 0.085, 1.0});
    engine.setCamera(render::OrthographicCamera3{
        .worldToView = huynity::math::translation(Vector3{{0.0, 0.0, 20.0}}),
        .width = 7.4,
        .height = 5.55,
        .nearPlane = 0.1,
        .farPlane = 50.0,
    });
    engine.setLighting({
        .ambient = {.color = {0.75, 0.82, 1.0, 1.0}, .intensity = 0.32},
        .directional =
            {
                {
                    .direction = Vector3{{-0.6, -1.0, 0.8}},
                    .color = {1.0, 0.9, 0.76, 1.0},
                    .intensity = 0.85,
                },
                {
                    .direction = Vector3{{0.8, -0.2, -0.4}},
                    .color = {0.45, 0.65, 1.0, 1.0},
                    .intensity = 0.25,
                },
            },
    });

    double yaw = std::numbers::pi / 4.0;
    double pitch = -0.55;
    int stepCount = 0;

    engine.onUpdate([&](double dt) {
        if (!continuous && ++stepCount >= 10) {
            engine.stop();
            return;
        }

        if (engine.input().isKeyPressed(huynity::adapters::window::KeyCode::Escape)) {
            engine.stop();
            return;
        }

        // Интерактивное вращение мышью при зажатой левой кнопке
        if (engine.input().isMouseButtonPressed(huynity::adapters::window::MouseButton::Left)) {
            constexpr double sensitivity = 0.008;
            yaw += engine.input().mouseDeltaX() * sensitivity;
            pitch += engine.input().mouseDeltaY() * sensitivity;
            pitch = std::clamp(pitch, -1.35, 1.35);
        } else {
            // Плавное горизонтальное авто-вращение, если мышь не зажата
            const double speed = engine.input().isKeyPressed(huynity::adapters::window::KeyCode::Space) ? 1.8 : 0.45;
            yaw += dt * speed;
        }

        const auto pitchRot = huynity::math::rotation(RotationAxis::X, pitch);
        const auto yawRot = huynity::math::rotation(RotationAxis::Y, yaw);
        (void)engine.scene().setLocalTransform(cube, huynity::math::compose(pitchRot, yawRot));
    });

    engine.run();

    if (!screenshotPath.empty()) {
        if (const auto* headless = dynamic_cast<const huynity::adapters::window::HeadlessWindow*>(&engine.window())) {
            if (const auto* frame = headless->lastPresentedFrame()) {
                savePpm(*frame, screenshotPath);
                std::cout << "Saved screenshot to " << screenshotPath << "\n";
            }
        }
    }

    std::cout << "Finished Hello, Cube successfully.\n";
    return 0;
}
