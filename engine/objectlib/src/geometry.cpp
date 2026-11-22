#include "huynity/objectlib/geometry.h"

#include "huynity/objectlib/objects.h"

#include <utility>
#include <variant>

namespace huynity::objectlib {

CreateResult<Geometry> createGeometry(GeometrySpec spec) {
    if (spec.valueless_by_exception()) {
        return std::unexpected(CreateError{CreateErrorCode::InvalidDescription});
    }

    return std::visit(
        [](SupportedParams auto&& params) -> CreateResult<Geometry> {
            using Params = decltype(params);
            return createGeometry(std::forward<Params>(params));
        },
        std::move(spec));
}

} // namespace huynity::objectlib
