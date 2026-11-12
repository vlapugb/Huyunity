#pragma once

#include "huynity/math/point.h"
#include "huynity/math/vector.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace huynity::math {

template <std::size_t dimension>
[[nodiscard]] bool finitePoints(std::span<const Point<dimension>> points) noexcept {
    return std::ranges::all_of(points, &Point<dimension>::finite);
}

template <std::size_t dimension>
[[nodiscard]] bool nonzero(const math::Vector<dimension>& vector) noexcept {
    return std::ranges::any_of(vector, [](double value) { return value != 0; });
}

[[nodiscard]] inline long double orientation(const math::Point2& a, const math::Point2& b,
                                             const math::Point2& c) noexcept {
    const auto abx = static_cast<long double>(b[0]) - a[0];
    const auto aby = static_cast<long double>(b[1]) - a[1];
    const auto acx = static_cast<long double>(c[0]) - a[0];
    const auto acy = static_cast<long double>(c[1]) - a[1];
    return abx * acy - aby * acx;
}

[[nodiscard]] inline int sign(long double value) noexcept {
    if (value < 0) {
        return -1;
    }
    if (value > 0) {
        return 1;
    }
    return 0;
}

[[nodiscard]] inline bool onSegment(const math::Point2& p, const math::Point2& a, const math::Point2& b) noexcept {
    return p[0] >= std::min(a[0], b[0]) && p[0] <= std::max(a[0], b[0]) && p[1] >= std::min(a[1], b[1]) &&
           p[1] <= std::max(a[1], b[1]);
}

[[nodiscard]] inline bool segmentsIntersect(const math::Point2& a, const math::Point2& b, const math::Point2& c,
                                            const math::Point2& d) noexcept {
    const auto abC = sign(orientation(a, b, c));
    const auto abD = sign(orientation(a, b, d));
    const auto cdA = sign(orientation(c, d, a));
    const auto cdB = sign(orientation(c, d, b));
    if (abC * abD < 0 && cdA * cdB < 0) {
        return true;
    }
    return (abC == 0 && onSegment(c, a, b)) || (abD == 0 && onSegment(d, a, b)) || (cdA == 0 && onSegment(a, c, d)) ||
           (cdB == 0 && onSegment(b, c, d));
}

[[nodiscard]] inline long double signedAreaTwice(std::span<const math::Point2> points) noexcept {
    long double area = 0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        area += orientation(points[0], points[i], points[i + 1]);
    }
    return area;
}

} // namespace huynity::math
