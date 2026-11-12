#include "huynity/objectlib/geometry.h"
#include "huynity/objectlib/operations/bounds.h"
#include "huynity/objectlib/operations/segments.h"
#include "huynity/objectlib/operations/tessellate.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace objects = huynity::objectlib;
namespace math = huynity::math;

#define CHECK(...)                                                             \
  do {                                                                         \
    if (!(__VA_ARGS__)) {                                                      \
      throw std::runtime_error(std::string(__FILE__) + ":" +                   \
                               std::to_string(__LINE__) + ": " +               \
                               #__VA_ARGS__);                                  \
    }                                                                          \
  } while (false)

template <typename T>
concept HasBounds =
    requires(const T &object) { objects::bounds(object); } &&
    (!std::same_as<decltype(objects::bounds(std::declval<const T &>())),
                   objects::RuntimeBoundsResult>);

static_assert(objects::SupportedParams<objects::Sphere3::Params>);
static_assert(objects::SupportedParams<objects::Polyline2::Params>);
static_assert(!objects::SupportedParams<int>);
static_assert(
    std::same_as<decltype(objects::createObject(objects::Sphere3::Params{})),
                 objects::CreateResult<objects::Sphere3>>);
static_assert(objects::Sphere3::Type == objects::ObjectType::Sphere);
static_assert(objects::Rectangle2::Type == objects::ObjectType::Rectangle);
static_assert(!std::default_initializable<objects::Sphere3>);
static_assert(!std::constructible_from<objects::Sphere3, double>);
static_assert(!std::default_initializable<objects::Polyline2>);
static_assert(!math::Dimension<1>);
static_assert(HasBounds<objects::Sphere3> && !HasBounds<objects::Line2> &&
              !HasBounds<objects::Ray3> && !HasBounds<objects::Plane3>);
static_assert(std::same_as<
              decltype(std::declval<const objects::Polyline2 &>().vertices()),
              std::span<const math::Point2>>);
static_assert(std::variant_size_v<objects::Geometry> == 23);
static_assert(std::variant_size_v<objects::GeometrySpec> == 23);

template <typename T, typename E> T take(std::expected<T, E> value) {
  CHECK(value.has_value());
  return std::move(*value);
}

bool near(double a, double b, double tolerance = 1e-12) {
  return std::abs(a - b) <= tolerance;
}
math::Point2 makePoint2(double x, double y) { return {{x, y}}; }
math::Point3 makePoint3(double x, double y, double z) { return {{x, y, z}}; }

template <typename T>
void createError(const objects::CreateResult<T> &result,
                 objects::CreateErrorCode code) {
  CHECK(!result);
  CHECK(result.error().code == code);
}

template <typename T>
void geometryError(const objects::GeometryResult<T> &result,
                   objects::GeometryErrorCode code) {
  CHECK(!result);
  CHECK(result.error().code == code);
}

double meshArea(const objects::TriangleMesh2 &mesh) {
  double area = 0;
  for (const auto &triangle : mesh.triangles()) {
    const auto &idx = triangle;
    const auto &a = mesh.positions()[idx[0]];
    const auto &b = mesh.positions()[idx[1]];
    const auto &c = mesh.positions()[idx[2]];
    const double twice =
        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    CHECK(twice > 0);
    area += twice / 2;
  }
  return area;
}

void creation() {
  const objects::Sphere3::Params defaults{};
  createError(objects::Sphere3::create(defaults),
              objects::CreateErrorCode::InvalidRadius);
  auto sphere =
      take(objects::createObject(objects::Sphere3::Params{.radius = 2}));
  CHECK(sphere.radius() == 2);
  for (double invalid : {0.0, -1.0, -0.0}) {
    createError(objects::Sphere3::create(objects::Sphere3::Params{invalid}),
                objects::CreateErrorCode::InvalidRadius);
  }
  for (double invalid : {std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
    createError(objects::Sphere3::create(objects::Sphere3::Params{invalid}),
                objects::CreateErrorCode::NonFiniteParameter);
    createError(objects::createObject(objects::Point2::Params{{invalid, 0}}),
                objects::CreateErrorCode::NonFiniteParameter);
  }
  CHECK(take(objects::Circle2::create(objects::Circle2::Params{3})).radius() ==
        3);
  CHECK(take(objects::Disk2::create(objects::Disk2::Params{3})).radius() == 3);
  CHECK(take(objects::Rectangle2::create(
                 objects::Rectangle2::Params{math::Vector2{{2, 4}}}))
            .size() == math::Vector2{{2, 4}});
  CHECK(take(objects::Box3::create(
                 objects::Box3::Params{math::Vector3{{2, 4, 6}}}))
            .size() == math::Vector3{{2, 4, 6}});
  createError(objects::Rectangle2::create(
                  objects::Rectangle2::Params{math::Vector2{{0, 4}}}),
              objects::CreateErrorCode::InvalidDimensions);
  createError(
      objects::Box3::create(objects::Box3::Params{math::Vector3{{2, -1, 6}}}),
      objects::CreateErrorCode::InvalidDimensions);
}

void primitives() {
  auto segment = take(objects::Segment2::create(
      objects::Segment2::Params{makePoint2(1, 2), makePoint2(1, 2)}));
  CHECK(segment.a() == segment.b());
  CHECK(take(objects::Line3::create(objects::Line3::Params{
                 makePoint3(1, 2, 3), math::Vector3{{0, 0, 2}}}))
            .direction()[2] == 2);
  CHECK(take(objects::Ray2::create(objects::Ray2::Params{
                 makePoint2(1, 2), math::Vector2{{3, 0}}}))
            .origin() == makePoint2(1, 2));
  createError(objects::Line2::create(
                  objects::Line2::Params{math::Point2{}, math::Vector2{}}),
              objects::CreateErrorCode::InvalidDirection);
  createError(objects::Ray3::create(
                  objects::Ray3::Params{math::Point3{}, math::Vector3{}}),
              objects::CreateErrorCode::InvalidDirection);
  createError(
      objects::Plane3::create(objects::Plane3::Params{math::Vector3{}, 1}),
      objects::CreateErrorCode::InvalidDirection);
  auto plane = take(objects::Plane3::create(
      objects::Plane3::Params{math::Vector3{{0, 0, 2}}, -4}));
  CHECK(plane.normal()[2] * 2 + plane.offset() == 0);
  CHECK(take(objects::Triangle2::create(objects::Triangle2::Params{
                 makePoint2(0, 0), makePoint2(2, 0), makePoint2(0, 2)}))
            .vertices()
            .size() == 3);
  CHECK(
      take(objects::Triangle3::create(objects::Triangle3::Params{
               makePoint3(0, 0, 1), makePoint3(2, 0, 1), makePoint3(0, 2, 1)}))
          .vertices()[2][2] == 1);
  createError(objects::Triangle2::create(objects::Triangle2::Params{
                  makePoint2(0, 0), makePoint2(1, 1), makePoint2(2, 2)}),
              objects::CreateErrorCode::DegenerateGeometry);
  createError(
      objects::Triangle3::create(objects::Triangle3::Params{
          makePoint3(0, 0, 0), makePoint3(1, 1, 1), makePoint3(2, 2, 2)}),
      objects::CreateErrorCode::DegenerateGeometry);
  CHECK(take(objects::Aabb2::create(
                 objects::Aabb2::Params{makePoint2(1, 2), makePoint2(1, 2)}))
            .min() == makePoint2(1, 2));
  createError(objects::Aabb3::create(objects::Aabb3::Params{
                  makePoint3(1, 0, 0), makePoint3(0, 1, 1)}),
              objects::CreateErrorCode::InvalidBounds);
  CHECK(math::dot(math::Vector3{{1, 2, 3}}, math::Vector3{{4, 5, 6}}) == 32);
  CHECK(math::cross(math::Vector3{{1, 0, 0}}, math::Vector3{{0, 1, 0}}) ==
        math::Vector3{{0, 0, 1}});
  CHECK(math::length(math::Vector2{{3, 4}}) == 5);
}

void collections() {
  createError(objects::Polyline2::create(objects::Polyline2::Params{{}, false}),
              objects::CreateErrorCode::TooFewVertices);
  createError(objects::Polyline2::create(objects::Polyline2::Params{
                  {makePoint2(0, 0), makePoint2(1, 0)}, true}),
              objects::CreateErrorCode::TooFewVertices);
  auto line = [] {
    objects::Polyline2::Params params{
        {makePoint2(0, 0), makePoint2(2, 0), makePoint2(2, 2)}, true};
    auto created = take(objects::createObject(params));
    params.vertices[0] = makePoint2(100, 100);
    return created;
  }();
  CHECK(line.vertices()[0] == makePoint2(0, 0));
  auto copy = line;
  line = take(objects::Polyline2::create(
      objects::Polyline2::Params{{makePoint2(5, 5), makePoint2(6, 6)}, false}));
  CHECK(copy.vertices()[0] == makePoint2(0, 0));
  CHECK(objects::segments(copy).size() == 3);
  CHECK(objects::segments(copy).back().b() == makePoint2(0, 0));
  CHECK(objects::segments(line).size() == 1);
  auto mesh =
      take(objects::TriangleMesh3::create(objects::TriangleMesh3::Params{
          {makePoint3(0, 0, 0), makePoint3(1, 0, 0), makePoint3(0, 1, 0)},
          {math::VectorUnsigned3{{0, 1, 2}}}}));
  CHECK(mesh.triangles()[0][2] == 2);
  createError(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{
                  {makePoint2(0, 0)}, {math::VectorUnsigned3{{0, 1, 2}}}}),
              objects::CreateErrorCode::InvalidIndex);
  CHECK(take(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{}))
            .positions()
            .empty());
  // A mesh is a container: unlike a standalone Triangle, it can contain
  // degenerate faces.
  CHECK(take(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{
                 {makePoint2(0, 0)}, {math::VectorUnsigned3{{0, 0, 0}}}}))
            .triangles()
            .size() == 1);
  auto polygon = take(objects::Polygon2::create(objects::Polygon2::Params{
      {makePoint2(0, 0), makePoint2(2, 0), makePoint2(2, 2), makePoint2(0, 2),
       makePoint2(0, 0)}}));
  CHECK(polygon.contour().size() == 4);
  createError(objects::Polygon2::create(objects::Polygon2::Params{
                  {makePoint2(0, 0), makePoint2(2, 2), makePoint2(0, 2),
                   makePoint2(2, 0)}}),
              objects::CreateErrorCode::SelfIntersection);
  createError(objects::Polygon2::create(objects::Polygon2::Params{
                  {makePoint2(0, 0), makePoint2(2, 0), makePoint2(1, 0),
                   makePoint2(1, 2), makePoint2(0, 2)}}),
              objects::CreateErrorCode::SelfIntersection);
}

