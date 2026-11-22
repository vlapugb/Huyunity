#include "huynity/renderer/software_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace huynity::renderer {
namespace detail {

[[nodiscard]] bool Projection::isValid() const noexcept {
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || farPlane <= nearPlane) {
        return false;
    }
    if (isPerspective) {
        return nearPlane > 0 && std::isfinite(tanHalfFov) && tanHalfFov > 0 && aspect > 0;
    }
    return nearPlane >= 0 && halfWidth > 0 && halfHeight > 0 && std::isfinite(halfWidth) && std::isfinite(halfHeight);
}

[[nodiscard]] std::optional<ProjectedVertex> Projection::project(const math::Point3& view) const noexcept {
    const double z = view[2];
    if (!isValid() || z < nearPlane || z > farPlane) {
        return std::nullopt;
    }
    if (isPerspective) {
        const double halfHeightAtDepth = z * tanHalfFov;
        return ProjectedVertex{view[0] / (halfHeightAtDepth * aspect), view[1] / halfHeightAtDepth, 1 / z, 1};
    }
    return ProjectedVertex{view[0] / halfWidth, view[1] / halfHeight, 1, z};
}

[[nodiscard]] Projection makeProjection(const render_contracts::Camera3& camera, std::size_t imageWidth,
                                        std::size_t imageHeight) noexcept {
    Projection result;
    if (imageHeight != 0) {
        result.aspect = static_cast<double>(imageWidth) / static_cast<double>(imageHeight);
    }
    std::visit(
        [&](const auto& value) {
            using CameraType = std::decay_t<decltype(value)>;
            result.nearPlane = value.nearPlane;
            result.farPlane = value.farPlane;
            if constexpr (std::is_same_v<CameraType, render_contracts::PerspectiveCamera3>) {
                result.isPerspective = true;
                result.tanHalfFov = std::tan(value.verticalFieldOfView / 2);
            } else {
                result.halfWidth = value.width / 2;
                result.halfHeight = value.height / 2;
            }
        },
        camera);
    return result;
}

[[nodiscard]] math::Vector3 normalized(const math::Vector3& value) noexcept {
    const double magnitude = math::length(value);
    if (magnitude == 0 || !std::isfinite(magnitude)) {
        return {};
    }
    return value * (1 / magnitude);
}

[[nodiscard]] render_contracts::Color scaleColor(const render_contracts::Color& color, double factor) noexcept {
    return {color.red * factor, color.green * factor, color.blue * factor, color.alpha};
}

void addTo(render_contracts::Color& target, const render_contracts::Color& addition) noexcept {
    target.red += addition.red;
    target.green += addition.green;
    target.blue += addition.blue;
}

[[nodiscard]] render_contracts::Color multiplyColors(const render_contracts::Color& first,
                                                     const render_contracts::Color& second) noexcept {
    return {first.red * second.red, first.green * second.green, first.blue * second.blue, first.alpha * second.alpha};
}

[[nodiscard]] std::expected<const std::vector<math::Vector3>*, RenderError>
ShadingDataCache::faceNormals(const render_contracts::MeshResource3& resource) {
    return normalsFor(resource, m_entries, m_uncachedFaceNormals, computeNormals);
}

[[nodiscard]] std::expected<const std::vector<math::Vector3>*, RenderError>
ShadingDataCache::vertexNormals(const render_contracts::MeshResource3& resource) {
    return normalsFor(resource, m_vertexEntries, m_uncachedVertexNormals, computeVertexNormals);
}

std::expected<const std::vector<math::Vector3>*, RenderError>
ShadingDataCache::normalsFor(const render_contracts::MeshResource3& resource, std::vector<Entry>& entries,
                             std::vector<math::Vector3>& temporary, CalculateNormals calculate) {
    const std::size_t triangleCount = resource.triangles.size() / sizeof(render_contracts::TriangleIndices);
    const std::size_t vertexCount = resource.positions.size();
    auto cached = entries.end();
    if (resource.owner) {
        cached = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return entry.positions == resource.positions.data() && entry.triangles == resource.triangles.data() &&
                   entry.vertexCount == vertexCount && entry.triangleCount == triangleCount &&
                   !entry.owner.owner_before(resource.owner) && !resource.owner.owner_before(entry.owner);
        });
    }
    if (cached != entries.end() && cached->version == resource.version) {
        ++m_hits;
        return &cached->normals;
    }
    auto normals = calculate(resource, triangleCount);
    if (!normals) {
        return std::unexpected(normals.error());
    }
    ++m_misses;
    if (!resource.owner) {
        temporary = std::move(*normals);
        return &temporary;
    }
    if (cached != entries.end()) {
        cached->version = resource.version;
        cached->normals = std::move(*normals);
        return &cached->normals;
    }
    entries.push_back({resource.owner, resource.positions.data(), resource.triangles.data(), vertexCount, triangleCount,
                       resource.version, std::move(*normals)});
    return &entries.back().normals;
}

void ShadingDataCache::releaseExpired() {
    const auto isExpired = [](const auto& entry) { return entry.owner.expired(); };
    std::erase_if(m_entries, isExpired);
    std::erase_if(m_vertexEntries, isExpired);
}

