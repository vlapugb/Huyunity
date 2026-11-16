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

std::shared_ptr<const image::ImageBuffer> createSandstoneTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create sandstone texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double strata =
                0.035 * std::sin(v * std::numbers::pi * 4.0 + 0.4 * std::sin(u * std::numbers::pi * 2.0)) +
                0.020 * std::cos(v * std::numbers::pi * 8.0 - u * std::numbers::pi * 2.0) +
                0.015 * std::sin((u + v) * std::numbers::pi * 6.0);
            writeRgba(*buf, x, y, 0.78 + strata, 0.41 + strata * 0.65, 0.25 + strata * 0.35);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createDeckGrateTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create deck texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double border = std::min({u, 1.0 - u, v, 1.0 - v});
            const bool rib = std::fmod(u * 4.0, 1.0) < 0.16 || std::fmod(v * 4.0, 1.0) < 0.16;
            double c = rib ? 0.30 : 0.42;
            if (border < 0.06) {
                c = 0.72;
            }
            writeRgba(*buf, x, y, c, c * 1.03, c * 1.10);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createSolarPanelTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create solar texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double border = std::min({u, 1.0 - u, v, 1.0 - v});
            const bool grid = std::fmod(u * 6.0, 1.0) < 0.08 || std::fmod(v * 8.0, 1.0) < 0.08;
            if (border < 0.04) {
                writeRgba(*buf, x, y, 0.72, 0.76, 0.80);
            } else if (grid) {
                writeRgba(*buf, x, y, 0.34, 0.46, 0.62);
            } else {
                writeRgba(*buf, x, y, 0.08, 0.19, 0.44);
            }
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createLandingPadTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create landing pad texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double d = std::hypot(u - 0.5, v - 0.5);
            double r = 0.25;
            double g = 0.27;
            double b = 0.31;
            if (std::abs(d - 0.38) < 0.045 || std::abs(d - 0.18) < 0.035) {
                r = 0.94;
                g = 0.68;
                b = 0.22;
            } else if ((std::abs(u - 0.5) < 0.03 || std::abs(v - 0.5) < 0.03) && d < 0.16) {
                r = 0.92;
                g = 0.84;
                b = 0.65;
            }
            writeRgba(*buf, x, y, r, g, b);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createCrateTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create crate texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double border = std::min({u, 1.0 - u, v, 1.0 - v});
            if (border < 0.10) {
                writeRgba(*buf, x, y, 0.30, 0.33, 0.38);
            } else if (std::abs((u - 0.5) - (v - 0.5)) < 0.06 || std::abs((u - 0.5) + (v - 0.5)) < 0.06) {
                writeRgba(*buf, x, y, 0.86, 0.56, 0.20);
            } else {
                writeRgba(*buf, x, y, 0.58, 0.62, 0.66);
            }
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

struct MeshData {
    std::vector<math::Point3> positions;
    std::vector<objects::TriangleIndices> triangles;
    std::vector<math::Point2> texCoords;
};

// Высотная функция рельефа красного каньона: южное плато, глубокое ущелье, северная меса и внешние скалы
double canyonTerrainHeight(double x, double z) {
    const double gorgeCenterZ = 1.6 * std::sin(x * 0.10);
    const double distToGorge = std::abs(z - gorgeCenterZ);
    const double gorgeFactor = std::exp(-(distToGorge * distToGorge) / 26.0);

    double baseHeight = 2.15;
    if (z > 6.0) {
        baseHeight = 2.45;
    }
    baseHeight -= gorgeFactor * 6.2;

    // Плоские площадки под посадочную площадку (юг) и обсерваторию (север)
    const double padDist = std::hypot(x - 0.0, z + 14.5);
    const double padBlend = std::exp(-(padDist * padDist) / 55.0);
    baseHeight = baseHeight * (1.0 - padBlend) + 2.15 * padBlend;

    const double obsDist = std::hypot(x - 0.0, z - 16.5);
    const double obsBlend = std::exp(-(obsDist * obsDist) / 70.0);
    baseHeight = baseHeight * (1.0 - obsBlend) + 2.45 * obsBlend;

    // Внешние скалы и каньонные башни по периметру
    const double rimDist = std::hypot(x * 0.88, z * 0.80);
    double cliffs = 0.0;
    if (rimDist > 21.0) {
        const double t = std::clamp((rimDist - 21.0) / 14.0, 0.0, 1.0);
        const double angular = 0.55 + 0.45 * std::sin(std::atan2(z, x) * 5.0 + 0.7) * std::cos(std::atan2(z, x) * 2.0);
        cliffs = t * t * (5.2 + 5.8 * angular);
    }

    // Небольшие волны породы на холмах
    const double detail = (1.0 - std::max(padBlend, obsBlend)) * 0.22 * std::sin(x * 0.24) * std::cos(z * 0.22);

    return baseHeight + cliffs + detail;
}