template <objects::SupportedParams Params>
void compareFactories(Params params, bool isValid = true) {
  using Object = typename Params::Object;

  auto typed = objects::createObject(params);
  auto dynamic =
      objects::createGeometry(objects::GeometrySpec{std::move(params)});
  CHECK(typed.has_value() == isValid);
  CHECK(dynamic.has_value() == isValid);
  if (!typed) {
    CHECK(dynamic.error() == typed.error());
    return;
  }
  CHECK(std::holds_alternative<Object>(*dynamic));
  const auto &actual = std::get<Object>(*dynamic);
  if constexpr (requires { actual.radius(); }) {
    CHECK(actual.radius() == typed->radius());
  }
  if constexpr (requires { actual.size(); }) {
    CHECK(actual.size() == typed->size());
  }
  if constexpr (requires { actual.vertices(); }) {
    CHECK(std::ranges::equal(actual.vertices(), typed->vertices()));
  }
  if constexpr (requires { actual.closed(); }) {
    CHECK(actual.closed() == typed->closed());
  }
  if constexpr (requires { actual.contour(); }) {
    CHECK(std::ranges::equal(actual.contour(), typed->contour()));
  }
  if constexpr (requires { actual.positions(); }) {
    CHECK(std::ranges::equal(actual.positions(), typed->positions()));
    CHECK(std::ranges::equal(actual.triangles(), typed->triangles()));
  }
  if constexpr (requires { actual.normal(); }) {
    CHECK(actual.normal() == typed->normal());
    CHECK(actual.offset() == typed->offset());
  }
  if constexpr (requires { actual.direction(); }) {
    CHECK(actual.direction() == typed->direction());
    CHECK(actual.origin() == typed->origin());
  }
  if constexpr (requires { actual.min(); }) {
    CHECK(actual.min() == typed->min());
    CHECK(actual.max() == typed->max());
  }
  if constexpr (requires { actual.a(); }) {
    CHECK(actual.a() == typed->a());
    CHECK(actual.b() == typed->b());
  }
  if constexpr (requires { actual.coordinates(); }) {
    CHECK(actual.coordinates() == typed->coordinates());
  }
}

