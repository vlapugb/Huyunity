#pragma once

#include "huynity/objectlib/errors.h"
#include "huynity/objectlib/objects.h"

namespace huynity::objectlib {

class Torus3 {
public:
    struct Params {
        using Object = Torus3;
        double majorRadius;
        double minorRadius;
    };

    static constexpr auto Dimension = 3;

    static constexpr auto Type = ObjectType::Torus;

    [[nodiscard]] static CreateResult<Torus3> create(Params params);
    [[nodiscard]] static CreateResult<Torus3> create(double radius, double height);

    [[nodiscard]] double majorRadius() const noexcept { return m_radius1; }
    [[nodiscard]] double minorRadius() const noexcept { return m_radius2; }

private:
    Torus3(double radius1, double radius2) : m_radius1(radius1), m_radius2(radius2) {}
    double m_radius1;
    double m_radius2;
};

} // namespace huynity::objectlib
