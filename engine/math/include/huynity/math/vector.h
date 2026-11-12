#pragma once

#include "huynity/math/dimension.h"

#include <cmath>

namespace huynity::math {

template <std::size_t dimension>
struct Vector : detail::DimContainer<double, dimension> {
  using detail::DimContainer<double, dimension>::DimContainer;
};

using Vector2 = Vector<2>;
using Vector3 = Vector<3>;

template <std::size_t dimension>
[[nodiscard]] constexpr Vector<dimension>
operator+(Vector<dimension> a, const Vector<dimension> &b) noexcept {
  for (std::size_t i = 0; i < dimension; ++i) {
    a[i] += b[i];
  }
  return a;
}

template <std::size_t dimension>
[[nodiscard]] constexpr Vector<dimension>
operator-(Vector<dimension> a, const Vector<dimension> &b) noexcept {
  for (std::size_t i = 0; i < dimension; ++i) {
    a[i] -= b[i];
  }
  return a;
}

template <std::size_t dimension>
[[nodiscard]] constexpr Vector<dimension> operator*(Vector<dimension> v,
                                                    double scalar) noexcept {
  for (double &value : v) {
    value *= scalar;
  }
  return v;
}

template <std::size_t dimension>
[[nodiscard]] constexpr double dot(const Vector<dimension> &a,
                                   const Vector<dimension> &b) noexcept {
  double result = 0;
  for (std::size_t i = 0; i < dimension; ++i) {
    result += a[i] * b[i];
  }
  return result;
}

[[nodiscard]] constexpr Vector3 cross(const Vector3 &a,
                                      const Vector3 &b) noexcept {
  const auto x = a[1] * b[2] - a[2] * b[1];
  const auto y = a[2] * b[0] - a[0] * b[2];
  const auto z = a[0] * b[1] - a[1] * b[0];
  return {x, y, z};
}

template <std::size_t dimension>
[[nodiscard]] double length(const Vector<dimension> &v) noexcept {
  if constexpr (dimension == 2) {
    return std::hypot(v[0], v[1]);
  } else {
    return std::hypot(v[0], v[1], v[2]);
  }
}

} // namespace huynity::math