template <typename T> void compareRuntimeBounds(const T &object) {
  auto typed = objects::bounds(object);
  auto dynamic = objects::bounds(objects::Geometry{object});
  CHECK(typed.has_value() == dynamic.has_value());
  if (!typed) {
    CHECK(dynamic.error() == typed.error());
    return;
  }
  using Box = typename decltype(typed)::value_type;
  CHECK(std::holds_alternative<Box>(*dynamic));
  const auto &actual = std::get<Box>(*dynamic);
  CHECK(actual.min() == typed->min());
  CHECK(actual.max() == typed->max());
}

void runtimeBounds() {
  static_assert(std::same_as<decltype(objects::bounds(
                                 std::declval<const objects::Geometry &>())),
                             objects::RuntimeBoundsResult>);
  compareRuntimeBounds(take(objects::Point2::create(makePoint2(2, 3))));
  compareRuntimeBounds(take(objects::Point3::create(makePoint3(2, 3, 4))));
  compareRuntimeBounds(take(objects::Segment2::create(
      objects::Segment2::Params{makePoint2(3, 4), makePoint2(-1, 8)})));
  compareRuntimeBounds(take(objects::Segment3::create(
      objects::Segment3::Params{makePoint3(3, 4, -2), makePoint3(-1, 8, 5)})));
  compareRuntimeBounds(
      take(objects::Triangle2::create(objects::Triangle2::Params{
          makePoint2(0, 0), makePoint2(1, 0), makePoint2(0, 1)})));
  compareRuntimeBounds(
      take(objects::Triangle3::create(objects::Triangle3::Params{
          makePoint3(0, 0, 0), makePoint3(1, 0, 0), makePoint3(0, 1, 0)})));
  compareRuntimeBounds(
      take(objects::Polyline2::create(objects::Polyline2::Params{
          {makePoint2(4, 5), makePoint2(-2, 7)}, false})));
  compareRuntimeBounds(
      take(objects::Polyline3::create(objects::Polyline3::Params{
          {makePoint3(4, 5, 6), makePoint3(-2, 7, -8)}, false})));
  compareRuntimeBounds(take(objects::Aabb2::create(
      objects::Aabb2::Params{makePoint2(-1, -2), makePoint2(3, 4)})));
  compareRuntimeBounds(take(objects::Aabb3::create(
      objects::Aabb3::Params{makePoint3(-1, -2, -3), makePoint3(3, 4, 5)})));
  compareRuntimeBounds(
      take(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{
          {makePoint2(0, 0), makePoint2(1, 0), makePoint2(0, 1)},
          {math::VectorUnsigned3{{0, 1, 2}}}})));
  compareRuntimeBounds(
      take(objects::TriangleMesh3::create(objects::TriangleMesh3::Params{
          {makePoint3(0, 0, 0), makePoint3(1, 0, 0), makePoint3(0, 1, 0)},
          {math::VectorUnsigned3{{0, 1, 2}}}})));
  compareRuntimeBounds(take(objects::Polygon2::create(objects::Polygon2::Params{
      {makePoint2(4, 5), makePoint2(6, 5), makePoint2(4, 8)}})));
  compareRuntimeBounds(take(objects::Rectangle2::create(
      objects::Rectangle2::Params{math::Vector2{{2, 4}}})));
  compareRuntimeBounds(
      take(objects::Circle2::create(objects::Circle2::Params{2})));
  compareRuntimeBounds(take(objects::Disk2::create(objects::Disk2::Params{2})));
  compareRuntimeBounds(
      take(objects::Sphere3::create(objects::Sphere3::Params{2})));
  compareRuntimeBounds(take(
      objects::Box3::create(objects::Box3::Params{math::Vector3{{2, 4, 6}}})));

  const auto line2 = take(objects::Line2::create(
      objects::Line2::Params{math::Point2{}, math::Vector2{{1, 0}}}));
  geometryError(objects::bounds(objects::Geometry{line2}),
                objects::GeometryErrorCode::UnsupportedOperation);
  const auto line3 = take(objects::Line3::create(
      objects::Line3::Params{math::Point3{}, math::Vector3{{0, 0, 1}}}));
  geometryError(objects::bounds(objects::Geometry{line3}),
                objects::GeometryErrorCode::UnsupportedOperation);
  const auto ray2 = take(objects::Ray2::create(
      objects::Ray2::Params{math::Point2{}, math::Vector2{{1, 0}}}));
  geometryError(objects::bounds(objects::Geometry{ray2}),
                objects::GeometryErrorCode::UnsupportedOperation);
  const auto ray3 = take(objects::Ray3::create(
      objects::Ray3::Params{math::Point3{}, math::Vector3{{0, 0, 1}}}));
  geometryError(objects::bounds(objects::Geometry{ray3}),
                objects::GeometryErrorCode::UnsupportedOperation);
  const auto plane = take(objects::Plane3::create(
      objects::Plane3::Params{math::Vector3{{0, 0, 1}}, 0}));
  geometryError(objects::bounds(objects::Geometry{plane}),
                objects::GeometryErrorCode::UnsupportedOperation);
  const auto emptyMesh =
      take(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{}));
  geometryError(objects::bounds(objects::Geometry{emptyMesh}),
                objects::GeometryErrorCode::EmptyGeometry);

  // Point validates its coordinates in the factory, so an invalid Point cannot
  // be built through the public API.
  auto point = take(objects::Point2::create(makePoint2(2, 3)));
  CHECK(take(objects::bounds(point)).min() == makePoint2(2, 3));

  auto isolated = [] {
    objects::Geometry geometry{
        take(objects::Polyline2::create(objects::Polyline2::Params{
            {makePoint2(4, 5), makePoint2(-2, 7)}, false}))};
    return objects::bounds(geometry);
  }();
  const auto &isolatedBox = std::get<objects::Aabb2>(*isolated);
  CHECK(isolatedBox.min() == makePoint2(-2, 5));
  CHECK(isolatedBox.max() == makePoint2(4, 7));
}

