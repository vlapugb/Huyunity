#pragma once

#include "huynity/math/point.h"
#include "huynity/math/validation.h"
#include "huynity/math/vector.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class Ray {
public:
  struct Params {
    using Object = Ray<dimension>;
    math::Point<dimension> origin;
    math::Vector<dimension> direction;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::Ray;

  [[nodiscard]] static CreateResult<Ray> create(Params params) {
    return create(params.origin, params.direction);
  }

  [[nodiscard]] static CreateResult<Ray>
  create(math::Point<dimension> origin, math::Vector<dimension> direction) {
    if (!origin.finite() || !direction.finite()) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    if (!math::nonzero(direction)) {
      return std::unexpected(CreateError{CreateErrorCode::InvalidDirection});
    }
    return Ray(origin, direction);
  }

  [[nodiscard]] math::Point<dimension> origin() const noexcept {
    return m_origin;
  }
  [[nodiscard]] math::Vector<dimension> direction() const noexcept {
    return m_direction;
  }

private:
  Ray(math::Point<dimension> origin, math::Vector<dimension> direction)
      : m_origin{origin}, m_direction{direction} {}
  math::Point<dimension> m_origin;
  math::Vector<dimension> m_direction;
};

using Ray2 = Ray<2>;
using Ray3 = Ray<3>;
} // namespace huynity::objectlib
