#pragma once

#include "huynity/objectlib/collections/polyline.h"
#include "huynity/objectlib/primitives/segment.h"

#include <vector>

namespace huynity::objectlib {

template <std::size_t dimension>
[[nodiscard]] std::vector<Segment<dimension>>
segments(const Polyline<dimension> &polyline) {
  const auto points = polyline.vertices();
  std::vector<Segment<dimension>> result;
  if (points.size() < 2) {
    return result;
  }
  result.reserve(points.size() - 1 + polyline.closed());
  // Polyline::create guarantees finite vertices, so Segment::create cannot fail
  // here.
  for (std::size_t i = 1; i < points.size(); ++i) {
    result.push_back(*Segment<dimension>::create({points[i - 1], points[i]}));
  }
  if (polyline.closed()) {
    result.push_back(
        *Segment<dimension>::create({points.back(), points.front()}));
  }
  return result;
}

} // namespace huynity::objectlib