template <typename T>
void compareRuntimeTessellation(const T &object,
                                objects::TessellationOptions options = {}) {
  auto typed = [&] {
    if constexpr (std::same_as<T, objects::Circle2>) {
      return objects::sampleCircle(object, options);
    } else {
      return objects::tessellate(object, options);
    }
  }();
  auto dynamic = objects::tessellate(objects::Geometry{object}, options);
  CHECK(typed.has_value() == dynamic.has_value());
  if (!typed) {
    CHECK(dynamic.error() == typed.error());
    return;
  }
  using Result = typename decltype(typed)::value_type;
  CHECK(std::holds_alternative<Result>(*dynamic));
  const auto &actual = std::get<Result>(*dynamic);
  if constexpr (std::same_as<Result, objects::Polyline2>) {
    CHECK(actual.closed() == typed->closed());
    CHECK(std::ranges::equal(actual.vertices(), typed->vertices()));
  } else {
    CHECK(std::ranges::equal(actual.positions(), typed->positions()));
    CHECK(std::ranges::equal(actual.triangles(), typed->triangles()));
  }
}

void runtimeTessellation() {
  static_assert(std::same_as<decltype(objects::tessellate(
                                 std::declval<const objects::Geometry &>())),
                             objects::RuntimeTessellationResult>);
  const objects::TessellationOptions options{.maxDeviation = 0.05,
                                             .maxPrimitiveCount = 4096};
  compareRuntimeTessellation(
      take(objects::Circle2::create(objects::Circle2::Params{2})), options);
  compareRuntimeTessellation(
      take(objects::Disk2::create(objects::Disk2::Params{2})), options);
  compareRuntimeTessellation(
      take(objects::Rectangle2::create(
          objects::Rectangle2::Params{math::Vector2{{2, 4}}})),
      options);
  compareRuntimeTessellation(
      take(objects::Sphere3::create(objects::Sphere3::Params{2})), options);
  compareRuntimeTessellation(take(objects::Box3::create(objects::Box3::Params{
                                 math::Vector3{{2, 4, 6}}})),
                             options);

  geometryError(
      objects::tessellate(objects::Geometry{take(objects::Circle2::create(
                              objects::Circle2::Params{2}))},
                          {.maxDeviation = 0}),
      objects::GeometryErrorCode::InvalidOptions);
  geometryError(
      objects::tessellate(objects::Geometry{take(objects::Sphere3::create(
                              objects::Sphere3::Params{2}))},
                          {.maxDeviation = 0.0001, .maxPrimitiveCount = 8}),
      objects::GeometryErrorCode::BudgetExceeded);
  const auto tiny =
      take(objects::Rectangle2::create(objects::Rectangle2::Params{
          math::Vector2{{std::numeric_limits<double>::denorm_min(), 1}}}));
  geometryError(objects::tessellate(objects::Geometry{tiny}),
                objects::GeometryErrorCode::NumericalFailure);
  geometryError(
      objects::tessellate(objects::Geometry{
          take(objects::Polygon2::create(objects::Polygon2::Params{
              {makePoint2(0, 0), makePoint2(1, 0), makePoint2(0, 1)}}))}),
      objects::GeometryErrorCode::UnsupportedOperation);
  geometryError(objects::tessellate(objects::Geometry{
                    take(objects::Point2::create(makePoint2(1, 2)))}),
                objects::GeometryErrorCode::UnsupportedOperation);

  auto centered = take(
      objects::tessellate(objects::Geometry{take(objects::Rectangle2::create(
          objects::Rectangle2::Params{math::Vector2{{2, 4}}}))}));
  const auto &centeredMesh = std::get<objects::TriangleMesh2>(centered);
  CHECK(centeredMesh.positions()[0] == makePoint2(-1, -2));
  CHECK(centeredMesh.positions()[2] == makePoint2(1, 2));

  auto isolated = [] {
    objects::Geometry geometry{
        take(objects::Disk2::create(objects::Disk2::Params{2}))};
    return objects::tessellate(
        geometry, {.maxDeviation = 0.05, .maxPrimitiveCount = 4096});
  }();
  const auto &isolatedMesh = std::get<objects::TriangleMesh2>(*isolated);
  CHECK(isolatedMesh.positions().front() == makePoint2(0, 0));
  CHECK(isolatedMesh.positions().size() == isolatedMesh.triangles().size() + 1);
}

