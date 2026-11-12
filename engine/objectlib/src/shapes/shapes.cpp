#include "huynity/math/validation.h"
#include "huynity/math/vector.h"
#include "huynity/objectlib/primitives/plane3.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/circle2.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/objectlib/shapes/sphere3.h"

#include <algorithm>
#include <cmath>

namespace huynity::objectlib {

namespace {

std::optional<CreateError> validateRadius(double radius) {
    if (!std::isfinite(radius)) {
        return CreateError{CreateErrorCode::NonFiniteParameter};
    }
    if (radius <= 0) {
        return CreateError{CreateErrorCode::InvalidRadius};
    }
    return std::nullopt;
}

template <std::size_t dimension>
std::optional<CreateError> validateSize(const math::Vector<dimension>& size) {
    if (!size.finite()) {
        return CreateError{CreateErrorCode::NonFiniteParameter};
    }
    if (std::ranges::any_of(std::ranges::subrange(size.cbegin(), size.cend()),
                            [](double value) { return value <= 0; })) {
        return CreateError{CreateErrorCode::InvalidDimensions};
    }
    return std::nullopt;
}

} // namespace

CreateResult<Sphere3> Sphere3::create(Sphere3::Params params) {
    return create(params.radius);
}

CreateResult<Sphere3> Sphere3::create(double radius) {
    if (const auto error = validateRadius(radius); error) {
        return std::unexpected(*error);
    }
    return Sphere3(radius);
}

CreateResult<Circle2> Circle2::create(Circle2::Params params) {
    return create(params.radius);
}

CreateResult<Circle2> Circle2::create(double radius) {
    if (const auto error = validateRadius(radius); error) {
        return std::unexpected(*error);
    }
    return Circle2(radius);
}

CreateResult<Disk2> Disk2::create(Disk2::Params params) {
    return create(params.radius);
}

CreateResult<Disk2> Disk2::create(double radius) {
    if (const auto error = validateRadius(radius); error) {
        return std::unexpected(*error);
    }
    return Disk2(radius);
}

CreateResult<Rectangle2> Rectangle2::create(Rectangle2::Params params) {
    return create(params.size);
}

CreateResult<Rectangle2> Rectangle2::create(math::Vector2 size) {
    if (const auto error = validateSize(size); error) {
        return std::unexpected(*error);
    }
    return Rectangle2(size);
}

CreateResult<Box3> Box3::create(Box3::Params params) {
    return create(params.size);
}

CreateResult<Box3> Box3::create(math::Vector3 size) {
    if (const auto error = validateSize(size); error) {
        return std::unexpected(*error);
    }
    return Box3(size);
}

CreateResult<Plane3> Plane3::create(Plane3::Params params) {
    return create(params.normal, params.offset);
}

CreateResult<Plane3> Plane3::create(math::Vector3 normal, double offset) {
    if (!normal.finite() || !std::isfinite(offset)) {
        return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    if (!math::nonzero(normal)) {
        return std::unexpected(CreateError{CreateErrorCode::InvalidDirection});
    }
    return Plane3(normal, offset);
}

} // namespace huynity::objectlib
