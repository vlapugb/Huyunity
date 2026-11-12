#include "huynity/objectlib/geometry.h"
#include "huynity/objectlib/operations/bounds.h"
#include "huynity/objectlib/operations/segments.h"
#include "huynity/objectlib/operations/tessellate.h"
#include "huynity/objectlib/operations/triangulate.h"

#include <concepts>
#include <cstddef>
#include <expected>
#include <print>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace objects = huynity::objectlib;
namespace math = huynity::math;

template <typename T, typename E> T take(std::expected<T, E> result) {
  if (!result) {
    throw std::runtime_error("Object Library operation failed");
  }
  return std::move(*result);
}

int main() {
  // Typed creation through Params

  const auto sphere =
      take(objects::createObject(objects::Sphere3::Params{.radius = 2.0}));
  std::println("sphere radius = {}", sphere.radius());

  // Convenience overload: createObject<Object>(args...)

  const auto box =
      take(objects::createObject<objects::Box3>(math::Vector3{{2, 4, 6}}));

  const auto point =
      take(objects::createObject<objects::Point2>(math::Point2{{1, 2}}));

  const auto segment = take(objects::createObject<objects::Segment2>(
      math::Point2{{0, 0}}, math::Point2{{2, 0}}));

  std::println("box size = {}, {}, {}", box.size()[0], box.size()[1],
               box.size()[2]);

  std::println("point = {}, {}", point.coordinates()[0],
               point.coordinates()[1]);

  std::println("segment a = {}, {}", segment.a()[0], segment.a()[1]);

  const auto line = take(objects::createObject<objects::Line3>(
      math::Point3{{0, 0, 0}}, math::Vector3{{0, 0, 1}}));

  const auto plane = take(
      objects::createObject<objects::Plane3>(math::Vector3{{0, 0, 1}}, -2.0));

  const auto triangle = take(objects::createObject<objects::Triangle2>(
      math::Point2{{0, 0}}, math::Point2{{2, 0}}, math::Point2{{0, 2}}));

  const auto aabb = take(objects::createObject<objects::Aabb2>(
      math::Point2{{-1, -2}}, math::Point2{{3, 4}}));

  std::println("line direction z = {}", line.direction()[2]);
  std::println("plane offset = {}", plane.offset());
  std::println("triangle vertices = {}", triangle.vertices().size());
  std::println("aabb min = {}, {}", aabb.min()[0], aabb.min()[1]);

  // Collections and ownership

  const auto polyline = take(objects::createObject<objects::Polyline2>(
      std::vector<math::Point2>{
          math::Point2{{0, 0}},
          math::Point2{{2, 0}},
          math::Point2{{2, 2}},
      },
      true));

  std::println("polyline vertices = {}, segments = {}",
               polyline.vertices().size(), objects::segments(polyline).size());

  const auto polygon =
      take(objects::createObject<objects::Polygon2>(std::vector<math::Point2>{
          math::Point2{{0, 0}},
          math::Point2{{4, 0}},
          math::Point2{{4, 4}},
          math::Point2{{0, 4}},
      }));

  std::println("polygon contour = {}", polygon.contour().size());

  const auto mesh = take(objects::createObject<objects::TriangleMesh2>(
      std::vector<math::Point2>{
          math::Point2{{0, 0}},
          math::Point2{{2, 0}},
          math::Point2{{0, 2}},
      },
      std::vector<math::VectorUnsigned3>{
          math::VectorUnsigned3{{0, 1, 2}},
      }));

  std::println("mesh positions = {}, triangles = {}", mesh.positions().size(),
               mesh.triangles().size());

  // Typed operations

  const objects::TessellationOptions options{
      .maxDeviation = 0.05,
      .maxPrimitiveCount = 4096,
  };

  const auto sphereMesh = take(objects::tessellate(sphere, options));
  const auto polygonMesh = take(objects::triangulate(polygon));
  const auto polygonBounds = take(objects::bounds(polygon));

  std::println("sphere mesh triangles = {}", sphereMesh.triangles().size());
  std::println("polygon triangles = {}", polygonMesh.triangles().size());
  std::println("polygon bounds = [{}, {}] .. [{}, {}]", polygonBounds.min()[0],
               polygonBounds.min()[1], polygonBounds.max()[0],
               polygonBounds.max()[1]);

  const auto circle = take(objects::createObject<objects::Circle2>(2.0));
  const auto circlePolyline = take(objects::sampleCircle(circle, options));

  std::println("circle polyline vertices = {}",
               circlePolyline.vertices().size());

  // Dynamic creation through GeometrySpec

  objects::GeometrySpec spec = objects::Box3::Params{math::Vector3{{1, 2, 3}}};

  const objects::Geometry dynamicGeometry =
      take(objects::createGeometry(std::move(spec)));

  const objects::Geometry directGeometry =
      take(objects::createGeometry(objects::Sphere3::Params{.radius = 3.0}));

  std::println("dynamic geometry index = {}", dynamicGeometry.index());
  std::println("direct geometry index = {}", directGeometry.index());

  // Runtime operations on Geometry

  const auto runtimeBounds = take(objects::bounds(dynamicGeometry));

  std::visit(
      [](const auto &result) {
        using Box = std::decay_t<decltype(result)>;
        std::println("runtime bounds:");
        for (std::size_t i = 0; i < Box::Dimension; ++i) {
          std::println("  dimension {}: [{}, {}]", i, result.min()[i],
                       result.max()[i]);
        }
      },
      runtimeBounds);

  const auto runtimeMesh = take(objects::tessellate(dynamicGeometry, options));

  std::visit(
      [](const auto &result) {
        using Result = std::decay_t<decltype(result)>;
        if constexpr (std::same_as<Result, objects::Polyline2>) {
          std::println("runtime polyline vertices = {}",
                       result.vertices().size());
        } else {
          std::println("runtime mesh triangles = {}",
                       result.triangles().size());
        }
      },
      runtimeMesh);

  return 0;
}
