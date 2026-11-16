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

std::shared_ptr<const image::ImageBuffer> createMarbleTileTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create marble texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const bool alt = ((u < 0.5) != (v < 0.5));
            const double lu = u < 0.5 ? u * 2.0 : (u - 0.5) * 2.0;
            const double lv = v < 0.5 ? v * 2.0 : (v - 0.5) * 2.0;
            const double seam = std::min({lu, 1.0 - lu, lv, 1.0 - lv});
            const double vein = 0.03 * std::sin((u * 3.0 + v * 2.0) * std::numbers::pi * 2.0) +
                                0.02 * std::cos((u * 5.0 - v * 4.0) * std::numbers::pi * 2.0);
            double r = (alt ? 0.90 : 0.84) + vein;
            double g = (alt ? 0.86 : 0.80) + vein * 0.9;
            double b = (alt ? 0.77 : 0.71) + vein * 0.7;
            if (seam < 0.045) {
                r *= 0.76;
                g *= 0.75;
                b *= 0.72;
            }
            writeRgba(*buf, x, y, r, g, b);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createGrassTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create grass texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double wave = 0.012 * std::sin(u * std::numbers::pi * 6.0) * std::cos(v * std::numbers::pi * 6.0) +
                                0.008 * std::sin((u + v) * std::numbers::pi * 10.0);
            writeRgba(*buf, x, y, 0.30 + wave * 0.7, 0.52 + wave, 0.22 + wave * 0.4);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createWaterTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create water texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double ripple = 0.5 + 0.5 * std::sin((u * 4.0 + std::sin(v * 6.283)) * std::numbers::pi * 2.0) *
                                            std::cos(v * std::numbers::pi * 4.0);
            const double highlight = 0.18 * std::pow(ripple, 3.0);
            writeRgba(*buf, x, y, 0.08 + 0.20 * highlight, 0.48 + 0.26 * highlight, 0.66 + 0.22 * highlight);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

std::shared_ptr<const image::ImageBuffer> createTerracottaTexture(std::size_t size) {
    auto buf = image::ImageBuffer::create({.width = size, .height = size});
    if (!buf) {
        throw std::runtime_error("Failed to create terracotta texture");
    }
    for (std::size_t y = 0; y < size; ++y) {
        const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(size);
        for (std::size_t x = 0; x < size; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(size);
            const double rib = 0.08 * std::sin(u * std::numbers::pi * 8.0);
            const double rowStep = std::fmod(v * 4.0, 1.0) < 0.12 ? -0.10 : 0.0;
            writeRgba(*buf, x, y, 0.72 + rib + rowStep, 0.30 + rib * 0.6 + rowStep * 0.6, 0.18 + rib * 0.4);
        }
    }
    return std::make_shared<const image::ImageBuffer>(std::move(*buf));
}

struct MeshData {
    std::vector<math::Point3> positions;
    std::vector<objects::TriangleIndices> triangles;
    std::vector<math::Point2> texCoords;
};

// Рельеф острова: ровное центральное плато с мягкими холмами по краям и спуском к морю
double islandElevation(double x, double z) {
    const double r = std::hypot(x, z);
    if (r <= 18.5) {
        return 0.0;
    }
    if (r <= 25.0) {
        const double t = (r - 18.5) / 6.5;
        const double hillMask = std::sin(t * std::numbers::pi);
        const double angular = 0.55 + 0.45 * std::cos(std::atan2(z, x) * 3.0 - 0.6);
        return hillMask * angular * 2.1;
    }
    const double drop = std::clamp((r - 25.0) / 9.0, 0.0, 1.0);
    return -drop * 2.4;
}

