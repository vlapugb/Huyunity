#pragma once

#include "huynity/math/point.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

template <std::size_t dimension>
    requires math::Dimension<dimension>
class Point {
public:
    struct Params {
        using Object = Point<dimension>;
        math::Point<dimension> coordinates;
    };

    static constexpr auto Dimension = dimension;

    static constexpr auto Type = ObjectType::Point;

    [[nodiscard]] static CreateResult<Point> create(Params params) { return create(params.coordinates); }

    [[nodiscard]] static CreateResult<Point> create(math::Point<dimension> coords) {
        if (!coords.finite()) {
            return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
        }
        return Point(coords);
    }

    [[nodiscard]] math::Point<dimension> coordinates() const noexcept { return m_coords; }

private:
    explicit Point(math::Point<dimension> coords) : m_coords(coords) {}

    math::Point<dimension> m_coords;
};

using Point2 = Point<2>;
using Point3 = Point<3>;
} // namespace huynity::objectlib
