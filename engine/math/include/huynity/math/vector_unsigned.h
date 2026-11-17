#pragma once

#include "huynity/math/dimension.h"

#include <cstdint>

namespace huynity::math {

template <std::size_t dimension>
struct VectorUnsigned : detail::DimContainer<std::uint32_t, dimension> {
    using detail::DimContainer<std::uint32_t, dimension>::DimContainer;
};

using VectorUnsigned2 = VectorUnsigned<2>;
using VectorUnsigned3 = VectorUnsigned<3>;
} // namespace huynity::math
