#pragma once

#include "huynity/math/point.h"
#include "huynity/math/validation.h"
#include "huynity/math/vector_unsigned.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

#include <cmath>
#include <span>
#include <vector>

namespace huynity::objectlib {

template <std::size_t dimension>
  requires math::Dimension<dimension>
class TriangleMesh {
public:
  struct Params {
    using Object = TriangleMesh;
    std::vector<math::Point<dimension>> positions;
    std::vector<math::VectorUnsigned3> triangles;
  };

  static constexpr auto Dimension = dimension;

  static constexpr auto Type = ObjectType::TriangleMesh;

  [[nodiscard]] static CreateResult<TriangleMesh> create(Params params) {
    return create(std::move(params.positions), std::move(params.triangles));
  }

  [[nodiscard]] static CreateResult<TriangleMesh>
  create(std::vector<math::Point<dimension>> &&positions,
         std::vector<math::VectorUnsigned3> &&triangles) {
    if (!math::finitePoints<dimension>(positions)) {
      return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    for (const auto &triangle : triangles) {
      for (auto index : triangle) {
        if (index >= positions.size()) {
          return std::unexpected(CreateError{CreateErrorCode::InvalidIndex});
        }
      }
    }
    return TriangleMesh(std::move(positions), std::move(triangles));
  }

  [[nodiscard]] std::span<const math::Point<dimension>>
  positions() const noexcept {
    return m_positions;
  }
  [[nodiscard]] std::span<const math::VectorUnsigned3>
  triangles() const noexcept {
    return m_triangles;
  }

private:
  TriangleMesh(std::vector<math::Point<dimension>> positions,
               std::vector<math::VectorUnsigned3> triangles)
      : m_positions{std::move(positions)}, m_triangles{std::move(triangles)} {}

  std::vector<math::Point<dimension>> m_positions;
  std::vector<math::VectorUnsigned3> m_triangles;
};

using TriangleMesh2 = TriangleMesh<2>;
using TriangleMesh3 = TriangleMesh<3>;
} // namespace huynity::objectlib
