#include "huynity/renderer/software_renderer.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace render = huynity::render_contracts;
namespace image = huynity::image;
namespace math = huynity::math;
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
constexpr Rgba RED{255, 0, 0, 255};
constexpr Rgba GREEN{0, 255, 0, 255};
constexpr Rgba BLUE{0, 0, 255, 255};

// Тестовый mesh живёт в вызывающей области видимости; снимок ссылается на него span-ами.
struct TestMesh {
    std::vector<math::Point3> positions;
    std::vector<render::TriangleIndices> triangles;
    std::shared_ptr<const void> owner = std::make_shared<const int>(0);
};

render::Color color(double red, double green, double blue) {
    return {red, green, blue, 1};
}

std::shared_ptr<const render::Material> material(double red, double green, double blue) {
    return std::make_shared<const render::Material>(color(red, green, blue));
}

void addResource(render::RenderSnapshot& snapshot, const TestMesh& mesh) {
    render::MeshResource3 resource;
    resource.owner = mesh.owner;
    resource.positions = mesh.positions;
    resource.triangles = std::as_bytes(std::span{mesh.triangles});
    snapshot.resources3.push_back(resource);
}

render::RenderSnapshot makeSnapshot(std::size_t width, std::size_t height) {
    render::RenderSnapshot snapshot;
    snapshot.imageWidth = width;
    snapshot.imageHeight = height;
    snapshot.clearColor = color(0, 0, 0);
    snapshot.materials = {material(1, 0, 0), material(0, 1, 0), material(0, 0, 1)};
    return snapshot;
}

render::OrthographicCamera3 orthoCamera(double width, double height, double nearPlane, double farPlane) {
    return {math::Transform3{}, width, height, nearPlane, farPlane};
}

// Освещение без затенения для тестов геометрии: белый ambient единичной интенсивности.
render::Lighting3 unlitLighting() {
    return {{render::Color{1, 1, 1, 1}, 1}, {}};
}

// Лицевая грань по соглашению рендерера: нормаль cross(b - a, c - a) смотрит в -z.
TestMesh frontTriangle(double x0, double y0, double x1, double y1, double x2, double y2, double z) {
    return {{{x0, y0, z}, {x1, y1, z}, {x2, y2, z}}, {{0, 1, 2}}, {}};
}

TestMesh centerTriangle(double extent, double z) {
    return frontTriangle(-extent, -extent, 0, extent, extent, -extent, z);
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

std::size_t countColor(const image::ImageBuffer& framebuffer, const Rgba& target) {
    std::size_t count = 0;
    for (std::size_t y = 0; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0; x < framebuffer.width(); ++x) {
            if (pixel(framebuffer, x, y) == target) {
                ++count;
            }
        }
    }
    return count;
}

double colorCentroidX(const image::ImageBuffer& framebuffer, const Rgba& target) {
    double sum = 0;
    std::size_t count = 0;
    for (std::size_t y = 0; y < framebuffer.height(); ++y) {
        for (std::size_t x = 0; x < framebuffer.width(); ++x) {
            if (pixel(framebuffer, x, y) == target) {
                sum += static_cast<double>(x);
                ++count;
            }
        }
    }
    return count == 0 ? -1 : sum / static_cast<double>(count);
}

void clearColorFillsEmptySnapshot() {
    auto snapshot = makeSnapshot(32, 16);
    snapshot.clearColor = color(0, 0, 1);
    auto framebuffer = makeFramebuffer(32, 16);
    CHECK(renderer::render(snapshot, framebuffer).has_value());
    CHECK(countColor(framebuffer, BLUE) == 32 * 16);
}

void clearColorPreservesPaddingWithOddStride() {
    constexpr std::size_t width = 3;
    constexpr std::size_t height = 4;
    constexpr std::size_t stride = width * 4 + 1;
    constexpr std::byte padding{0xa5};
    auto framebuffer = image::ImageBuffer::create({.width = width, .height = height, .stride = stride});
    CHECK(framebuffer.has_value());
    auto bytes = framebuffer->bytes();
    std::fill(bytes.begin(), bytes.end(), padding);

    auto snapshot = makeSnapshot(width, height);
    snapshot.clearColor = {1, 0.5, 0.25, 0.75};
    CHECK(renderer::render(snapshot, *framebuffer).has_value());
    CHECK(countColor(*framebuffer, {255, 128, 64, 191}) == width * height);
    for (std::size_t y = 0; y < height; ++y) {
        CHECK(framebuffer->row(y).back() == padding);
    }
}

