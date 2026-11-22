#pragma once

#include "huynity/math/vector.h"
#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Rectangle2 {
public:
    struct Params {
        using Object = Rectangle2;
        math::Vector2 size;
    };

    static constexpr auto Dimension = 2;

    static constexpr auto Type = ObjectType::Rectangle;

    [[nodiscard]] static CreateResult<Rectangle2> create(Params params);
    [[nodiscard]] static CreateResult<Rectangle2> create(math::Vector2 size);

    [[nodiscard]] math::Vector2 size() const noexcept { return m_size; }

private:
    explicit Rectangle2(math::Vector2 size) : m_size{size} {}
    math::Vector2 m_size;
};
} // namespace huynity::objectlib
