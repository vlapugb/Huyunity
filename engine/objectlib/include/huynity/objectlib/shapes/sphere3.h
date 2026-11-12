#pragma once

#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Sphere3 {
public:
  struct Params {
    using Object = Sphere3;
    double radius;
  };

  static constexpr auto Dimension = 3;

  static constexpr auto Type = ObjectType::Sphere;

  [[nodiscard]] static CreateResult<Sphere3> create(Params params);
  [[nodiscard]] static CreateResult<Sphere3> create(double radius);

  [[nodiscard]] double radius() const noexcept { return m_radius; }

private:
  explicit Sphere3(double radius) : m_radius{radius} {}
  double m_radius;
};
} // namespace huynity::objectlib