void frontTriangleIsVisibleInsideFrustum() {
    auto snapshot = makeSnapshot(64, 64);
    auto mesh = centerTriangle(1, 10);
    addResource(snapshot, mesh);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems.push_back({0, math::Transform3{}, 0, true});
    snapshot.passes.push_back(pass);

    auto framebuffer = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, framebuffer).has_value());
    CHECK(pixel(framebuffer, 32, 32) == RED);
    CHECK(pixel(framebuffer, 0, 0) == BLACK);
    CHECK(pixel(framebuffer, 63, 63) == BLACK);
    const std::size_t red = countColor(framebuffer, RED);
    CHECK(red > 100);
    CHECK(red < 64 * 64 / 2);
}

void reversedWindingIsCulledOnlyWhenEnabled() {
    auto snapshot = makeSnapshot(64, 64);
    auto mesh = centerTriangle(1, 10);
    mesh.triangles = {{0, 2, 1}}; // обратный порядок обхода: нормаль смотрит от камеры
    addResource(snapshot, mesh);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems.push_back({0, math::Transform3{}, 0, true});
    snapshot.passes.push_back(pass);

    auto culled = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, culled).has_value());
    CHECK(countColor(culled, RED) == 0);

    auto& item = std::get<render::World3DPass>(snapshot.passes.back()).drawItems.back();
    item.backfaceCulling = false;
    auto rendered = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, rendered).has_value());
    CHECK(countColor(rendered, RED) > 100);
}

void cameraAndItemTransformsShiftProjection() {
    auto base = makeSnapshot(64, 64);
    auto mesh = centerTriangle(0.5, 10);
    addResource(base, mesh);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems.push_back({0, math::Transform3{}, 0, true});
    base.passes.push_back(pass);

    auto identity = makeFramebuffer(64, 64);
    CHECK(renderer::render(base, identity).has_value());
    const double center = colorCentroidX(identity, RED);
    CHECK(center > 28 && center < 36);

    // камера сместилась в +x мира: содержимое кадра смещается в -x вида
    auto cameraMoved = base;
    std::get<render::World3DPass>(cameraMoved.passes.back()).camera =
        render::OrthographicCamera3{math::translation(math::Vector3{-1, 0, 0}), 4, 4, 0.1, 100};
    auto shifted = makeFramebuffer(64, 64);
    CHECK(renderer::render(cameraMoved, shifted).has_value());
    const double shiftedLeft = colorCentroidX(shifted, RED);
    CHECK(shiftedLeft < center - 12);

    // преобразование экземпляра смещает объект в мировых координатах
    auto itemMoved = base;
    std::get<render::World3DPass>(itemMoved.passes.back()).drawItems.back().localToWorld =
        math::translation(math::Vector3{1, 0, 0});
    auto moved = makeFramebuffer(64, 64);
    CHECK(renderer::render(itemMoved, moved).has_value());
    const double shiftedRight = colorCentroidX(moved, RED);
    CHECK(shiftedRight > center + 12);
}

void nearerTriangleCoversRegardlessOfSubmissionOrder() {
    auto near = frontTriangle(-1, -1, 0, 1, 1, -1, 10);
    auto far = frontTriangle(-1, -1, 0, 1, 1, -1, 20);
    for (const bool nearFirst : {false, true}) {
        auto snapshot = makeSnapshot(64, 64);
        addResource(snapshot, near);
        addResource(snapshot, far);
        render::World3DPass pass;
        pass.camera = orthoCamera(4, 4, 0.1, 100);
        pass.lighting = unlitLighting();
        const render::DrawItem3 nearItem{0, math::Transform3{}, 0, true}; // красный, z = 10
        const render::DrawItem3 farItem{1, math::Transform3{}, 2, true};  // синий, z = 20
        if (nearFirst) {
            pass.drawItems = {nearItem, farItem};
        } else {
            pass.drawItems = {farItem, nearItem};
        }
        snapshot.passes.push_back(pass);
        auto framebuffer = makeFramebuffer(64, 64);
        CHECK(renderer::render(snapshot, framebuffer).has_value());
        CHECK(pixel(framebuffer, 32, 32) == RED);
        CHECK(countColor(framebuffer, BLUE) == 0);
    }
}

