#pragma once

#include "huynity/math/point.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

#include <span>
#include <vector>

namespace huynity::objectlib {

class Polygon2 {
public:
  struct Params {
    using Object = Polygon2;
    std::vector<math::Point2> contour;
  };

  static constexpr auto Dimension = 2;

  static constexpr auto Type = ObjectType::Polygon;

  [[nodiscard]] static CreateResult<Polygon2> create(Params params);
  [[nodiscard]] static CreateResult<Polygon2>
  create(std::vector<math::Point2> &&contour);

  [[nodiscard]] std::span<const math::Point2> contour() const noexcept {
    return m_contour;
  }

private:
  explicit Polygon2(std::vector<math::Point2> contour)
      : m_contour{std::move(contour)} {}
  std::vector<math::Point2> m_contour;
};
} // namespace huynity::objectlib