void runtime() {
  runtimeBounds();
  runtimeTessellation();
  compareFactories(objects::Point2::Params{{1, 2}});
  compareFactories(objects::Point3::Params{{1, 2, 3}});
  compareFactories(
      objects::Segment2::Params{makePoint2(0, 0), makePoint2(1, 1)});
  compareFactories(
      objects::Segment3::Params{makePoint3(0, 0, 0), makePoint3(1, 1, 1)});
  compareFactories(
      objects::Line2::Params{math::Point2{}, math::Vector2{{1, 0}}});
  compareFactories(
      objects::Line3::Params{math::Point3{}, math::Vector3{{0, 0, 1}}});
  compareFactories(
      objects::Ray2::Params{math::Point2{}, math::Vector2{{1, 0}}});
  compareFactories(
      objects::Ray3::Params{math::Point3{}, math::Vector3{{0, 0, 1}}});
  compareFactories(objects::Triangle2::Params{
      makePoint2(0, 0), makePoint2(1, 0), makePoint2(0, 1)});
  compareFactories(objects::Triangle3::Params{
      makePoint3(0, 0, 0), makePoint3(1, 0, 0), makePoint3(0, 1, 0)});
  compareFactories(
      objects::Polyline2::Params{{makePoint2(0, 0), makePoint2(1, 1)}, false});
  compareFactories(objects::Polyline3::Params{
      {makePoint3(0, 0, 0), makePoint3(1, 1, 1)}, false});
  compareFactories(objects::Aabb2::Params{makePoint2(0, 0), makePoint2(1, 1)});
  compareFactories(
      objects::Aabb3::Params{makePoint3(0, 0, 0), makePoint3(1, 1, 1)});
  compareFactories(objects::TriangleMesh2::Params{});
  compareFactories(objects::TriangleMesh3::Params{});
  compareFactories(objects::Plane3::Params{math::Vector3{{0, 0, 1}}, 0});
  compareFactories(objects::Polygon2::Params{
      {makePoint2(0, 0), makePoint2(1, 0), makePoint2(0, 1)}});
  compareFactories(objects::Rectangle2::Params{math::Vector2{{2, 4}}});
  compareFactories(objects::Circle2::Params{2});
  compareFactories(objects::Disk2::Params{2});
  compareFactories(objects::Sphere3::Params{2});
  compareFactories(objects::Box3::Params{math::Vector3{{2, 4, 6}}});
  compareFactories(objects::Sphere3::Params{-1}, false);
  compareFactories(objects::Polyline2::Params{{}, true}, false);
  compareFactories(objects::Ray3::Params{math::Point3{}, math::Vector3{}},
                   false);
  compareFactories(
      objects::TriangleMesh2::Params{{}, {math::VectorUnsigned3{{0, 1, 2}}}},
      false);
}

