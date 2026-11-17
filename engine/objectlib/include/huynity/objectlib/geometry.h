#pragma once

#include "huynity/objectlib/collections/polygon2.h"
#include "huynity/objectlib/collections/polyline.h"
#include "huynity/objectlib/collections/triangle_mesh.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"
#include "huynity/objectlib/primitives/aabb.h"
#include "huynity/objectlib/primitives/line.h"
#include "huynity/objectlib/primitives/plane3.h"
#include "huynity/objectlib/primitives/point.h"
#include "huynity/objectlib/primitives/ray.h"
#include "huynity/objectlib/primitives/segment.h"
#include "huynity/objectlib/primitives/triangle.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/circle2.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/objectlib/shapes/sphere3.h"

#include <variant>

namespace huynity::objectlib {

using Geometry = std::variant<Point2, Point3, Segment2, Segment3, Line2, Line3, Ray2, Ray3, Triangle2, Triangle3,
                              Polyline2, Polyline3, Aabb2, Aabb3, TriangleMesh2, TriangleMesh3, Plane3, Polygon2,
                              Rectangle2, Circle2, Disk2, Sphere3, Box3>;

using GeometrySpec =
    std::variant<Point2::Params, Point3::Params, Segment2::Params, Segment3::Params, Line2::Params, Line3::Params,
                 Ray2::Params, Ray3::Params, Triangle2::Params, Triangle3::Params, Polyline2::Params, Polyline3::Params,
                 Aabb2::Params, Aabb3::Params, TriangleMesh2::Params, TriangleMesh3::Params, Plane3::Params,
                 Polygon2::Params, Rectangle2::Params, Circle2::Params, Disk2::Params, Sphere3::Params, Box3::Params>;

template <typename Params>
    requires SupportedParams<Params>
[[nodiscard]] CreateResult<Geometry> createGeometry(Params params) {
    using Object = typename Params::Object;

    auto object = Object::create(std::move(params));
    if (!object) {
        return std::unexpected(object.error());
    }

    return Geometry{std::in_place_type<Object>, std::move(*object)};
}

[[nodiscard]] CreateResult<Geometry> createGeometry(GeometrySpec spec);

} // namespace huynity::objectlib
