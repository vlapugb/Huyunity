#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"
#include "huynity/image/image_buffer.h"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace core = huynity::core;
namespace image = huynity::image;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace window = huynity::adapters::window;

void savePpm(const image::ImageBuffer& buffer, const std::string& path) {
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

void writeRgba(image::ImageBuffer& buffer, std::size_t x, std::size_t y, double r, double g, double b, double a = 1.0) {
    auto row = buffer.row(y);
    const std::size_t off = x * 4;
    const auto toByte = [](double v) { return static_cast<std::byte>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    row[off + 0] = toByte(r);
    row[off + 1] = toByte(g);
    row[off + 2] = toByte(b);
    row[off + 3] = toByte(a);
}

std::shared_ptr<const image::ImageBuffer> createCubePanelTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create cube texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double edgeDist = std::min({u, 1.0 - u, v, 1.0 - v});
            const double dx = std::abs(u - 0.5);
            const double dy = std::abs(v - 0.5);

            double r = 0.94 - 0.16 * (dx + dy);
            double g = 0.42 - 0.10 * (dx + dy);
            double b = 0.16;

            // Светлая геометрическая рамка и центральная эмблема
            if (edgeDist < 0.065) {
                r = 0.22;
                g = 0.24;
                b = 0.28;
            } else if (edgeDist < 0.105) {
                r = 0.96;
                g = 0.86;
                b = 0.68;
            } else if (dx + dy < 0.24) {
                r = 0.98;
                g = 0.82;
                b = 0.45;
            } else if (std::abs(dx - dy) < 0.035 && dx < 0.34) {
                r = 0.95;
                g = 0.88;
                b = 0.74;
            }
            writeRgba(*buf, x, y, r, g, b);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createStudioFloorTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create floor texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const bool tileA = ((u < 0.5) != (v < 0.5));
            const double localU = u < 0.5 ? u * 2.0 : (u - 0.5) * 2.0;
            const double localV = v < 0.5 ? v * 2.0 : (v - 0.5) * 2.0;
            const double seam = std::min({localU, 1.0 - localU, localV, 1.0 - localV});

            double base = tileA ? 0.82 : 0.72;
            if (seam < 0.035) {
                base = 0.54;
            }
            writeRgba(*buf, x, y, base * 0.96, base * 0.98, base * 1.02);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

struct TexturedMeshData {
    std::vector<math::Point3> positions;
    std::vector<objects::TriangleIndices> triangles;
    std::vector<math::Point2> texCoords;
};

TexturedMeshData makeTexturedCube(double halfExtent) {
    TexturedMeshData data;
    const auto addFace = [&](const math::Point3& bl, const math::Point3& tl, const math::Point3& tr,
                             const math::Point3& br) {
        const auto base = static_cast<std::uint32_t>(data.positions.size());
        data.positions.push_back(bl);
        data.positions.push_back(tl);
        data.positions.push_back(tr);
        data.positions.push_back(br);
        data.texCoords.push_back({{0.0, 1.0}});
        data.texCoords.push_back({{0.0, 0.0}});
        data.texCoords.push_back({{1.0, 0.0}});
        data.texCoords.push_back({{1.0, 1.0}});
        data.triangles.push_back({{base, base + 1, base + 2}});
        data.triangles.push_back({{base, base + 2, base + 3}});
    };

    const double h = halfExtent;
    // -Z (лицевая грань)
    addFace({{-h, -h, -h}}, {{-h, h, -h}}, {{h, h, -h}}, {{h, -h, -h}});
    // +Z (дальняя грань)
    addFace({{h, -h, h}}, {{h, h, h}}, {{-h, h, h}}, {{-h, -h, h}});
    // -X (левая грань)
    addFace({{-h, -h, h}}, {{-h, h, h}}, {{-h, h, -h}}, {{-h, -h, -h}});
    // +X (правая грань)
    addFace({{h, -h, -h}}, {{h, h, -h}}, {{h, h, h}}, {{h, -h, h}});
    // +Y (верхняя грань)
    addFace({{-h, h, -h}}, {{-h, h, h}}, {{h, h, h}}, {{h, h, -h}});
    // -Y (нижняя грань)
    addFace({{-h, -h, h}}, {{-h, -h, -h}}, {{h, -h, -h}}, {{h, -h, h}});
    return data;
}

TexturedMeshData makeTiledFloor(double halfSize, int subdivisions, double uvRepeats) {
    TexturedMeshData data;
    const int stride = subdivisions + 1;
    data.positions.reserve(static_cast<std::size_t>(stride * stride));
    data.texCoords.reserve(static_cast<std::size_t>(stride * stride));
    for (int z = 0; z <= subdivisions; ++z) {
        const double tz = static_cast<double>(z) / static_cast<double>(subdivisions);
        const double wz = -halfSize + tz * (2.0 * halfSize);
        for (int x = 0; x <= subdivisions; ++x) {
            const double tx = static_cast<double>(x) / static_cast<double>(subdivisions);
            const double wx = -halfSize + tx * (2.0 * halfSize);
            data.positions.push_back({{wx, 0.0, wz}});
            data.texCoords.push_back({{tx * uvRepeats, tz * uvRepeats}});
        }
    }
    for (int z = 0; z < subdivisions; ++z) {
        for (int x = 0; x < subdivisions; ++x) {
            const auto i0 = static_cast<std::uint32_t>(z * stride + x);
            const auto i1 = static_cast<std::uint32_t>((z + 1) * stride + x);
            const auto i2 = static_cast<std::uint32_t>((z + 1) * stride + x + 1);
            const auto i3 = static_cast<std::uint32_t>(z * stride + x + 1);
            data.triangles.push_back({{i0, i1, i2}});
            data.triangles.push_back({{i0, i2, i3}});
        }
    }
    return data;
}

math::Vector3 normalized(const math::Vector3& value) {
    const double len = math::length(value);
    return len > 1e-12 ? value * (1.0 / len) : math::Vector3{};
}

math::Transform3 lookAt(const math::Vector3& eye, const math::Vector3& target) {
    const auto forward = normalized(target - eye);
    const auto right = normalized(math::cross(math::Vector3{{0.0, 1.0, 0.0}}, forward));
    const auto up = math::cross(forward, right);
    const std::array<math::Vector3, 3> basis{right, up, forward};
    math::Transform3 result;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            result.values[row * math::Transform3::rowSize + col] = basis[row][col];
        }
        result.values[row * math::Transform3::rowSize + 3] = -math::dot(basis[row], eye);
    }
    return result;
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

    std::cout << "Starting Huynity: Hello, Cube (" << (continuous ? "interactive mode" : "CI mode") << ")...\n";
    if (continuous) {
        std::cout
            << "Controls: Left-drag orbit camera, Scroll zoom, Space boost, F wireframe, L light orbit, Esc exit.\n";
    }

    huynity::Engine engine(
        {.windowSize = {800, 600}, .windowTitle = "Huynity: Hello, Cube", .forceHeadless = !continuous});

    // 1. Текстурированный студийный пол (принимает тени, плавно уходит в дымку горизонта)
    const auto floorMesh = makeTiledFloor(20.0, 14, 14.0);
    const auto floorId = engine.create<objects::ObjectType::TriangleMesh3>(
        {.positions = floorMesh.positions, .triangles = floorMesh.triangles});
    (void)engine.scene().setTexCoords(floorId, floorMesh.texCoords);
    (void)engine.scene().setShadowOptions(floorId, false, true);
    (void)engine.scene().setLocalTransform(floorId, math::translation(math::Vector3{{0.0, -1.65, 0.0}}));
    (void)engine.scene().setMaterial(floorId, std::make_shared<const render::Material>(render::Material{
                                                  .color = {1.0, 1.0, 1.0, 1.0},
                                                  .texture = createStudioFloorTexture(64),
                                                  .filter = render::TextureFilter::Bilinear,
                                                  .wrap = render::TextureWrap::Repeat,
                                              }));

    // 2. Ступенчатый постамент под кубом (отбрасывает и принимает тени)
    const auto plinthBase = engine.create<objects::ObjectType::Box3>({.size = {3.6, 0.28, 3.6}});
    (void)engine.scene().setLocalTransform(plinthBase, math::translation(math::Vector3{{0.0, -1.51, 0.0}}));
    (void)engine.scene().setMaterial(plinthBase,
                                     std::make_shared<const render::Material>(render::Color{0.24, 0.27, 0.33, 1.0}));

    const auto plinthTop = engine.create<objects::ObjectType::Box3>({.size = {2.4, 0.22, 2.4}});
    (void)engine.scene().setLocalTransform(plinthTop, math::translation(math::Vector3{{0.0, -1.26, 0.0}}));
    (void)engine.scene().setMaterial(plinthTop,
                                     std::make_shared<const render::Material>(render::Color{0.34, 0.38, 0.45, 1.0}));

    // 3. Главный текстурированный куб с динамическими тенями
    const auto cubeMesh = makeTexturedCube(1.0);
    const auto cube = engine.create<objects::ObjectType::TriangleMesh3>(
        {.positions = cubeMesh.positions, .triangles = cubeMesh.triangles}, core::Mobility::Dynamic);
    (void)engine.scene().setTexCoords(cube, cubeMesh.texCoords);
    (void)engine.scene().setMaterial(cube, std::make_shared<const render::Material>(render::Material{
                                               .color = {1.0, 1.0, 1.0, 1.0},
                                               .texture = createCubePanelTexture(128),
                                               .filter = render::TextureFilter::Bilinear,
                                               .wrap = render::TextureWrap::Clamp,
                                           }));

    // 4. Иерархический узел спутников (Gouraud-сфера + каркасный wireframe-кристалл)
    const auto moonPivot =
        engine.create<objects::ObjectType::Box3>({.size = {0.01, 0.01, 0.01}}, core::Mobility::Dynamic);
    (void)engine.scene().setVisibility(moonPivot, false);

    const auto moon = engine.create<objects::ObjectType::Sphere3>({.radius = 0.42}, core::Mobility::Dynamic);
    (void)engine.scene().setShading(moon, render::ShadingModel::Gouraud);
    (void)engine.scene().setMaterial(moon,
                                     std::make_shared<const render::Material>(render::Color{0.20, 0.84, 0.78, 1.0}));
    (void)engine.scene().setParent(moon, moonPivot);
    (void)engine.scene().setLocalTransform(moon, math::translation(math::Vector3{{2.25, 0.35, 0.0}}));

    const auto wireOrbiter =
        engine.create<objects::ObjectType::Box3>({.size = {0.62, 0.62, 0.62}}, core::Mobility::Dynamic);
    (void)engine.scene().setWireframe(wireOrbiter, true);
    (void)engine.scene().setShadowOptions(wireOrbiter, false, false);
    (void)engine.scene().setMaterial(wireOrbiter, std::make_shared<const render::Material>(render::Material{
                                                      .color = {0.42, 0.90, 1.0, 1.0}, .unlit = true}));
    (void)engine.scene().setParent(wireOrbiter, moonPivot);
    (void)engine.scene().setLocalTransform(wireOrbiter, math::translation(math::Vector3{{-2.15, 0.25, 0.0}}));

    // 5. Неосвещаемый маркер источника света (unlit)
    const auto lightMarker = engine.create<objects::ObjectType::Sphere3>({.radius = 0.20}, core::Mobility::Dynamic);
    (void)engine.scene().setShadowOptions(lightMarker, false, false);
    (void)engine.scene().setMaterial(lightMarker, std::make_shared<const render::Material>(render::Material{
                                                      .color = {1.0, 0.94, 0.72, 1.0}, .unlit = true}));

    // 6. Минимальный полупрозрачный HUD-индикатор
    const auto hudPanel = engine.create<objects::ObjectType::Rectangle2>({.size = {172.0, 26.0}});
    (void)engine.scene().setRenderProperties(hudPanel, core::RenderSpace::Hud, 0, 0);
    (void)engine.scene().setLocalTransform(hudPanel, math::translation(math::Vector2{{102.0, 28.0}}));
    (void)engine.scene().setMaterial(hudPanel,
                                     std::make_shared<const render::Material>(render::Color{0.06, 0.09, 0.14, 0.72}));

    const auto hudBar = engine.create<objects::ObjectType::Rectangle2>({.size = {148.0, 6.0}}, core::Mobility::Dynamic);
    (void)engine.scene().setRenderProperties(hudBar, core::RenderSpace::Hud, 0, 1);
    (void)engine.scene().setLocalTransform(hudBar, math::translation(math::Vector2{{102.0, 28.0}}));
    (void)engine.scene().setMaterial(hudBar,
                                     std::make_shared<const render::Material>(render::Color{0.96, 0.56, 0.20, 0.92}));

    double cubeYaw = 0.05;
    double cubePitch = 0.28;
    double orbitTime = 0.22;
    double lightAngle = -2.35;
    bool lightOrbiting = true;
    bool cubeWireframe = false;
    bool fWasDown = false;
    bool lWasDown = false;

    double camYaw = -0.58;
    double camPitch = 0.34;
    double camDist = 7.2;

    const auto updateScene = [&](double dt) {
        const auto& input = engine.input();
        if (input.isKeyPressed(window::KeyCode::Escape)) {
            engine.stop();
            return;
        }

        const bool fDown = input.isKeyPressed(window::KeyCode::F);
        if (fDown && !fWasDown) {
            cubeWireframe = !cubeWireframe;
            (void)engine.scene().setWireframe(cube, cubeWireframe);
        }
        fWasDown = fDown;

        const bool lDown = input.isKeyPressed(window::KeyCode::L);
        if (lDown && !lWasDown) {
            lightOrbiting = !lightOrbiting;
        }
        lWasDown = lDown;

        if (input.isMouseButtonPressed(window::MouseButton::Left)) {
            camYaw += input.mouseDeltaX() * 0.007;
            camPitch = std::clamp(camPitch + input.mouseDeltaY() * 0.007, 0.08, 1.25);
        }
        if (std::abs(input.scrollDelta()) > 0.01) {
            camDist = std::clamp(camDist - input.scrollDelta() * 0.6, 4.2, 13.0);
        }
        if (input.isKeyPressed(window::KeyCode::W) || input.isKeyPressed(window::KeyCode::Up)) {
            camDist = std::max(4.2, camDist - dt * 4.5);
        }
        if (input.isKeyPressed(window::KeyCode::S) || input.isKeyPressed(window::KeyCode::Down)) {
            camDist = std::min(13.0, camDist + dt * 4.5);
        }
        if (input.isKeyPressed(window::KeyCode::A) || input.isKeyPressed(window::KeyCode::Left)) {
            camYaw -= dt * 1.1;
        }
        if (input.isKeyPressed(window::KeyCode::D) || input.isKeyPressed(window::KeyCode::Right)) {
            camYaw += dt * 1.1;
        }

        const double speed = input.isKeyPressed(window::KeyCode::Space) ? 2.6 : 1.0;
        cubeYaw += dt * 0.75 * speed;
        cubePitch += dt * 0.35 * speed;
        orbitTime += dt * 1.25 * speed;
        if (lightOrbiting) {
            lightAngle += dt * 0.42;
        }

        const double hoverY = 0.25 + 0.14 * std::sin(orbitTime * 1.4);
        const auto cubeTransform = math::compose(math::translation(math::Vector3{{0.0, hoverY, 0.0}}),
                                                 math::compose(math::rotation(math::RotationAxis::Y, cubeYaw),
                                                               math::rotation(math::RotationAxis::X, cubePitch)));
        (void)engine.scene().setLocalTransform(cube, cubeTransform);

        const auto wireTransform = math::compose(math::translation(math::Vector3{{-2.15, 0.25, 0.0}}),
                                                 math::compose(math::rotation(math::RotationAxis::Y, -orbitTime * 1.4),
                                                               math::rotation(math::RotationAxis::X, orbitTime * 0.9)));
        (void)engine.scene().setLocalTransform(wireOrbiter, wireTransform);

        const auto pivotTransform = math::compose(math::translation(math::Vector3{{0.0, hoverY * 0.5, 0.0}}),
                                                  math::compose(math::rotation(math::RotationAxis::Y, orbitTime),
                                                                math::rotation(math::RotationAxis::Z, 0.24)));
        (void)engine.scene().setLocalTransform(moonPivot, pivotTransform);

        const math::Vector3 sunPos{{std::cos(lightAngle) * 5.0, 5.6, std::sin(lightAngle) * 5.0}};
        (void)engine.scene().setLocalTransform(lightMarker, math::translation(sunPos));

        const math::Vector3 lightDir = normalized(sunPos * -1.0);
        engine.setLighting({
            .ambient = {.color = {0.72, 0.80, 0.96, 1.0}, .intensity = 0.34},
            .directional =
                {
                    {.direction = lightDir, .color = {1.0, 0.94, 0.84, 1.0}, .intensity = 0.92},
                    {.direction = normalized(math::Vector3{{-0.5, -0.3, -0.7}}),
                     .color = {0.42, 0.62, 0.96, 1.0},
                     .intensity = 0.22},
                },
            .shadows =
                {
                    .enabled = true,
                    .resolution = 768,
                    .extent = 6.0,
                    .center = {{0.0, -0.8, 0.0}},
                    .depthRange = 28.0,
                    .bias = 0.025,
                    .minVisibility = 0.0,
                },
            .fog =
                {
                    .enabled = true,
                    .color = {0.17, 0.23, 0.33, 1.0},
                    .start = 13.0,
                    .end = 27.0,
                },
            .sky =
                {
                    .enabled = true,
                    .zenithColor = {0.07, 0.11, 0.19, 1.0},
                    .horizonColor = {0.17, 0.23, 0.33, 1.0},
                    .groundColor = {0.10, 0.13, 0.18, 1.0},
                    .sunColor = {1.0, 0.92, 0.72, 1.0},
                    .sunGlow = 0.45,
                },
        });

        const double cosP = std::cos(camPitch);
        const math::Vector3 eye{{
            std::sin(camYaw) * cosP * camDist,
            std::sin(camPitch) * camDist - 0.1,
            -std::cos(camYaw) * cosP * camDist,
        }};
        engine.setCamera(render::PerspectiveCamera3{
            .worldToView = lookAt(eye, math::Vector3{{0.0, -0.15, 0.0}}),
            .verticalFieldOfView = 0.95,
            .nearPlane = 0.1,
            .farPlane = 60.0,
        });

        const double barScale = 0.25 + 0.75 * (0.5 + 0.5 * std::sin(orbitTime * 1.5));
        (void)engine.scene().setLocalTransform(
            hudBar, math::compose(math::translation(math::Vector2{{28.0 + 74.0 * barScale, 28.0}}),
                                  math::scaling(math::Vector2{{barScale, 1.0}})));
    };

    updateScene(0.0);
    engine.onUpdate([&](double dt) { updateScene(dt); });

    if (continuous) {
        engine.run();
    } else {
        for (int step = 0; step < 10 && engine.window().isOpen(); ++step) {
            engine.step(1.0 / 60.0);
        }
        engine.stop();
    }

    if (!screenshotPath.empty()) {
        if (const auto* headless = dynamic_cast<const window::HeadlessWindow*>(&engine.window())) {
            if (const auto* frame = headless->lastPresentedFrame()) {
                savePpm(*frame, screenshotPath);
                std::cout << "Saved screenshot to " << screenshotPath << "\n";
            }
        }
    }

    std::cout << "Finished Hello, Cube successfully.\n";
    return 0;
}
