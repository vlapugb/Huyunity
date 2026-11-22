#include "huynity/objectlib/operations/triangulate.h"

#include "huynity/math/validation.h"
#include "huynity/math/vector_unsigned.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>

namespace huynity::objectlib {

TessellationResult<2> triangulate(const Polygon2& polygon) {
    const auto points = polygon.contour();
    if (points.size() < 3) {
        return std::unexpected(GeometryError{GeometryErrorCode::InvalidGeometry});
    }
    if (points.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(GeometryError{GeometryErrorCode::BudgetExceeded});
    }
    std::vector<std::uint32_t> ring(points.size());
    std::iota(ring.begin(), ring.end(), std::uint32_t{0});
    if (math::signedAreaTwice(points) < 0) {
        std::ranges::reverse(ring);
    }

    // Straight boundary vertices are not needed as corners of output triangles.
    bool hasRemovedVertex = true;
    while (hasRemovedVertex && ring.size() > 3) {
        hasRemovedVertex = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = ring[(i + ring.size() - 1) % ring.size()];
            const auto b = ring[i];
            const auto c = ring[(i + 1) % ring.size()];
            if (math::orientation(points[a], points[b], points[c]) == 0) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                hasRemovedVertex = true;
                break;
            }
        }
    }
    std::vector<math::VectorUnsigned3> triangles;
    triangles.reserve(ring.size() - 2);
    while (ring.size() > 3) {
        bool hasFoundEar = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = ring[(i + ring.size() - 1) % ring.size()];
            const auto b = ring[i];
            const auto c = ring[(i + 1) % ring.size()];
            if (math::orientation(points[a], points[b], points[c]) <= 0) {
                continue;
            }
            bool isOccupied = false;
            for (auto other : ring) {
                if (other == a || other == b || other == c) {
                    continue;
                }
                const auto& p = points[other];
                if (math::orientation(points[a], points[b], p) >= 0 &&
                    math::orientation(points[b], points[c], p) >= 0 &&
                    math::orientation(points[c], points[a], p) >= 0) {
                    isOccupied = true;
                    break;
                }
            }
            if (isOccupied) {
                continue;
            }
            triangles.push_back({{a, b, c}});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            hasFoundEar = true;
            break;
        }
        if (!hasFoundEar) {
            return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
        }
    }
    if (math::orientation(points[ring[0]], points[ring[1]], points[ring[2]]) <= 0) {
        return std::unexpected(GeometryError{GeometryErrorCode::NumericalFailure});
    }
    triangles.push_back({{ring[0], ring[1], ring[2]}});
    auto mesh = TriangleMesh2::create({std::vector<math::Point2>{points.begin(), points.end()}, std::move(triangles)});
    if (!mesh) {
        return std::unexpected(GeometryError{GeometryErrorCode::InvalidGeometry});
    }
    return std::move(*mesh);
}

} // namespace huynity::objectlib
