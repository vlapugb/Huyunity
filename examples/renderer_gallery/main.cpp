#include "huynity/engine.h"
#include "huynity/math/transform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace window = huynity::adapters::window;
using huynity::core::ObjectId;

ObjectId box(huynity::Engine& engine, math::Vector3 size, math::Vector3 position, render::Color color) {
    const auto id = engine.create<objects::ObjectType::Box3>({.size = size});
    (void)engine.scene().setLocalTransform(id, math::translation(position));
    (void)engine.scene().setMaterial(id, std::make_shared<const render::Material>(color));
    return id;
}

// Тела вращения задаются профилем: радиус и высота; открытый верх остаётся полым.
ObjectId vessel(huynity::Engine& engine, const std::vector<math::Point2>& profile, math::Vector3 position,
                render::Color color) {
    std::vector<math::Point3> positions;
    std::vector<objects::TriangleIndices> triangles;
    constexpr std::uint32_t sides = 64;
    for (const auto& point : profile) {
        for (std::uint32_t side = 0; side < sides; ++side) {
            const double angle = side * 2.0 * std::numbers::pi / sides;
            positions.push_back({{point[0] * std::cos(angle), point[1], point[0] * std::sin(angle)}});
        }
    }
    for (std::uint32_t row = 0; row + 1 < profile.size(); ++row) {
        for (std::uint32_t side = 0; side < sides; ++side) {
            const auto a = row * sides + side;
            const auto b = row * sides + (side + 1) % sides;
            triangles.push_back({{a, a + sides, b}});
            triangles.push_back({{b, a + sides, b + sides}});
        }
    }
    const auto id = engine.create<objects::ObjectType::TriangleMesh3>(
        {.positions = std::move(positions), .triangles = std::move(triangles)});
    (void)engine.scene().setLocalTransform(id, math::translation(position));
    (void)engine.scene().setMaterial(id, std::make_shared<const render::Material>(color));
    (void)engine.scene().setShading(id, render::ShadingModel::Gouraud);
    (void)engine.scene().setBackfaceCulling(id, false);
    return id;
}

void saveFrame(huynity::Engine& engine, const std::string& path) {
    auto frame = engine.captureFrame();
    if (!frame) {
        throw std::runtime_error("Cannot capture gallery frame");
    }
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << frame->width() << ' ' << frame->height() << "\n255\n";
    for (std::size_t y = 0; y < frame->height(); ++y) {
        for (std::size_t x = 0; x < frame->width(); ++x) {
            output.write(reinterpret_cast<const char*>(frame->row(y).data() + x * 4), 3);
        }
    }
    if (!output) {
        throw std::runtime_error("Cannot write gallery screenshot");
    }
}
} // namespace

