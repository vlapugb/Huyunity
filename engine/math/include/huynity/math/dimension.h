#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <type_traits>

namespace huynity::math {

template <std::size_t dimension>
concept Dimension = dimension == 2 || dimension == 3 || dimension == 4;

namespace detail {

template <typename T, std::size_t dimension>
    requires Dimension<dimension> && std::is_arithmetic_v<T>
class DimContainer {
public:
    constexpr DimContainer() { m_components.fill(T()); }

    constexpr DimContainer(std::initializer_list<T> list) {
        const auto n = std::min(list.size(), dimension);
        std::copy_n(list.begin(), n, begin());
        std::fill(begin() + n, end(), T());
    }

    constexpr T& operator[](std::size_t i) { return m_components[i]; }
    constexpr T operator[](std::size_t i) const { return m_components[i]; }

    constexpr T* begin() { return m_components.begin(); }
    constexpr T* end() { return m_components.end(); }

    constexpr const T* begin() const { return m_components.begin(); }
    constexpr const T* end() const { return m_components.end(); }

    constexpr const T* cbegin() const noexcept { return m_components.cbegin(); }
    constexpr const T* cend() const noexcept { return m_components.cend(); }

    constexpr friend bool operator==(const DimContainer&, const DimContainer&) = default;

    [[nodiscard]] constexpr bool finite() const
        requires std::is_floating_point_v<T>
    {
        return std::ranges::all_of(m_components, [](T value) { return std::isfinite(value); });
    }

private:
    std::array<T, dimension> m_components;
};

} // namespace detail
} // namespace huynity::math