void partiallyOverlappingTrianglesKeepOwnRegions() {
    auto red = frontTriangle(-2, -1, 0, 1, 0.5, -1, 20);  // левее и дальше
    auto blue = frontTriangle(-0.5, -1, 0, 1, 2, -1, 10); // правее и ближе
    auto snapshot = makeSnapshot(64, 64);
    addResource(snapshot, red);
    addResource(snapshot, blue);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems = {{0, math::Transform3{}, 0, true}, {1, math::Transform3{}, 2, true}};
    snapshot.passes.push_back(pass);

    auto framebuffer = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, framebuffer).has_value());
    CHECK(countColor(framebuffer, RED) > 50);
    CHECK(countColor(framebuffer, BLUE) > 50);
    CHECK(pixel(framebuffer, 20, 32) == RED);  // только дальний треугольник
    CHECK(pixel(framebuffer, 44, 32) == BLUE); // только ближний треугольник
    CHECK(pixel(framebuffer, 32, 32) == BLUE); // перекрытие: ближний перекрывает дальний
}

void perspectiveCameraHalvesAngularSizeWithDepth() {
    const render::PerspectiveCamera3 camera{math::Transform3{}, std::numbers::pi / 2, 0.1, 100};
    std::size_t nearCount = 0;
    std::size_t farCount = 0;
    for (const double z : {2.0, 4.0}) {
        auto snapshot = makeSnapshot(64, 64);
        auto mesh = centerTriangle(1, z);
        addResource(snapshot, mesh);
        render::World3DPass pass;
        pass.camera = camera;
        pass.lighting = unlitLighting();
        pass.drawItems.push_back({0, math::Transform3{}, 0, true});
        snapshot.passes.push_back(pass);
        auto framebuffer = makeFramebuffer(64, 64);
        CHECK(renderer::render(snapshot, framebuffer).has_value());
        const std::size_t count = countColor(framebuffer, RED);
        CHECK(count > 10);
        if (z == 2.0) {
            nearCount = count;
        } else {
            farCount = count;
        }
    }
    // удвоение глубины вдвое уменьшает линейный размер: площадь падает примерно в 4 раза
    CHECK(farCount * 2 < nearCount);
    CHECK(farCount * 6 > nearCount);
}

void outsideDepthRangeIsNotRendered() {
    auto snapshot = makeSnapshot(64, 64);
    auto beyond = centerTriangle(1, 200);
    auto before = centerTriangle(1, 0.01);
    addResource(snapshot, beyond);
    addResource(snapshot, before);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems = {{0, math::Transform3{}, 0, true}, {1, math::Transform3{}, 1, true}};
    snapshot.passes.push_back(pass);

    auto framebuffer = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, framebuffer).has_value());
    CHECK(countColor(framebuffer, RED) == 0);
    CHECK(countColor(framebuffer, GREEN) == 0);
}

void sizeMismatchAndInvalidIndicesAreErrors() {
    auto snapshot = makeSnapshot(64, 64);
    auto framebuffer = makeFramebuffer(32, 32);
    CHECK(renderer::render(snapshot, framebuffer).error() == renderer::RenderError::SizeMismatch);

    auto mesh = centerTriangle(1, 10);
    addResource(snapshot, mesh);
    render::World3DPass pass;
    pass.camera = orthoCamera(4, 4, 0.1, 100);
    pass.lighting = unlitLighting();
    pass.drawItems.push_back({7, math::Transform3{}, 0, true});
    snapshot.passes.push_back(pass);
    auto target = makeFramebuffer(64, 64);
    CHECK(renderer::render(snapshot, target).error() == renderer::RenderError::InvalidResource);

    std::get<render::World3DPass>(snapshot.passes.back()).drawItems.back() = {0, math::Transform3{}, 9, true};
    CHECK(renderer::render(snapshot, target).error() == renderer::RenderError::InvalidMaterial);
}

int main() {
    try {
        clearColorFillsEmptySnapshot();
        clearColorPreservesPaddingWithOddStride();
        frontTriangleIsVisibleInsideFrustum();
        reversedWindingIsCulledOnlyWhenEnabled();
        cameraAndItemTransformsShiftProjection();
        nearerTriangleCoversRegardlessOfSubmissionOrder();
        partiallyOverlappingTrianglesKeepOwnRegions();
        perspectiveCameraHalvesAngularSizeWithDepth();
        outsideDepthRangeIsNotRendered();
        sizeMismatchAndInvalidIndicesAreErrors();
        std::cout << "software_renderer: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