void bounds() {
  auto line = take(objects::Polyline3::create(objects::Polyline3::Params{
      {makePoint3(-2, 5, 1), makePoint3(7, -1, 3), makePoint3(0, 2, -4)},
      false}));
  auto box = take(objects::bounds(line));
  CHECK(box.min() == makePoint3(-2, -1, -4));
  CHECK(box.max() == makePoint3(7, 5, 3));
  auto sphere = take(objects::Sphere3::create(objects::Sphere3::Params{2}));
  auto sphereBox = take(objects::bounds(sphere));
  CHECK(sphereBox.min() == makePoint3(-2, -2, -2));
  CHECK(sphereBox.max() == makePoint3(2, 2, 2));
  CHECK(take(objects::bounds(take(objects::Rectangle2::create(
                 objects::Rectangle2::Params{math::Vector2{{2, 4}}}))))
            .max() == makePoint2(1, 2));
  CHECK(take(objects::bounds(take(objects::Box3::create(
                 objects::Box3::Params{math::Vector3{{2, 4, 6}}}))))
            .min() == makePoint3(-1, -2, -3));
  auto empty =
      take(objects::TriangleMesh2::create(objects::TriangleMesh2::Params{}));
  geometryError(objects::bounds(empty),
                objects::GeometryErrorCode::EmptyGeometry);
  CHECK(take(objects::bounds(take(objects::Point2::create(makePoint2(2, 3)))))
            .min() == makePoint2(2, 3));
}

