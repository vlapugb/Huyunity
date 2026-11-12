#pragma once

#include "huynity/math/point.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class Segment {
public:
  struct Params {
    using Object = Segment<dimension>;
    math::Point<dimension> a;
    math::Point<dimension> b;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::Segment;

  [[nodiscard]] static CreateResult<Segment> create(Params params) {
    return create(params.a, params.b);
  }

  [[nodiscard]] static CreateResult<Segment> create(math::Point<dimension> a,
                                                    math::Point<dimension> b) {
    if (!a.finite() || !b.finite()) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    return Segment(a, b);
  }

  [[nodiscard]] math::Point<dimension> a() const noexcept {
    return m_firstPoint;
  }
  [[nodiscard]] math::Point<dimension> b() const noexcept {
    return m_secondPoint;
  }

private:
  Segment(math::Point<dimension> a, math::Point<dimension> b)
      : m_firstPoint{a}, m_secondPoint{b} {}
  math::Point<dimension> m_firstPoint;
  math::Point<dimension> m_secondPoint;
};

using Segment2 = Segment<2>;
using Segment3 = Segment<3>;
} // namespace huynity::objectlib
