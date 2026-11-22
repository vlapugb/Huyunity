#pragma once

#include "huynity/core/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <numbers>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace huynity::core {

struct PreparedDraw3 {
    std::shared_ptr<const objectlib::TessellatedGeometry> geometry;
    math::Transform3 localToWorld;
    std::shared_ptr<const render_contracts::Material> material;
    bool wireframe{false};
    render_contracts::ShadingModel shading{render_contracts::ShadingModel::Flat};
    bool backfaceCulling{true};
    bool castShadow{true};
    bool receiveShadow{true};
    std::shared_ptr<const std::vector<math::Point2>> texCoords;
};

struct PreparedDraw2 {
    std::shared_ptr<const objectlib::TessellatedGeometry> geometry;
    math::Transform2 localToWorld;
    std::shared_ptr<const render_contracts::Material> material;
    std::int32_t layer{};
    std::int32_t order{};
    double alpha{1};
    bool wireframe{false};
    std::shared_ptr<const std::vector<math::Point2>> texCoords;
};

struct WorldDrawList3 {
    render_contracts::Camera3 camera;
    render_contracts::Lighting3 lighting;
    std::vector<PreparedDraw3> drawItems;
};

struct WorldDrawList2 {
    render_contracts::Camera2 camera;
    std::vector<PreparedDraw2> drawItems;
};

struct HudDrawList2 {
    std::vector<PreparedDraw2> drawItems;
};

using PreparedPass = std::variant<WorldDrawList3, WorldDrawList2, HudDrawList2>;

struct PreparedPasses {
    std::vector<PreparedPass> passes;
};

struct PassesConfig {
    std::optional<render_contracts::Camera3> camera3;
    std::optional<render_contracts::Camera2> camera2;
    bool hudEnabled{true};
    bool world2DFirst{};
    render_contracts::Lighting3 lighting;
    objectlib::TessellationOptions tessellation;
};

class RenderPreparation {
public:
    template <typename DrawList>
    using Result = std::expected<DrawList, objectlib::GeometryError>;