MeshData makeCanyonMesh(double halfSize, int subdivisions, double uvRepeats) {
    MeshData data;
    const int stride = subdivisions + 1;
    for (int iz = 0; iz <= subdivisions; ++iz) {
        const double tz = static_cast<double>(iz) / static_cast<double>(subdivisions);
        const double z = -halfSize + tz * (2.0 * halfSize);
        for (int ix = 0; ix <= subdivisions; ++ix) {
            const double tx = static_cast<double>(ix) / static_cast<double>(subdivisions);
            const double x = -halfSize + tx * (2.0 * halfSize);
            data.positions.push_back({{x, canyonTerrainHeight(x, z), z}});
            data.texCoords.push_back({{tx * uvRepeats, tz * uvRepeats}});
        }
    }
    for (int iz = 0; iz < subdivisions; ++iz) {
        for (int ix = 0; ix < subdivisions; ++ix) {
            const auto i0 = static_cast<std::uint32_t>(iz * stride + ix);
            const auto i1 = static_cast<std::uint32_t>((iz + 1) * stride + ix);
            const auto i2 = static_cast<std::uint32_t>((iz + 1) * stride + ix + 1);
            const auto i3 = static_cast<std::uint32_t>(iz * stride + ix + 1);
            data.triangles.push_back({{i0, i1, i2}});
            data.triangles.push_back({{i0, i2, i3}});
        }
    }
    return data;
}

MeshData makePlaneMesh(double halfWidth, double halfDepth, int segX, int segZ, double uvX, double uvZ) {
    MeshData data;
    const int stride = segX + 1;
    for (int iz = 0; iz <= segZ; ++iz) {
        const double tz = static_cast<double>(iz) / static_cast<double>(segZ);
        const double z = -halfDepth + tz * (2.0 * halfDepth);
        for (int ix = 0; ix <= segX; ++ix) {
            const double tx = static_cast<double>(ix) / static_cast<double>(segX);
            const double x = -halfWidth + tx * (2.0 * halfWidth);
            data.positions.push_back({{x, 0.0, z}});
            data.texCoords.push_back({{tx * uvX, tz * uvZ}});
        }
    }
    for (int iz = 0; iz < segZ; ++iz) {
        for (int ix = 0; ix < segX; ++ix) {
            const auto i0 = static_cast<std::uint32_t>(iz * stride + ix);
            const auto i1 = static_cast<std::uint32_t>((iz + 1) * stride + ix);
            const auto i2 = static_cast<std::uint32_t>((iz + 1) * stride + ix + 1);
            const auto i3 = static_cast<std::uint32_t>(iz * stride + ix + 1);
            data.triangles.push_back({{i0, i1, i2}});
            data.triangles.push_back({{i0, i2, i3}});
        }
    }
    return data;
}

MeshData makeTexturedBox(double sx, double sy, double sz, double uvScaleX = 1.0, double uvScaleY = 1.0) {
    MeshData data;
    const double hx = sx * 0.5;
    const double hy = sy * 0.5;
    const double hz = sz * 0.5;
    const auto addFace = [&](const math::Point3& bl, const math::Point3& tl, const math::Point3& tr,
                             const math::Point3& br, double uSpan, double vSpan) {
        const auto base = static_cast<std::uint32_t>(data.positions.size());
        data.positions.push_back(bl);
        data.positions.push_back(tl);
        data.positions.push_back(tr);
        data.positions.push_back(br);
        data.texCoords.push_back({{0.0, vSpan}});
        data.texCoords.push_back({{0.0, 0.0}});
        data.texCoords.push_back({{uSpan, 0.0}});
        data.texCoords.push_back({{uSpan, vSpan}});
        data.triangles.push_back({{base, base + 1, base + 2}});
        data.triangles.push_back({{base, base + 2, base + 3}});
    };
    addFace({{-hx, -hy, -hz}}, {{-hx, hy, -hz}}, {{hx, hy, -hz}}, {{hx, -hy, -hz}}, uvScaleX, uvScaleY);
    addFace({{hx, -hy, hz}}, {{hx, hy, hz}}, {{-hx, hy, hz}}, {{-hx, -hy, hz}}, uvScaleX, uvScaleY);
    addFace({{-hx, -hy, hz}}, {{-hx, hy, hz}}, {{-hx, hy, -hz}}, {{-hx, -hy, -hz}}, uvScaleX, uvScaleY);
    addFace({{hx, -hy, -hz}}, {{hx, hy, -hz}}, {{hx, hy, hz}}, {{hx, -hy, hz}}, uvScaleX, uvScaleY);
    addFace({{-hx, hy, -hz}}, {{-hx, hy, hz}}, {{hx, hy, hz}}, {{hx, hy, -hz}}, uvScaleX, uvScaleY);
    addFace({{-hx, -hy, hz}}, {{-hx, -hy, -hz}}, {{hx, -hy, -hz}}, {{hx, -hy, hz}}, uvScaleX, uvScaleY);
    return data;
}