void triangulation() {
  std::vector<math::Point2> contour{makePoint2(0, 0), makePoint2(3, 0),
                                    makePoint2(3, 1), makePoint2(1, 1),
                                    makePoint2(1, 3), makePoint2(0, 3)};
  for (int direction = 0; direction < 2; ++direction) {
    auto polygon =
        take(objects::Polygon2::create(objects::Polygon2::Params{contour}));
    auto mesh = take(objects::triangulate(polygon));
    CHECK(near(meshArea(mesh), 5));
    for (const auto &triangle : mesh.triangles()) {
      const auto &idx = triangle;
      const auto a = mesh.positions()[idx[0]];
      const auto b = mesh.positions()[idx[1]];
      const auto c = mesh.positions()[idx[2]];
      for (int i = 0; i <= 4; ++i) {
        for (int j = 0; j + i <= 4; ++j) {
          const double x = (i * a[0] + j * b[0] + (4 - i - j) * c[0]) / 4;
          const double y = (i * a[1] + j * b[1] + (4 - i - j) * c[1]) / 4;
          CHECK(x >= 0 && y >= 0 && x <= 3 && y <= 3 && (x <= 1 || y <= 1));
        }
      }
    }
    std::ranges::reverse(contour);
  }
  auto flatCorner = take(objects::Polygon2::create(objects::Polygon2::Params{
      {makePoint2(0, 0), makePoint2(1, 0), makePoint2(2, 0), makePoint2(2, 2),
       makePoint2(0, 2)}}));
  CHECK(near(meshArea(take(objects::triangulate(flatCorner))), 4));

  // Simple radial polygons exercise different concavities; shoelace area is
  // independent of the ear-clipping choices used by the implementation.
  std::mt19937 generator{12345};
  std::uniform_real_distribution<double> radii(0.5, 3.0);
  for (int sample = 0; sample < 100; ++sample) {
    const int count = 3 + sample % 13;
    std::vector<math::Point2> points;
    for (int i = 0; i < count; ++i) {
      const double angle = 2 * std::numbers::pi * i / count;
      const double radius = radii(generator);
      points.push_back(
          makePoint2(radius * std::cos(angle), radius * std::sin(angle)));
    }
    double expectedArea = 0;
    for (int i = 0; i < count; ++i) {
      const auto &a = points[i];
      const auto &b = points[(i + 1) % count];
      expectedArea += a[0] * b[1] - a[1] * b[0];
    }
    auto polygon = take(objects::Polygon2::create(
        objects::Polygon2::Params{std::move(points)}));
    CHECK(near(meshArea(take(objects::triangulate(polygon))), expectedArea / 2,
               1e-10));
  }
}

void checkClosedSurface(const objects::TriangleMesh3 &mesh, double scale) {
  std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
  for (const auto &triangle : mesh.triangles()) {
    const auto &idx = triangle;
    math::Vector3 a{};
    math::Vector3 b{};
    math::Vector3 c{};
    for (std::size_t i = 0; i < 3; ++i) {
      a[i] = mesh.positions()[idx[0]][i] / scale;
      b[i] = mesh.positions()[idx[1]][i] / scale;
      c[i] = mesh.positions()[idx[2]][i] / scale;
    }
    CHECK(math::dot(a, math::cross(b - a, c - a)) > 0);
    for (int edge = 0; edge < 3; ++edge) {
      const auto first = idx[edge];
      const auto second = idx[(edge + 1) % 3];
      ++edges[std::minmax(first, second)];
    }
  }
  for (const auto &[edge, count] : edges) {
    (void)edge;
    CHECK(count == 2);
  }
  CHECK(static_cast<long long>(mesh.positions().size()) -
            static_cast<long long>(edges.size()) +
            static_cast<long long>(mesh.triangles().size()) ==
        2);
}