[[nodiscard]] std::expected<std::vector<math::Vector3>, RenderError>
ShadingDataCache::computeNormals(const render_contracts::MeshResource3& resource, std::size_t triangleCount) {
    const auto* triangles = reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
    std::vector<math::Vector3> normals;
    normals.reserve(triangleCount);
    for (std::size_t index = 0; index < triangleCount; ++index) {
        const auto& triangle = triangles[index];
        if (triangle.a >= resource.positions.size() || triangle.b >= resource.positions.size() ||
            triangle.c >= resource.positions.size()) {
            return std::unexpected(RenderError::InvalidResource);
        }
        const auto normal = math::cross(resource.positions[triangle.b] - resource.positions[triangle.a],
                                        resource.positions[triangle.c] - resource.positions[triangle.a]);
        normals.push_back(normalized(normal));
    }
    return normals;
}

[[nodiscard]] std::expected<std::vector<math::Vector3>, RenderError>
ShadingDataCache::computeVertexNormals(const render_contracts::MeshResource3& resource, std::size_t triangleCount) {
    const auto* triangles = reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
    const auto vertexCount = resource.positions.size();
    std::vector<math::Vector3> normals(vertexCount, math::Vector3{});
    for (std::size_t index = 0; index < triangleCount; ++index) {
        const auto& triangle = triangles[index];
        if (triangle.a >= vertexCount || triangle.b >= vertexCount || triangle.c >= vertexCount) {
            return std::unexpected(RenderError::InvalidResource);
        }
        const auto normal = math::cross(resource.positions[triangle.b] - resource.positions[triangle.a],
                                        resource.positions[triangle.c] - resource.positions[triangle.a]);
        normals[triangle.a] = normals[triangle.a] + normal;
        normals[triangle.b] = normals[triangle.b] + normal;
        normals[triangle.c] = normals[triangle.c] + normal;
    }
    for (auto& normal : normals) {
        normal = normalized(normal);
    }
    return normals;
}

[[nodiscard]] std::byte toByte(double channel) noexcept {
    return static_cast<std::byte>(std::lround(std::clamp(channel, 0.0, 1.0) * 255));
}

void writePixel(image::ImageBuffer& framebuffer, std::size_t x, std::size_t y,
                const render_contracts::Color& color) noexcept {
    const auto bytes = framebuffer.bytes();
    const std::size_t offset = y * framebuffer.stride() + x * 4;
    bytes[offset] = toByte(color.red);
    bytes[offset + 1] = toByte(color.green);
    bytes[offset + 2] = toByte(color.blue);
    bytes[offset + 3] = toByte(color.alpha);
}

[[nodiscard]] render_contracts::Color readPixel(const image::ImageBuffer& buffer, std::size_t x,
                                                std::size_t y) noexcept {
    const auto bytes = buffer.bytes();
    const std::size_t offset = y * buffer.stride() + x * 4;
    if (offset + 3 >= bytes.size()) {
        return {0, 0, 0, 0};
    }
    return {
        static_cast<double>(std::to_integer<std::uint8_t>(bytes[offset])) / 255.0,
        static_cast<double>(std::to_integer<std::uint8_t>(bytes[offset + 1])) / 255.0,
        static_cast<double>(std::to_integer<std::uint8_t>(bytes[offset + 2])) / 255.0,
        static_cast<double>(std::to_integer<std::uint8_t>(bytes[offset + 3])) / 255.0,
    };
}

[[nodiscard]] double applyWrap(double coordinate, TextureWrap wrap) noexcept {
    if (wrap == TextureWrap::Clamp) {
        return std::clamp(coordinate, 0.0, 1.0);
    }
    const double wrapped = coordinate - std::floor(coordinate);
    return std::clamp(wrapped, 0.0, 1.0);
}

[[nodiscard]] render_contracts::Color sampleTextureNearest(const image::ImageBuffer& texture, double u, double v,
                                                           TextureWrap wrap) noexcept {
    if (texture.width() == 0 || texture.height() == 0 || texture.bytes().empty() || !std::isfinite(u) ||
        !std::isfinite(v)) {
        return {0, 0, 0, 0};
    }
    const double wrappedU = applyWrap(u, wrap);
    const double wrappedV = applyWrap(v, wrap);

    const auto width = texture.width();
    const auto height = texture.height();

    const double coordX = std::floor(wrappedU * static_cast<double>(width));
    const auto x = static_cast<std::size_t>(std::clamp(coordX, 0.0, static_cast<double>(width - 1)));

    const double coordY = std::floor(wrappedV * static_cast<double>(height));
    const auto y = static_cast<std::size_t>(std::clamp(coordY, 0.0, static_cast<double>(height - 1)));

    return readPixel(texture, x, y);
}