MeshData makeIslandTerrain(double halfSize, int subdivisions, double uvRepeats) {
    MeshData data;
    const int stride = subdivisions + 1;
    for (int iz = 0; iz <= subdivisions; ++iz) {
        const double tz = static_cast<double>(iz) / static_cast<double>(subdivisions);
        const double z = -halfSize + tz * (2.0 * halfSize);
        for (int ix = 0; ix <= subdivisions; ++ix) {
            const double tx = static_cast<double>(ix) / static_cast<double>(subdivisions);
            const double x = -halfSize + tx * (2.0 * halfSize);
            data.positions.push_back({{x, islandElevation(x, z), z}});
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

MeshData makeTexturedBox(double sx, double sy, double sz, double uvScale = 0.5) {
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
    addFace({{-hx, -hy, -hz}}, {{-hx, hy, -hz}}, {{hx, hy, -hz}}, {{hx, -hy, -hz}}, sx * uvScale, sy * uvScale);
    addFace({{hx, -hy, hz}}, {{hx, hy, hz}}, {{-hx, hy, hz}}, {{-hx, -hy, hz}}, sx * uvScale, sy * uvScale);
    addFace({{-hx, -hy, hz}}, {{-hx, hy, hz}}, {{-hx, hy, -hz}}, {{-hx, -hy, -hz}}, sz * uvScale, sy * uvScale);
    addFace({{hx, -hy, -hz}}, {{hx, hy, -hz}}, {{hx, hy, hz}}, {{hx, -hy, hz}}, sz * uvScale, sy * uvScale);
    addFace({{-hx, hy, -hz}}, {{-hx, hy, hz}}, {{hx, hy, hz}}, {{hx, hy, -hz}}, sx * uvScale, sz * uvScale);
    addFace({{-hx, -hy, hz}}, {{-hx, -hy, -hz}}, {{hx, -hy, -hz}}, {{hx, -hy, hz}}, sx * uvScale, sz * uvScale);
    return data;
}

MeshData makeTaperedCylinder(double bottomRadius, double topRadius, double height, int sides) {
    MeshData data;
    const double halfH = height * 0.5;
    for (int i = 0; i <= sides; ++i) {
        const double angle = (2.0 * std::numbers::pi * static_cast<double>(i)) / static_cast<double>(sides);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double u = static_cast<double>(i) / static_cast<double>(sides);
        data.positions.push_back({{bottomRadius * c, -halfH, bottomRadius * s}});
        data.texCoords.push_back({{u * 2.0, 1.0}});
        data.positions.push_back({{topRadius * c, halfH, topRadius * s}});
        data.texCoords.push_back({{u * 2.0, 0.0}});
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
    data.texCoords.push_back({{0.5, 0.5}});
    const auto topCenter = static_cast<std::uint32_t>(data.positions.size());
    data.positions.push_back({{0.0, halfH, 0.0}});
    data.texCoords.push_back({{0.5, 0.5}});
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

MeshData makeTorusRing(double majorRadius, double minorRadius, int majorSegments, int minorSegments) {
    MeshData data;
    const int ringStride = minorSegments + 1;
    for (int i = 0; i <= majorSegments; ++i) {
        const double u = (2.0 * std::numbers::pi * static_cast<double>(i)) / static_cast<double>(majorSegments);
        const double cosU = std::cos(u);
        const double sinU = std::sin(u);
        for (int j = 0; j <= minorSegments; ++j) {
            const double v = (2.0 * std::numbers::pi * static_cast<double>(j)) / static_cast<double>(minorSegments);
            const double cosV = std::cos(v);
            const double sinV = std::sin(v);
            const double rad = majorRadius + minorRadius * cosV;
            data.positions.push_back({{rad * cosU, minorRadius * sinV, rad * sinU}});
            data.texCoords.push_back(
                {{static_cast<double>(i) / majorSegments, static_cast<double>(j) / minorSegments}});
        }
    }
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            const auto a = static_cast<std::uint32_t>(i * ringStride + j);
            const auto b = static_cast<std::uint32_t>((i + 1) * ringStride + j);
            const auto c = static_cast<std::uint32_t>((i + 1) * ringStride + j + 1);
            const auto d = static_cast<std::uint32_t>(i * ringStride + j + 1);
            data.triangles.push_back({{a, d, c}});
            data.triangles.push_back({{a, c, b}});
        }
    }
    return data;
}

MeshData makePitchedRoof(double width, double height, double depth) {
    MeshData data;
    const double hw = width * 0.5;
    const double hd = depth * 0.5;
    const auto addTri = [&](const math::Point3& a, const math::Point3& b, const math::Point3& c, const math::Point2& ua,
                            const math::Point2& ub, const math::Point2& uc) {
        const auto base = static_cast<std::uint32_t>(data.positions.size());
        data.positions.push_back(a);
        data.positions.push_back(b);
        data.positions.push_back(c);
        data.texCoords.push_back(ua);
        data.texCoords.push_back(ub);
        data.texCoords.push_back(uc);
        data.triangles.push_back({{base, base + 1, base + 2}});
    };
    const auto addQuad = [&](const math::Point3& bl, const math::Point3& tl, const math::Point3& tr,
                             const math::Point3& br, double uMax, double vMax) {
        const auto base = static_cast<std::uint32_t>(data.positions.size());
        data.positions.push_back(bl);
        data.positions.push_back(tl);
        data.positions.push_back(tr);
        data.positions.push_back(br);
        data.texCoords.push_back({{0.0, vMax}});
        data.texCoords.push_back({{0.0, 0.0}});
        data.texCoords.push_back({{uMax, 0.0}});
        data.texCoords.push_back({{uMax, vMax}});
        data.triangles.push_back({{base, base + 1, base + 2}});
        data.triangles.push_back({{base, base + 2, base + 3}});
    };

    // Фронтоны (-Z и +Z) оставляем светлым мрамором, а скаты крыши выносим отдельной сеткой или группой
    addTri({{-hw, 0.0, -hd}}, {{0.0, height, -hd}}, {{hw, 0.0, -hd}}, {{0.0, 1.0}}, {{0.5, 0.0}}, {{1.0, 1.0}});
    addTri({{hw, 0.0, hd}}, {{0.0, height, hd}}, {{-hw, 0.0, hd}}, {{0.0, 1.0}}, {{0.5, 0.0}}, {{1.0, 1.0}});
    addQuad({{-hw, 0.0, -hd}}, {{hw, 0.0, -hd}}, {{hw, 0.0, hd}}, {{-hw, 0.0, hd}}, 2.0, 2.0);
    return data;
}

MeshData makeRoofSlopes(double width, double height, double depth) {
    MeshData data;
    const double hw = width * 0.5;
    const double hd = depth * 0.5;
    const auto addQuad = [&](const math::Point3& bl, const math::Point3& tl, const math::Point3& tr,
                             const math::Point3& br, double uMax, double vMax) {
        const auto base = static_cast<std::uint32_t>(data.positions.size());
        data.positions.push_back(bl);
        data.positions.push_back(tl);
        data.positions.push_back(tr);
        data.positions.push_back(br);
        data.texCoords.push_back({{0.0, vMax}});
        data.texCoords.push_back({{0.0, 0.0}});
        data.texCoords.push_back({{uMax, 0.0}});
        data.texCoords.push_back({{uMax, vMax}});
        data.triangles.push_back({{base, base + 1, base + 2}});
        data.triangles.push_back({{base, base + 2, base + 3}});
    };
    addQuad({{-hw, 0.0, hd}}, {{0.0, height, hd}}, {{0.0, height, -hd}}, {{-hw, 0.0, -hd}}, 6.0, 2.5);
    addQuad({{hw, 0.0, -hd}}, {{0.0, height, -hd}}, {{0.0, height, hd}}, {{hw, 0.0, hd}}, 6.0, 2.5);
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

struct CylinderCollider {
    double x{};
    double z{};
    double radius{};
};

double walkableHeightAt(double x, double z) {
    // 1. Храм на северной части плато (x in [-6.8, 6.8], z in [7.2, 16.8])
    const double dxTemple = std::abs(x);
    if (dxTemple <= 6.8 && z >= 7.2 && z <= 16.8) {
        if (dxTemple <= 5.4 && z >= 8.6 && z <= 15.4) {
            return 0.78;
        }
        if (dxTemple <= 6.1 && z >= 7.9 && z <= 16.1) {
            return 0.52;
        }
        return 0.26;
    }

    // 2. Мостики и центральный подиум астролябии посреди бассейна (x in [-5.0, 5.0], z in [-5.8, 4.2])
    if (std::abs(x) <= 2.3 && std::abs(z + 0.8) <= 2.3) {
        return 0.28;
    }
    if (std::abs(x) <= 1.2 && z >= -6.0 && z <= 4.4) {
        return 0.16;
    }
    if (std::abs(z + 0.8) <= 1.2 && x >= -5.2 && x <= 5.2) {
        return 0.16;
    }
    // Бассейн с неглубокой водой
    if (std::abs(x) <= 4.5 && z >= -5.3 && z <= 3.7) {
        return -0.22;
    }
    // Мраморная площадь вокруг бассейна
    if (std::abs(x) <= 10.5 && z >= -11.5 && z <= 8.0) {
        return 0.06;
    }

    return std::max(-0.5, islandElevation(x, z));
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

    std::cout << "Starting Huynity: Sunlit Sanctuary (" << (continuous ? "interactive mode" : "CI mode") << ")...\n";
    if (continuous) {
        std::cout << "Controls: WASD move, Mouse/touchpad motion or Q/E/Arrows look, Space jump, Shift sprint, "
                     "F sun angle, V wireframe scan, Esc exit.\n";
    }

    huynity::Engine engine({.windowSize = {1280, 720},
                            .windowTitle = "Huynity: Sunlit Sanctuary",
                            .forceHeadless = !continuous && screenshotPath.empty()});

    engine.setHudEnabled(false);

    // Текстуры и материалы
    const auto marbleTex = createMarbleTileTexture(64);
    const auto grassTex = createGrassTexture(64);
    const auto waterTex = createWaterTexture(64);
    const auto roofTex = createTerracottaTexture(64);

    const auto grassMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = grassTex,
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto seaMat = std::make_shared<const render::Material>(render::Material{
        .color = {0.82, 0.94, 1.0, 1.0},
        .texture = waterTex,
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto poolWaterMat = std::make_shared<const render::Material>(render::Material{
        .color = {0.95, 1.0, 1.0, 1.0},
        .texture = waterTex,
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto marbleFloorMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = marbleTex,
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto marbleColumnMat = std::make_shared<const render::Material>(render::Color{0.93, 0.90, 0.83, 1.0});
    const auto limestoneMat = std::make_shared<const render::Material>(render::Color{0.84, 0.79, 0.70, 1.0});
    const auto terracottaMat = std::make_shared<const render::Material>(render::Material{
        .color = {1.0, 1.0, 1.0, 1.0},
        .texture = roofTex,
        .filter = render::TextureFilter::Bilinear,
        .wrap = render::TextureWrap::Repeat,
    });
    const auto goldMat = std::make_shared<const render::Material>(render::Color{0.94, 0.74, 0.24, 1.0});
    const auto bronzeMat = std::make_shared<const render::Material>(render::Color{0.68, 0.48, 0.24, 1.0});
    const auto emberMat =
        std::make_shared<const render::Material>(render::Material{.color = {1.0, 0.78, 0.26, 1.0}, .unlit = true});
    const auto trunkMat = std::make_shared<const render::Material>(render::Color{0.42, 0.28, 0.17, 1.0});
    const auto foliageDarkMat = std::make_shared<const render::Material>(render::Color{0.16, 0.42, 0.21, 1.0});
    const auto foliageBrightMat = std::make_shared<const render::Material>(render::Color{0.22, 0.52, 0.27, 1.0});

    // 1. Остров и окружающее море
    const auto islandId = createMeshObject(engine, makeIslandTerrain(34.0, 28, 16.0), grassMat);
    (void)engine.scene().setShading(islandId, render::ShadingModel::Gouraud);
    (void)engine.scene().setShadowOptions(islandId, false, true);

    const auto seaId = createMeshObject(engine, makePlaneMesh(105.0, 105.0, 16, 16, 26.0, 26.0), seaMat);
    (void)engine.scene().setLocalTransform(seaId, math::translation(math::Vector3{{0.0, -1.35, 0.0}}));
    (void)engine.scene().setShadowOptions(seaId, false, true);

    // 2. Главная мраморная площадь и утопленный бассейн
    const auto plazaId = createMeshObject(engine, makePlaneMesh(10.5, 10.2, 8, 8, 7.0, 7.0), marbleFloorMat);
    (void)engine.scene().setLocalTransform(plazaId, math::translation(math::Vector3{{0.0, 0.04, -1.5}}));
    (void)engine.scene().setShadowOptions(plazaId, false, true);

    const auto poolWaterId = createMeshObject(engine, makePlaneMesh(4.5, 4.5, 6, 6, 3.0, 3.0), poolWaterMat);
    (void)engine.scene().setLocalTransform(poolWaterId, math::translation(math::Vector3{{0.0, 0.08, -0.8}}));
    (void)engine.scene().setShadowOptions(poolWaterId, false, true);

    // Бортики бассейна и пешеходные мостики
    const auto curbN = createMeshObject(engine, makeTexturedBox(9.6, 0.22, 0.55), limestoneMat);
    (void)engine.scene().setLocalTransform(curbN, math::translation(math::Vector3{{0.0, 0.11, 3.9}}));
    const auto curbS = createMeshObject(engine, makeTexturedBox(9.6, 0.22, 0.55), limestoneMat);
    (void)engine.scene().setLocalTransform(curbS, math::translation(math::Vector3{{0.0, 0.11, -5.5}}));
    const auto curbW = createMeshObject(engine, makeTexturedBox(0.55, 0.22, 9.4), limestoneMat);
    (void)engine.scene().setLocalTransform(curbW, math::translation(math::Vector3{{-4.65, 0.11, -0.8}}));
    const auto curbE = createMeshObject(engine, makeTexturedBox(0.55, 0.22, 9.4), limestoneMat);
    (void)engine.scene().setLocalTransform(curbE, math::translation(math::Vector3{{4.65, 0.11, -0.8}}));

    const auto bridgeNS = createMeshObject(engine, makeTexturedBox(2.2, 0.14, 10.2), marbleFloorMat);
    (void)engine.scene().setLocalTransform(bridgeNS, math::translation(math::Vector3{{0.0, 0.13, -0.8}}));
    const auto bridgeEW = createMeshObject(engine, makeTexturedBox(10.2, 0.14, 2.2), marbleFloorMat);
    (void)engine.scene().setLocalTransform(bridgeEW, math::translation(math::Vector3{{0.0, 0.13, -0.8}}));
    const auto altarBase = createMeshObject(engine, makeTexturedBox(4.4, 0.28, 4.4), marbleFloorMat);
    (void)engine.scene().setLocalTransform(altarBase, math::translation(math::Vector3{{0.0, 0.14, -0.8}}));

    // 3. Небесная астролябия в центре площади
    std::vector<CylinderCollider> colliders;
    const auto astrolabePedestal = createMeshObject(engine, makeTaperedCylinder(0.85, 0.72, 0.80, 12), limestoneMat);
    (void)engine.scene().setShading(astrolabePedestal, render::ShadingModel::Gouraud);
    (void)engine.scene().setLocalTransform(astrolabePedestal, math::translation(math::Vector3{{0.0, 0.68, -0.8}}));
    colliders.push_back({0.0, -0.8, 1.05});

    const auto ringMesh = makeTorusRing(1.18, 0.075, 22, 8);
    const auto outerRing = createMeshObject(engine, ringMesh, goldMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(outerRing, render::ShadingModel::Gouraud);

    const auto innerRingMesh = makeTorusRing(0.82, 0.065, 20, 8);
    const auto innerRing = createMeshObject(engine, innerRingMesh, bronzeMat, core::Mobility::Dynamic);
    (void)engine.scene().setShading(innerRing, render::ShadingModel::Gouraud);

    const auto sunCore = createMeshObject(engine, makeTaperedCylinder(0.13, 0.04, 1.1, 16), bronzeMat);
    (void)engine.scene().setShading(sunCore, render::ShadingModel::Gouraud);
    (void)engine.scene().setShadowOptions(sunCore, false, false);
    (void)engine.scene().setLocalTransform(sunCore, math::translation(math::Vector3{{0.0, 2.35, -0.8}}));
    (void)engine.scene().setMaterial(sunCore, bronzeMat);

    const auto axisSupport = createMeshObject(engine, makeTaperedCylinder(0.10, 0.10, 1.8, 16), bronzeMat);
    (void)engine.scene().setLocalTransform(axisSupport, math::translation(math::Vector3{{0, 1.85, -0.8}}));

    // 4. Классический храм с колоннадой и ступенчатым стилобатом (северная часть)
    const auto step1 = createMeshObject(engine, makeTexturedBox(13.6, 0.26, 9.6), limestoneMat);
    (void)engine.scene().setLocalTransform(step1, math::translation(math::Vector3{{0.0, 0.13, 12.0}}));
    const auto step2 = createMeshObject(engine, makeTexturedBox(12.2, 0.26, 8.2), marbleFloorMat);
    (void)engine.scene().setLocalTransform(step2, math::translation(math::Vector3{{0.0, 0.39, 12.0}}));
    const auto step3 = createMeshObject(engine, makeTexturedBox(10.8, 0.26, 6.8), marbleFloorMat);
    (void)engine.scene().setLocalTransform(step3, math::translation(math::Vector3{{0.0, 0.65, 12.0}}));

    // Шаблонная колонна для переиспользования в сцене
    const auto shaftTemplate = createMeshObject(engine, makeTaperedCylinder(0.36, 0.30, 3.6, 12), marbleColumnMat);
    (void)engine.scene().setShading(shaftTemplate, render::ShadingModel::Gouraud);
    (void)engine.scene().setVisibility(shaftTemplate, false);

    const auto colBaseTemplate = createMeshObject(engine, makeTexturedBox(0.92, 0.24, 0.92), limestoneMat);
    (void)engine.scene().setVisibility(colBaseTemplate, false);

    const auto colCapTemplate = createMeshObject(engine, makeTexturedBox(0.92, 0.22, 0.92), limestoneMat);
    (void)engine.scene().setVisibility(colCapTemplate, false);

    const std::array<double, 4> frontColX{-4.4, -1.5, 1.5, 4.4};
    std::vector<math::Point2> columnPositions;
    for (const double cx : frontColX) {
        columnPositions.push_back({{cx, 9.35}});
        columnPositions.push_back({{cx, 14.65}});
    }
    columnPositions.push_back({{-4.4, 12.0}});
    columnPositions.push_back({{4.4, 12.0}});

    for (const auto& pos : columnPositions) {
        const double cx = pos[0];
        const double cz = pos[1];
        if (const auto base = engine.scene().createInstance(colBaseTemplate)) {
            (void)engine.scene().setVisibility(*base, true);
            (void)engine.scene().setLocalTransform(*base, math::translation(math::Vector3{{cx, 0.90, cz}}));
        }
        if (const auto shaft = engine.scene().createInstance(shaftTemplate)) {
            (void)engine.scene().setVisibility(*shaft, true);
            (void)engine.scene().setLocalTransform(*shaft, math::translation(math::Vector3{{cx, 2.82, cz}}));
        }
        if (const auto cap = engine.scene().createInstance(colCapTemplate)) {
            (void)engine.scene().setVisibility(*cap, true);
            (void)engine.scene().setLocalTransform(*cap, math::translation(math::Vector3{{cx, 4.72, cz}}));
        }
        colliders.push_back({cx, cz, 0.65});
    }

    // Антаблемент, мраморный фронтон и двускатная терракотовая крыша
    const auto architrave = createMeshObject(engine, makeTexturedBox(10.6, 0.56, 6.8), limestoneMat);
    (void)engine.scene().setLocalTransform(architrave, math::translation(math::Vector3{{0.0, 5.10, 12.0}}));
    const auto pedimentId = createMeshObject(engine, makePitchedRoof(10.4, 1.95, 6.6), limestoneMat);
    (void)engine.scene().setLocalTransform(pedimentId, math::translation(math::Vector3{{0.0, 5.38, 12.0}}));
    const auto roofId = createMeshObject(engine, makeRoofSlopes(11.6, 2.15, 7.6), terracottaMat);
    (void)engine.scene().setBackfaceCulling(roofId, false);
    (void)engine.scene().setLocalTransform(roofId, math::translation(math::Vector3{{0.0, 5.42, 12.0}}));

    // Алтарная стела внутри храма
    const auto shrineBlock = createMeshObject(engine, makeTexturedBox(1.8, 1.4, 1.0), marbleFloorMat);
    (void)engine.scene().setLocalTransform(shrineBlock, math::translation(math::Vector3{{0.0, 1.48, 12.6}}));
    colliders.push_back({0.0, 12.6, 1.1});

    // 5. Южные входные ворота и светильники-жаровни
    for (const double side : {-3.0, 3.0}) {
        const auto pier = createMeshObject(engine, makeTexturedBox(1.1, 3.6, 1.1), limestoneMat);
        (void)engine.scene().setLocalTransform(pier, math::translation(math::Vector3{{side, 1.80, -10.0}}));
        const auto cap = createMeshObject(engine, makeTexturedBox(1.35, 0.25, 1.35), marbleFloorMat);
        (void)engine.scene().setLocalTransform(cap, math::translation(math::Vector3{{side, 3.72, -10.0}}));
        const auto ember = engine.create<objects::ObjectType::Sphere3>({.radius = 0.24});
        (void)engine.scene().setShading(ember, render::ShadingModel::Gouraud);
        (void)engine.scene().setShadowOptions(ember, false, false);
        (void)engine.scene().setLocalTransform(ember, math::translation(math::Vector3{{side, 4.08, -10.0}}));
        (void)engine.scene().setMaterial(ember, emberMat);
        colliders.push_back({side, -10.0, 0.80});
    }

    // Обелиски по бокам площади
    for (const double side : {-8.5, 8.5}) {
        for (const double zPos : {-7.0, 4.0}) {
            const auto plinth = createMeshObject(engine, makeTexturedBox(1.1, 0.7, 1.1), limestoneMat);
            (void)engine.scene().setLocalTransform(plinth, math::translation(math::Vector3{{side, 0.35, zPos}}));
            const auto shaft = createMeshObject(engine, makeTaperedCylinder(0.42, 0.16, 2.8, 4), marbleColumnMat);
            (void)engine.scene().setLocalTransform(
                shaft, math::compose(math::translation(math::Vector3{{side, 2.1, zPos}}),
                                     math::rotation(math::RotationAxis::Y, std::numbers::pi * 0.25)));
            colliders.push_back({side, zPos, 0.80});
        }
    }

    // 6. Средиземноморские кипарисы и пинии (общая геометрия)
    const auto trunkTemplate = createMeshObject(engine, makeTaperedCylinder(0.24, 0.14, 2.6, 8), trunkMat);
    (void)engine.scene().setShading(trunkTemplate, render::ShadingModel::Gouraud);
    (void)engine.scene().setVisibility(trunkTemplate, false);

    const auto cypressTemplate = createMeshObject(engine, makeTaperedCylinder(0.95, 0.05, 4.8, 10), foliageDarkMat);
    (void)engine.scene().setShading(cypressTemplate, render::ShadingModel::Gouraud);
    (void)engine.scene().setVisibility(cypressTemplate, false);

    const auto pineCrownTemplate = createMeshObject(engine, makeTaperedCylinder(2.1, 1.1, 1.25, 10), foliageBrightMat);
    (void)engine.scene().setShading(pineCrownTemplate, render::ShadingModel::Gouraud);
    (void)engine.scene().setVisibility(pineCrownTemplate, false);

    struct TreeSpot {
        double x{};
        double z{};
        double scale{};
        bool isPine{};
    };
    const std::array<TreeSpot, 12> trees{{
        {-13.0, -9.0, 1.05, false},
        {-14.5, -1.5, 1.20, true},
        {-13.5, 6.5, 1.10, false},
        {-12.0, 14.0, 0.95, true},
        {13.0, -9.0, 1.00, false},
        {14.5, -1.0, 1.15, true},
        {13.5, 7.0, 1.05, false},
        {12.0, 14.5, 1.00, true},
        {-9.5, -15.0, 0.95, true},
        {9.5, -15.0, 1.05, false},
        {-8.0, 18.5, 1.15, true},
        {8.0, 18.5, 1.10, false},
    }};
    for (const auto& t : trees) {
        const double groundY = islandElevation(t.x, t.z);
        if (const auto trunk = engine.scene().createInstance(trunkTemplate)) {
            (void)engine.scene().setVisibility(*trunk, true);
            (void)engine.scene().setLocalTransform(
                *trunk, math::compose(math::translation(math::Vector3{{t.x, groundY + 1.2 * t.scale, t.z}}),
                                      math::scaling(math::Vector3{{t.scale, t.scale, t.scale}})));
        }
        if (t.isPine) {
            if (const auto crown = engine.scene().createInstance(pineCrownTemplate)) {
                (void)engine.scene().setVisibility(*crown, true);
                (void)engine.scene().setLocalTransform(
                    *crown, math::compose(math::translation(math::Vector3{{t.x, groundY + 2.8 * t.scale, t.z}}),
                                          math::scaling(math::Vector3{{t.scale, t.scale, t.scale}})));
            }
        } else {
            if (const auto cypress = engine.scene().createInstance(cypressTemplate)) {
                (void)engine.scene().setVisibility(*cypress, true);
                (void)engine.scene().setLocalTransform(
                    *cypress, math::compose(math::translation(math::Vector3{{t.x, groundY + 3.0 * t.scale, t.z}}),
                                            math::scaling(math::Vector3{{t.scale, t.scale, t.scale}})));
            }
        }
        colliders.push_back({t.x, t.z, 0.55 * t.scale});
    }

    // 9. Игрок от первого лица (свободное перемещение по острову, ступеням и площади)
    double playerX = -3.5;
    double playerZ = -9.0;
    double playerY = walkableHeightAt(playerX, playerZ);
    double velocityY = 0.0;
    bool isGrounded = true;
    double lookYaw = 0.14;
    double lookPitch = 0.10;
    double animTime = 0.0;
    double headBobTimer = 0.0;
    double sunPhase = 0.65;
    bool wireframeScan = false;
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
            sunPhase = (sunPhase < 1.0) ? 1.35 : 0.65;
        }
        fWasDown = fDown;

        const bool vDown = input.isKeyPressed(window::KeyCode::V);
        if (vDown && !vWasDown) {
            wireframeScan = !wireframeScan;
            (void)engine.scene().setWireframe(outerRing, wireframeScan);
            (void)engine.scene().setWireframe(innerRing, wireframeScan);
        }
        vWasDown = vDown;

        // Свободный обзор мышью или тачпадом без зажатой кнопки.
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
        const double moveSpeed = sprinting ? 8.2 : 4.8;
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

        // Ограничение береговой линией острова и скользящие коллизии с колоннами/деревьями
        const double distFromCenter = std::hypot(playerX, playerZ);
        if (distFromCenter > 29.0) {
            const double scale = 29.0 / distFromCenter;
            playerX *= scale;
            playerZ *= scale;
        }
        constexpr double playerRadius = 0.42;
        for (const auto& col : colliders) {
            const double dx = playerX - col.x;
            const double dz = playerZ - col.z;
            const double dist = std::hypot(dx, dz);
            const double minDist = col.radius + playerRadius;
            if (dist < minDist) {
                if (dist > 1e-6) {
                    const double push = minDist / dist;
                    playerX = col.x + dx * push;
                    playerZ = col.z + dz * push;
                } else {
                    playerZ = col.z - minDist;
                }
            }
        }

        // Прыжок и гравитация с плавным подъёмом по ступеням
        if (isGrounded && input.isKeyPressed(window::KeyCode::Space)) {
            velocityY = 6.2;
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

        // Медленное вращение механической астролябии
        animTime += dt;
        const auto astroBase = math::translation(math::Vector3{{0.0, 2.35, -0.8}});
        (void)engine.scene().setLocalTransform(
            outerRing, math::compose(astroBase, math::compose(math::rotation(math::RotationAxis::Y, animTime * 0.85),
                                                              math::rotation(math::RotationAxis::X, 0.45))));
        (void)engine.scene().setLocalTransform(
            innerRing, math::compose(astroBase, math::compose(math::rotation(math::RotationAxis::Z, -animTime * 1.15),
                                                              math::rotation(math::RotationAxis::X, 1.10))));

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
            .farPlane = 135.0,
        });

        // Область теней закреплена на площади и храме и не зависит от камеры.
        const math::Vector3 sunDir = normalized(math::Vector3{{-0.52 * sunPhase, -0.78, 0.42}});
        engine.setLighting({
            .ambient = {.color = {0.72, 0.84, 1.0, 1.0}, .intensity = 0.36},
            .directional =
                {
                    {.direction = sunDir, .color = {1.0, 0.95, 0.84, 1.0}, .intensity = 0.96},
                    {.direction = normalized(math::Vector3{{0.5, -0.25, -0.6}}),
                     .color = {0.52, 0.74, 0.98, 1.0},
                     .intensity = 0.22},
                },
            .shadows =
                {
                    .enabled = true,
                    .resolution = 2048,
                    .extent = 36.0,
                    .center = {{0.0, 3.0, 5.0}},
                    .depthRange = 100.0,
                    .bias = 0.08,
                    .minVisibility = 0.25,
                },
            .fog =
                {
                    .enabled = true,
                    .color = {0.76, 0.87, 0.95, 1.0},
                    .start = 40.0,
                    .end = 108.0,
                },
            .sky =
                {
                    .enabled = true,
                    .zenithColor = {0.18, 0.48, 0.86, 1.0},
                    .horizonColor = {0.76, 0.88, 0.96, 1.0},
                    .groundColor = {0.36, 0.56, 0.72, 1.0},
                    .sunColor = {1.0, 0.96, 0.82, 1.0},
                    .sunGlow = 0.65,
                },
        });
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

    std::cout << "Finished Sunlit Sanctuary successfully.\n";
    return 0;
}
