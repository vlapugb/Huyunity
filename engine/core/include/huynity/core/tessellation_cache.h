#pragma once

#include "huynity/objectlib/operations/tessellate.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <set>
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
        const Key key{geometry.get(), geometryVersion, std::bit_cast<std::uint64_t>(options.maxDeviation),
                      options.maxPrimitiveCount};
        auto cached = m_entries.begin();
        if (m_entries.size() != 1 || cached->first != key) {
            cached = m_entries.find(key);
        }
        if (cached != m_entries.end()) {
            if (!cached->second.source.expired()) {
                ++m_cacheHits;
                return cached->second.tessellation;
            }
            // Адрес освобождённой геометрии мог быть выдан новому объекту.
            m_entries.erase(cached);
        }
        collectDetachedSources();

        if (const auto* mesh3 = std::get_if<objectlib::TriangleMesh3>(geometry.get())) {
            ++m_calculations;
            if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::InvalidOptions});
            }
            if (mesh3->triangles().size() > options.maxPrimitiveCount) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::BudgetExceeded});
            }
            auto resource = std::make_shared<const objectlib::TessellatedGeometry>(*mesh3);
            m_entries.emplace(key, Entry{geometry, resource});
            return resource;
        }
        if (const auto* mesh2 = std::get_if<objectlib::TriangleMesh2>(geometry.get())) {
            ++m_calculations;
            if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::InvalidOptions});
            }
            if (mesh2->triangles().size() > options.maxPrimitiveCount) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::BudgetExceeded});
            }
            auto resource = std::make_shared<const objectlib::TessellatedGeometry>(*mesh2);
            m_entries.emplace(key, Entry{geometry, resource});
            return resource;
        }

        auto calculated = objectlib::tessellate(*geometry, options);
        ++m_calculations;
        if (!calculated) {
            return std::unexpected(calculated.error());
        }
        auto resource = std::make_shared<const objectlib::TessellatedGeometry>(std::move(*calculated));
        m_entries.emplace(key, Entry{geometry, resource});
        return resource;
    }

    [[nodiscard]] std::size_t calculations() const noexcept { return m_calculations; }
    [[nodiscard]] std::size_t cacheHits() const noexcept { return m_cacheHits; }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    /// @brief Отмечает источник, от которого отказался экземпляр.
    void markSourceDetached(const std::shared_ptr<const objectlib::Geometry>& source) {
        if (source && !m_entries.empty()) {
            m_detachedSources.emplace(source);
            m_checksUntilSweep = 0;
        }
    }

    /// @brief Удаляет записи после потери источников, включая удержанные внешним SceneView.
    void collectDetachedSources() {
        if (m_detachedSources.empty()) {
            return;
        }
        if (m_checksUntilSweep > 0) {
            --m_checksUntilSweep;
            return;
        }
        constexpr std::size_t sweepInterval = 120;
        if (std::erase_if(m_detachedSources, [](const auto& source) { return source.expired(); }) > 0) {
            std::erase_if(m_entries, [](const auto& entry) { return entry.second.source.expired(); });
        }
        m_checksUntilSweep = sweepInterval - 1;
    }

private:
    struct Key {
        const objectlib::Geometry* geometry;
        std::uint64_t geometryVersion;
        std::uint64_t deviationBits;
        std::size_t maxPrimitiveCount;

        bool operator==(const Key&) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            auto hash = std::hash<const objectlib::Geometry*>{}(key.geometry);
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

    struct Entry {
        std::weak_ptr<const objectlib::Geometry> source;
        std::shared_ptr<const objectlib::TessellatedGeometry> tessellation;
    };

    std::unordered_map<Key, Entry, KeyHash> m_entries;
    std::set<std::weak_ptr<const objectlib::Geometry>, std::owner_less<>> m_detachedSources;
    std::size_t m_checksUntilSweep{};
    std::size_t m_calculations{};
    std::size_t m_cacheHits{};
};

} // namespace huynity::core