[[nodiscard]] render_contracts::Color sampleTextureBilinear(const image::ImageBuffer& texture, double u, double v,
                                                            TextureWrap wrap) noexcept {
    if (texture.width() == 0 || texture.height() == 0 || texture.bytes().empty() || !std::isfinite(u) ||
        !std::isfinite(v)) {
        return {0, 0, 0, 0};
    }
    const double wrappedU = applyWrap(u, wrap);
    const double wrappedV = applyWrap(v, wrap);

    const auto width = texture.width();
    const auto height = texture.height();

    const double px = wrappedU * static_cast<double>(width - 1);
    const double py = wrappedV * static_cast<double>(height - 1);

    const double floorX = std::floor(px);
    const auto x0 = static_cast<std::size_t>(std::clamp(floorX, 0.0, static_cast<double>(width - 1)));
    const auto x1 = std::min(x0 + 1, width - 1);
    const double fx = std::clamp(px - floorX, 0.0, 1.0);

    const double floorY = std::floor(py);
    const auto y0 = static_cast<std::size_t>(std::clamp(floorY, 0.0, static_cast<double>(height - 1)));
    const auto y1 = std::min(y0 + 1, height - 1);
    const double fy = std::clamp(py - floorY, 0.0, 1.0);

    const auto c00 = readPixel(texture, x0, y0);
    const auto c10 = readPixel(texture, x1, y0);
    const auto c01 = readPixel(texture, x0, y1);
    const auto c11 = readPixel(texture, x1, y1);

    const auto lerp = [](double a, double b, double t) noexcept { return (1.0 - t) * a + t * b; };

    const double topRed = lerp(c00.red, c10.red, fx);
    const double topGreen = lerp(c00.green, c10.green, fx);
    const double topBlue = lerp(c00.blue, c10.blue, fx);
    const double topAlpha = lerp(c00.alpha, c10.alpha, fx);

    const double bottomRed = lerp(c01.red, c11.red, fx);
    const double bottomGreen = lerp(c01.green, c11.green, fx);
    const double bottomBlue = lerp(c01.blue, c11.blue, fx);
    const double bottomAlpha = lerp(c01.alpha, c11.alpha, fx);

    return {
        lerp(topRed, bottomRed, fy),
        lerp(topGreen, bottomGreen, fy),
        lerp(topBlue, bottomBlue, fy),
        lerp(topAlpha, bottomAlpha, fy),
    };
}

[[nodiscard]] render_contracts::Color sampleTexture(const image::ImageBuffer& texture, double u, double v,
                                                    TextureFilter filter, TextureWrap wrap) noexcept {
    if (filter == TextureFilter::Bilinear) {
        return sampleTextureBilinear(texture, u, v, wrap);
    }
    return sampleTextureNearest(texture, u, v, wrap);
}

void clear(image::ImageBuffer& framebuffer, const render_contracts::Color& color) noexcept {
    const std::array pixel{toByte(color.red), toByte(color.green), toByte(color.blue), toByte(color.alpha)};
    const auto rowBytes = framebuffer.width() * pixel.size();
    for (std::size_t y = 0; y < framebuffer.height(); ++y) {
        auto* row = framebuffer.row(y).data();
        for (std::size_t offset = 0; offset < rowBytes; offset += pixel.size()) {
            std::memcpy(row + offset, pixel.data(), pixel.size());
        }
    }
}

void rasterizeTriangle(const ProjectedVertex& first, const ProjectedVertex& second, const ProjectedVertex& third,
                       const render_contracts::Color& color, image::ImageBuffer& framebuffer,
                       std::vector<double>& depth) noexcept {
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const std::array<double, 3> screenX{(first.ndcX + 1) * 0.5 * width, (second.ndcX + 1) * 0.5 * width,
                                        (third.ndcX + 1) * 0.5 * width};
    const std::array<double, 3> screenY{(1 - first.ndcY) * 0.5 * height, (1 - second.ndcY) * 0.5 * height,
                                        (1 - third.ndcY) * 0.5 * height};
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (area == 0) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    const double invArea = 1.0 / area;
    const std::array<ProjectedVertex, 3> vertices{first, second, third};
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) * invArea;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) * invArea;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) * invArea;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            const double invZ = weight0 * vertices[0].invZ + weight1 * vertices[1].invZ + weight2 * vertices[2].invZ;
            if (invZ <= 0) {
                continue;
            }
            const double z =
                (weight0 * vertices[0].zOverW + weight1 * vertices[1].zOverW + weight2 * vertices[2].zOverW) / invZ;
            const std::size_t pixel = y * framebuffer.width() + x;
            if (z < depth[pixel]) {
                depth[pixel] = z;
                writePixel(framebuffer, x, y, color);
            }
        }
    }
}

void rasterizeTriangleGouraud(const ProjectedVertex& first, const ProjectedVertex& second, const ProjectedVertex& third,
                              const render_contracts::Color& c0, const render_contracts::Color& c1,
                              const render_contracts::Color& c2, image::ImageBuffer& framebuffer,
                              std::vector<double>& depth) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0 || depth.empty()) {
        return;
    }
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const std::array<double, 3> screenX{(first.ndcX + 1) * 0.5 * width, (second.ndcX + 1) * 0.5 * width,
                                        (third.ndcX + 1) * 0.5 * width};
    const std::array<double, 3> screenY{(1 - first.ndcY) * 0.5 * height, (1 - second.ndcY) * 0.5 * height,
                                        (1 - third.ndcY) * 0.5 * height};
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (std::abs(area) <= 1e-9) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    const double invArea = 1.0 / area;
    const std::array<ProjectedVertex, 3> vertices{first, second, third};
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) * invArea;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) * invArea;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) * invArea;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            const double invZ = weight0 * vertices[0].invZ + weight1 * vertices[1].invZ + weight2 * vertices[2].invZ;
            if (invZ <= 0) {
                continue;
            }
            const double z =
                (weight0 * vertices[0].zOverW + weight1 * vertices[1].zOverW + weight2 * vertices[2].zOverW) / invZ;
            const std::size_t pixel = y * framebuffer.width() + x;
            if (pixel < depth.size() && z < depth[pixel]) {
                depth[pixel] = z;
                const render_contracts::Color interpolatedColor{
                    weight0 * c0.red + weight1 * c1.red + weight2 * c2.red,
                    weight0 * c0.green + weight1 * c1.green + weight2 * c2.green,
                    weight0 * c0.blue + weight1 * c1.blue + weight2 * c2.blue,
                    weight0 * c0.alpha + weight1 * c1.alpha + weight2 * c2.alpha,
                };
                writePixel(framebuffer, x, y, interpolatedColor);
            }
        }
    }
}

