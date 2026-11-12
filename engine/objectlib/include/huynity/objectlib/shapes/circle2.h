#pragma once

#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Circle2 {
public:
  struct Params {
    using Object = Circle2;
    double radius;
  };

  static constexpr auto Dimension = 2;

  static constexpr auto Type = ObjectType::Circle;

  [[nodiscard]] static CreateResult<Circle2> create(Params params);
  [[nodiscard]] static CreateResult<Circle2> create(double radius);

  [[nodiscard]] double radius() const noexcept { return m_radius; }

private:
  explicit Circle2(double radius) : m_radius{radius} {}
  double m_radius;
};
} // namespace huynity::objectlib
