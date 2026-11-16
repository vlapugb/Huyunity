#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"
#include "huynity/math/transform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace window = huynity::adapters::window;

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

std::shared_ptr<const render::Material> makeMaterial(const render::Color& color) {
    return std::make_shared<const render::Material>(color);
}

core::ObjectId addTransformedBox(huynity::Engine& engine, const math::Vector3& size, const math::Transform3& transform,
                                 const render::Color& color) {
    const auto id = engine.create<objects::ObjectType::Box3>({.size = size});
    (void)engine.scene().setLocalTransform(id, transform);
    (void)engine.scene().setMaterial(id, makeMaterial(color));
    return id;
}

core::ObjectId addBox(huynity::Engine& engine, const math::Vector3& size, const math::Vector3& position,
                      const render::Color& color) {
    return addTransformedBox(engine, size, math::translation(position), color);
}

core::ObjectId addSphere(huynity::Engine& engine, double radius, const math::Transform3& transform,
                         const render::Color& color) {
    const auto id = engine.create<objects::ObjectType::Sphere3>({.radius = radius});
    (void)engine.scene().setLocalTransform(id, transform);
    (void)engine.scene().setMaterial(id, makeMaterial(color));
    return id;
}

void addPine(huynity::Engine& engine, const math::Vector3& position, double scale) {
    addBox(engine, {{0.22 * scale, 1.4 * scale, 0.22 * scale}}, position + math::Vector3{{0, 0.7 * scale, 0}},
           {0.30, 0.19, 0.11, 1});
    for (int tier = 0; tier < 3; ++tier) {
        std::vector<math::Point3> positions;
        std::vector<objects::TriangleIndices> triangles;
        const double radius = (1.15 - tier * 0.26) * scale;
        const double bottom = (0.8 + tier * 0.7) * scale;
        positions.push_back({{0, bottom + 1.6 * scale, 0}});
        positions.push_back({{0, bottom, 0}});
        for (std::uint32_t side = 0; side < 10; ++side) {
            const double angle = side * std::numbers::pi / 5;
            positions.push_back({{radius * std::cos(angle), bottom, radius * std::sin(angle)}});
        }
        for (std::uint32_t side = 0; side < 10; ++side) {
            const auto next = (side + 1) % 10 + 2;
            triangles.push_back({{0, next, side + 2}});
            triangles.push_back({{1, side + 2, next}});
        }
        const auto crown = engine.create<objects::ObjectType::TriangleMesh3>(
            {.positions = std::move(positions), .triangles = std::move(triangles)});
        (void)engine.scene().setLocalTransform(crown, math::translation(position));
        (void)engine.scene().setMaterial(crown, makeMaterial({0.13 + tier * 0.02, 0.29 + tier * 0.04, 0.22, 1}));
    }
}

void addRoof(huynity::Engine& engine, math::Vector3 center, double width, double depth, double height) {
    const auto roof = engine.create<objects::ObjectType::TriangleMesh3>({
        .positions = {{{-width / 2, 0, -depth / 2}},
                      {{width / 2, 0, -depth / 2}},
                      {{0, height, -depth / 2}},
                      {{-width / 2, 0, depth / 2}},
                      {{width / 2, 0, depth / 2}},
                      {{0, height, depth / 2}}},
        .triangles = {{{0, 2, 1}}, {{3, 4, 5}}, {{0, 3, 5}}, {{0, 5, 2}}, {{1, 2, 5}}, {{1, 5, 4}}},
    });
    (void)engine.scene().setLocalTransform(roof, math::translation(center));
    (void)engine.scene().setMaterial(roof, makeMaterial({0.40, 0.18, 0.13, 1}));
}