    [[nodiscard]] static Result<WorldDrawList3> prepareWorld3D(Scene& scene, render_contracts::Camera3 camera,
                                                               render_contracts::Lighting3 lighting = {},
                                                               objectlib::TessellationOptions options = {},
                                                               double aspect = 1.0) {
        scene.m_tessellationCache.collectDetachedSources();
        auto spatial = scene.refreshSpatialState();
        if (!spatial) {
            return std::unexpected(spatial.error());
        }
        WorldDrawList3 result{std::move(camera), std::move(lighting), {}};
        const auto worldToView = std::visit([](const auto& value) { return value.worldToView; }, result.camera);
        const auto [ids, instances] = scene.instancesForRendering();
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto& instance = instances[index];
            const auto* transform = std::get_if<math::Transform3>(&instance.worldTransform);
            if (instance.renderSpace != RenderSpace::World || !instance.isVisible || transform == nullptr ||
                !instance.worldBounds.has_value()) {
                continue;
            }
            const auto& bounds = std::get<objectlib::Aabb3>(*instance.worldBounds);
            const bool inCamera = isVisible(bounds, worldToView, result.camera, aspect);
            bool inShadowCasterVolume = false;
            if (!inCamera && result.lighting.shadows.enabled && instance.castShadow) {
                const double pad = result.lighting.shadows.extent + result.lighting.shadows.depthRange * 0.5;
                const auto& center = result.lighting.shadows.center;
                inShadowCasterVolume = bounds.max()[0] >= center[0] - pad && bounds.min()[0] <= center[0] + pad &&
                                       bounds.max()[1] >= center[1] - pad && bounds.min()[1] <= center[1] + pad &&
                                       bounds.max()[2] >= center[2] - pad && bounds.min()[2] <= center[2] + pad;
            }
            if (!inCamera && !inShadowCasterVolume) {
                continue;
            }
            auto geometry = scene.tessellation(ids[index], options);
            if (!geometry) {
                return std::unexpected(geometry.error());
            }
            result.drawItems.push_back({std::move(*geometry), *transform, instance.material, instance.wireframe,
                                        instance.shading, instance.backfaceCulling, instance.castShadow,
                                        instance.receiveShadow, instance.texCoords});
        }
        return result;
    }

    [[nodiscard]] static Result<WorldDrawList2> prepareWorld2D(Scene& scene, render_contracts::Camera2 camera,
                                                               objectlib::TessellationOptions options = {}) {
        scene.m_tessellationCache.collectDetachedSources();
        auto spatial = scene.refreshSpatialState();
        if (!spatial) {
            return std::unexpected(spatial.error());
        }
        WorldDrawList2 result{camera, {}};
        const auto [ids, instances] = scene.instancesForRendering();
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto& instance = instances[index];
            const auto* transform = std::get_if<math::Transform2>(&instance.worldTransform);
            if (instance.renderSpace != RenderSpace::World || !instance.isVisible || transform == nullptr ||
                !instance.worldBounds.has_value()) {
                continue;
            }
            const auto& bounds = std::get<objectlib::Aabb2>(*instance.worldBounds);
            if (!isVisible(bounds, camera)) {
                continue;
            }
            auto geometry = scene.tessellation(ids[index], options);
            if (!geometry) {
                return std::unexpected(geometry.error());
            }
            const double alpha = instance.material == nullptr ? 1 : instance.material->color.alpha;
            result.drawItems.push_back({std::move(*geometry), *transform, instance.material, instance.layer,
                                        instance.order, alpha, instance.wireframe, instance.texCoords});
        }
        return result;
    }

    [[nodiscard]] static Result<HudDrawList2> prepareHud2D(Scene& scene, objectlib::TessellationOptions options = {}) {
        scene.m_tessellationCache.collectDetachedSources();
        HudDrawList2 result;
        const auto [ids, instances] = scene.instancesForRendering();
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto& instance = instances[index];
            const auto* transform = std::get_if<math::Transform2>(&instance.worldTransform);
            if (instance.renderSpace != RenderSpace::Hud || !instance.isVisible || transform == nullptr) {
                continue;
            }
            auto geometry = scene.tessellation(ids[index], options);
            if (!geometry) {
                return std::unexpected(geometry.error());
            }
            const double alpha = instance.material == nullptr ? 1 : instance.material->color.alpha;
            result.drawItems.push_back({std::move(*geometry), *transform, instance.material, instance.layer,
                                        instance.order, alpha, instance.wireframe, instance.texCoords});
        }
        std::stable_sort(result.drawItems.begin(), result.drawItems.end(), [](const auto& first, const auto& second) {
            return std::tie(first.layer, first.order) < std::tie(second.layer, second.order);
        });
        return result;
    }

    [[nodiscard]] static Result<PreparedPasses> preparePasses(Scene& scene, const PassesConfig& config,
                                                              double aspect = 1.0) {
        if (!config.camera2 && !config.camera3 && !config.hudEnabled) {
            scene.m_tessellationCache.collectDetachedSources();
        }
        PreparedPasses result;
        auto appendWorld2 = [&]() -> std::expected<void, objectlib::GeometryError> {
            if (!config.camera2) {
                return {};
            }
            auto prepared = prepareWorld2D(scene, *config.camera2, config.tessellation);
            if (!prepared) {
                return std::unexpected(prepared.error());
            }
            result.passes.push_back(std::move(*prepared));
            return {};
        };
        if (config.world2DFirst) {
            auto appended = appendWorld2();
            if (!appended) {
                return std::unexpected(appended.error());
            }
        }
        if (config.camera3) {
            auto prepared = prepareWorld3D(scene, *config.camera3, config.lighting, config.tessellation, aspect);
            if (!prepared) {
                return std::unexpected(prepared.error());
            }
            result.passes.push_back(std::move(*prepared));
        }
        if (!config.world2DFirst) {
            auto appended = appendWorld2();
            if (!appended) {
                return std::unexpected(appended.error());
            }
        }
        if (config.hudEnabled) {
            auto prepared = prepareHud2D(scene, config.tessellation);
            if (!prepared) {
                return std::unexpected(prepared.error());
            }
            result.passes.push_back(std::move(*prepared));
        }
        return result;
    }

    [[nodiscard]] static Result<render_contracts::RenderSnapshot>
    prepareSnapshot(Scene& scene, const PassesConfig& config, std::size_t imageWidth, std::size_t imageHeight) {
        const double aspect =
            imageHeight > 0 ? static_cast<double>(imageWidth) / static_cast<double>(imageHeight) : 1.0;
        auto prepared = preparePasses(scene, config, aspect);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        render_contracts::RenderSnapshot result;
        result.imageWidth = imageWidth;
        result.imageHeight = imageHeight;
        result.materials.push_back(std::make_shared<const render_contracts::Material>());
        SnapshotIndices indices;
        for (auto& pass : prepared->passes) {
            auto appended = appendSnapshotPass(pass, result, indices);
            if (!appended) {
                return std::unexpected(appended.error());
            }
        }
        return result;
    }

