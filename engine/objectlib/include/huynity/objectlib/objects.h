#pragma once

#include "huynity/objectlib/errors.h"

#include <concepts>
#include <utility>

namespace huynity::objectlib {

enum class ObjectType {
  Aabb,
  Box,
  Circle,
  Disk,
  Point,
  Line,
  Plane,
  Polygon,
  Polyline,
  Ray,
  Rectangle,
  Segment,
  Sphere,
  Triangle,
  TriangleMesh,
};

template <typename Object>
concept SupportedObject = requires(typename Object::Params params) {
  { Object::create(std::move(params)) } -> std::same_as<CreateResult<Object>>;
  { Object::Type } -> std::convertible_to<ObjectType>;
};

template <typename Params>
concept SupportedParams =
    requires { typename Params::Object; } &&
    std::same_as<Params, typename Params::Object::Params> &&
    SupportedObject<typename Params::Object>;

template <typename Params>
  requires SupportedParams<Params>
[[nodiscard]] CreateResult<typename Params::Object>
createObject(Params params) {
  using Object = typename Params::Object;
  return Object::create(std::move(params));
}

template <typename Object, typename... Args>
[[nodiscard]] CreateResult<Object> createObject(Args &&...args) {
  return Object::create(std::forward<Args>(args)...);
}

} // namespace huynity::objectlib
