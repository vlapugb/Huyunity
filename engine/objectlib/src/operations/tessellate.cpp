#include "huynity/objectlib/operations/tessellate.h"

#include "huynity/math/vector_unsigned.h"
#include "huynity/objectlib/collections/triangle_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace huynity::objectlib {

namespace {

GeometryResult<void> validateOptions(TessellationOptions options) {
    if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
        return std::unexpected(GeometryError{GeometryErrorCode::InvalidOptions});
    }
    return {};
}

GeometryResult<std::size_t> circleSegmentCount(double radius, TessellationOptions options) {
    if (const auto valid = validateOptions(options); !valid) {
        return std::unexpected(valid.error());
    }
    // r * (1 - cos(pi/n)) is the largest distance from an arc to its chord.
    // The asin form avoids cancellation in 1 - deviation/r.
    const long double ratio = static_cast<long double>(options.maxDeviation) / radius;
    const auto halfAngle = 2 * std::asin(std::sqrt(std::min(ratio / 2, 1.0L)));
    const auto count = std::max(3.0L, std::ceil(std::numbers::pi_v<long double> / halfAngle));
    const auto limit = std::min<std::size_t>(options.maxPrimitiveCount, std::numeric_limits<std::uint32_t>::max() - 1U);
    if (!std::isfinite(count) || count > static_cast<long double>(limit)) {
        return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
    }
    return static_cast<std::size_t>(count);
}

template <std::size_t dimension>
TessellationResult<dimension> makeMesh(typename TriangleMesh<dimension>::Params params) {
    auto mesh = TriangleMesh<dimension>::create(std::move(params));
    if (!mesh) {
        if (mesh.error().code == CreateErrorCode::InvalidIndex) {
            return std::unexpected(GeometryError{GeometryErrorCode::InvalidGeometry});
        }
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    return std::move(*mesh);
}

GeometryResult<long double> sphereFaceDeviation(const math::Point3& a, const math::Point3& b, const math::Point3& c) {
    std::array<long double, 3> ab{};
    std::array<long double, 3> ac{};
    std::array<long double, 3> normal{};
    for (std::size_t i = 0; i < 3; ++i) {
        ab[i] = static_cast<long double>(b[i]) - a[i];
        ac[i] = static_cast<long double>(c[i]) - a[i];
    }
    for (std::size_t i = 0; i < 3; ++i) {
        normal[i] = ab[(i + 1) % 3] * ac[(i + 2) % 3] - ab[(i + 2) % 3] * ac[(i + 1) % 3];
    }
    const auto normalLength = std::hypot(normal[0], normal[1], normal[2]);
    if (normalLength == 0 || !std::isfinite(normalLength)) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    const auto planeDistance = std::abs(normal[0] * a[0] + normal[1] * a[1] + normal[2] * a[2]) / normalLength;
    return std::max(0.0L, 1 - planeDistance);
}

} // namespace

PolylineResult<2> sampleCircle(const Circle2& circle, TessellationOptions options) {
    auto count = circleSegmentCount(circle.radius(), options);
    if (!count) {
        return std::unexpected(count.error());
    }
    std::vector<math::Point2> points;
    points.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i) {
        const auto angle = 2 * std::numbers::pi_v<long double> * i / *count;
        points.push_back({static_cast<double>(circle.radius() * std::cos(angle)),
                          static_cast<double>(circle.radius() * std::sin(angle))});
    }
    auto result = Polyline2::create({std::move(points), true});
    if (!result) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    return std::move(*result);
}

TessellationResult<2> tessellate(const Disk2& disk, TessellationOptions options) {
    auto circle = Circle2::create(disk.radius());
    if (!circle) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    auto contour = sampleCircle(*circle, options);
    if (!contour) {
        return std::unexpected(contour.error());
    }
    const auto points = contour->vertices();
    std::vector<math::Point2> positions;
    positions.reserve(points.size() + 1);
    positions.push_back({});
    positions.insert(positions.end(), points.begin(), points.end());
    std::vector<math::VectorUnsigned3> triangles;
    triangles.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        triangles.push_back(
            {{0, static_cast<std::uint32_t>(i + 1), static_cast<std::uint32_t>((i + 1) % points.size() + 1)}});
    }
    return makeMesh<2>({std::move(positions), std::move(triangles)});
}

TessellationResult<2> tessellate(const Rectangle2& rectangle, TessellationOptions options) {
    if (const auto valid = validateOptions(options); !valid) {
        return std::unexpected(valid.error());
    }
    if (options.maxPrimitiveCount < 2) {
        return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
    }
    const auto half = rectangle.size() * 0.5;
    if (half[0] == 0 || half[1] == 0) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    return makeMesh<2>({{{{-half[0], -half[1]}}, {{half[0], -half[1]}}, {{half[0], half[1]}}, {{-half[0], half[1]}}},
                        {{{0, 1, 2}}, {{0, 2, 3}}}});
}