void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const std::array<math::Point2, 3>& uv,
                                 const image::ImageBuffer& texture, const render_contracts::Color& c0,
                                 const render_contracts::Color& c1, const render_contracts::Color& c2,
                                 image::ImageBuffer& framebuffer, std::vector<double>& depth, TextureFilter filter,
                                 TextureWrap wrap) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0 || depth.empty() || texture.width() == 0 ||
        texture.height() == 0) {
        return;
    }
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const std::array<double, 3> screenX{(first.ndcX + 1) * 0.5 * width, (second.ndcX + 1) * 0.5 * width,
                                        (third.ndcX + 1) * 0.5 * width};
    const std::array<double, 3> screenY{(1 - first.ndcY) * 0.5 * height, (1 - second.ndcY) * 0.5 * height,
                                        (1 - third.ndcY) * 0.5 * height};
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (std::abs(area) <= 1e-9) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    const double invArea = 1.0 / area;
    const std::array<ProjectedVertex, 3> vertices{first, second, third};
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) * invArea;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) * invArea;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) * invArea;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            const double invZ = weight0 * vertices[0].invZ + weight1 * vertices[1].invZ + weight2 * vertices[2].invZ;
            if (invZ <= 0) {
                continue;
            }
            const double z =
                (weight0 * vertices[0].zOverW + weight1 * vertices[1].zOverW + weight2 * vertices[2].zOverW) / invZ;
            const std::size_t pixel = y * framebuffer.width() + x;
            if (pixel < depth.size() && z < depth[pixel]) {
                const double u = (weight0 * uv[0][0] * vertices[0].invZ + weight1 * uv[1][0] * vertices[1].invZ +
                                  weight2 * uv[2][0] * vertices[2].invZ) /
                                 invZ;
                const double v = (weight0 * uv[0][1] * vertices[0].invZ + weight1 * uv[1][1] * vertices[1].invZ +
                                  weight2 * uv[2][1] * vertices[2].invZ) /
                                 invZ;
                const auto texColor = sampleTexture(texture, u, v, filter, wrap);
                const render_contracts::Color lightColor{
                    weight0 * c0.red + weight1 * c1.red + weight2 * c2.red,
                    weight0 * c0.green + weight1 * c1.green + weight2 * c2.green,
                    weight0 * c0.blue + weight1 * c1.blue + weight2 * c2.blue,
                    weight0 * c0.alpha + weight1 * c1.alpha + weight2 * c2.alpha,
                };
                const auto finalColor = multiplyColors(texColor, lightColor);
                if (finalColor.alpha <= 0.0) {
                    continue;
                }
                depth[pixel] = z;
                writePixel(framebuffer, x, y, finalColor);
            }
        }
    }
}

void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const std::array<math::Point2, 3>& uv,
                                 const image::ImageBuffer& texture, const render_contracts::Color& color,
                                 image::ImageBuffer& framebuffer, std::vector<double>& depth, TextureFilter filter,
                                 TextureWrap wrap) noexcept {
    rasterizeTriangleTextured3D(first, second, third, uv, texture, color, color, color, framebuffer, depth, filter,
                                wrap);
}

void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const math::Point2& uv0, const math::Point2& uv1,
                                 const math::Point2& uv2, const image::ImageBuffer& texture,
                                 const render_contracts::Color& color, image::ImageBuffer& framebuffer,
                                 std::vector<double>& depth, TextureFilter filter, TextureWrap wrap) noexcept {
    rasterizeTriangleTextured3D(first, second, third, std::array<math::Point2, 3>{uv0, uv1, uv2}, texture, color, color,
                                color, framebuffer, depth, filter, wrap);
}

void blendPixel(image::ImageBuffer& framebuffer, std::size_t x, std::size_t y,
                const render_contracts::Color& color) noexcept {
    const auto bytes = framebuffer.bytes();
    const std::size_t offset = y * framebuffer.stride() + x * 4;
    const double alpha = std::clamp(color.alpha, 0.0, 1.0);
    const double inverse = 1 - alpha;
    const std::array<double, 3> source{color.red, color.green, color.blue};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double destination = static_cast<double>(std::to_integer<int>(bytes[offset + channel])) / 255;
        bytes[offset + channel] = toByte(source[channel] * alpha + destination * inverse);
    }
    const double destinationAlpha = static_cast<double>(std::to_integer<int>(bytes[offset + 3])) / 255;
    bytes[offset + 3] = toByte(alpha + destinationAlpha * inverse);
}

