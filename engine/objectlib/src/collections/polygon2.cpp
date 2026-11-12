#include "huynity/objectlib/collections/polygon2.h"

#include "huynity/math/validation.h"
#include "huynity/objectlib/errors.h"

#include <cmath>
#include <utility>

namespace huynity::objectlib {

CreateResult<Polygon2> Polygon2::create(Polygon2::Params params) {
    return create(std::move(params.contour));
}

CreateResult<Polygon2> Polygon2::create(std::vector<math::Point2>&& contour) {
    if (!math::finitePoints<2>(contour)) {
        return std::unexpected(CreateError{CreateErrorCode::NonFiniteParameter});
    }
    if (contour.size() > 1 && contour.front() == contour.back()) {
        contour.pop_back();
    }
    if (contour.size() < 3) {
        return std::unexpected(CreateError{CreateErrorCode::TooFewVertices});
    }
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const auto& a = contour[i];
        const auto& b = contour[(i + 1) % contour.size()];
        const auto& c = contour[(i + 2) % contour.size()];
        if (a == b) {
            return std::unexpected(CreateError{CreateErrorCode::DegenerateGeometry});
        }
        const auto turn = math::orientation(a, b, c);
        if (!std::isfinite(turn)) {
            return std::unexpected(CreateError{CreateErrorCode::NumericalFailure});
        }
        if (turn == 0) {
            const long double forward =
                (static_cast<long double>(b[0]) - a[0]) * (static_cast<long double>(c[0]) - b[0]) +
                (static_cast<long double>(b[1]) - a[1]) * (static_cast<long double>(c[1]) - b[1]);
            if (forward < 0) {
                return std::unexpected(CreateError{CreateErrorCode::SelfIntersection});
            }
        }
        for (std::size_t j = i + 1; j < contour.size(); ++j) {
            if (j == i + 1 || (i == 0 && j + 1 == contour.size())) {
                continue;
            }
            if (math::segmentsIntersect(a, b, contour[j], contour[(j + 1) % contour.size()])) {
                return std::unexpected(CreateError{CreateErrorCode::SelfIntersection});
            }
        }
    }
    const auto area = math::signedAreaTwice(contour);
    if (!std::isfinite(area)) {
        return std::unexpected(CreateError{CreateErrorCode::NumericalFailure});
    }
    if (area == 0) {
        return std::unexpected(CreateError{CreateErrorCode::DegenerateGeometry});
    }
    return Polygon2(std::move(contour));
}

} // namespace huynity::objectlib
