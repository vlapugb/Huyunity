#include "huynity/objectlib/geometry.h"

#include <type_traits>
#include <utility>
#include <variant>

namespace huynity::objectlib {

CreateResult<Geometry> createGeometry(GeometrySpec spec) {
    if (spec.valueless_by_exception()) {
        return std::unexpected(CreateError{CreateErrorCode::InvalidDescription});
    }

    return std::visit(
        [](auto params) -> CreateResult<Geometry> {
            using Params = std::remove_cvref_t<decltype(params)>;
            using Object = typename Params::Object;

            auto object = Object::create(std::move(params));
            if (!object) {
                return std::unexpected(object.error());
            }

            return Geometry{std::in_place_type<Object>, std::move(*object)};
        },
        std::move(spec));
}

} // namespace huynity::objectlib