int main(int argc, char* argv[]) {
    bool continuous = false;
    std::string screenshot;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--continuous" || argument == "-c") {
            continuous = true;
        } else if ((argument == "--screenshot" || argument == "-s") && index + 1 < argc) {
            screenshot = argv[++index];
        }
    }
    huynity::Engine engine({.windowSize = {1280, 720},
                            .windowTitle = "Huynity / Forms in Light",
                            .forceHeadless = !continuous && screenshot.empty()});
    engine.setHudEnabled(false);
    engine.setClearColor({0.12, 0.16, 0.19, 1});
    engine.setLighting({
        .ambient = {{0.83, 0.89, 1.0, 1}, 0.65},
        .directional = {{{{-0.6, -1.0, 0.4}}, {1.0, 0.87, 0.70, 1}, 0.80}},
        .shadows = {.enabled = true,
                    .resolution = 2048,
                    .extent = 18,
                    .center = {{0, 2, 0}},
                    .depthRange = 50,
                    .bias = 0.07,
                    .minVisibility = 0.25},
    });
    constexpr render::Color stone{0.80, 0.75, 0.65, 1};
    constexpr render::Color dark{0.17, 0.23, 0.25, 1};
    constexpr render::Color brass{0.70, 0.45, 0.20, 1};
    box(engine, {{26, 0.4, 18}}, {{0, -0.3, 0}}, {0.36, 0.39, 0.37, 1});
    // Узкие швы отделяют крупные каменные плиты без контурной сетки.
    for (int x = -5; x <= 5; ++x) {
        for (int z = -3; z <= 3; ++z) {
            box(engine, {{2.16, 0.06, 2.16}}, {{x * 2.2, -0.06, z * 2.2}},
                (x + z) % 2 == 0 ? render::Color{0.68, 0.66, 0.59, 1} : render::Color{0.64, 0.63, 0.57, 1});
        }
    }
    box(engine, {{25, 9, 0.5}}, {{0, 4.3, 7.3}}, stone);
    box(engine, {{25, 0.18, 0.20}}, {{0, 0.25, 6.95}}, brass);
    for (const double x : {-9.0, -5.4, -1.8, 1.8, 5.4, 9.0}) {
        box(engine, {{0.22, 8, 0.22}}, {{x, 4, 6.90}}, {0.61, 0.55, 0.45, 1});
    }
    // Открытая колоннада и деревянные ламели дают залу глубину и ритм.
    for (const double z : {-5.0, 0.0, 5.0}) {
        for (const double x : {-10.0, 10.0}) {
            box(engine, {{0.6, 7.5, 0.7}}, {{x, 3.75, z}}, stone);
        }
        box(engine, {{20.6, 0.5, 0.7}}, {{0, 7.25, z}}, stone);
    }
    for (int index = 0; index < 14; ++index) {
        box(engine, {{0.12, 0.24, 12}}, {{-9.1 + index * 1.4, 7.5, 0}}, dark);
    }
    for (const double x : {-5.0, 0.0, 5.0}) {
        box(engine, {{3.0, 0.14, 3.0}}, {{x, 0.07, 0}}, brass);
        box(engine, {{2.6, 1.0, 2.6}}, {{x, 0.64, 0}}, dark);
        box(engine, {{2.72, 0.12, 2.72}}, {{x, 1.2, 0}}, stone);
    }
    vessel(engine,
           {{{0.62, 0},
             {0.70, 0.12},
             {0.92, 0.6},
             {1.02, 1.1},
             {0.95, 1.6},
             {0.65, 2.1},
             {0.38, 2.3},
             {0.38, 2.7},
             {0.46, 2.8},
             {0.34, 2.8},
             {0.28, 2.3}}},
           {{-5, 1.28, 0}}, {0.68, 0.26, 0.15, 1});
    vessel(engine,
           {{{0.8, 0},
             {0.9, 0.12},
             {0.9, 0.28},
             {0.45, 0.45},
             {0.35, 1.3},
             {0.55, 1.9},
             {0.95, 2.3},
             {1.1, 2.6},
             {1.12, 2.72},
             {0.98, 2.72},
             {0.85, 2.4},
             {0.4, 2.0},
             {0.20, 1.4}}},
           {{5, 1.28, 0}}, {0.24, 0.43, 0.40, 1});
    // Центральная скульптура: две закрученные ленты из бронзы.
    for (const double phase : {0.0, std::numbers::pi}) {
        std::vector<math::Point3> positions;
        std::vector<objects::TriangleIndices> triangles;
        for (std::uint32_t step = 0; step <= 96; ++step) {
            const double t = static_cast<double>(step) / 96;
            const double angle = t * std::numbers::pi * 2 + phase;
            for (const double radius : {0.65, 1.15}) {
                positions.push_back({{radius * std::cos(angle), 1.3 + t * 3.4, radius * std::sin(angle)}});
            }
            if (step > 0) {
                const auto base = step * 2;
                triangles.push_back({{base - 2, base, base + 1}});
                triangles.push_back({{base - 2, base + 1, base - 1}});
            }
        }
        const auto ribbon = engine.create<objects::ObjectType::TriangleMesh3>(
            {.positions = std::move(positions), .triangles = std::move(triangles)});
        (void)engine.scene().setMaterial(ribbon, std::make_shared<const render::Material>(brass));
        (void)engine.scene().setBackfaceCulling(ribbon, false);
        (void)engine.scene().setShading(ribbon, render::ShadingModel::Gouraud);
    }
    double yaw = -0.17;
    double width = 23;
    const auto update = [&](double dt) {
        const auto& input = engine.input();
        if (input.isKeyPressed(window::KeyCode::Escape)) {
            engine.stop();
            return;
        }
        if (input.isKeyPressed(window::KeyCode::A)) {
            yaw -= dt * 0.3;
        }
        if (input.isKeyPressed(window::KeyCode::D)) {
            yaw += dt * 0.3;
        }
        width = std::clamp(width - input.scrollDelta(), 17.0, 30.0);
        const auto orbit =
            math::compose(math::rotation(math::RotationAxis::X, -0.19), math::rotation(math::RotationAxis::Y, yaw));
        const auto size = engine.window().size();
        engine.setCamera(render::OrthographicCamera3{
            .worldToView = math::compose(math::translation(math::Vector3{{0, -2.7, 32}}), orbit),
            .width = width,
            .height = width * size.height / size.width,
            .nearPlane = 0.1,
            .farPlane = 90});
    };
    update(0);
    engine.onUpdate(update);
    engine.step(0.0);
    if (!screenshot.empty()) {
        saveFrame(engine, screenshot);
    }
    if (continuous) {
        engine.run();
    } else {
        engine.step(0);
    }

    std::cout << "Forms in Light: A/D orbit, mouse wheel zoom, Esc exit.\n";
}