void rasterizeLine2D(int x0, int y0, int x1, int y1, const render_contracts::Color& color,
                     image::ImageBuffer& framebuffer) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0) {
        return;
    }
    const auto plot = [&](int x, int y) noexcept {
        if (x >= 0 && y >= 0 && static_cast<std::size_t>(x) < framebuffer.width() &&
            static_cast<std::size_t>(y) < framebuffer.height()) {
            const auto ux = static_cast<std::size_t>(x);
            const auto uy = static_cast<std::size_t>(y);
            if (color.alpha < 1.0) {
                blendPixel(framebuffer, ux, uy, color);
            } else {
                writePixel(framebuffer, ux, uy, color);
            }
        }
    };
    const std::int64_t dx = std::abs(static_cast<std::int64_t>(x1) - static_cast<std::int64_t>(x0));
    const int sx = x0 < x1 ? 1 : -1;
    const std::int64_t dy = -std::abs(static_cast<std::int64_t>(y1) - static_cast<std::int64_t>(y0));
    const int sy = y0 < y1 ? 1 : -1;
    std::int64_t error = dx + dy;

    int currentX = x0;
    int currentY = y0;
    while (true) {
        plot(currentX, currentY);
        if (currentX == x1 && currentY == y1) {
            break;
        }
        const std::int64_t e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            currentX += sx;
        }
        if (e2 <= dx) {
            error += dx;
            currentY += sy;
        }
    }
}

void rasterizeLine3D(int x0, int y0, double z0, int x1, int y1, double z1, const render_contracts::Color& color,
                     image::ImageBuffer& framebuffer, std::vector<double>& depth) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0 || depth.empty()) {
        return;
    }
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    int currentStep = 0;

    const auto plot = [&](int x, int y, double z) noexcept {
        if (x >= 0 && y >= 0 && static_cast<std::size_t>(x) < framebuffer.width() &&
            static_cast<std::size_t>(y) < framebuffer.height()) {
            const auto ux = static_cast<std::size_t>(x);
            const auto uy = static_cast<std::size_t>(y);
            const std::size_t pixel = uy * framebuffer.width() + ux;
            if (pixel < depth.size() && z < depth[pixel]) {
                depth[pixel] = z;
                if (color.alpha < 1.0) {
                    blendPixel(framebuffer, ux, uy, color);
                } else {
                    writePixel(framebuffer, ux, uy, color);
                }
            }
        }
    };

    const std::int64_t dx = std::abs(static_cast<std::int64_t>(x1) - static_cast<std::int64_t>(x0));
    const int sx = x0 < x1 ? 1 : -1;
    const std::int64_t dy = -std::abs(static_cast<std::int64_t>(y1) - static_cast<std::int64_t>(y0));
    const int sy = y0 < y1 ? 1 : -1;
    std::int64_t error = dx + dy;

    int currentX = x0;
    int currentY = y0;
    while (true) {
        const double t =
            steps == 0 ? 0.0 : std::clamp(static_cast<double>(currentStep) / static_cast<double>(steps), 0.0, 1.0);
        const double z = z0 + t * (z1 - z0);
        plot(currentX, currentY, z);
        ++currentStep;

        if (currentX == x1 && currentY == y1) {
            break;
        }
        const std::int64_t e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            currentX += sx;
        }
        if (e2 <= dx) {
            error += dx;
            currentY += sy;
        }
    }
}

[[nodiscard]] render_contracts::Color shadeFlat(const render_contracts::Material& material,
                                                const render_contracts::AmbientLight& ambient,
                                                const std::vector<ViewDirectionalLight>& lights,
                                                const math::Vector3& viewNormal, bool isFront) noexcept {
    const auto normal = isFront ? viewNormal : viewNormal * -1;
    auto light = scaleColor(ambient.color, ambient.intensity);
    for (const auto& directional : lights) {
        const double factor = std::max(0.0, math::dot(normal, directional.towardLight));
        addTo(light, scaleColor(directional.color, directional.intensity * factor));
    }
    return multiplyColors(material.color, light);
}

