#pragma once

#include "huynity/math/validation.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

#include <cmath>
#include <span>
#include <vector>

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class Polyline {
public:
  struct Params {
    using Object = Polyline;
    std::vector<math::Point<dimension>> vertices;
    bool closed;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::Polyline;

  [[nodiscard]] static CreateResult<Polyline> create(Params params) {
    return create(std::move(params.vertices), params.closed);
  }

  [[nodiscard]] static CreateResult<Polyline>
  create(std::vector<math::Point<dimension>> &&vertices, bool closed) {
    if (!math::finitePoints<dimension>(vertices)) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    if (vertices.size() < (closed ? 3U : 2U)) {
      return std::unexpected(CreateError{CreateErrorCode::TooFewVertices});
    }
    return Polyline(std::move(vertices), closed);
  }

  [[nodiscard]] std::span<const math::Point<dimension>>
  vertices() const noexcept {
    return m_vertices;
  }
  [[nodiscard]] bool closed() const noexcept { return m_isClosed; }

private:
  Polyline(std::vector<math::Point<dimension>> vertices, bool closed)
      : m_vertices{std::move(vertices)}, m_isClosed{closed} {}

  std::vector<math::Point<dimension>> m_vertices;
  bool m_isClosed;
};

using Polyline2 = Polyline<2>;
using Polyline3 = Polyline<3>;
} // namespace huynity::objectlib
