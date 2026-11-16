#pragma once

#include "huynity/objectlib/operations/tessellate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

namespace huynity::core {

class TessellationCache {
public:
    using Result = std::expected<std::shared_ptr<const objectlib::TessellatedGeometry>, objectlib::GeometryError>;

    [[nodiscard]] Result get(std::shared_ptr<const objectlib::Geometry> geometry, std::uint64_t geometryVersion,
                             objectlib::TessellationOptions options) {
        const auto cached = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
            return entry.geometry == geometry && entry.geometryVersion == geometryVersion &&
                   entry.options.maxDeviation == options.maxDeviation &&
                   entry.options.maxPrimitiveCount == options.maxPrimitiveCount;
        });
        if (cached != m_entries.end()) {
            ++m_cacheHits;
            return cached->tessellation;
        }

        if (const auto* mesh3 = std::get_if<objectlib::TriangleMesh3>(geometry.get())) {
            ++m_calculations;
            if (!std::isfinite(options.maxDeviation) || options.maxDeviation <= 0 || options.maxPrimitiveCount == 0) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::InvalidOptions});
            }
            if (mesh3->triangles().size() > options.maxPrimitiveCount) {
                return std::unexpected(objectlib::GeometryError{objectlib::GeometryErrorCode::BudgetExceeded});
            }
            auto resource = std::make_shared<const objectlib::TessellatedGeometry>(*mesh3);
            m_entries.push_back({std::move(geometry), geometryVersion, options, resource});
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
            m_entries.push_back({std::move(geometry), geometryVersion, options, resource});
            return resource;
        }

        auto calculated = objectlib::tessellate(*geometry, options);
        ++m_calculations;
        if (!calculated) {
            return std::unexpected(calculated.error());
        }
        auto resource = std::make_shared<const objectlib::TessellatedGeometry>(std::move(*calculated));
        m_entries.push_back({std::move(geometry), geometryVersion, options, resource});
        return resource;
    }

    [[nodiscard]] std::size_t calculations() const noexcept { return m_calculations; }
    [[nodiscard]] std::size_t cacheHits() const noexcept { return m_cacheHits; }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

private:
    struct Entry {
        std::shared_ptr<const objectlib::Geometry> geometry;
        std::uint64_t geometryVersion;
        objectlib::TessellationOptions options;
        std::shared_ptr<const objectlib::TessellatedGeometry> tessellation;
    };

    std::vector<Entry> m_entries;
    std::size_t m_calculations{};
    std::size_t m_cacheHits{};
};

} // namespace huynity::core