[[nodiscard]] std::expected<void, RenderError>
renderWorld3D(const render_contracts::RenderSnapshot& snapshot, const render_contracts::World3DPass& pass,
              image::ImageBuffer& framebuffer, std::vector<double>& depth, ShadingDataCache& shadingCache) {
    const auto projection = makeProjection(pass.camera, snapshot.imageWidth, snapshot.imageHeight);
    const auto worldToView = std::visit([](const auto& value) { return value.worldToView; }, pass.camera);
    std::vector<ViewDirectionalLight> viewLights;
    viewLights.reserve(pass.lighting.directional.size());
    for (const auto& directional : pass.lighting.directional) {
        viewLights.push_back({normalized(math::transformVector(worldToView, directional.direction) * -1),
                              directional.color, directional.intensity});
    }
    for (const auto& item : pass.drawItems) {
        if (item.resource >= snapshot.resources3.size()) {
            return std::unexpected(RenderError::InvalidResource);
        }
        if (item.material >= snapshot.materials.size()) {
            return std::unexpected(RenderError::InvalidMaterial);
        }
        const auto& resource = snapshot.resources3[item.resource];
        const auto& material = *snapshot.materials[item.material];
        const auto localToView = math::compose(worldToView, item.localToWorld);
        const bool isGouraud = (item.shading == render_contracts::ShadingModel::Gouraud);
        const bool isTextured =
            (material.texture != nullptr) && (resource.texCoords.size() == resource.positions.size());
        const std::vector<math::Vector3>* faceNormals = nullptr;
        const std::vector<math::Vector3>* vertexNormals = nullptr;
        if (isGouraud) {
            auto normals = shadingCache.vertexNormals(resource);
            if (!normals) {
                return std::unexpected(normals.error());
            }
            vertexNormals = *normals;
        } else {
            auto normals = shadingCache.faceNormals(resource);
            if (!normals) {
                return std::unexpected(normals.error());
            }
            faceNormals = *normals;
        }
        const std::size_t triangleCount = resource.triangles.size() / sizeof(render_contracts::TriangleIndices);
        const auto* triangles = reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
        for (std::size_t index = 0; index < triangleCount; ++index) {
            const auto& triangle = triangles[index];
            const auto viewFirst = math::transformPoint(localToView, resource.positions[triangle.a]);
            const auto viewSecond = math::transformPoint(localToView, resource.positions[triangle.b]);
            const auto viewThird = math::transformPoint(localToView, resource.positions[triangle.c]);
            // ориентация грани нужна и при выключенном culling: обратная грань
            // освещается с перевёрнутой нормалью
            const auto faceNormal = math::cross(viewSecond - viewFirst, viewThird - viewFirst);
            const math::Vector3 fromOrigin{viewFirst[0], viewFirst[1], viewFirst[2]};
            const bool isFront = projection.isPerspective ? math::dot(faceNormal, fromOrigin) < 0 : faceNormal[2] < 0;
            if (item.backfaceCulling && !isFront) {
                continue;
            }
            const auto first = projection.project(viewFirst);
            const auto second = projection.project(viewSecond);
            const auto third = projection.project(viewThird);
            if (!first || !second || !third) {
                continue;
            }
            if (isGouraud) {
                const auto n0View = normalized(math::transformVector(localToView, (*vertexNormals)[triangle.a]));
                const auto n1View = normalized(math::transformVector(localToView, (*vertexNormals)[triangle.b]));
                const auto n2View = normalized(math::transformVector(localToView, (*vertexNormals)[triangle.c]));

                const auto c0 = shadeFlat(material, pass.lighting.ambient, viewLights, n0View, isFront);
                const auto c1 = shadeFlat(material, pass.lighting.ambient, viewLights, n1View, isFront);
                const auto c2 = shadeFlat(material, pass.lighting.ambient, viewLights, n2View, isFront);

                if (item.wireframe) {
                    const double width = static_cast<double>(framebuffer.width());
                    const double height = static_cast<double>(framebuffer.height());
                    const int x0 = static_cast<int>(std::lround((first->ndcX + 1) * 0.5 * width));
                    const int y0 = static_cast<int>(std::lround((1 - first->ndcY) * 0.5 * height));
                    const double z0 = viewFirst[2];
                    const int x1 = static_cast<int>(std::lround((second->ndcX + 1) * 0.5 * width));
                    const int y1 = static_cast<int>(std::lround((1 - second->ndcY) * 0.5 * height));
                    const double z1 = viewSecond[2];
                    const int x2 = static_cast<int>(std::lround((third->ndcX + 1) * 0.5 * width));
                    const int y2 = static_cast<int>(std::lround((1 - third->ndcY) * 0.5 * height));
                    const double z2 = viewThird[2];

                    rasterizeLine3D(x0, y0, z0, x1, y1, z1, c0, framebuffer, depth);
                    rasterizeLine3D(x1, y1, z1, x2, y2, z2, c1, framebuffer, depth);
                    rasterizeLine3D(x2, y2, z2, x0, y0, z2, c2, framebuffer, depth);
                } else if (isTextured) {
                    const std::array<math::Point2, 3> uvs{
                        resource.texCoords[triangle.a], resource.texCoords[triangle.b], resource.texCoords[triangle.c]};
                    rasterizeTriangleTextured3D(*first, *second, *third, uvs, *material.texture, c0, c1, c2,
                                                framebuffer, depth, material.filter, material.wrap);
                } else {
                    rasterizeTriangleGouraud(*first, *second, *third, c0, c1, c2, framebuffer, depth);
                }
            } else {
                const auto viewNormal = normalized(math::transformVector(localToView, (*faceNormals)[index]));
                const auto shaded = shadeFlat(material, pass.lighting.ambient, viewLights, viewNormal, isFront);
                if (item.wireframe) {
                    const double width = static_cast<double>(framebuffer.width());
                    const double height = static_cast<double>(framebuffer.height());
                    const int x0 = static_cast<int>(std::lround((first->ndcX + 1) * 0.5 * width));
                    const int y0 = static_cast<int>(std::lround((1 - first->ndcY) * 0.5 * height));
                    const double z0 = viewFirst[2];
                    const int x1 = static_cast<int>(std::lround((second->ndcX + 1) * 0.5 * width));
                    const int y1 = static_cast<int>(std::lround((1 - second->ndcY) * 0.5 * height));
                    const double z1 = viewSecond[2];
                    const int x2 = static_cast<int>(std::lround((third->ndcX + 1) * 0.5 * width));
                    const int y2 = static_cast<int>(std::lround((1 - third->ndcY) * 0.5 * height));
                    const double z2 = viewThird[2];

                    rasterizeLine3D(x0, y0, z0, x1, y1, z1, shaded, framebuffer, depth);
                    rasterizeLine3D(x1, y1, z1, x2, y2, z2, shaded, framebuffer, depth);
                    rasterizeLine3D(x2, y2, z2, x0, y0, z0, shaded, framebuffer, depth);
                } else if (isTextured) {
                    const std::array<math::Point2, 3> uvs{
                        resource.texCoords[triangle.a], resource.texCoords[triangle.b], resource.texCoords[triangle.c]};
                    rasterizeTriangleTextured3D(*first, *second, *third, uvs, *material.texture, shaded, framebuffer,
                                                depth, material.filter, material.wrap);
                } else {
                    rasterizeTriangle(*first, *second, *third, shaded, framebuffer, depth);
                }
            }
        }
    }
    return {};
}

