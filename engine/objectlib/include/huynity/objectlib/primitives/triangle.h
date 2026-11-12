#pragma once

#include "huynity/math/validation.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

#include <cmath>

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class Triangle {
public:
  struct Params {
    using Object = Triangle<dimension>;
    math::Point<dimension> a;
    math::Point<dimension> b;
    math::Point<dimension> c;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::Triangle;

  [[nodiscard]] static CreateResult<Triangle> create(Params params) {
    return create(params.a, params.b, params.c);
  }

  [[nodiscard]] static CreateResult<Triangle> create(math::Point<dimension> a,
                                                     math::Point<dimension> b,
                                                     math::Point<dimension> c) {
    const auto vertices = std::array{a, b, c};
    if (!math::finitePoints<dimension>(vertices)) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    auto isDegenerate = true;
    if constexpr (dimension == 2) {
      const auto area = math::orientation(a, b, c);
      if (!std::isfinite(area)) {
        return std::unexpected(CreateError{CreateErrorCode::NumericalFailure});
      }
      isDegenerate = area == 0;
    } else {
      std::array<long double, 3> u{};
      std::array<long double, 3> v{};
      std::array<long double, 3> normal{};
      for (std::size_t i = 0; i < 3; ++i) {
        u[i] = static_cast<long double>(b[i]) - a[i];
        v[i] = static_cast<long double>(c[i]) - a[i];
      }
      for (std::size_t i = 0; i < 3; ++i) {
        normal[i] =
            u[(i + 1) % 3] * v[(i + 2) % 3] - u[(i + 2) % 3] * v[(i + 1) % 3];
        if (!std::isfinite(normal[i])) {
          return std::unexpected(
              CreateError{CreateErrorCode::NumericalFailure});
        }
      }
      isDegenerate = normal[0] == 0 && normal[1] == 0 && normal[2] == 0;
    }
    if (isDegenerate) {
      return std::unexpected(CreateError{CreateErrorCode::DegenerateGeometry});
    }
    return Triangle(vertices);
  }

  [[nodiscard]] std::span<const math::Point<dimension>, 3>
  vertices() const noexcept {
    return m_vertices;
  }

private:
  explicit Triangle(std::array<math::Point<dimension>, 3> vertices)
      : m_vertices{vertices} {}
  std::array<math::Point<dimension>, 3> m_vertices;
};

using Triangle2 = Triangle<2>;
using Triangle3 = Triangle<3>;
} // namespace huynity::objectlib