private:
    struct MeshKey {
        const void* geometry{};
        const void* texCoords{};
        bool operator==(const MeshKey&) const = default;
    };
    struct MeshKeyHash {
        std::size_t operator()(const MeshKey& key) const noexcept {
            const auto first = std::hash<const void*>{}(key.geometry);
            return first ^ (std::hash<const void*>{}(key.texCoords) + 0x9e3779b9 + (first << 6) + (first >> 2));
        }
    };
    struct SnapshotIndices {
        std::unordered_map<const render_contracts::Material*, std::size_t> materials;
        std::unordered_map<MeshKey, std::size_t, MeshKeyHash> meshes3;
        std::unordered_map<MeshKey, std::size_t, MeshKeyHash> meshes2;
    };

    [[nodiscard]] static std::uint64_t pointerId(const void* first, const void* second = nullptr) {
        const auto firstValue = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(first));
        const auto secondValue = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(second));
        return firstValue ^ (secondValue + 0x9e3779b97f4a7c15ULL + (firstValue << 6) + (firstValue >> 2));
    }

    [[nodiscard]] static render_contracts::MaterialIndex
    materialIndex(const std::shared_ptr<const render_contracts::Material>& material,
                  render_contracts::RenderSnapshot& snapshot, SnapshotIndices& indices) {
        if (material == nullptr) {
            return 0;
        }
        if (const auto found = indices.materials.find(material.get()); found != indices.materials.end()) {
            return found->second;
        }
        snapshot.materials.push_back(withTextureIdentity(material));
        indices.materials.emplace(material.get(), snapshot.materials.size() - 1);
        return snapshot.materials.size() - 1;
    }

    /// @brief Присваивает текстуре стабильный идентификатор при первом попадании в снимок.
    ///
    /// ImageBuffer неизменяем (shared_ptr<const>), поэтому замена изображения — это
    /// уже другой textureId; версия остаётся прежней, а смена пары (textureId,
    /// textureVersion) сигнализирует бэкенду о перезагрузке текстуры. Материал
    /// копируется, чтобы не менять const-оригинал в сцене.
    [[nodiscard]] static std::shared_ptr<const render_contracts::Material>
    withTextureIdentity(const std::shared_ptr<const render_contracts::Material>& material) {
        if (material->texture == nullptr || material->textureId != 0) {
            return material;
        }
        auto identified = std::make_shared<render_contracts::Material>(*material);
        identified->textureId = pointerId(material->texture.get());
        identified->textureVersion = 1;
        return identified;
    }

    [[nodiscard]] static std::expected<void, objectlib::GeometryError>
    appendSnapshotPass(PreparedPass& pass, render_contracts::RenderSnapshot& snapshot, SnapshotIndices& indices) {
        return std::visit(
            [&](auto& source) -> std::expected<void, objectlib::GeometryError> {
                using PassType = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<PassType, WorldDrawList3>) {
                    render_contracts::World3DPass target{source.camera, source.lighting, {}};
                    for (const auto& draw : source.drawItems) {
                        const auto* mesh = std::get_if<objectlib::TriangleMesh3>(draw.geometry.get());
                        if (mesh == nullptr) {
                            return std::unexpected(
                                objectlib::GeometryError{objectlib::GeometryErrorCode::UnsupportedOperation});
                        }
                        const MeshKey key{draw.geometry.get(), draw.texCoords.get()};
                        const auto found = indices.meshes3.find(key);
                        const auto index = found == indices.meshes3.end() ? snapshot.resources3.size() : found->second;
                        if (found == indices.meshes3.end()) {
                            render_contracts::MeshResource3 resource;
                            resource.owner = draw.geometry;
                            resource.positions = mesh->positions();
                            const auto triangles = mesh->triangles();
                            resource.triangles = std::as_bytes(triangles);
                            if (draw.texCoords && draw.texCoords->size() == mesh->positions().size()) {
                                resource.texCoords = *draw.texCoords;
                                resource.texCoordsOwner = draw.texCoords;
                            }
                            resource.resourceId = pointerId(key.geometry, key.texCoords);
                            resource.version = 1;
                            snapshot.resources3.push_back(resource);
                            indices.meshes3.emplace(key, index);
                        }
                        target.drawItems.push_back(
                            {index, draw.localToWorld, materialIndex(draw.material, snapshot, indices),
                             draw.backfaceCulling, draw.wireframe, draw.shading, draw.castShadow, draw.receiveShadow});
                    }
                    snapshot.passes.push_back(std::move(target));
                } else {
                    std::vector<render_contracts::DrawItem2> items;
                    for (const auto& draw : source.drawItems) {
                        const auto* mesh = std::get_if<objectlib::TriangleMesh2>(draw.geometry.get());
                        if (mesh == nullptr) {
                            return std::unexpected(
                                objectlib::GeometryError{objectlib::GeometryErrorCode::UnsupportedOperation});
                        }
                        const MeshKey key{draw.geometry.get(), draw.texCoords.get()};
                        const auto found = indices.meshes2.find(key);
                        const auto index = found == indices.meshes2.end() ? snapshot.resources2.size() : found->second;
                        if (found == indices.meshes2.end()) {
                            render_contracts::MeshResource2 resource;
                            resource.owner = draw.geometry;
                            resource.positions = mesh->positions();
                            const auto triangles = mesh->triangles();
                            resource.triangles = std::as_bytes(triangles);
                            if (draw.texCoords && draw.texCoords->size() == mesh->positions().size()) {
                                resource.texCoords = *draw.texCoords;
                                resource.texCoordsOwner = draw.texCoords;
                            }
                            resource.resourceId = pointerId(key.geometry, key.texCoords);
                            resource.version = 1;
                            snapshot.resources2.push_back(resource);
                            indices.meshes2.emplace(key, index);
                        }
                        items.push_back({index, draw.localToWorld, materialIndex(draw.material, snapshot, indices),
                                         draw.layer, draw.order, draw.wireframe});
                    }
                    if constexpr (std::is_same_v<PassType, WorldDrawList2>) {
                        snapshot.passes.push_back(render_contracts::World2DPass{source.camera, std::move(items)});
                    } else {
                        snapshot.passes.push_back(render_contracts::Hud2DPass{std::move(items)});
                    }
                }
                return {};
            },
            pass);
    }

    template <std::size_t dimension>
    [[nodiscard]] static objectlib::Aabb<dimension> transformBounds(const objectlib::Aabb<dimension>& bounds,
                                                                    const math::Transform<dimension>& transform) {
        constexpr std::size_t cornerCount = std::size_t{1} << dimension;
        std::array<math::Point<dimension>, cornerCount> corners;
        for (std::size_t corner = 0; corner < cornerCount; ++corner) {
            for (std::size_t axis = 0; axis < dimension; ++axis) {
                corners[corner][axis] =
                    (corner & (std::size_t{1} << axis)) == 0 ? bounds.min()[axis] : bounds.max()[axis];
            }
            corners[corner] = math::transformPoint(transform, corners[corner]);
        }
        math::Point<dimension> min = corners.front();
        math::Point<dimension> max = min;
        for (const auto& corner : corners) {
            for (std::size_t axis = 0; axis < dimension; ++axis) {
                min[axis] = std::min(min[axis], corner[axis]);
                max[axis] = std::max(max[axis], corner[axis]);
            }
        }
        return *objectlib::Aabb<dimension>::create({min, max});
    }

    [[nodiscard]] static bool intersects(const objectlib::Aabb2& bounds, double width, double height) {
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
            return false;
        }
        return bounds.max()[0] >= -width / 2 && bounds.min()[0] <= width / 2 && bounds.max()[1] >= -height / 2 &&
               bounds.min()[1] <= height / 2;
    }

    [[nodiscard]] static bool isVisible(const objectlib::Aabb2& worldBounds, const render_contracts::Camera2& camera) {
        return intersects(transformBounds(worldBounds, camera.worldToView), camera.width, camera.height);
    }

    [[nodiscard]] static bool isVisible(const objectlib::Aabb3& worldBounds, const math::Transform3& worldToView,
                                        const render_contracts::Camera3& camera, double aspect = 1.0) {
        const auto bounds = transformBounds(worldBounds, worldToView);
        return std::visit(
            [&](const auto& value) {
                using CameraType = std::decay_t<decltype(value)>;
                if (!std::isfinite(value.nearPlane) || !std::isfinite(value.farPlane) || value.nearPlane < 0 ||
                    value.farPlane < value.nearPlane) {
                    return false;
                }
                if (bounds.max()[2] < value.nearPlane || bounds.min()[2] > value.farPlane) {
                    return false;
                }
                if constexpr (std::is_same_v<CameraType, render_contracts::OrthographicCamera3>) {
                    if (!std::isfinite(value.width) || !std::isfinite(value.height) || value.width <= 0 ||
                        value.height <= 0) {
                        return false;
                    }
                    return bounds.max()[0] >= -value.width / 2 && bounds.min()[0] <= value.width / 2 &&
                           bounds.max()[1] >= -value.height / 2 && bounds.min()[1] <= value.height / 2;
                } else {
                    if (!std::isfinite(value.verticalFieldOfView) || value.verticalFieldOfView <= 0 ||
                        value.verticalFieldOfView >= std::numbers::pi) {
                        return false;
                    }
                    const double farthestDepth = std::min(bounds.max()[2], value.farPlane);
                    if (farthestDepth < value.nearPlane) {
                        return false;
                    }
                    const double safeAspect = (std::isfinite(aspect) && aspect > 0.0) ? aspect : 1.0;
                    const double halfVertical = farthestDepth * std::tan(value.verticalFieldOfView / 2);
                    const double halfHorizontal = halfVertical * safeAspect;
                    return bounds.max()[0] >= -halfHorizontal && bounds.min()[0] <= halfHorizontal &&
                           bounds.max()[1] >= -halfVertical && bounds.min()[1] <= halfVertical;
                }
            },
            camera);
    }

    [[nodiscard]] static bool isVisible(const objectlib::Aabb3& worldBounds, const math::Transform3& worldToView,
                                        const render_contracts::PerspectiveCamera3& camera, double aspect = 1.0) {
        return isVisible(worldBounds, worldToView, render_contracts::Camera3{camera}, aspect);
    }
};

} // namespace huynity::core