void rasterizeTriangle2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                         const render_contracts::Color& color, image::ImageBuffer& framebuffer) noexcept {
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (area == 0) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) / area;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) / area;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) / area;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            blendPixel(framebuffer, x, y, color);
        }
    }
}

void rasterizeTriangleGouraud2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                                const render_contracts::Color& c0, const render_contracts::Color& c1,
                                const render_contracts::Color& c2, image::ImageBuffer& framebuffer) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0) {
        return;
    }
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (std::abs(area) <= 1e-9) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) / area;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) / area;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) / area;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            const render_contracts::Color interpolatedColor{
                weight0 * c0.red + weight1 * c1.red + weight2 * c2.red,
                weight0 * c0.green + weight1 * c1.green + weight2 * c2.green,
                weight0 * c0.blue + weight1 * c1.blue + weight2 * c2.blue,
                weight0 * c0.alpha + weight1 * c1.alpha + weight2 * c2.alpha,
            };
            blendPixel(framebuffer, x, y, interpolatedColor);
        }
    }
}

void rasterizeTriangleGouraud2D(const math::Point2& p0, const math::Point2& p1, const math::Point2& p2,
                                const render_contracts::Color& c0, const render_contracts::Color& c1,
                                const render_contracts::Color& c2, image::ImageBuffer& framebuffer) noexcept {
    rasterizeTriangleGouraud2D(std::array<double, 3>{p0[0], p1[0], p2[0]}, std::array<double, 3>{p0[1], p1[1], p2[1]},
                               c0, c1, c2, framebuffer);
}

void rasterizeTriangleTextured2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                                 const std::array<math::Point2, 3>& uv, const image::ImageBuffer& texture,
                                 const render_contracts::Color& tintColor, image::ImageBuffer& framebuffer,
                                 TextureFilter filter, TextureWrap wrap) noexcept {
    if (framebuffer.width() == 0 || framebuffer.height() == 0 || texture.width() == 0 || texture.height() == 0) {
        return;
    }
    const double width = static_cast<double>(framebuffer.width());
    const double height = static_cast<double>(framebuffer.height());
    const double area = edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], screenX[2], screenY[2]);
    if (std::abs(area) <= 1e-9) {
        return;
    }
    const double minXf = std::min({screenX[0], screenX[1], screenX[2]});
    const double maxXf = std::max({screenX[0], screenX[1], screenX[2]});
    const double minYf = std::min({screenY[0], screenY[1], screenY[2]});
    const double maxYf = std::max({screenY[0], screenY[1], screenY[2]});
    if (maxXf < 0 || maxYf < 0 || minXf >= width || minYf >= height) {
        return;
    }
    const auto clampX = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, width - 1)));
    };
    const auto clampY = [&](double value) {
        return static_cast<std::size_t>(std::floor(std::clamp(value, 0.0, height - 1)));
    };
    for (std::size_t y = clampY(minYf); y <= clampY(maxYf); ++y) {
        for (std::size_t x = clampX(minXf); x <= clampX(maxXf); ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double sampleY = static_cast<double>(y) + 0.5;
            const double weight0 =
                edgeFunction(screenX[1], screenY[1], screenX[2], screenY[2], sampleX, sampleY) / area;
            const double weight1 =
                edgeFunction(screenX[2], screenY[2], screenX[0], screenY[0], sampleX, sampleY) / area;
            const double weight2 =
                edgeFunction(screenX[0], screenY[0], screenX[1], screenY[1], sampleX, sampleY) / area;
            constexpr double tolerance = -1e-9;
            if (weight0 < tolerance || weight1 < tolerance || weight2 < tolerance) {
                continue;
            }
            const double u = weight0 * uv[0][0] + weight1 * uv[1][0] + weight2 * uv[2][0];
            const double v = weight0 * uv[0][1] + weight1 * uv[1][1] + weight2 * uv[2][1];
            const auto texColor = sampleTexture(texture, u, v, filter, wrap);
            const auto finalColor = multiplyColors(texColor, tintColor);
            if (finalColor.alpha <= 0.0) {
                continue;
            }
            blendPixel(framebuffer, x, y, finalColor);
        }
    }
}

void rasterizeTriangleTextured2D(const math::Point2& p0, const math::Point2& p1, const math::Point2& p2,
                                 const math::Point2& uv0, const math::Point2& uv1, const math::Point2& uv2,
                                 const image::ImageBuffer& texture, const render_contracts::Color& tintColor,
                                 image::ImageBuffer& framebuffer, TextureFilter filter, TextureWrap wrap) noexcept {
    rasterizeTriangleTextured2D(std::array<double, 3>{p0[0], p1[0], p2[0]}, std::array<double, 3>{p0[1], p1[1], p2[1]},
                                std::array<math::Point2, 3>{uv0, uv1, uv2}, texture, tintColor, framebuffer, filter,
                                wrap);
}