TessellationResult<3> tessellate(const Box3& box, TessellationOptions options) {
    if (const auto valid = validateOptions(options); !valid) {
        return std::unexpected(valid.error());
    }
    if (options.maxPrimitiveCount < 12) {
        return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
    }
    const auto h = box.size() * 0.5;
    if (h[0] == 0 || h[1] == 0 || h[2] == 0) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    return makeMesh<3>({{{{-h[0], -h[1], -h[2]}},
                         {{h[0], -h[1], -h[2]}},
                         {{h[0], h[1], -h[2]}},
                         {{-h[0], h[1], -h[2]}},
                         {{-h[0], -h[1], h[2]}},
                         {{h[0], -h[1], h[2]}},
                         {{h[0], h[1], h[2]}},
                         {{-h[0], h[1], h[2]}}},
                        {{{0, 2, 1}},
                         {{0, 3, 2}},
                         {{4, 5, 6}},
                         {{4, 6, 7}},
                         {{0, 1, 5}},
                         {{0, 5, 4}},
                         {{1, 2, 6}},
                         {{1, 6, 5}},
                         {{2, 3, 7}},
                         {{2, 7, 6}},
                         {{3, 0, 4}},
                         {{3, 4, 7}}}});
}

TessellationResult<3> tessellate(const Sphere3& sphere, TessellationOptions options) {
    if (const auto valid = validateOptions(options); !valid) {
        return std::unexpected(valid.error());
    }
    if (options.maxPrimitiveCount < 8) {
        return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
    }
    // Subdivide an octahedron; keep unit directions until final scaling.
    std::vector<math::Point3> directions{{{0, 0, 1}}, {{0, 0, -1}}, {{1, 0, 0}},
                                         {{0, 1, 0}}, {{-1, 0, 0}}, {{0, -1, 0}}};
    std::vector<math::VectorUnsigned3> triangles{{{0, 2, 3}}, {{0, 3, 4}}, {{0, 4, 5}}, {{0, 5, 2}},
                                                 {{1, 3, 2}}, {{1, 4, 3}}, {{1, 5, 4}}, {{1, 2, 5}}};
    const auto allowedDeviation = static_cast<long double>(options.maxDeviation) / sphere.radius();
    for (;;) {
        bool isPrecise = true;
        for (const auto& triangle : triangles) {
            const auto& idx = triangle;
            auto deviation = sphereFaceDeviation(directions[idx[0]], directions[idx[1]], directions[idx[2]]);
            if (!deviation) {
                return std::unexpected(deviation.error());
            }
            if (*deviation > allowedDeviation) {
                isPrecise = false;
                break;
            }
        }
        if (isPrecise) {
            break;
        }
        if (triangles.size() > options.maxPrimitiveCount / 4) {
            return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
        }
        std::unordered_map<std::uint64_t, std::uint32_t> midpointCache;
        auto midpoint = [&](std::uint32_t a, std::uint32_t b) -> GeometryResult<std::uint32_t> {
            const auto [small, large] = std::minmax(a, b);
            const auto key = (static_cast<std::uint64_t>(small) << 32) | large;
            if (auto it = midpointCache.find(key); it != midpointCache.end()) {
                return it->second;
            }
            if (directions.size() >= std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
            }
            math::Point3 p;
            for (std::size_t i = 0; i < 3; ++i) {
                p[i] = directions[a][i] + directions[b][i];
            }
            const auto norm = std::hypot(p[0], p[1], p[2]);
            if (norm == 0 || !std::isfinite(norm)) {
                return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
            }
            for (auto& value : p) {
                value /= norm;
            }
            const auto index = static_cast<std::uint32_t>(directions.size());
            directions.push_back(p);
            midpointCache.emplace(key, index);
            return index;
        };
        std::vector<math::VectorUnsigned3> next;
        next.reserve(triangles.size() * 4);
        for (const auto& triangle : triangles) {
            const auto a = triangle[0];
            const auto b = triangle[1];
            const auto c = triangle[2];
            auto ab = midpoint(a, b);
            auto bc = midpoint(b, c);
            auto ca = midpoint(c, a);
            if (!ab) {
                return std::unexpected(ab.error());
            }
            if (!bc) {
                return std::unexpected(bc.error());
            }
            if (!ca) {
                return std::unexpected(ca.error());
            }
            next.insert(next.end(), {{{a, *ab, *ca}}, {{*ab, b, *bc}}, {{*ca, *bc, c}}, {{*ab, *bc, *ca}}});
        }
        triangles = std::move(next);
    }
    for (auto& point : directions) {
        for (auto& coordinate : point) {
            coordinate *= sphere.radius();
        }
    }
    return makeMesh<3>({std::move(directions), std::move(triangles)});
}

} // namespace huynity::objectlib
