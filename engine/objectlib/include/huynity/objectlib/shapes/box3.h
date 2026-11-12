#pragma once

#include "huynity/math/vector.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Box3 {
public:
  struct Params {
    using Object = Box3;
    math::Vector3 size;
  };

  static constexpr auto Dimension = 3;

  static constexpr auto Type = ObjectType::Box;

  [[nodiscard]] static CreateResult<Box3> create(Params params);
  [[nodiscard]] static CreateResult<Box3> create(math::Vector3 size);

  [[nodiscard]] math::Vector3 size() const noexcept { return m_size; }

private:
  explicit Box3(math::Vector3 size) : m_size{size} {}
  math::Vector3 m_size;
};
} // namespace huynity::objectlib