MeshData makeCylinder(double bottomRadius, double topRadius, double height, int sides) {
    MeshData data;
    const double halfH = height * 0.5;
    for (int i = 0; i <= sides; ++i) {
        const double angle = (2.0 * std::numbers::pi * static_cast<double>(i)) / static_cast<double>(sides);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        data.positions.push_back({{bottomRadius * c, -halfH, bottomRadius * s}});
        data.positions.push_back({{topRadius * c, halfH, topRadius * s}});
    }
    for (int i = 0; i < sides; ++i) {
        const auto b0 = static_cast<std::uint32_t>(i * 2);
        const auto t0 = b0 + 1;
        const auto b1 = b0 + 2;
        const auto t1 = b0 + 3;
        data.triangles.push_back({{b0, t0, t1}});
        data.triangles.push_back({{b0, t1, b1}});
    }
    const auto bottomCenter = static_cast<std::uint32_t>(data.positions.size());
    data.positions.push_back({{0.0, -halfH, 0.0}});
    const auto topCenter = static_cast<std::uint32_t>(data.positions.size());
    data.positions.push_back({{0.0, halfH, 0.0}});
    for (int i = 0; i < sides; ++i) {
        const auto b0 = static_cast<std::uint32_t>(i * 2);
        const auto b1 = b0 + 2;
        data.triangles.push_back({{bottomCenter, b0, b1}});
        if (topRadius > 1e-4) {
            data.triangles.push_back({{topCenter, b1 + 1, b0 + 1}});
        }
    }
    return data;
}

MeshData makeParabolicDish(double radius, double depth, int ringSegs, int lonSegs) {
    MeshData data;
    const int stride = lonSegs + 1;
    for (int ir = 0; ir <= ringSegs; ++ir) {
        const double t = static_cast<double>(ir) / static_cast<double>(ringSegs);
        const double r = radius * t;
        const double y = depth * t * t;
        for (int lon = 0; lon <= lonSegs; ++lon) {
            const double phi = (2.0 * std::numbers::pi * static_cast<double>(lon)) / static_cast<double>(lonSegs);
            data.positions.push_back({{r * std::cos(phi), y, r * std::sin(phi)}});
        }
    }
    for (int ir = 0; ir < ringSegs; ++ir) {
        for (int lon = 0; lon < lonSegs; ++lon) {
            const auto a = static_cast<std::uint32_t>(ir * stride + lon);
            const auto b = static_cast<std::uint32_t>((ir + 1) * stride + lon);
            const auto c = static_cast<std::uint32_t>((ir + 1) * stride + lon + 1);
            const auto d = static_cast<std::uint32_t>(ir * stride + lon + 1);
            data.triangles.push_back({{a, b, c}});
            data.triangles.push_back({{a, c, d}});
        }
    }
    return data;
}

MeshData makeDome(double radius, int latSegs, int lonSegs) {
    MeshData data;
    const int stride = lonSegs + 1;
    for (int lat = 0; lat <= latSegs; ++lat) {
        const double theta = (std::numbers::pi * 0.5) * static_cast<double>(lat) / static_cast<double>(latSegs);
        const double y = radius * std::sin(theta);
        const double r = radius * std::cos(theta);
        for (int lon = 0; lon <= lonSegs; ++lon) {
            const double phi = (2.0 * std::numbers::pi * static_cast<double>(lon)) / static_cast<double>(lonSegs);
            data.positions.push_back({{r * std::cos(phi), y, r * std::sin(phi)}});
        }
    }
    for (int lat = 0; lat < latSegs; ++lat) {
        for (int lon = 0; lon < lonSegs; ++lon) {
            const auto a = static_cast<std::uint32_t>(lat * stride + lon);
            const auto b = static_cast<std::uint32_t>((lat + 1) * stride + lon);
            const auto c = static_cast<std::uint32_t>((lat + 1) * stride + lon + 1);
            const auto d = static_cast<std::uint32_t>(lat * stride + lon + 1);
            data.triangles.push_back({{a, b, c}});
            data.triangles.push_back({{a, c, d}});
        }
    }
    return data;
}

core::ObjectId createMeshObject(huynity::Engine& engine, const MeshData& mesh,
                                const std::shared_ptr<const render::Material>& material,
                                core::Mobility mobility = core::Mobility::Static) {
    const auto id = engine.create<objects::ObjectType::TriangleMesh3>(
        {.positions = mesh.positions, .triangles = mesh.triangles}, mobility);
    if (!mesh.texCoords.empty()) {
        (void)engine.scene().setTexCoords(id, mesh.texCoords);
    }
    (void)engine.scene().setMaterial(id, material);
    return id;
}

struct BoxCollider {
    double minX{};
    double maxX{};
    double minZ{};
    double maxZ{};
};