render::OrthographicCamera3 makeCamera(double yaw, double pitch, double width) {
    const auto target = math::translation(math::Vector3{{0.0, -1.0, 34.0}});
    const auto orbit =
        math::compose(math::rotation(math::RotationAxis::X, pitch), math::rotation(math::RotationAxis::Y, yaw));
    return {
        .worldToView = math::compose(target, orbit),
        .width = width,
        .height = width * 9.0 / 16.0,
        .nearPlane = 0.1,
        .farPlane = 90.0,
    };
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

    std::cout << "Starting Huynity: Windmill Village (" << (continuous ? "continuous interactive mode" : "10 CI steps")
              << ")...\n";
    if (continuous) {
        std::cout << "Controls: A/D or Left/Right rotate, W/S or Up/Down zoom, Space speeds up the mill, Esc exits.\n";
    }

    huynity::Engine engine({.windowSize = {1280, 720},
                            .windowTitle = "Huynity: Windmill Village",
                            .forceHeadless = !continuous && screenshotPath.empty()});
    engine.setClearColor({0.16, 0.22, 0.23, 1.0});
    engine.setHudEnabled(false);
    engine.setLighting({
        .ambient = {.color = {0.78, 0.86, 1.0, 1.0}, .intensity = 0.42},
        .directional =
            {
                {
                    .direction = math::Vector3{{-0.7, -1.0, 0.65}},
                    .color = {1.0, 0.91, 0.75, 1.0},
                    .intensity = 0.82,
                },
                {
                    .direction = math::Vector3{{0.8, -0.3, -0.5}},
                    .color = {0.55, 0.72, 1.0, 1.0},
                    .intensity = 0.18,
                },
            },
        .shadows = {.enabled = true,
                    .resolution = 2048,
                    .extent = 16,
                    .center = {{0, 1, 0}},
                    .depthRange = 45,
                    .bias = 0.08,
                    .minVisibility = 0.25},
    });

    addBox(engine, {18.4, 0.4, 14.4}, {0.0, -0.65, 0.0}, {0.28, 0.19, 0.11, 1.0});
    addBox(engine, {18.0, 0.5, 14.0}, {0.0, -0.25, 0.0}, {0.38, 0.55, 0.25, 1.0});

    const auto shoreTransform = math::compose(math::translation(math::Vector3{{4.1, 0.0, 1.8}}),
                                              math::scaling(math::Vector3{{1.4, 0.025, 1.0}}));
    addSphere(engine, 2.25, shoreTransform, {0.68, 0.59, 0.36, 1.0});
    const auto waterTransform = math::compose(math::translation(math::Vector3{{4.1, 0.045, 1.8}}),
                                              math::scaling(math::Vector3{{1.4, 0.02, 1.0}}));
    addSphere(engine, 2.0, waterTransform, {0.16, 0.48, 0.65, 1.0});

    addBox(engine, {3.4, 2.4, 2.8}, {-1.8, 1.2, 0.2}, {0.88, 0.76, 0.55, 1.0});
    addRoof(engine, {{-1.8, 2.4, 0.2}}, 4.0, 3.3, 1.35);
    for (const double x : {-3.4, -0.2}) {
        addBox(engine, {0.12, 2.4, 0.12}, {x, 1.2, -1.25}, {0.26, 0.19, 0.12, 1});
    }
    addBox(engine, {3.4, 0.12, 0.12}, {-1.8, 2.22, -1.25}, {0.26, 0.19, 0.12, 1});
    // Дома вдоль дальней улицы, каждый с цельной двускатной крышей.
    for (const double x : {1.6, 5.1}) {
        addBox(engine, {2.4, 1.7, 2.2}, {x, 0.85, 4.0}, {0.82, 0.69, 0.48, 1});
        addRoof(engine, {{x, 1.7, 4.0}}, 2.9, 2.65, 1.05);
        addBox(engine, {0.55, 1.1, 0.08}, {x, 0.55, 2.86}, {0.28, 0.22, 0.15, 1});
        for (const double side : {-0.75, 0.75}) {
            addBox(engine, {0.4, 0.5, 0.08}, {x + side, 1.1, 2.86}, {0.22, 0.36, 0.36, 1});
        }
    }
    // Дощатый причал опирается на берег, а не висит над прудом.
    for (int plank = 0; plank < 9; ++plank) {
        addBox(engine, {1.1, 0.12, 0.22}, {2.5, 0.15, -0.2 + plank * 0.25}, {0.48, 0.34, 0.21, 1});
    }
    addBox(engine, {0.78, 1.45, 0.12}, {-1.8, 0.73, -1.25}, {0.29, 0.16, 0.09, 1.0});
    addBox(engine, {0.68, 0.72, 0.10}, {-2.9, 1.55, -1.26}, {0.25, 0.55, 0.67, 1.0});
    addBox(engine, {0.68, 0.72, 0.10}, {-0.7, 1.55, -1.26}, {0.25, 0.55, 0.67, 1.0});
    addBox(engine, {0.48, 0.9, 0.48}, {-2.65, 3.0, 0.75}, {0.62, 0.48, 0.34, 1.0});

    for (const double z : {-2.2, -3.8, -5.4}) {
        addBox(engine, {1.35, 0.08, 1.25}, {-1.8, 0.04, z}, {0.67, 0.57, 0.38, 1.0});
    }

    addBox(engine, {1.55, 1.2, 1.55}, {-5.1, 0.6, 1.5}, {0.79, 0.68, 0.49, 1.0});
    addBox(engine, {1.3, 1.2, 1.3}, {-5.1, 1.8, 1.5}, {0.82, 0.71, 0.52, 1.0});
    addBox(engine, {1.05, 1.1, 1.05}, {-5.1, 2.95, 1.5}, {0.85, 0.74, 0.55, 1.0});
    addBox(engine, {1.35, 0.32, 1.35}, {-5.1, 3.65, 1.5}, {0.42, 0.20, 0.11, 1.0});
    addBox(engine, {0.42, 0.85, 0.08}, {-5.1, 0.43, 0.70}, {0.25, 0.14, 0.08, 1.0});

    const auto rotorPosition = math::Vector3{{-5.1, 2.95, 0.88}};
    const auto rotor = addSphere(engine, 0.24, math::translation(rotorPosition), {0.60, 0.40, 0.19, 1.0});
    (void)engine.scene().setMobility(rotor, core::Mobility::Dynamic);
    for (std::uint32_t blade = 0; blade < 4; ++blade) {
        const auto bladeId = addBox(engine, {0.20, 1.55, 0.12}, {}, {0.91, 0.83, 0.63, 1.0});
        (void)engine.scene().setMobility(bladeId, core::Mobility::Dynamic);
        const double bladeAngle = std::numbers::pi * 0.5 * static_cast<double>(blade) + std::numbers::pi / 4.0;
        const auto bladeTransform = math::compose(math::rotation(math::RotationAxis::Z, bladeAngle),
                                                  math::translation(math::Vector3{{0.0, 0.93, 0.0}}));
        (void)engine.scene().setLocalTransform(bladeId, bladeTransform);
        (void)engine.scene().setParent(bladeId, rotor);
    }

    addPine(engine, {-7.2, 0.0, 4.6}, 1.05);
    addPine(engine, {-7.3, 0.0, -3.8}, 0.85);
    addPine(engine, {0.3, 0.0, 5.0}, 1.10);
    addPine(engine, {6.9, 0.0, 4.7}, 0.95);
    addPine(engine, {7.1, 0.0, -3.9}, 0.90);

    for (const double x : {-1.0, 1.0, 3.0, 5.0, 7.0}) {
        addBox(engine, {0.18, 1.0, 0.18}, {x, 0.5, 5.9}, {0.47, 0.31, 0.16, 1.0});
    }
    addBox(engine, {8.2, 0.14, 0.14}, {3.0, 0.38, 5.9}, {0.55, 0.37, 0.20, 1.0});
    addBox(engine, {8.2, 0.14, 0.14}, {3.0, 0.78, 5.9}, {0.55, 0.37, 0.20, 1.0});

    for (const auto& rock : std::array{math::Vector3{{1.8, 0.18, 0.2}}, math::Vector3{{2.1, 0.14, 3.4}},
                                       math::Vector3{{6.2, 0.16, 3.6}}}) {
        const auto transform = math::compose(math::translation(rock), math::scaling(math::Vector3{{1.3, 0.65, 0.9}}));
        addSphere(engine, 0.32, transform, {0.43, 0.45, 0.40, 1.0});
    }

    // Огород и цветники связывают дома с землёй и оживляют передний план.
    addBox(engine, {2.8, 0.08, 2.0}, {3.0, 0.04, -4.3}, {0.28, 0.20, 0.13, 1});
    for (int row = 0; row < 4; ++row) {
        for (int plant = 0; plant < 7; ++plant) {
            addBox(engine, {0.22, 0.2, 0.22}, {1.85 + plant * 0.38, 0.18, -4.9 + row * 0.4},
                   {0.26, 0.43 + row * 0.03, 0.19, 1});
        }
    }
    for (const double x : {-2.9, -0.7}) {
        addBox(engine, {0.9, 0.15, 0.3}, {x, 1.15, -1.45}, {0.38, 0.26, 0.16, 1});
        for (int flower = 0; flower < 5; ++flower) {
            addBox(engine, {0.12, 0.18, 0.12}, {x - 0.3 + flower * 0.15, 1.28, -1.45}, {0.76, 0.57, 0.26, 1});
        }
    }

    double rotorAngle = 0.0;
    double cameraYaw = -0.52;
    double cameraPitch = -0.58;
    double cameraWidth = 25.0;
    int stepCount = 0;
    engine.setCamera(makeCamera(cameraYaw, cameraPitch, cameraWidth));

    engine.onUpdate([&](double dt) {
        if (!continuous && ++stepCount >= 10) {
            engine.stop();
            return;
        }

        const auto& input = engine.input();
        if (input.isKeyPressed(window::KeyCode::Escape)) {
            engine.stop();
            return;
        }

        // Вращение камеры мышью при зажатой левой кнопке
        if (input.isMouseButtonPressed(window::MouseButton::Left)) {
            constexpr double mouseSensitivity = 0.006;
            cameraYaw += input.mouseDeltaX() * mouseSensitivity;
            cameraPitch = std::clamp(cameraPitch + input.mouseDeltaY() * mouseSensitivity, -1.2, -0.2);
        }

        // Зум колесом мыши
        if (std::abs(input.scrollDelta()) > 0.01) {
            cameraWidth = std::clamp(cameraWidth - input.scrollDelta() * 1.5, 12.0, 32.0);
        }

        if (input.isKeyPressed(window::KeyCode::A) || input.isKeyPressed(window::KeyCode::Left)) {
            cameraYaw -= dt * 0.7;
        }
        if (input.isKeyPressed(window::KeyCode::D) || input.isKeyPressed(window::KeyCode::Right)) {
            cameraYaw += dt * 0.7;
        }
        if (input.isKeyPressed(window::KeyCode::W) || input.isKeyPressed(window::KeyCode::Up)) {
            cameraWidth = std::max(12.0, cameraWidth - dt * 7.0);
        }
        if (input.isKeyPressed(window::KeyCode::S) || input.isKeyPressed(window::KeyCode::Down)) {
            cameraWidth = std::min(32.0, cameraWidth + dt * 7.0);
        }
        engine.setCamera(makeCamera(cameraYaw, cameraPitch, cameraWidth));

        const double speed = input.isKeyPressed(window::KeyCode::Space) ? 2.4 : 0.65;
        rotorAngle += dt * speed;
        const auto rotorTransform =
            math::compose(math::translation(rotorPosition), math::rotation(math::RotationAxis::Z, rotorAngle));
        (void)engine.scene().setLocalTransform(rotor, rotorTransform);
    });

    engine.step(0.0);
    if (!screenshotPath.empty()) {
        auto frame = engine.captureFrame();
        if (!frame) {
            throw std::runtime_error("Cannot capture village frame");
        }
        savePpm(*frame, screenshotPath);
    }
    if (continuous) {
        engine.run();
    } else {
        for (int step = 0; step < 5; ++step) {
            engine.step(1.0 / 60.0);
        }
    }

    std::cout << "Finished Windmill Village successfully.\n";
    return 0;
}
