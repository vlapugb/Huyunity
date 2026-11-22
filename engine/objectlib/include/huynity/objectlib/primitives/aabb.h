#pragma once

#include "huynity/math/point.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {
template <std::size_t dimension>
    requires math::Dimension<dimension>
class Aabb {
public:
    struct Params {
        using Object = Aabb<dimension>;
        math::Point<dimension> min;
        math::Point<dimension> max;
    };

    static constexpr auto Dimension = dimension;

    static constexpr auto Type = ObjectType::Aabb;

    [[nodiscard]] static CreateResult<Aabb> create(Params params) { return create(params.min, params.max); }

    [[nodiscard]] static CreateResult<Aabb> create(math::Point<dimension> min, math::Point<dimension> max) {
        if (!min.finite() || !max.finite()) {
            return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
        }
        for (std::size_t i = 0; i < dimension; ++i) {
            if (min[i] > max[i]) {
                return std::unexpected(CreateError{CreateErrorCode::InvalidBounds});
            }
        }
        return Aabb(min, max);
    }

    [[nodiscard]] math::Point<dimension> min() const noexcept { return m_min; }
    [[nodiscard]] math::Point<dimension> max() const noexcept { return m_max; }

private:
    Aabb(math::Point<dimension> min, math::Point<dimension> max) : m_min{min}, m_max{max} {}
    math::Point<dimension> m_min;
    math::Point<dimension> m_max;
};

using Aabb2 = Aabb<2>;
using Aabb3 = Aabb<3>;
} // namespace huynity::objectlib
