#pragma once

#include "huynity/math/dimension.h"
#include "huynity/math/vector.h"

#include <cmath>

namespace huynity::math {

template <std::size_t dimension>
struct Point : detail::DimContainer<double, dimension> {
  using detail::DimContainer<double, dimension>::DimContainer;
};

using Point2 = Point<2>;
using Point3 = Point<3>;

template <std::size_t dimension>
[[nodiscard]] constexpr Vector<dimension>
operator-(const Point<dimension> &a, const Point<dimension> &b) noexcept {
  Vector<dimension> result;
  for (std::size_t i = 0; i < dimension; ++i) {
    result[i] = a[i] - b[i];
  }
  return result;
}

template <std::size_t dimension>
[[nodiscard]] constexpr Point<dimension>
operator+(Point<dimension> p, const Vector<dimension> &v) noexcept {
  for (std::size_t i = 0; i < dimension; ++i) {
    p[i] += v[i];
  }
  return p;
}

} // namespace huynity::math
