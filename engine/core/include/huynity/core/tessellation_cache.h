#pragma once

#include "huynity/objectlib/operations/tessellate.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <variant>

namespace huynity::core {

class TessellationCache {
public:
    using Result = std::expected<std::shared_ptr<const objectlib::TessellatedGeometry>, objectlib::GeometryError>;

    [[nodiscard]] Result get(std::shared_ptr<const objectlib::Geometry> geometry, std::uint64_t geometryVersion,
                             objectlib::TessellationOptions options) {
        // В кеш попадает только допустимое положительное отклонение. Сравнение битов
        // сохраняет точность ключа и задаёт отношение эквивалентности даже для NaN при поиске.
        Key key{std::move(geometry), geometryVersion, std::bit_cast<std::uint64_t>(options.maxDeviation),
                options.maxPrimitiveCount};
        auto cached = m_entries.begin();
        if (m_entries.size() != 1 || cached->first != key) {
            cached = m_entries.find(key);
        }
        if (cached != m_entries.end()) {
            ++m_cacheHits;
            return cached->second;
        }

        if (const auto* mesh3 = std::get_if<objectlib::TriangleMesh3>(key.geometry.get())) {
            ++m_calculations;
            if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::InvalidOptions});
            }
            if (mesh3->triangles().size() > options.maxPrimitiveCount) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::BudgetExceeded});
            }
            auto resource = std::make_shared<const objectlib::TessellatedGeometry>(*mesh3);
            m_entries.emplace(std::move(key), resource);
            return resource;
        }
        if (const auto* mesh2 = std::get_if<objectlib::TriangleMesh2>(key.geometry.get())) {
            ++m_calculations;
            if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::InvalidOptions});
            }
            if (mesh2->triangles().size() > options.maxPrimitiveCount) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::BudgetExceeded});
            }
            auto resource = std::make_shared<const objectlib::TessellatedGeometry>(*mesh2);
            m_entries.emplace(std::move(key), resource);
            return resource;
        }

        auto calculated = objectlib::tessellate(*key.geometry, options);
        ++m_calculations;
        if (!calculated) {
            return std::unexpected(calculated.error());
        }
        auto resource = std::make_shared<const objectlib::TessellatedGeometry>(std::move(*calculated));
        m_entries.emplace(std::move(key), resource);
        return resource;
    }

    [[nodiscard]] std::size_t calculations() const noexcept { return m_calculations; }
    [[nodiscard]] std::size_t cacheHits() const noexcept { return m_cacheHits; }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

private:
    struct Key {
        std::shared_ptr<const objectlib::Geometry> geometry;
        std::uint64_t geometryVersion;
        std::uint64_t deviationBits;
        std::size_t maxPrimitiveCount;

        bool operator==(const Key&) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            auto hash = std::hash<const objectlib::Geometry*>{}(key.geometry.get());
            const auto combine = [&hash](std::size_t value) {
                constexpr std::size_t hashConstant = 0x9e3779b9;
                hash ^= value + hashConstant + (hash << 6) + (hash >> 2);
            };
            combine(std::hash<std::uint64_t>{}(key.geometryVersion));
            combine(std::hash<std::uint64_t>{}(key.deviationBits));
            combine(std::hash<std::size_t>{}(key.maxPrimitiveCount));
            return hash;
        }
    };

    std::unordered_map<Key, std::shared_ptr<const objectlib::TessellatedGeometry>, KeyHash> m_entries;
    std::size_t m_calculations{};
    std::size_t m_cacheHits{};
};

} // namespace huynity::core
