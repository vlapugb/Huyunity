#pragma once

#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Cone3 {
public:
    struct Params {
        using Object = Cone3;
        double radius;
        double height;
    };

    static constexpr auto Dimension = 3;

    static constexpr auto Type = ObjectType::Cone;

    [[nodiscard]] static CreateResult<Cone3> create(Params params);
    [[nodiscard]] static CreateResult<Cone3> create(double radius, double height);

    [[nodiscard]] double radius() const noexcept { return m_radius; }
    [[nodiscard]] double height() const noexcept { return m_height; }

private:
    Cone3(double radius, double height) : m_radius(radius), m_height(height) {}
    double m_radius;
    double m_height;
};

} // namespace huynity::objectlib