[[nodiscard]] std::expected<void, RenderError> render2DPass(const render_contracts::RenderSnapshot& snapshot,
                                                            std::span<const render_contracts::DrawItem2> items,
                                                            const std::optional<render_contracts::Camera2>& camera,
                                                            image::ImageBuffer& framebuffer) {
    std::vector<const render_contracts::DrawItem2*> sorted;
    sorted.reserve(items.size());
    for (const auto& item : items) {
        sorted.push_back(&item);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* first, const auto* second) {
        return std::tie(first->layer, first->order) < std::tie(second->layer, second->order);
    });
    double halfWidth = 0;
    double halfHeight = 0;
    bool isCameraValid = false;
    if (camera) {
        halfWidth = camera->width / 2;
        halfHeight = camera->height / 2;
        isCameraValid = halfWidth > 0 && halfHeight > 0 && std::isfinite(halfWidth) && std::isfinite(halfHeight);
    }
    for (const auto* item : sorted) {
        if (item->resource >= snapshot.resources2.size()) {
            return std::unexpected(RenderError::InvalidResource);
        }
        if (item->material >= snapshot.materials.size()) {
            return std::unexpected(RenderError::InvalidMaterial);
        }
        if (camera && !isCameraValid) {
            continue;
        }
        const auto& resource = snapshot.resources2[item->resource];
        const auto& material = *snapshot.materials[item->material];
        const bool isTextured =
            (material.texture != nullptr) && (resource.texCoords.size() == resource.positions.size());
        const auto localToView = camera ? math::compose(camera->worldToView, item->localToWorld) : item->localToWorld;
        const std::size_t triangleCount = resource.triangles.size() / sizeof(render_contracts::TriangleIndices);
        const auto* triangles = reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
        for (std::size_t index = 0; index < triangleCount; ++index) {
            const auto& triangle = triangles[index];
            if (triangle.a >= resource.positions.size() || triangle.b >= resource.positions.size() ||
                triangle.c >= resource.positions.size()) {
                return std::unexpected(RenderError::InvalidResource);
            }
            const std::array<math::Point2, 3> view{math::transformPoint(localToView, resource.positions[triangle.a]),
                                                   math::transformPoint(localToView, resource.positions[triangle.b]),
                                                   math::transformPoint(localToView, resource.positions[triangle.c])};
            std::array<double, 3> screenX;
            std::array<double, 3> screenY;
            for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                if (camera) {
                    const double ndcX = view[vertex][0] / halfWidth;
                    const double ndcY = view[vertex][1] / halfHeight;
                    screenX[vertex] = (ndcX + 1) * 0.5 * static_cast<double>(framebuffer.width());
                    screenY[vertex] = (1 - ndcY) * 0.5 * static_cast<double>(framebuffer.height());
                } else {
                    screenX[vertex] = view[vertex][0];
                    screenY[vertex] = view[vertex][1];
                }
            }
            if (item->wireframe) {
                const int x0 = static_cast<int>(std::lround(screenX[0]));
                const int y0 = static_cast<int>(std::lround(screenY[0]));
                const int x1 = static_cast<int>(std::lround(screenX[1]));
                const int y1 = static_cast<int>(std::lround(screenY[1]));
                const int x2 = static_cast<int>(std::lround(screenX[2]));
                const int y2 = static_cast<int>(std::lround(screenY[2]));

                rasterizeLine2D(x0, y0, x1, y1, material.color, framebuffer);
                rasterizeLine2D(x1, y1, x2, y2, material.color, framebuffer);
                rasterizeLine2D(x2, y2, x0, y0, material.color, framebuffer);
            } else if (isTextured) {
                const std::array<math::Point2, 3> uvs{resource.texCoords[triangle.a], resource.texCoords[triangle.b],
                                                      resource.texCoords[triangle.c]};
                rasterizeTriangleTextured2D(screenX, screenY, uvs, *material.texture, material.color, framebuffer,
                                            material.filter, material.wrap);
            } else {
                rasterizeTriangle2D(screenX, screenY, material.color, framebuffer);
            }
        }
    }
    return {};
}

} // namespace detail

[[nodiscard]] std::expected<void, RenderError>
SoftwareRenderer::render(const render_contracts::RenderSnapshot& snapshot, image::ImageBuffer& framebuffer) {
    if (framebuffer.width() != snapshot.imageWidth || framebuffer.height() != snapshot.imageHeight) {
        return std::unexpected(RenderError::SizeMismatch);
    }
    m_shadingCache.releaseExpired();
    detail::clear(framebuffer, snapshot.clearColor);
    const std::size_t pixelCount = snapshot.imageWidth * snapshot.imageHeight;
    if (m_depthBuffer.size() != pixelCount) {
        m_depthBuffer.resize(pixelCount);
    }
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), std::numeric_limits<double>::infinity());
    for (const auto& pass : snapshot.passes) {
        std::expected<void, RenderError> rendered;
        if (const auto* world3 = std::get_if<render_contracts::World3DPass>(&pass)) {
            rendered = detail::renderWorld3D(snapshot, *world3, framebuffer, m_depthBuffer, m_shadingCache);
        } else if (const auto* world2 = std::get_if<render_contracts::World2DPass>(&pass)) {
            rendered = detail::render2DPass(snapshot, world2->drawItems, world2->camera, framebuffer);
        } else {
            const auto& hud = std::get<render_contracts::Hud2DPass>(pass);
            rendered = detail::render2DPass(snapshot, hud.drawItems, std::nullopt, framebuffer);
        }
        if (!rendered) {
            return rendered;
        }
    }
    return {};
}

} // namespace huynity::renderer
