#pragma once

#include "huynity/math/vector.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Plane3 {
public:
  struct Params {
    using Object = Plane3;
    math::Vector3 normal;
    double offset;
  };

  static constexpr auto Dimension = 3;

  static constexpr auto Type = ObjectType::Plane;

  [[nodiscard]] static CreateResult<Plane3> create(Params params);
  [[nodiscard]] static CreateResult<Plane3> create(math::Vector3 normal,
                                                   double offset);

  [[nodiscard]] math::Vector3 normal() const noexcept { return m_normal; }
  [[nodiscard]] double offset() const noexcept { return m_offset; }

private:
  Plane3(math::Vector3 normal, double offset)
      : m_normal{normal}, m_offset{offset} {}
  math::Vector3 m_normal;
  double m_offset;
};
} // namespace huynity::objectlib
