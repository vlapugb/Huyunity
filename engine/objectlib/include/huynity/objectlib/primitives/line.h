#pragma once

#include "huynity/math/point.h"
#include "huynity/math/validation.h"
#include "huynity/math/vector.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class Line {
public:
  struct Params {
    using Object = Line<dimension>;
    math::Point<dimension> origin;
    math::Vector<dimension> direction;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::Line;

  [[nodiscard]] static CreateResult<Line> create(Params params) {
    return create(params.origin, params.direction);
  }

  [[nodiscard]] static CreateResult<Line>
  create(math::Point<dimension> origin, math::Vector<dimension> direction) {
    if (!origin.finite() || !direction.finite()) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    if (!math::nonzero(direction)) {
      return std::unexpected(CreateError{CreateErrorCode::InvalidDirection});
    }
    return Line(origin, direction);
  }

  [[nodiscard]] math::Point<dimension> origin() const noexcept {
    return m_origin;
  }
  [[nodiscard]] math::Vector<dimension> direction() const noexcept {
    return m_direction;
  }

private:
  Line(math::Point<dimension> origin, math::Vector<dimension> direction)
      : m_origin{origin}, m_direction{direction} {}
  math::Point<dimension> m_origin;
  math::Vector<dimension> m_direction;
};

using Line2 = Line<2>;
using Line3 = Line<3>;
} // namespace huynity::objectlib