void tessellation() {
  auto circle = take(objects::Circle2::create(objects::Circle2::Params{2}));
  const objects::TessellationOptions options{.maxDeviation = 0.02,
                                             .maxPrimitiveCount = 4096};
  auto contour = take(objects::sampleCircle(circle, options));
  CHECK(contour.closed());
  for (const auto &edge : objects::segments(contour)) {
    math::Vector2 midpoint{
        {(edge.a()[0] + edge.b()[0]) / 2, (edge.a()[1] + edge.b()[1]) / 2}};
    CHECK(2 - math::length(midpoint) <= options.maxDeviation + 1e-12);
  }
  CHECK(take(objects::sampleCircle(circle, {.maxDeviation = 0.005}))
            .vertices()
            .size() >= contour.vertices().size());
  auto disk = take(objects::tessellate(
      take(objects::Disk2::create(objects::Disk2::Params{2})), options));
  CHECK(meshArea(disk) <= 4 * std::numbers::pi);
  CHECK(4 * std::numbers::pi - meshArea(disk) <=
        4 * std::numbers::pi * options.maxDeviation);
  CHECK(near(meshArea(take(objects::tessellate(take(objects::Rectangle2::create(
                 objects::Rectangle2::Params{math::Vector2{{2, 4}}}))))),
             8));
  auto box =
      take(objects::tessellate(take(objects::Box3::create(objects::Box3::Params{
                                   math::Vector3{{2, 4, 6}}})),
                               options));
  checkClosedSurface(box, 1);
  double volume = 0;
  for (const auto &triangle : box.triangles()) {
    const auto &idx = triangle;
    const auto a = box.positions()[idx[0]] - makePoint3(0, 0, 0);
    const auto b = box.positions()[idx[1]] - makePoint3(0, 0, 0);
    const auto c = box.positions()[idx[2]] - makePoint3(0, 0, 0);
    volume += math::dot(a, math::cross(b, c)) / 6;
  }
  CHECK(near(volume, 48));
  auto sphere = take(objects::Sphere3::create(objects::Sphere3::Params{2}));
  auto sphereMesh = take(objects::tessellate(sphere, options));
  checkClosedSurface(sphereMesh, 2);
  CHECK(sphereMesh.triangles().size() <= options.maxPrimitiveCount);
  for (const auto &point : sphereMesh.positions()) {
    CHECK(near(math::length(point - makePoint3(0, 0, 0)), 2));
  }
  for (const auto &triangle : sphereMesh.triangles()) {
    const auto &idx = triangle;
    const auto a = sphereMesh.positions()[idx[0]] - makePoint3(0, 0, 0);
    const auto b = sphereMesh.positions()[idx[1]] - makePoint3(0, 0, 0);
    const auto c = sphereMesh.positions()[idx[2]] - makePoint3(0, 0, 0);
    const auto normal = math::cross(b - a, c - a);
    const auto distance = math::dot(a, normal) / math::length(normal);
    CHECK(2 - distance <= options.maxDeviation + 1e-12);
  }
  geometryError(objects::sampleCircle(circle, {.maxDeviation = 0}),
                objects::GeometryErrorCode::InvalidOptions);
  geometryError(objects::sampleCircle(
                    circle, {.maxDeviation = 0.001, .maxPrimitiveCount = 3}),
                objects::GeometryErrorCode::BudgetExceeded);
  geometryError(objects::tessellate(
                    sphere, {.maxDeviation = 0.0001, .maxPrimitiveCount = 8}),
                objects::GeometryErrorCode::BudgetExceeded);
  geometryError(
      objects::tessellate(take(objects::Box3::create(
                              objects::Box3::Params{math::Vector3{{1, 1, 1}}})),
                          {.maxPrimitiveCount = 11}),
      objects::GeometryErrorCode::BudgetExceeded);
}

void numerics() {
  const double huge = std::numeric_limits<double>::max();
  auto segment = take(objects::Segment2::create(
      objects::Segment2::Params{makePoint2(-huge, 0), makePoint2(huge, 0)}));
  CHECK(take(objects::bounds(segment)).max()[0] == huge);
  auto tiny = take(objects::Triangle2::create(objects::Triangle2::Params{
      makePoint2(0, 0), makePoint2(1e-200, 0), makePoint2(0, 1e-200)}));
  CHECK(tiny.vertices()[1][0] == 1e-200);
  auto circle = take(objects::Circle2::create(objects::Circle2::Params{huge}));
  geometryError(objects::sampleCircle(
                    circle, {.maxDeviation = 1e-200, .maxPrimitiveCount = 100}),
                objects::GeometryErrorCode::BudgetExceeded);
  auto tinyRectangle =
      take(objects::Rectangle2::create(objects::Rectangle2::Params{
          math::Vector2{{std::numeric_limits<double>::denorm_min(), 1}}}));
  geometryError(objects::tessellate(tinyRectangle),
                objects::GeometryErrorCode::NumericalFailure);
  geometryError(objects::bounds(tinyRectangle),
                objects::GeometryErrorCode::NumericalFailure);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  geometryError(objects::sampleCircle(
                    take(objects::Circle2::create(objects::Circle2::Params{1})),
                    {.maxDeviation = nan}),
                objects::GeometryErrorCode::InvalidOptions);
  geometryError(objects::tessellate(
                    take(objects::Sphere3::create(objects::Sphere3::Params{1})),
                    {.maxPrimitiveCount = 0}),
                objects::GeometryErrorCode::InvalidOptions);
  // Scaling must not overflow in the sphere algorithm's intermediate
  // computations.
  auto largeSphere = take(objects::tessellate(
      take(objects::Sphere3::create(objects::Sphere3::Params{1e200})),
      {.maxDeviation = 1e198, .maxPrimitiveCount = 8192}));
  checkClosedSurface(largeSphere, 1e200);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    return 2;
  }
  const std::map<std::string_view, void (*)()> tests{
      {"creation", creation},
      {"primitives", primitives},
      {"collections", collections},
      {"runtime", runtime},
      {"bounds", bounds},
      {"triangulation", triangulation},
      {"tessellation", tessellation},
      {"numerics", numerics},
  };
  try {
    const auto it = tests.find(argv[1]);
    if (it == tests.end()) {
      return 2;
    }
    it->second();
    std::cout << argv[1] << ": OK\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