double walkableHeightAt(double x, double z) {
    double best = canyonTerrainHeight(x, z);

    // 1. Подвесной мост через каньон: x in [-2.1, 2.1], z in [-8.2, 8.2]
    if (std::abs(x) <= 2.1 && z >= -8.2 && z <= 8.2) {
        best = std::max(best, 2.40);
    }

    // 2. Посадочная площадка на юге: x in [-4.5, 4.5], z in [-19.0, -10.0]
    if (std::abs(x) <= 4.5 && z >= -19.0 && z <= -10.0) {
        best = std::max(best, 2.28);
    }

    // 3. Пандус и смотровая вышка на юге (x in [-11.5, -7.5], z in [-18.5, -9.5])
    if (x >= -11.2 && x <= -7.8 && z >= -18.5 && z <= -12.5) {
        const double t = std::clamp((z + 18.5) / 6.0, 0.0, 1.0);
        best = std::max(best, 2.2 + t * 2.6);
    }
    if (x >= -11.5 && x <= -7.5 && z >= -12.5 && z <= -9.5) {
        best = std::max(best, 4.80);
    }

    // 4. Пандус и балкон обсерватории на севере:
    if (x >= -8.2 && x <= -4.4 && z >= 10.5 && z <= 16.5) {
        const double t = std::clamp((z - 10.5) / 6.0, 0.0, 1.0);
        best = std::max(best, 2.45 + t * 2.35);
    }
    if (std::hypot(x, z - 16.5) <= 5.6 && std::hypot(x, z - 16.5) >= 3.3) {
        best = std::max(best, 4.80);
    }

    return best;
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

    std::cout << "Starting Huynity: Canyon Observatory (" << (continuous ? "interactive mode" : "CI mode") << ")...\n";
    if (continuous) {
        std::cout << "Controls: WASD move, Mouse/touchpad motion or Q/E/Arrows look, Space jump, Shift sprint, "
                     "F sunset/noon, V wireframe telemetry, Esc exit.\n";
    }

    huynity::Engine engine({.windowSize = {1280, 720},
                            .windowTitle = "Huynity: Canyon Observatory",
                            .forceHeadless = !continuous && screenshotPath.empty()});

    engine.setHudEnabled(false);

    // Текстуры и материалы
    const auto sandstoneMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = createSandstoneTexture(64),
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto deckMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = createDeckGrateTexture(64),
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto solarMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = createSolarPanelTexture(64),
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Clamp,
    });
    const auto padMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = createLandingPadTexture(64),
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Clamp,
    });
    const auto crateMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = createCrateTexture(64),
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Clamp,
    });
    const auto riverMat = std::make_shared<const render::Material>(render::Color{0.10, 0.34, 0.44, 1.0});
    const auto steelRedMat = std::make_shared<const render::Material>(render::Color{0.78, 0.28, 0.16, 1.0});
    const auto darkMetalMat = std::make_shared<const render::Material>(render::Color{0.22, 0.25, 0.30, 1.0});
    const auto concreteMat = std::make_shared<const render::Material>(render::Color{0.48, 0.57, 0.55, 1.0});
    const auto domeMat = std::make_shared<const render::Material>(render::Color{0.92, 0.86, 0.69, 1.0});
    const auto brassMat = std::make_shared<const render::Material>(render::Color{0.86, 0.66, 0.26, 1.0});
    const auto amberBeaconMat =
        std::make_shared<const render::Material>(render::Material{.color = {1.0, 0.68, 0.18, 1.0}, .unlit = true});
    const auto cyanBeaconMat =
        std::make_shared<const render::Material>(render::Material{.color = {0.26, 0.92, 1.0, 1.0}, .unlit = true});

    // 1. Каньонный рельеф и река на дне ущелья
    const auto terrainId = createMeshObject(engine, makeCanyonMesh(42.0, 32, 8.0), sandstoneMat);
    (void)engine.scene().setShading(terrainId, render::ShadingModel::Gouraud);
    (void)engine.scene().setShadowOptions(terrainId, false, true);

    const auto riverId = createMeshObject(engine, makePlaneMesh(42.0, 9.0, 8, 4, 4.0, 2.0), riverMat);
    (void)engine.scene().setLocalTransform(riverId, math::translation(math::Vector3{{0.0, -3.1, 0.0}}));
    (void)engine.scene().setShadowOptions(riverId, false, true);

    std::vector<BoxCollider> colliders;

    // 2. Подвесной мост через ущелье каньона (z in [-8, 8], y = 2.4)
    const auto bridgeDeck = createMeshObject(engine, makeTexturedBox(4.0, 0.28, 16.4, 2.0, 8.0), deckMat);
    (void)engine.scene().setLocalTransform(bridgeDeck, math::translation(math::Vector3{{0.0, 2.26, 0.0}}));

    for (const double sideX : {-1.95, 1.95}) {
        // Верхний и нижний поручни
        const auto topRail = createMeshObject(engine, makeTexturedBox(0.18, 0.16, 16.4, 1.0, 8.0), steelRedMat);
        (void)engine.scene().setLocalTransform(topRail, math::translation(math::Vector3{{sideX, 3.35, 0.0}}));
        const auto midRail = createMeshObject(engine, makeTexturedBox(0.12, 0.12, 16.4, 1.0, 8.0), darkMetalMat);
        (void)engine.scene().setLocalTransform(midRail, math::translation(math::Vector3{{sideX, 2.85, 0.0}}));

        for (const double postZ : {-7.6, -3.8, 0.0, 3.8, 7.6}) {
            const auto post = createMeshObject(engine, makeTexturedBox(0.22, 1.25, 0.22), steelRedMat);
            (void)engine.scene().setLocalTransform(post, math::translation(math::Vector3{{sideX, 2.88, postZ}}));
            const auto lamp = engine.create<objects::ObjectType::Box3>({.size = {{0.16, 0.10, 0.16}}});
            (void)engine.scene().setShadowOptions(lamp, false, false);
            (void)engine.scene().setLocalTransform(lamp, math::translation(math::Vector3{{sideX, 3.58, postZ}}));
            (void)engine.scene().setMaterial(lamp, amberBeaconMat);
        }
        // Несущие опоры под мостом, уходящие в каньон
        for (const double pierZ : {-5.0, 5.0}) {
            const auto pier = createMeshObject(engine, makeTexturedBox(0.55, 6.2, 0.55), darkMetalMat);
            (void)engine.scene().setLocalTransform(pier, math::translation(math::Vector3{{sideX * 0.88, -0.8, pierZ}}));
        }
    }

    // 3. Южная база: посадочная площадка, контейнеры, солнечные панели и смотровая вышка
    const auto landingPad = createMeshObject(engine, makeTexturedBox(9.0, 0.26, 9.0, 1.0, 1.0), padMat);
    (void)engine.scene().setLocalTransform(landingPad, math::translation(math::Vector3{{0.0, 2.15, -14.5}}));

    for (const double sx : {-4.2, 4.2}) {
        for (const double sz : {-18.7, -10.3}) {
            const auto marker = engine.create<objects::ObjectType::Box3>({.size = {{0.10, 0.08, 0.10}}});
            (void)engine.scene().setShadowOptions(marker, false, false);
            (void)engine.scene().setLocalTransform(marker, math::translation(math::Vector3{{sx, 2.38, sz}}));
            (void)engine.scene().setMaterial(marker, cyanBeaconMat);
        }
    }

    // Контейнеры на краю площадки
    const auto crateTemplate = createMeshObject(engine, makeTexturedBox(1.25, 1.25, 1.25), crateMat);
    (void)engine.scene().setVisibility(crateTemplate, false);
    if (const auto c1 = engine.scene().createInstance(crateTemplate)) {
        (void)engine.scene().setVisibility(*c1, true);
        (void)engine.scene().setLocalTransform(*c1, math::translation(math::Vector3{{3.4, 2.90, -11.4}}));
    }
    if (const auto c2 = engine.scene().createInstance(crateTemplate)) {
        (void)engine.scene().setVisibility(*c2, true);
        (void)engine.scene().setLocalTransform(*c2, math::compose(math::translation(math::Vector3{{4.7, 2.90, -12.6}}),
                                                                  math::rotation(math::RotationAxis::Y, 0.38)));
    }
    colliders.push_back({2.6, 5.6, -13.5, -10.6});

    // Солнечные батареи на северной месе слева от обсерватории (хорошо видны с моста и южного плато)
    for (int i = 0; i < 3; ++i) {
        const double px = -12.8 + static_cast<double>(i) * 3.0;
        const double pz = 14.5;
        const double py = canyonTerrainHeight(px, pz);
        const auto stand = createMeshObject(engine, makeCylinder(0.16, 0.16, 1.8, 8), darkMetalMat);
        (void)engine.scene().setShading(stand, render::ShadingModel::Gouraud);
        (void)engine.scene().setLocalTransform(stand, math::translation(math::Vector3{{px, py + 0.85, pz}}));

        const auto panel = createMeshObject(engine, makeTexturedBox(2.3, 0.12, 2.9), solarMat);
        (void)engine.scene().setLocalTransform(
            panel, math::compose(math::translation(math::Vector3{{px, py + 1.85, pz}}),
                                 math::compose(math::rotation(math::RotationAxis::Y, 0.28),
                                               math::rotation(math::RotationAxis::X, -0.52))));
        colliders.push_back({px - 1.2, px + 1.2, pz - 1.4, pz + 1.4});
    }

    // Смотровая вышка с прогулочным пандусом на западе южного плато
    const auto towerDeck = createMeshObject(engine, makeTexturedBox(4.0, 0.28, 3.4, 2.0, 2.0), deckMat);
    (void)engine.scene().setLocalTransform(towerDeck, math::translation(math::Vector3{{-9.5, 4.66, -11.0}}));
    const auto towerRamp = createMeshObject(engine, makeTexturedBox(3.4, 0.24, 6.55, 1.5, 3.0), deckMat);
    (void)engine.scene().setLocalTransform(towerRamp,
                                           math::compose(math::translation(math::Vector3{{-9.5, 3.38, -15.5}}),
                                                         math::rotation(math::RotationAxis::X, -0.41)));
    for (const double lx : {-11.2, -7.8}) {
        for (const double lz : {-12.4, -9.6}) {
            const auto leg = createMeshObject(engine, makeTexturedBox(0.28, 3.0, 0.28), steelRedMat);
            (void)engine.scene().setLocalTransform(leg, math::translation(math::Vector3{{lx, 3.2, lz}}));
        }
    }
    const auto towerPost = createMeshObject(engine, makeTexturedBox(0.18, 0.90, 0.18), steelRedMat);
    (void)engine.scene().setLocalTransform(towerPost, math::translation(math::Vector3{{-7.8, 5.15, -9.6}}));
    const auto towerBeacon = engine.create<objects::ObjectType::Box3>({.size = {{0.16, 0.12, 0.16}}});
    (void)engine.scene().setShadowOptions(towerBeacon, false, false);
    (void)engine.scene().setLocalTransform(towerBeacon, math::translation(math::Vector3{{-7.8, 5.72, -9.6}}));
    (void)engine.scene().setMaterial(towerBeacon, amberBeaconMat);

    // 4. Северная меса: Главная обсерватория с куполом, балконом, пандусом и телескопом
    const auto obsBase = createMeshObject(engine, makeCylinder(3.4, 3.4, 3.2, 12), concreteMat);
    (void)engine.scene().setShading(obsBase, render::ShadingModel::Gouraud);
    (void)engine.scene().setLocalTransform(obsBase, math::translation(math::Vector3{{0.0, 3.9, 16.5}}));
    colliders.push_back({-3.3, 3.3, 13.2, 19.8});

    const auto obsBalcony = createMeshObject(engine, makeCylinder(5.6, 5.6, 0.35, 16), deckMat);
    (void)engine.scene().setLocalTransform(obsBalcony, math::translation(math::Vector3{{0.0, 4.63, 16.5}}));

    // Тёмные проёмы и козырьки делают цилиндрическое здание читаемым с моста.
    for (const double x : {-1.8, 0.0, 1.8}) {
        const double z = 16.5 - std::sqrt(3.4 * 3.4 - x * x) - 0.04;
        const auto opening = createMeshObject(engine, makeTexturedBox(0.85, 1.2, 0.16), darkMetalMat);
        (void)engine.scene().setLocalTransform(opening, math::translation(math::Vector3{{x, 3.7, z}}));
        const auto hood = createMeshObject(engine, makeTexturedBox(1.1, 0.12, 0.45), brassMat);
        (void)engine.scene().setLocalTransform(hood, math::translation(math::Vector3{{x, 4.35, z - 0.1}}));
    }

    const auto obsRamp = createMeshObject(engine, makeTexturedBox(3.6, 0.24, 6.45, 1.5, 3.0), deckMat);
    (void)engine.scene().setLocalTransform(obsRamp, math::compose(math::translation(math::Vector3{{-6.3, 3.55, 13.5}}),
                                                                  math::rotation(math::RotationAxis::X, -0.38)));

    // Вращающийся купол обсерватории, телескоп и каркасный сенсорный обруч
    const auto obsDome = createMeshObject(engine, makeDome(3.35, 9, 20), domeMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(obsDome, render::ShadingModel::Gouraud);

    const auto telescope =
        createMeshObject(engine, makeCylinder(0.42, 0.34, 4.2, 12), brassMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(telescope, render::ShadingModel::Gouraud);
    (void)engine.scene().setParent(telescope, obsDome);
    (void)engine.scene().setLocalTransform(telescope, math::compose(math::translation(math::Vector3{{0.0, 1.9, -2.1}}),
                                                                    math::rotation(math::RotationAxis::X, -0.95)));

    const auto sensorRing =
        createMeshObject(engine, makeCylinder(3.42, 3.42, 0.16, 48), brassMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(sensorRing, render::ShadingModel::Gouraud);
    (void)engine.scene().setShadowOptions(sensorRing, false, false);

    // Вторая вышка связи с вращающимся радаром справа от обсерватории
    const auto radarMast = createMeshObject(engine, makeCylinder(0.32, 0.18, 5.8, 8), steelRedMat);
    (void)engine.scene().setShading(radarMast, render::ShadingModel::Gouraud);
    (void)engine.scene().setLocalTransform(radarMast, math::translation(math::Vector3{{10.5, 5.2, 13.5}}));
    colliders.push_back({9.8, 11.2, 12.8, 14.2});

    const auto radarDish =
        createMeshObject(engine, makeParabolicDish(1.75, 0.75, 6, 16), domeMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(radarDish, render::ShadingModel::Gouraud);
    (void)engine.scene().setBackfaceCulling(radarDish, false);

    const auto dishFeedRod =
        createMeshObject(engine, makeCylinder(0.06, 0.04, 1.35, 6), darkMetalMat, core::Mobility::Dynamic);
    (void)engine.scene().setParent(dishFeedRod, radarDish);
    (void)engine.scene().setLocalTransform(dishFeedRod, math::translation(math::Vector3{{0.0, 0.68, 0.0}}));

    const auto dishFeedTip = engine.create<objects::ObjectType::Sphere3>({.radius = 0.12}, core::Mobility::Dynamic);
    (void)engine.scene().setShadowOptions(dishFeedTip, false, false);
    (void)engine.scene().setMaterial(dishFeedTip, cyanBeaconMat);
    (void)engine.scene().setParent(dishFeedTip, radarDish);
    (void)engine.scene().setLocalTransform(dishFeedTip, math::translation(math::Vector3{{0.0, 1.38, 0.0}}));

    // 6. Игрок от первого лица (панорамный вид с южного края на ущелье, мост, солнечные батареи и обсерваторию)
    double playerX = -4.0;
    double playerZ = -10.0;
    double playerY = walkableHeightAt(playerX, playerZ);
    double velocityY = 0.0;
    bool isGrounded = true;
    double lookYaw = 0.10;
    double lookPitch = 0.04;
    double animTime = 0.0;
    double headBobTimer = 0.0;
    bool sunsetMode = true;
    bool wireframeMode = false;
    bool fWasDown = false;
    bool vWasDown = false;

    const auto updateScene = [&](double dt) {
        const auto& input = engine.input();
        if (input.isKeyPressed(window::KeyCode::Escape)) {
            engine.stop();
            return;
        }

        const bool fDown = input.isKeyPressed(window::KeyCode::F);
        if (fDown && !fWasDown) {
            sunsetMode = !sunsetMode;
        }
        fWasDown = fDown;

        const bool vDown = input.isKeyPressed(window::KeyCode::V);
        if (vDown && !vWasDown) {
            wireframeMode = !wireframeMode;
            (void)engine.scene().setWireframe(obsDome, wireframeMode);
            (void)engine.scene().setWireframe(radarDish, wireframeMode);
        }
        vWasDown = vDown;

        lookYaw += input.mouseDeltaX() * 0.0045;
        lookPitch = std::clamp(lookPitch - input.mouseDeltaY() * 0.0045, -1.2, 1.15);
        double keyTurn = 0.0;
        if (input.isKeyPressed(window::KeyCode::Q) || input.isKeyPressed(window::KeyCode::Left)) {
            keyTurn -= 1.0;
        }
        if (input.isKeyPressed(window::KeyCode::E) || input.isKeyPressed(window::KeyCode::Right)) {
            keyTurn += 1.0;
        }
        lookYaw += keyTurn * 1.9 * dt;

        double moveForward = 0.0;
        double moveStrafe = 0.0;
        if (input.isKeyPressed(window::KeyCode::W) || input.isKeyPressed(window::KeyCode::Up)) {
            moveForward += 1.0;
        }
        if (input.isKeyPressed(window::KeyCode::S) || input.isKeyPressed(window::KeyCode::Down)) {
            moveForward -= 1.0;
        }
        if (input.isKeyPressed(window::KeyCode::D)) {
            moveStrafe += 1.0;
        }
        if (input.isKeyPressed(window::KeyCode::A)) {
            moveStrafe -= 1.0;
        }

        const bool sprinting = input.isKeyPressed(window::KeyCode::LeftShift);
        const double moveSpeed = sprinting ? 8.8 : 5.2;
        const double dirLen = std::hypot(moveForward, moveStrafe);
        if (dirLen > 1e-6) {
            const double normF = moveForward / dirLen;
            const double normS = moveStrafe / dirLen;
            const double sinY = std::sin(lookYaw);
            const double cosY = std::cos(lookYaw);
            const double stepDist = moveSpeed * dt;
            playerX += (sinY * normF + cosY * normS) * stepDist;
            playerZ += (cosY * normF - sinY * normS) * stepDist;
            headBobTimer += dt * (sprinting ? 12.0 : 8.5);
        }

        playerX = std::clamp(playerX, -28.0, 28.0);
        playerZ = std::clamp(playerZ, -26.0, 28.0);

        constexpr double playerRadius = 0.42;
        for (const auto& col : colliders) {
            const double closestX = std::clamp(playerX, col.minX, col.maxX);
            const double closestZ = std::clamp(playerZ, col.minZ, col.maxZ);
            const double dx = playerX - closestX;
            const double dz = playerZ - closestZ;
            const double dist = std::hypot(dx, dz);
            if (dist < playerRadius) {
                if (dist > 1e-6) {
                    const double push = (playerRadius - dist) / dist;
                    playerX += dx * push;
                    playerZ += dz * push;
                } else {
                    playerZ = col.minZ - playerRadius;
                }
            }
        }

        if (isGrounded && input.isKeyPressed(window::KeyCode::Space)) {
            velocityY = 6.5;
            isGrounded = false;
        }
        velocityY -= 16.5 * dt;
        playerY += velocityY * dt;
        const double floorY = walkableHeightAt(playerX, playerZ);
        if (playerY <= floorY) {
            playerY = floorY;
            velocityY = 0.0;
            isGrounded = true;
        } else {
            isGrounded = false;
        }

        // Анимация купола обсерватории, сенсорного обруча и радара
        animTime += dt;
        (void)engine.scene().setLocalTransform(
            obsDome, math::compose(math::translation(math::Vector3{{0.0, 5.45, 16.5}}),
                                   math::rotation(math::RotationAxis::Y, -0.45 + std::sin(animTime * 0.45) * 0.85)));
        (void)engine.scene().setLocalTransform(sensorRing, math::translation(math::Vector3{{0.0, 5.50, 16.5}}));
        (void)engine.scene().setLocalTransform(
            radarDish, math::compose(math::translation(math::Vector3{{10.5, 8.2, 13.5}}),
                                     math::compose(math::rotation(math::RotationAxis::Y, animTime * 0.9),
                                                   math::rotation(math::RotationAxis::X, -0.9))));

        // Камера от первого лица
        const double bobOffset = isGrounded ? std::sin(headBobTimer) * 0.035 : 0.0;
        const math::Vector3 eye{{playerX, playerY + 1.72 + bobOffset, playerZ}};
        const double cosP = std::cos(lookPitch);
        const math::Vector3 forward{{
            std::sin(lookYaw) * cosP,
            std::sin(lookPitch),
            std::cos(lookYaw) * cosP,
        }};
        engine.setCamera(render::PerspectiveCamera3{
            .worldToView = lookAt(eye, eye + forward),
            .verticalFieldOfView = 0.90,
            .nearPlane = 0.10,
            .farPlane = 145.0,
        });

        // Закатное или полуденное освещение, тени и атмосферная дымка каньона
        if (sunsetMode) {
            engine.setLighting({
                .ambient = {.color = {0.72, 0.56, 0.72, 1.0}, .intensity = 0.58},
                .directional =
                    {
                        {.direction = normalized(math::Vector3{{-0.58, -0.42, -0.69}}),
                         .color = {1.0, 0.76, 0.48, 1.0},
                         .intensity = 1.02},
                        {.direction = normalized(math::Vector3{{0.5, -0.35, 0.6}}),
                         .color = {0.42, 0.52, 0.88, 1.0},
                         .intensity = 0.24},
                    },
                .shadows =
                    {
                        .enabled = true,
                        .resolution = 2048,
                        .extent = 44.0,
                        .center = {{0.0, 3.0, 3.0}},
                        .depthRange = 120.0,
                        .bias = 0.08,
                        .minVisibility = 0.30,
                    },
                .fog =
                    {
                        .enabled = true,
                        .color = {0.86, 0.54, 0.36, 1.0},
                        .start = 42.0,
                        .end = 115.0,
                    },
                .sky =
                    {
                        .enabled = true,
                        .zenithColor = {0.11, 0.14, 0.28, 1.0},
                        .horizonColor = {0.94, 0.58, 0.34, 1.0},
                        .groundColor = {0.42, 0.24, 0.20, 1.0},
                        .sunColor = {1.0, 0.86, 0.56, 1.0},
                        .sunGlow = 0.85,
                    },
            });
        } else {
            engine.setLighting({
                .ambient = {.color = {0.76, 0.84, 0.98, 1.0}, .intensity = 0.38},
                .directional =
                    {
                        {.direction = normalized(math::Vector3{{-0.35, -0.88, -0.32}}),
                         .color = {1.0, 0.96, 0.88, 1.0},
                         .intensity = 0.95},
                        {.direction = normalized(math::Vector3{{0.5, -0.3, 0.6}}),
                         .color = {0.48, 0.68, 0.98, 1.0},
                         .intensity = 0.22},
                    },
                .shadows =
                    {
                        .enabled = true,
                        .resolution = 2048,
                        .extent = 44.0,
                        .center = {{0.0, 3.0, 3.0}},
                        .depthRange = 120.0,
                        .bias = 0.08,
                        .minVisibility = 0.30,
                    },
                .fog =
                    {
                        .enabled = true,
                        .color = {0.78, 0.84, 0.90, 1.0},
                        .start = 44.0,
                        .end = 115.0,
                    },
                .sky =
                    {
                        .enabled = true,
                        .zenithColor = {0.20, 0.48, 0.84, 1.0},
                        .horizonColor = {0.78, 0.86, 0.92, 1.0},
                        .groundColor = {0.52, 0.36, 0.28, 1.0},
                        .sunColor = {1.0, 0.96, 0.84, 1.0},
                        .sunGlow = 0.60,
                    },
            });
        }
    };

    updateScene(0.0);
    engine.onUpdate([&](double dt) { updateScene(dt); });

    engine.step(0.0);
    if (!screenshotPath.empty()) {
        auto frame = engine.captureFrame();
        if (!frame) {
            throw std::runtime_error("Cannot capture scene frame");
        }
        savePpm(*frame, screenshotPath);
    }
    if (continuous) {
        engine.run();
    } else {
        for (int step = 0; step < 10 && engine.window().isOpen(); ++step) {
            engine.step(1.0 / 60.0);
        }
    }

    std::cout << "Finished Canyon Observatory successfully.\n";
    return 0;
}
