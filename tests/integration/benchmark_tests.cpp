#include "huynity/adapters/renderer_actor/fake_renderer_adapter.h"
#include "huynity/core/render_preparation.h"
#include "huynity/core/scene.h"
#include "huynity/image/image_buffer.h"
#include "huynity/math/transform.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/circle2.h"
#include "huynity/objectlib/shapes/disk2.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/objectlib/shapes/sphere3.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/gl_renderer.h"
#include "huynity/renderer/software_renderer.h"
#include "huynity/runtime/fixed_step_update.h"
#include "huynity/runtime/frame_completions.h"
#include "huynity/runtime/frame_pipeline.h"
#include "huynity/runtime/framebuffer_pool.h"
#include "huynity/runtime/main_loop.h"
#include "huynity/runtime/presentation_port.h"
#include "huynity/runtime/renderer_port.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace core = huynity::core;
namespace image = huynity::image;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace renderer = huynity::renderer;
namespace runtime = huynity::runtime;
namespace adapter = huynity::adapters::renderer_actor;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

constexpr std::size_t BENCHMARK_IMAGE_WIDTH = 64;
constexpr std::size_t BENCHMARK_IMAGE_HEIGHT = 64;

// ============================================================================
// Структуры телеметрии и профилирования кадра
// ============================================================================

struct PhaseTimings {
    std::uint64_t updateUs{};
    std::uint64_t prepareUs{};
    std::uint64_t renderUs{};
    std::uint64_t presentUs{};

    [[nodiscard]] std::uint64_t totalUs() const noexcept { return updateUs + prepareUs + renderUs + presentUs; }
};

struct DirtyMetrics {
    std::size_t dirtyTransformUpdates{};
    std::size_t dirtyBoundsUpdates{};
};

struct CacheMetrics {
    std::size_t tessellationHits{};
    std::size_t tessellationCalculations{};
    std::size_t shadingHits{};
    std::size_t shadingMisses{};

    [[nodiscard]] double tessellationHitRate() const noexcept {
        const auto total = tessellationHits + tessellationCalculations;
        return total > 0 ? static_cast<double>(tessellationHits) / static_cast<double>(total) : 1.0;
    }

    [[nodiscard]] double shadingHitRate() const noexcept {
        const auto total = shadingHits + shadingMisses;
        return total > 0 ? static_cast<double>(shadingHits) / static_cast<double>(total) : 1.0;
    }
};

struct GeometryMetrics {
    std::size_t objectCount{};
    std::size_t drawItemCount{};
    std::size_t triangleCount{};
};

struct BufferPoolMetrics {
    std::size_t allocations{};
    std::size_t pooled{};
    std::size_t maxPooled{};
};

struct PipelineMetrics {
    std::size_t rejected{};
    std::size_t ready{};
    std::size_t dropped{};
};

struct FrameTelemetry {
    std::uint64_t frameId{};
    PhaseTimings timings;
    DirtyMetrics dirty;
    CacheMetrics cache;
    GeometryMetrics geometry;
    BufferPoolMetrics pool;
    PipelineMetrics pipeline;
};

struct BenchmarkSummary {
    std::string scenarioName;
    std::size_t frameCount{};
    double avgUpdateUs{};
    double avgPrepareUs{};
    double avgRenderUs{};
    double avgPresentUs{};
    double avgTotalUs{};
    std::size_t totalDirtyTransforms{};
    std::size_t totalDirtyBounds{};
    std::size_t totalTessellationCalculations{};
    std::size_t totalTessellationHits{};
    std::size_t totalShadingMisses{};
    std::size_t totalShadingHits{};
    std::size_t peakBufferAllocations{};
    std::size_t totalPipelineRejected{};
    std::size_t totalPipelineReady{};
    std::size_t totalPipelineDropped{};
};

// ============================================================================
// Вспомогательные классы и функции
// ============================================================================

class SimpleSyncPresentationPort final : public runtime::PresentationPort {
public:
    std::unique_ptr<image::ImageBuffer> present(std::unique_ptr<image::ImageBuffer> framebuffer) override {
        ++m_presentCount;
        return framebuffer;
    }

    [[nodiscard]] std::size_t presentCount() const noexcept { return m_presentCount; }

private:
    std::size_t m_presentCount{};
};

[[nodiscard]] std::shared_ptr<const render::Material> makeMaterial(double red, double green, double blue,
                                                                   double alpha = 1.0) {
    return std::make_shared<const render::Material>(render::Color{red, green, blue, alpha});
}

[[nodiscard]] std::size_t countDrawItems(const render::RenderSnapshot& snapshot) noexcept {
    std::size_t count = 0;
    for (const auto& pass : snapshot.passes) {
        if (const auto* world3 = std::get_if<render::World3DPass>(&pass)) {
            count += world3->drawItems.size();
        } else if (const auto* world2 = std::get_if<render::World2DPass>(&pass)) {
            count += world2->drawItems.size();
        } else if (const auto* hud = std::get_if<render::Hud2DPass>(&pass)) {
            count += hud->drawItems.size();
        }
    }
    return count;
}

[[nodiscard]] std::size_t countTriangles(const render::RenderSnapshot& snapshot) noexcept {
    std::size_t count = 0;
    for (const auto& pass : snapshot.passes) {
        if (const auto* world3 = std::get_if<render::World3DPass>(&pass)) {
            for (const auto& item : world3->drawItems) {
                if (item.resource < snapshot.resources3.size()) {
                    count += snapshot.resources3[item.resource].triangles.size() / sizeof(render::TriangleIndices);
                }
            }
        } else if (const auto* world2 = std::get_if<render::World2DPass>(&pass)) {
            for (const auto& item : world2->drawItems) {
                if (item.resource < snapshot.resources2.size()) {
                    count += snapshot.resources2[item.resource].triangles.size() / sizeof(render::TriangleIndices);
                }
            }
        } else if (const auto* hud = std::get_if<render::Hud2DPass>(&pass)) {
            for (const auto& item : hud->drawItems) {
                if (item.resource < snapshot.resources2.size()) {
                    count += snapshot.resources2[item.resource].triangles.size() / sizeof(render::TriangleIndices);
                }
            }
        }
    }
    return count;
}

void printSummary(const BenchmarkSummary& summary) {
    std::cout << "------------------------------------------------------------\n"
              << "Scenario: " << summary.scenarioName << " (" << summary.frameCount << " frames)\n"
              << "  Avg phase timings (us):"
              << " update=" << std::fixed << std::setprecision(1) << summary.avgUpdateUs
              << ", prepare=" << summary.avgPrepareUs << ", render=" << summary.avgRenderUs
              << ", present=" << summary.avgPresentUs << " [total=" << summary.avgTotalUs << " us]\n"
              << "  Dirty updates: transforms=" << summary.totalDirtyTransforms
              << ", bounds=" << summary.totalDirtyBounds << "\n"
              << "  Tessellation cache: hits=" << summary.totalTessellationHits
              << ", calculations=" << summary.totalTessellationCalculations << " (hit rate: "
              << (summary.totalTessellationHits + summary.totalTessellationCalculations > 0
                      ? 100.0 * static_cast<double>(summary.totalTessellationHits) /
                            static_cast<double>(summary.totalTessellationHits + summary.totalTessellationCalculations)
                      : 100.0)
              << "%)\n"
              << "  Shading cache: hits=" << summary.totalShadingHits << ", misses=" << summary.totalShadingMisses
              << " (hit rate: "
              << (summary.totalShadingHits + summary.totalShadingMisses > 0
                      ? 100.0 * static_cast<double>(summary.totalShadingHits) /
                            static_cast<double>(summary.totalShadingHits + summary.totalShadingMisses)
                      : 100.0)
              << "%)\n"
              << "  Buffer pool: peak allocations=" << summary.peakBufferAllocations << "\n"
              << "  Pipeline: ready=" << summary.totalPipelineReady << ", rejected=" << summary.totalPipelineRejected
              << ", dropped=" << summary.totalPipelineDropped << "\n"
              << "------------------------------------------------------------\n";
}

// ============================================================================
// Сценарий 1: static (100+ статических объектов)
// ============================================================================

void scenarioStatic() {
    constexpr std::size_t OBJECT_COUNT = 128;
    constexpr std::size_t FRAME_COUNT = 10;

    core::Scene scene;
    std::vector<core::ObjectId> objects;
    objects.reserve(OBJECT_COUNT);

    const auto material = makeMaterial(0.8, 0.2, 0.2);
    for (std::size_t i = 0; i < OBJECT_COUNT; ++i) {
        auto created = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{1.0, 1.0, 1.0}}});
        CHECK(created.has_value());
        const auto id = *created;
        CHECK(scene.setMobility(id, core::Mobility::Static).has_value());
        const double x = static_cast<double>(i % 16) * 2.0 - 15.0;
        const double y = static_cast<double>(i / 16) * 2.0 - 7.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector3{{x, y, 10.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::World, 0, 0).has_value());
        CHECK(scene.setMaterial(id, material).has_value());
        objects.push_back(id);
    }
    CHECK(scene.size() == OBJECT_COUNT);

    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 40.0,
        .height = 40.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    config.hudEnabled = false;
    config.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.3},
        .directional =
            {
                {
                    .direction = math::Vector3{{1.0, 1.0, 1.0}},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.7,
                },
            },
    };

    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    renderer::SoftwareRenderer softwareRenderer;
    SimpleSyncPresentationPort presentation;

    // Кадр 0: прогрев и первичная инициализация (тесселяция и построение кеша)
    {
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        CHECK(softwareRenderer.render(*snapshotResult, *framebuffer).has_value());
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
    }

    CHECK(scene.tessellationCalculations() == OBJECT_COUNT);
    CHECK(softwareRenderer.shadingCacheMisses() == OBJECT_COUNT);
    CHECK(pipeline.pool().createdCount() == 1);

    // Замер кадров в стабильном режиме: проверка отсутствия dirty updates и 100% cache hits
    std::vector<FrameTelemetry> telemetryFrames;
    telemetryFrames.reserve(FRAME_COUNT);

    std::size_t prevTessCalcs = scene.tessellationCalculations();
    std::size_t prevTessHits = scene.tessellationCacheHits();
    std::size_t prevShadingMisses = softwareRenderer.shadingCacheMisses();
    std::size_t prevShadingHits = softwareRenderer.shadingCacheHits();

    for (std::size_t frameIndex = 1; frameIndex <= FRAME_COUNT; ++frameIndex) {
        FrameTelemetry telemetry;
        telemetry.frameId = frameIndex;

        // Фаза 1: Update
        const auto updateStart = std::chrono::steady_clock::now();
        // В стабильном статическом режиме изменений сцены нет
        std::size_t dirtyTransforms = 0;
        std::size_t dirtyBounds = 0;
        for (const auto id : objects) {
            const auto* inst = scene.find(id);
            if (inst->dirty.bounds) {
                ++dirtyBounds;
            }
        }
        const auto updateEnd = std::chrono::steady_clock::now();
        telemetry.timings.updateUs =
            std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count();
        telemetry.dirty.dirtyTransformUpdates = dirtyTransforms;
        telemetry.dirty.dirtyBoundsUpdates = dirtyBounds;

        // Фаза 2: Prepare
        const auto prepareStart = std::chrono::steady_clock::now();
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto snapshot = std::move(*snapshotResult);
        const auto prepareEnd = std::chrono::steady_clock::now();
        telemetry.timings.prepareUs =
            std::chrono::duration_cast<std::chrono::microseconds>(prepareEnd - prepareStart).count();

        const std::size_t curTessCalcs = scene.tessellationCalculations();
        const std::size_t curTessHits = scene.tessellationCacheHits();
        telemetry.cache.tessellationCalculations = curTessCalcs - prevTessCalcs;
        telemetry.cache.tessellationHits = curTessHits - prevTessHits;
        prevTessCalcs = curTessCalcs;
        prevTessHits = curTessHits;

        telemetry.geometry.objectCount = scene.size();
        telemetry.geometry.drawItemCount = countDrawItems(snapshot);
        telemetry.geometry.triangleCount = countTriangles(snapshot);

        // Фаза 3: Render
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        const auto renderStart = std::chrono::steady_clock::now();
        CHECK(softwareRenderer.render(snapshot, *framebuffer).has_value());
        const auto renderEnd = std::chrono::steady_clock::now();
        telemetry.timings.renderUs =
            std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart).count();

        const std::size_t curShadingMisses = softwareRenderer.shadingCacheMisses();
        const std::size_t curShadingHits = softwareRenderer.shadingCacheHits();
        telemetry.cache.shadingMisses = curShadingMisses - prevShadingMisses;
        telemetry.cache.shadingHits = curShadingHits - prevShadingHits;
        prevShadingMisses = curShadingMisses;
        prevShadingHits = curShadingHits;

        // Фаза 4: Present
        const auto presentStart = std::chrono::steady_clock::now();
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
        const auto presentEnd = std::chrono::steady_clock::now();
        telemetry.timings.presentUs =
            std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count();

        telemetry.pool.allocations = pipeline.pool().createdCount();
        telemetry.pool.pooled = pipeline.pool().pooledCount();
        telemetry.pool.maxPooled = pipeline.pool().maxPooled();

        telemetry.pipeline.ready = 1;

        // Инварианты стабильного статического режима:
        CHECK(telemetry.dirty.dirtyTransformUpdates == 0);
        CHECK(telemetry.dirty.dirtyBoundsUpdates == 0);
        CHECK(telemetry.cache.tessellationCalculations == 0);
        CHECK(telemetry.cache.tessellationHits == OBJECT_COUNT);
        CHECK(telemetry.cache.shadingMisses == 0);
        CHECK(telemetry.cache.shadingHits == OBJECT_COUNT);
        CHECK(telemetry.pool.allocations == 1); // новых аллокаций нет

        telemetryFrames.push_back(telemetry);
    }

    // Итоговая статистика сценария
    BenchmarkSummary summary;
    summary.scenarioName = "static (128 objects, post-init baseline)";
    summary.frameCount = FRAME_COUNT;
    for (const auto& f : telemetryFrames) {
        summary.avgUpdateUs += static_cast<double>(f.timings.updateUs);
        summary.avgPrepareUs += static_cast<double>(f.timings.prepareUs);
        summary.avgRenderUs += static_cast<double>(f.timings.renderUs);
        summary.avgPresentUs += static_cast<double>(f.timings.presentUs);
        summary.avgTotalUs += static_cast<double>(f.timings.totalUs());
        summary.totalDirtyTransforms += f.dirty.dirtyTransformUpdates;
        summary.totalDirtyBounds += f.dirty.dirtyBoundsUpdates;
        summary.totalTessellationCalculations += f.cache.tessellationCalculations;
        summary.totalTessellationHits += f.cache.tessellationHits;
        summary.totalShadingMisses += f.cache.shadingMisses;
        summary.totalShadingHits += f.cache.shadingHits;
        summary.peakBufferAllocations = std::max(summary.peakBufferAllocations, f.pool.allocations);
        summary.totalPipelineReady += f.pipeline.ready;
    }
    summary.avgUpdateUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPrepareUs /= static_cast<double>(FRAME_COUNT);
    summary.avgRenderUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPresentUs /= static_cast<double>(FRAME_COUNT);
    summary.avgTotalUs /= static_cast<double>(FRAME_COUNT);

    printSummary(summary);
}

// ============================================================================
// Сценарий 2: moving instances (100+ динамических экземпляров общей геометрии)
// ============================================================================

void scenarioMovingInstances() {
    constexpr std::size_t INSTANCE_COUNT = 128;
    constexpr std::size_t FRAME_COUNT = 5;

    core::Scene scene;

    // Исходная разделяемая геометрия (Box3)
    auto sourceBox = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{1.0, 1.0, 1.0}}});
    CHECK(sourceBox.has_value());
    const auto sourceId = *sourceBox;

    std::vector<core::ObjectId> instances;
    instances.reserve(INSTANCE_COUNT);

    const auto material = makeMaterial(0.2, 0.8, 0.2);
    for (std::size_t i = 0; i < INSTANCE_COUNT; ++i) {
        auto instance = scene.createInstance(sourceId);
        CHECK(instance.has_value());
        const auto id = *instance;
        CHECK(scene.setMobility(id, core::Mobility::Dynamic).has_value());
        const double x = static_cast<double>(i % 16) * 2.0 - 15.0;
        const double y = static_cast<double>(i / 16) * 2.0 - 7.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector3{{x, y, 10.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::World, 0, 0).has_value());
        CHECK(scene.setMaterial(id, material).has_value());
        instances.push_back(id);

        // Инвариант разделения геометрии: экземпляры указывают на единый ресурс
        CHECK(scene.find(id)->geometry == scene.find(sourceId)->geometry);
    }

    // Исходный объект скрыт, чтобы не рендериться отдельно от экземпляров
    CHECK(scene.setVisibility(sourceId, false).has_value());

    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 40.0,
        .height = 40.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    config.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.3},
        .directional =
            {
                {
                    .direction = math::Vector3{{1.0, 1.0, 1.0}},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.7,
                },
            },
    };

    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    renderer::SoftwareRenderer softwareRenderer;
    SimpleSyncPresentationPort presentation;

    // Кадр 0: прогрев (тесселяция вычисляется РОВНО 1 раз для разделяемой геометрии)
    {
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        CHECK(softwareRenderer.render(*snapshotResult, *framebuffer).has_value());
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
    }

    CHECK(scene.tessellationCalculations() == 1);
    CHECK(scene.tessellationCacheHits() == INSTANCE_COUNT - 1);
    CHECK(softwareRenderer.shadingCacheMisses() == 1);
    CHECK(softwareRenderer.shadingCacheHits() == INSTANCE_COUNT - 1);

    // Замер кадров движения: изменение transform для каждого экземпляра
    std::vector<FrameTelemetry> telemetryFrames;
    telemetryFrames.reserve(FRAME_COUNT);

    std::size_t prevTessCalcs = scene.tessellationCalculations();
    std::size_t prevTessHits = scene.tessellationCacheHits();
    std::size_t prevShadingMisses = softwareRenderer.shadingCacheMisses();
    std::size_t prevShadingHits = softwareRenderer.shadingCacheHits();

    for (std::size_t frameIndex = 1; frameIndex <= FRAME_COUNT; ++frameIndex) {
        FrameTelemetry telemetry;
        telemetry.frameId = frameIndex;

        // Фаза 1: Update (смещение всех 128 динамических экземпляров)
        const auto updateStart = std::chrono::steady_clock::now();
        const double delta = static_cast<double>(frameIndex) * 0.1;
        std::size_t dirtyTransforms = 0;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            const double x = static_cast<double>(i % 16) * 2.0 - 15.0 + delta;
            const double y = static_cast<double>(i / 16) * 2.0 - 7.0;
            CHECK(scene.setLocalTransform(instances[i], math::translation(math::Vector3{{x, y, 10.0}})).has_value());
            ++dirtyTransforms;
        }

        std::size_t dirtyBounds = 0;
        for (const auto id : instances) {
            if (scene.find(id)->dirty.bounds) {
                ++dirtyBounds;
            }
        }
        const auto updateEnd = std::chrono::steady_clock::now();
        telemetry.timings.updateUs =
            std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count();
        telemetry.dirty.dirtyTransformUpdates = dirtyTransforms;
        telemetry.dirty.dirtyBoundsUpdates = dirtyBounds;

        // Фаза 2: Prepare
        const auto prepareStart = std::chrono::steady_clock::now();
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto snapshot = std::move(*snapshotResult);
        const auto prepareEnd = std::chrono::steady_clock::now();
        telemetry.timings.prepareUs =
            std::chrono::duration_cast<std::chrono::microseconds>(prepareEnd - prepareStart).count();

        // Проверка: в снимке ровно один mesh-ресурс для всех 128 экземпляров (нет копирования геометрии)
        CHECK(snapshot.resources3.size() == 1);
        const auto& worldPass = std::get<render::World3DPass>(snapshot.passes[0]);
        CHECK(worldPass.drawItems.size() == INSTANCE_COUNT);
        for (const auto& item : worldPass.drawItems) {
            CHECK(item.resource == 0);
        }

        const std::size_t curTessCalcs = scene.tessellationCalculations();
        const std::size_t curTessHits = scene.tessellationCacheHits();
        telemetry.cache.tessellationCalculations = curTessCalcs - prevTessCalcs;
        telemetry.cache.tessellationHits = curTessHits - prevTessHits;
        prevTessCalcs = curTessCalcs;
        prevTessHits = curTessHits;

        telemetry.geometry.objectCount = scene.size() - 1; // видимые
        telemetry.geometry.drawItemCount = countDrawItems(snapshot);
        telemetry.geometry.triangleCount = countTriangles(snapshot);

        // Фаза 3: Render
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        const auto renderStart = std::chrono::steady_clock::now();
        CHECK(softwareRenderer.render(snapshot, *framebuffer).has_value());
        const auto renderEnd = std::chrono::steady_clock::now();
        telemetry.timings.renderUs =
            std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart).count();

        const std::size_t curShadingMisses = softwareRenderer.shadingCacheMisses();
        const std::size_t curShadingHits = softwareRenderer.shadingCacheHits();
        telemetry.cache.shadingMisses = curShadingMisses - prevShadingMisses;
        telemetry.cache.shadingHits = curShadingHits - prevShadingHits;
        prevShadingMisses = curShadingMisses;
        prevShadingHits = curShadingHits;

        // Фаза 4: Present
        const auto presentStart = std::chrono::steady_clock::now();
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
        const auto presentEnd = std::chrono::steady_clock::now();
        telemetry.timings.presentUs =
            std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count();

        telemetry.pool.allocations = pipeline.pool().createdCount();
        telemetry.pool.pooled = pipeline.pool().pooledCount();
        telemetry.pool.maxPooled = pipeline.pool().maxPooled();
        telemetry.pipeline.ready = 1;

        // Инварианты движения:
        // 1. Dirty updates только для transform и bounds (ровно 128)
        CHECK(telemetry.dirty.dirtyTransformUpdates == INSTANCE_COUNT);
        CHECK(telemetry.dirty.dirtyBoundsUpdates == INSTANCE_COUNT);
        // 2. Тесселяция не пересчитывается: 0 calculations, 100% hits
        CHECK(telemetry.cache.tessellationCalculations == 0);
        CHECK(telemetry.cache.tessellationHits == INSTANCE_COUNT);
        // 3. Shading data нормалей повторно используется: 100% hits
        CHECK(telemetry.cache.shadingMisses == 0);
        CHECK(telemetry.cache.shadingHits == INSTANCE_COUNT);

        telemetryFrames.push_back(telemetry);
    }

    BenchmarkSummary summary;
    summary.scenarioName = "moving instances (128 dynamic instances, shared geometry)";
    summary.frameCount = FRAME_COUNT;
    for (const auto& f : telemetryFrames) {
        summary.avgUpdateUs += static_cast<double>(f.timings.updateUs);
        summary.avgPrepareUs += static_cast<double>(f.timings.prepareUs);
        summary.avgRenderUs += static_cast<double>(f.timings.renderUs);
        summary.avgPresentUs += static_cast<double>(f.timings.presentUs);
        summary.avgTotalUs += static_cast<double>(f.timings.totalUs());
        summary.totalDirtyTransforms += f.dirty.dirtyTransformUpdates;
        summary.totalDirtyBounds += f.dirty.dirtyBoundsUpdates;
        summary.totalTessellationCalculations += f.cache.tessellationCalculations;
        summary.totalTessellationHits += f.cache.tessellationHits;
        summary.totalShadingMisses += f.cache.shadingMisses;
        summary.totalShadingHits += f.cache.shadingHits;
        summary.peakBufferAllocations = std::max(summary.peakBufferAllocations, f.pool.allocations);
        summary.totalPipelineReady += f.pipeline.ready;
    }
    summary.avgUpdateUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPrepareUs /= static_cast<double>(FRAME_COUNT);
    summary.avgRenderUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPresentUs /= static_cast<double>(FRAME_COUNT);
    summary.avgTotalUs /= static_cast<double>(FRAME_COUNT);

    printSummary(summary);
}

// ============================================================================
// Сценарий 3: geometry edits (изменение геометрии одного объекта)
// ============================================================================

void scenarioGeometryEdits() {
    constexpr std::size_t OBJECT_COUNT = 100;

    core::Scene scene;
    std::vector<core::ObjectId> objects;
    objects.reserve(OBJECT_COUNT);

    const auto material = makeMaterial(0.2, 0.4, 0.8);
    for (std::size_t i = 0; i < OBJECT_COUNT; ++i) {
        const double sz = 1.0 + static_cast<double>(i) * 0.01;
        auto created = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{sz, sz, sz}}});
        CHECK(created.has_value());
        const auto id = *created;
        const double x = static_cast<double>(i % 10) * 2.0 - 9.0;
        const double y = static_cast<double>(i / 10) * 2.0 - 9.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector3{{x, y, 10.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::World, 0, 0).has_value());
        CHECK(scene.setMaterial(id, material).has_value());
        objects.push_back(id);
    }

    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 30.0,
        .height = 30.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };

    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    renderer::SoftwareRenderer softwareRenderer;
    SimpleSyncPresentationPort presentation;

    // Кадр 0: Первичное вычисление тесселяции для всех 100 объектов
    {
        auto snap0 =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snap0.has_value());
        auto fb = pipeline.acquireFramebuffer();
        CHECK(fb != nullptr);
        CHECK(softwareRenderer.render(*snap0, *fb).has_value());
        fb = presentation.present(std::move(fb));
        pipeline.releaseFromPresentation(std::move(fb));
    }
    CHECK(scene.tessellationCalculations() == OBJECT_COUNT);

    // Кадр 1: Стабильный проход без изменений геометрии -> 100% hits, 0 calculations
    const std::size_t calcsBeforeEdit = scene.tessellationCalculations();
    const std::size_t hitsBeforeEdit = scene.tessellationCacheHits();
    {
        auto snap1 =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snap1.has_value());
        auto fb = pipeline.acquireFramebuffer();
        CHECK(softwareRenderer.render(*snap1, *fb).has_value());
        fb = presentation.present(std::move(fb));
        pipeline.releaseFromPresentation(std::move(fb));
    }
    CHECK(scene.tessellationCalculations() - calcsBeforeEdit == 0);
    CHECK(scene.tessellationCacheHits() - hitsBeforeEdit == OBJECT_COUNT);

    // Кадр 2: Редактирование геометрии РОВНО ОДНОГО объекта (индекс 42)
    const auto targetId = objects[42];
    auto newBox = objects::Box3::create({.size = math::Vector3{{3.5, 4.5, 5.5}}});
    CHECK(newBox.has_value());
    CHECK(scene.setGeometry(targetId, *newBox).has_value());

    // Проверяем, что dirty.tessellation установлен для изменённого объекта
    CHECK(scene.find(targetId)->dirty.tessellation);
    CHECK(scene.find(targetId)->geometryVersion == 2);

    const auto calcsBefore = scene.tessellationCalculations();
    const auto hitsBefore = scene.tessellationCacheHits();
    const auto shadingMissesBefore = softwareRenderer.shadingCacheMisses();
    const auto shadingHitsBefore = softwareRenderer.shadingCacheHits();

    const auto prepareStart = std::chrono::steady_clock::now();
    auto snap2 = core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
    const auto prepareEnd = std::chrono::steady_clock::now();
    CHECK(snap2.has_value());

    auto fb2 = pipeline.acquireFramebuffer();
    const auto renderStart = std::chrono::steady_clock::now();
    CHECK(softwareRenderer.render(*snap2, *fb2).has_value());
    const auto renderEnd = std::chrono::steady_clock::now();
    fb2 = presentation.present(std::move(fb2));
    pipeline.releaseFromPresentation(std::move(fb2));

    const auto deltaCalcs = scene.tessellationCalculations() - calcsBefore;
    const auto deltaHits = scene.tessellationCacheHits() - hitsBefore;
    const auto deltaShadingMisses = softwareRenderer.shadingCacheMisses() - shadingMissesBefore;
    const auto deltaShadingHits = softwareRenderer.shadingCacheHits() - shadingHitsBefore;

    // Главные инварианты частичного редактирования:
    // 1. Ровно 1 пересчёт тесселяции (только для изменённого объекта)
    CHECK(deltaCalcs == 1);
    // 2. Ровно 99 попаданий в кеш (все остальные объекты сохранили свои кеши)
    CHECK(deltaHits == OBJECT_COUNT - 1);
    CHECK(!scene.find(targetId)->dirty.tessellation);
    // 3. Shading-данные: ровно 1 пересчёт нормалей для изменённого mesh-ресурса, 99 попаданий
    CHECK(deltaShadingMisses == 1);
    CHECK(deltaShadingHits == OBJECT_COUNT - 1);

    // Кадр 3: Следующий кадр без дальнейших изменений -> снова 100 попаданий, 0 пересчётов
    const auto calcsAfter = scene.tessellationCalculations();
    const auto hitsAfter = scene.tessellationCacheHits();
    auto snap3 = core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
    CHECK(snap3.has_value());
    CHECK(scene.tessellationCalculations() - calcsAfter == 0);
    CHECK(scene.tessellationCacheHits() - hitsAfter == OBJECT_COUNT);

    BenchmarkSummary summary;
    summary.scenarioName = "geometry edits (1 edit out of 100 objects)";
    summary.frameCount = 1;
    summary.avgPrepareUs =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(prepareEnd - prepareStart).count());
    summary.avgRenderUs =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart).count());
    summary.avgTotalUs = summary.avgPrepareUs + summary.avgRenderUs;
    summary.totalDirtyTransforms = 0;
    summary.totalDirtyBounds = 1;
    summary.totalTessellationCalculations = deltaCalcs;
    summary.totalTessellationHits = deltaHits;
    summary.totalShadingMisses = deltaShadingMisses;
    summary.totalShadingHits = deltaShadingHits;
    summary.peakBufferAllocations = pipeline.pool().createdCount();
    summary.totalPipelineReady = 1;

    printSummary(summary);
}

// ============================================================================
// Сценарий 4: 2D-only (100+ 2D-объектов + HUD)
// ============================================================================

void scenario2DOnly() {
    constexpr std::size_t OBJECT_COUNT_2D = 100;
    constexpr std::size_t HUD_COUNT = 10;
    constexpr std::size_t FRAME_COUNT = 5;

    core::Scene scene;
    std::vector<core::ObjectId> worldObjects;
    worldObjects.reserve(OBJECT_COUNT_2D);

    const auto worldMaterial = makeMaterial(0.1, 0.6, 0.9);
    for (std::size_t i = 0; i < OBJECT_COUNT_2D; ++i) {
        auto created = scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{1.5, 1.5}}});
        CHECK(created.has_value());
        const auto id = *created;
        const double x = static_cast<double>(i % 10) * 2.5 - 11.25;
        const double y = static_cast<double>(i / 10) * 2.5 - 11.25;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector2{{x, y}})).has_value());
        const auto layer = static_cast<std::int32_t>(i / 40);
        const auto order = static_cast<std::int32_t>(i);
        CHECK(scene.setRenderProperties(id, core::RenderSpace::World, layer, order).has_value());
        CHECK(scene.setMaterial(id, worldMaterial).has_value());
        worldObjects.push_back(id);
    }

    std::vector<core::ObjectId> hudObjects;
    hudObjects.reserve(HUD_COUNT);
    const auto hudMaterial = makeMaterial(1.0, 0.9, 0.1, 0.8);
    for (std::size_t i = 0; i < HUD_COUNT; ++i) {
        auto created = scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{6.0, 4.0}}});
        CHECK(created.has_value());
        const auto id = *created;
        const double x = static_cast<double>(i) * 6.0 + 3.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector2{{x, 5.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::Hud, 0, static_cast<std::int32_t>(i)).has_value());
        CHECK(scene.setMaterial(id, hudMaterial).has_value());
        hudObjects.push_back(id);
    }

    core::PassesConfig config;
    config.camera2 = render::Camera2{
        .worldToView = math::Transform2{},
        .width = 30.0,
        .height = 30.0,
    };
    config.camera3 = std::nullopt; // Чисто 2D конвейер, без 3D-камеры
    config.hudEnabled = true;

    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    renderer::SoftwareRenderer softwareRenderer;
    SimpleSyncPresentationPort presentation;

    std::vector<FrameTelemetry> telemetryFrames;
    telemetryFrames.reserve(FRAME_COUNT);

    for (std::size_t frameIndex = 0; frameIndex < FRAME_COUNT; ++frameIndex) {
        FrameTelemetry telemetry;
        telemetry.frameId = frameIndex;

        // Фаза 1: Update
        const auto updateStart = std::chrono::steady_clock::now();
        // Сцена 2D статична в данном замере
        const auto updateEnd = std::chrono::steady_clock::now();
        telemetry.timings.updateUs =
            std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count();

        // Фаза 2: Prepare
        const auto prepareStart = std::chrono::steady_clock::now();
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto snapshot = std::move(*snapshotResult);
        const auto prepareEnd = std::chrono::steady_clock::now();
        telemetry.timings.prepareUs =
            std::chrono::duration_cast<std::chrono::microseconds>(prepareEnd - prepareStart).count();

        // Инварианты структуры 2D-only снимка:
        CHECK(snapshot.resources3.empty()); // 0 3D-ресурсов
        CHECK(snapshot.passes.size() == 2);
        CHECK(std::holds_alternative<render::World2DPass>(snapshot.passes[0]));
        CHECK(std::holds_alternative<render::Hud2DPass>(snapshot.passes[1]));

        const auto& world2DPass = std::get<render::World2DPass>(snapshot.passes[0]);
        const auto& hudPass = std::get<render::Hud2DPass>(snapshot.passes[1]);
        CHECK(world2DPass.drawItems.size() == OBJECT_COUNT_2D);
        CHECK(hudPass.drawItems.size() == HUD_COUNT);

        // Проверка стабильной сортировки слоёв (layer, order) в World2DPass и Hud2DPass
        for (std::size_t i = 1; i < world2DPass.drawItems.size(); ++i) {
            const auto& prev = world2DPass.drawItems[i - 1];
            const auto& curr = world2DPass.drawItems[i];
            const auto prevKey = std::tie(prev.layer, prev.order);
            const auto currKey = std::tie(curr.layer, curr.order);
            CHECK(prevKey <= currKey);
        }
        for (std::size_t i = 1; i < hudPass.drawItems.size(); ++i) {
            const auto& prev = hudPass.drawItems[i - 1];
            const auto& curr = hudPass.drawItems[i];
            const auto prevKey = std::tie(prev.layer, prev.order);
            const auto currKey = std::tie(curr.layer, curr.order);
            CHECK(prevKey <= currKey);
        }

        telemetry.geometry.objectCount = scene.size();
        telemetry.geometry.drawItemCount = countDrawItems(snapshot);
        telemetry.geometry.triangleCount = countTriangles(snapshot);

        // Фаза 3: Render (2D-растеризация алгоритмом художника с alpha blending, без depth buffer)
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        const auto renderStart = std::chrono::steady_clock::now();
        CHECK(softwareRenderer.render(snapshot, *framebuffer).has_value());
        const auto renderEnd = std::chrono::steady_clock::now();
        telemetry.timings.renderUs =
            std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart).count();

        // Shading cache для 3D normal computations не задействуется
        CHECK(softwareRenderer.shadingCacheMisses() == 0);
        CHECK(softwareRenderer.shadingCacheHits() == 0);

        // Фаза 4: Present
        const auto presentStart = std::chrono::steady_clock::now();
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
        const auto presentEnd = std::chrono::steady_clock::now();
        telemetry.timings.presentUs =
            std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count();

        telemetry.pool.allocations = pipeline.pool().createdCount();
        telemetry.pool.pooled = pipeline.pool().pooledCount();
        telemetry.pipeline.ready = 1;

        telemetryFrames.push_back(telemetry);
    }

    BenchmarkSummary summary;
    summary.scenarioName = "2D-only (100 World2D + 10 Hud2D objects)";
    summary.frameCount = FRAME_COUNT;
    for (const auto& f : telemetryFrames) {
        summary.avgUpdateUs += static_cast<double>(f.timings.updateUs);
        summary.avgPrepareUs += static_cast<double>(f.timings.prepareUs);
        summary.avgRenderUs += static_cast<double>(f.timings.renderUs);
        summary.avgPresentUs += static_cast<double>(f.timings.presentUs);
        summary.avgTotalUs += static_cast<double>(f.timings.totalUs());
        summary.peakBufferAllocations = std::max(summary.peakBufferAllocations, f.pool.allocations);
        summary.totalPipelineReady += f.pipeline.ready;
    }
    summary.avgUpdateUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPrepareUs /= static_cast<double>(FRAME_COUNT);
    summary.avgRenderUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPresentUs /= static_cast<double>(FRAME_COUNT);
    summary.avgTotalUs /= static_cast<double>(FRAME_COUNT);

    printSummary(summary);
}

// ============================================================================
// Сценарий 5: 3D+HUD (3D-сцена со светом + HUD)
// ============================================================================

void scenario3DWithHud() {
    constexpr std::size_t OBJECT_COUNT_3D = 100;
    constexpr std::size_t HUD_COUNT = 10;
    constexpr std::size_t FRAME_COUNT = 5;

    core::Scene scene;
    const auto worldMaterial = makeMaterial(0.7, 0.3, 0.3);
    for (std::size_t i = 0; i < OBJECT_COUNT_3D; ++i) {
        auto created = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{1.0, 1.0, 1.0}}});
        CHECK(created.has_value());
        const auto id = *created;
        const double x = static_cast<double>(i % 10) * 2.0 - 9.0;
        const double y = static_cast<double>(i / 10) * 2.0 - 9.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector3{{x, y, 10.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::World, 0, 0).has_value());
        CHECK(scene.setMaterial(id, worldMaterial).has_value());
    }

    const auto hudMaterial = makeMaterial(1.0, 1.0, 0.0, 0.85);
    for (std::size_t i = 0; i < HUD_COUNT; ++i) {
        auto created = scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{5.0, 3.0}}});
        CHECK(created.has_value());
        const auto id = *created;
        const double x = static_cast<double>(i) * 5.0 + 3.0;
        CHECK(scene.setLocalTransform(id, math::translation(math::Vector2{{x, 4.0}})).has_value());
        CHECK(scene.setRenderProperties(id, core::RenderSpace::Hud, 0, static_cast<std::int32_t>(i)).has_value());
        CHECK(scene.setMaterial(id, hudMaterial).has_value());
    }

    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 30.0,
        .height = 30.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    config.hudEnabled = true;
    config.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.25},
        .directional =
            {
                {
                    .direction = math::Vector3{{1.0, 1.0, 1.0}},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.75,
                },
            },
    };

    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    renderer::SoftwareRenderer softwareRenderer;
    SimpleSyncPresentationPort presentation;

    // Кадр 0: прогрев (тесселяция и построение кеша normal shading)
    {
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        CHECK(softwareRenderer.render(*snapshotResult, *framebuffer).has_value());
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
    }

    std::vector<FrameTelemetry> telemetryFrames;
    telemetryFrames.reserve(FRAME_COUNT);

    std::size_t prevTessCalcs = scene.tessellationCalculations();
    std::size_t prevTessHits = scene.tessellationCacheHits();
    std::size_t prevShadingMisses = softwareRenderer.shadingCacheMisses();
    std::size_t prevShadingHits = softwareRenderer.shadingCacheHits();

    for (std::size_t frameIndex = 1; frameIndex <= FRAME_COUNT; ++frameIndex) {
        FrameTelemetry telemetry;
        telemetry.frameId = frameIndex;

        const auto updateStart = std::chrono::steady_clock::now();
        const auto updateEnd = std::chrono::steady_clock::now();
        telemetry.timings.updateUs =
            std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count();

        const auto prepareStart = std::chrono::steady_clock::now();
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
        CHECK(snapshotResult.has_value());
        auto snapshot = std::move(*snapshotResult);
        const auto prepareEnd = std::chrono::steady_clock::now();
        telemetry.timings.prepareUs =
            std::chrono::duration_cast<std::chrono::microseconds>(prepareEnd - prepareStart).count();

        // Проверка композиции: World3DPass затем Hud2DPass
        CHECK(snapshot.passes.size() == 2);
        CHECK(std::holds_alternative<render::World3DPass>(snapshot.passes[0]));
        CHECK(std::holds_alternative<render::Hud2DPass>(snapshot.passes[1]));

        const std::size_t curTessCalcs = scene.tessellationCalculations();
        const std::size_t curTessHits = scene.tessellationCacheHits();
        telemetry.cache.tessellationCalculations = curTessCalcs - prevTessCalcs;
        telemetry.cache.tessellationHits = curTessHits - prevTessHits;
        prevTessCalcs = curTessCalcs;
        prevTessHits = curTessHits;

        telemetry.geometry.objectCount = scene.size();
        telemetry.geometry.drawItemCount = countDrawItems(snapshot);
        telemetry.geometry.triangleCount = countTriangles(snapshot);

        // Треугольники: 100 кубов (1200 треуг.) + 10 HUD-прямоугольников (20 треуг.) = 1220 треугольников
        CHECK(telemetry.geometry.drawItemCount == OBJECT_COUNT_3D + HUD_COUNT);
        CHECK(telemetry.geometry.triangleCount == OBJECT_COUNT_3D * 12 + HUD_COUNT * 2);

        auto framebuffer = pipeline.acquireFramebuffer();
        CHECK(framebuffer != nullptr);
        const auto renderStart = std::chrono::steady_clock::now();
        // Выполняется flat shading, depth-buffer тест для 3D и наложение HUD с alpha blending
        CHECK(softwareRenderer.render(snapshot, *framebuffer).has_value());
        const auto renderEnd = std::chrono::steady_clock::now();
        telemetry.timings.renderUs =
            std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart).count();

        const std::size_t curShadingMisses = softwareRenderer.shadingCacheMisses();
        const std::size_t curShadingHits = softwareRenderer.shadingCacheHits();
        telemetry.cache.shadingMisses = curShadingMisses - prevShadingMisses;
        telemetry.cache.shadingHits = curShadingHits - prevShadingHits;
        prevShadingMisses = curShadingMisses;
        prevShadingHits = curShadingHits;

        const auto presentStart = std::chrono::steady_clock::now();
        framebuffer = presentation.present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));
        const auto presentEnd = std::chrono::steady_clock::now();
        telemetry.timings.presentUs =
            std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count();

        telemetry.pool.allocations = pipeline.pool().createdCount();
        telemetry.pool.pooled = pipeline.pool().pooledCount();
        telemetry.pipeline.ready = 1;

        // В стабильном режиме: 100% hits для тесселяции и затенения
        CHECK(telemetry.cache.tessellationCalculations == 0);
        CHECK(telemetry.cache.tessellationHits == OBJECT_COUNT_3D + HUD_COUNT);
        CHECK(telemetry.cache.shadingMisses == 0);
        CHECK(telemetry.cache.shadingHits == OBJECT_COUNT_3D);

        telemetryFrames.push_back(telemetry);
    }

    BenchmarkSummary summary;
    summary.scenarioName = "3D+HUD (100 World3D + 10 Hud2D, lit + depth test)";
    summary.frameCount = FRAME_COUNT;
    for (const auto& f : telemetryFrames) {
        summary.avgUpdateUs += static_cast<double>(f.timings.updateUs);
        summary.avgPrepareUs += static_cast<double>(f.timings.prepareUs);
        summary.avgRenderUs += static_cast<double>(f.timings.renderUs);
        summary.avgPresentUs += static_cast<double>(f.timings.presentUs);
        summary.avgTotalUs += static_cast<double>(f.timings.totalUs());
        summary.totalTessellationCalculations += f.cache.tessellationCalculations;
        summary.totalTessellationHits += f.cache.tessellationHits;
        summary.totalShadingMisses += f.cache.shadingMisses;
        summary.totalShadingHits += f.cache.shadingHits;
        summary.peakBufferAllocations = std::max(summary.peakBufferAllocations, f.pool.allocations);
        summary.totalPipelineReady += f.pipeline.ready;
    }
    summary.avgUpdateUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPrepareUs /= static_cast<double>(FRAME_COUNT);
    summary.avgRenderUs /= static_cast<double>(FRAME_COUNT);
    summary.avgPresentUs /= static_cast<double>(FRAME_COUNT);
    summary.avgTotalUs /= static_cast<double>(FRAME_COUNT);

    printSummary(summary);
}

// ============================================================================
// Сценарий 6: slow Renderer (симуляция медленного рендерера и обратного давления)
// ============================================================================

void scenarioSlowRenderer() {
    runtime::CompletionChannel channel;
    adapter::FakeRendererAdapter fakeAdapter{channel};
    runtime::FramePipeline pipeline;
    pipeline.resize({BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT});
    SimpleSyncPresentationPort presentation;

    core::Scene scene;
    auto box = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{2.0, 2.0, 2.0}}});
    CHECK(box.has_value());

    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 10.0,
        .height = 10.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };

    const auto snapStart = std::chrono::steady_clock::now();
    auto snapshotResult =
        core::RenderPreparation::prepareSnapshot(scene, config, BENCHMARK_IMAGE_WIDTH, BENCHMARK_IMAGE_HEIGHT);
    const auto snapEnd = std::chrono::steady_clock::now();
    CHECK(snapshotResult.has_value());
    const auto snapshot = std::move(*snapshotResult);
    const auto snapDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(snapEnd - snapStart).count();

    PipelineMetrics pipelineStats;
    std::uint64_t totalPrepareUs = snapDurationUs;
    std::uint64_t totalSubmitUs = 0;
    std::uint64_t totalPresentUs = 0;

    // 1. Отправка первого кадра (Frame 1)
    CHECK(pipeline.canPrepare());
    auto fb1 = pipeline.acquireFramebuffer();
    CHECK(fb1 != nullptr);
    runtime::RenderFrame frame1{
        .frameId = 1,
        .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
        .framebuffer = std::move(fb1),
    };
    const auto submit1Start = std::chrono::steady_clock::now();
    const auto submit1 = pipeline.submit(fakeAdapter, frame1);
    const auto submit1End = std::chrono::steady_clock::now();
    totalSubmitUs += std::chrono::duration_cast<std::chrono::microseconds>(submit1End - submit1Start).count();
    CHECK(submit1 == runtime::SubmitResult::Accepted);
    CHECK(fakeAdapter.isBusy());
    CHECK(pipeline.hasActiveFrame());
    CHECK(!pipeline.hasPreparedFrame());

    // 2. Пока Frame 1 рендерится (Renderer занят), подготавливаем Frame 2 в prepared-слот (глубина 1+1)
    CHECK(pipeline.canPrepare()); // перекрытие prepare с активным render разрешено
    auto fb2 = pipeline.acquireFramebuffer();
    CHECK(fb2 != nullptr);
    runtime::RenderFrame frame2{
        .frameId = 2,
        .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
        .framebuffer = std::move(fb2),
    };
    const bool stored2 = pipeline.storePrepared(frame2);
    CHECK(stored2);
    CHECK(pipeline.hasPreparedFrame());

    // 3. Обратное давление: Frame 1 ещё исполняется, prepared-слот занят Frame 2
    //    canPrepare() SHALL быть false! Подготовка новых снимков блокируется.
    CHECK(!pipeline.canPrepare());

    // Попытка отправить кадр 3 напрямую в занятый порт адаптера отклоняется (SubmitResult::Rejected)
    auto fb3 = pipeline.acquireFramebuffer();
    runtime::RenderFrame frame3{
        .frameId = 3,
        .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
        .framebuffer = std::move(fb3),
    };
    const auto submit3 = pipeline.submit(fakeAdapter, frame3);
    CHECK(submit3 == runtime::SubmitResult::Rejected);
    ++pipelineStats.rejected;
    // Буфер отклонённого кадра возвращаем в пул
    pipeline.releaseFromPresentation(std::move(frame3.framebuffer));

    // Попытка dispatch prepared-слота при занятом адаптере не отправляет снимок
    const auto dispatchBusy = pipeline.dispatchPrepared(fakeAdapter);
    CHECK(dispatchBusy == runtime::DispatchResult::Empty);
    CHECK(pipeline.hasPreparedFrame()); // Frame 2 сохранён в слоте

    // 4. Медленный Renderer завершает Frame 1 (FrameReady)
    CHECK(fakeAdapter.completeReady());
    auto completion1 = pipeline.complete(channel);
    CHECK(completion1.outcome == runtime::FrameOutcome::Ready);
    ++pipelineStats.ready;
    pipeline.handleCompletedFrame(std::move(completion1), presentation);
    CHECK(presentation.presentCount() == 1);
    CHECK(!fakeAdapter.isBusy());
    CHECK(!pipeline.hasActiveFrame());

    // 5. Теперь Renderer свободен -> dispatchPrepared успешно отправляет сохранённый Frame 2
    const auto dispatchResult = pipeline.dispatchPrepared(fakeAdapter);
    CHECK(dispatchResult == runtime::DispatchResult::Submitted);
    CHECK(fakeAdapter.isBusy());
    CHECK(pipeline.hasActiveFrame());
    CHECK(!pipeline.hasPreparedFrame()); // prepared-слот освободился

    // 6. Проверка отсечки кадров при resize (stale dropping):
    //    Пока Frame 2 рендерится, помещаем Frame 4 в prepared-слот
    CHECK(pipeline.canPrepare());
    auto fb4 = pipeline.acquireFramebuffer();
    runtime::RenderFrame frame4{
        .frameId = 4,
        .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
        .framebuffer = std::move(fb4),
    };
    CHECK(pipeline.storePrepared(frame4));
    CHECK(pipeline.hasPreparedFrame());

    // Происходит resize поверхности (изменение поколения)
    pipeline.resize({128, 128});
    // Подготовленный кадр прежнего размера уничтожен при resize
    CHECK(!pipeline.hasPreparedFrame());
    ++pipelineStats.dropped;

    // Frame 2 завершается на адаптере
    CHECK(fakeAdapter.completeReady());
    auto completion2 = pipeline.complete(channel);
    // Кадр прежнего поколения отброшен без показа
    CHECK(completion2.outcome == runtime::FrameOutcome::Stale);
    ++pipelineStats.dropped;
    pipeline.handleCompletedFrame(std::move(completion2), presentation);
    CHECK(presentation.presentCount() == 1); // счетчик present не увеличился

    // 7. Проверка инвариантов пула буферов: утечек нет, переиспользование ограничено maxPooled
    CHECK(pipeline.pool().pooledCount() <= pipeline.pool().maxPooled());

    // 8. Симуляция главного цикла runIteration с FixedStepUpdate в условиях занятого Renderer
    runtime::FixedStepUpdate fixedUpdate{1.0 / 60.0, 4};
    std::uint64_t nextFrameId = 10;

    // Итерация A: отправка первого кадра в свободный порт
    {
        runtime::LoopEvents events;
        auto loopRes = runtime::runIteration(
            fakeAdapter, pipeline, channel, fixedUpdate, events, 1.0 / 60.0, []() {}, [](double) {},
            [&]() -> bool {
                auto fb = pipeline.acquireFramebuffer();
                if (!fb) {
                    return false;
                }
                runtime::RenderFrame rf{
                    .frameId = nextFrameId++,
                    .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
                    .framebuffer = std::move(fb),
                };
                return pipeline.storePrepared(rf);
            });
        CHECK(loopRes.prepared);
        CHECK(loopRes.dispatch == runtime::DispatchResult::Submitted);
        CHECK(fakeAdapter.isBusy());
    }

    // Итерация B: Renderer занят, prepare сохраняет кадр в prepared-слот (глубина 1+1)
    {
        runtime::LoopEvents events;
        auto loopRes = runtime::runIteration(
            fakeAdapter, pipeline, channel, fixedUpdate, events, 1.0 / 60.0, []() {}, [](double) {},
            [&]() -> bool {
                auto fb = pipeline.acquireFramebuffer();
                if (!fb) {
                    return false;
                }
                runtime::RenderFrame rf{
                    .frameId = nextFrameId++,
                    .snapshot = std::make_unique<render::RenderSnapshot>(snapshot),
                    .framebuffer = std::move(fb),
                };
                return pipeline.storePrepared(rf);
            });
        CHECK(loopRes.prepared);
        CHECK(loopRes.dispatch == runtime::DispatchResult::Empty);
        CHECK(pipeline.hasPreparedFrame());
    }

    // Итерация C: Renderer занят И prepared-слот занят -> canPrepare() == false!
    // Лямбда подготовки даже не вызывается, предотвращая бесполезные вычисления
    {
        runtime::LoopEvents events;
        bool prepareLambdaCalled = false;
        auto loopRes = runtime::runIteration(
            fakeAdapter, pipeline, channel, fixedUpdate, events, 1.0 / 60.0, []() {}, [](double) {},
            [&]() -> bool {
                prepareLambdaCalled = true;
                return false;
            });
        CHECK(!loopRes.prepared);
        CHECK(!prepareLambdaCalled);
        CHECK(loopRes.dispatch == runtime::DispatchResult::Empty);
    }

    // Освобождаем Renderer от активного кадра
    CHECK(fakeAdapter.completeReady());
    auto loopCompletion = pipeline.complete(channel);
    CHECK(loopCompletion.outcome == runtime::FrameOutcome::Ready);
    ++pipelineStats.ready;
    pipeline.handleCompletedFrame(std::move(loopCompletion), presentation);

    // Итерация D: Renderer освободился -> dispatchPrepared отправляет сохранённый кадр
    {
        runtime::LoopEvents events;
        auto loopRes = runtime::runIteration(
            fakeAdapter, pipeline, channel, fixedUpdate, events, 1.0 / 60.0, []() {}, [](double) {},
            [&]() -> bool { return false; });
        CHECK(loopRes.dispatch == runtime::DispatchResult::Submitted);
        CHECK(fakeAdapter.isBusy());
        CHECK(!pipeline.hasPreparedFrame());
    }

    // Завершаем финальный кадр
    CHECK(fakeAdapter.completeReady());
    auto finalCompletion = pipeline.complete(channel);
    CHECK(finalCompletion.outcome == runtime::FrameOutcome::Ready);
    ++pipelineStats.ready;
    pipeline.handleCompletedFrame(std::move(finalCompletion), presentation);

    // 9. Завершение работы конвейера
    pipeline.requestStop();
    pipeline.confirmStopped();

    BenchmarkSummary summary;
    summary.scenarioName = "slow Renderer (1+1 backpressure, rejected & dropped frames)";
    summary.frameCount = 4;
    summary.avgPrepareUs = static_cast<double>(totalPrepareUs);
    summary.avgRenderUs = static_cast<double>(totalSubmitUs);
    summary.avgPresentUs = static_cast<double>(totalPresentUs);
    summary.avgTotalUs = summary.avgPrepareUs + summary.avgRenderUs + summary.avgPresentUs;
    summary.peakBufferAllocations = pipeline.pool().createdCount();
    summary.totalPipelineReady = pipelineStats.ready;
    summary.totalPipelineRejected = pipelineStats.rejected;
    summary.totalPipelineDropped = pipelineStats.dropped;

    printSummary(summary);
}

// ============================================================================
// Main
// ============================================================================

// ============================================================================
// Сценарий 7: GPU (OpenGL 3.3 Core) против CPU (SoftwareRenderer)
// ============================================================================
// Скрытый GLX-pbuffer контекст создаётся через dlopen без зависимостей сборки
// от X11; при недоступном GL-дисплее сценарий выводит пропуск и успешно завершается.

namespace glx {

constexpr int drawableType = 0x8010;
constexpr int renderType = 0x8011;
constexpr int rgbaBit = 0x00000001;
constexpr int pbufferBit = 0x00000004;
constexpr int pbufferWidth = 0x8041;
constexpr int pbufferHeight = 0x8042;
constexpr int contextMajor = 0x2091;
constexpr int contextMinor = 0x2092;
constexpr int contextProfileMask = 0x9126;
constexpr int contextCoreProfile = 0x00000001;

class Context {
public:
    static std::unique_ptr<Context> create() {
        void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        void* glx = dlopen("libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
        if (glx == nullptr) {
            glx = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        }
        if (x11 == nullptr || glx == nullptr) {
            return nullptr;
        }
        auto context = std::unique_ptr<Context>(new Context(x11, glx));
        if (!context->initialize()) {
            return nullptr;
        }
        g_active = context.get();
        return context;
    }

    ~Context() {
        if (m_display != nullptr) {
            m_makeCurrent(m_display, 0, nullptr);
            m_destroyPbuffer(m_display, m_pbuffer);
            m_destroyContext(m_display, m_context);
            m_closeDisplay(m_display);
        }
        dlclose(m_x11);
        dlclose(m_glx);
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] renderer::GlRenderer::GlProcResolver resolver() const noexcept { return &resolveProc; }

private:
    using GetProcFn = void* (*)(const unsigned char*);

    static void* resolveProc(const char* name) {
        return g_active->m_getProc(reinterpret_cast<const unsigned char*>(name));
    }

    Context(void* x11, void* glx) : m_x11(x11), m_glx(glx) {}

    bool initialize() {
        const auto xOpenDisplay = reinterpret_cast<void* (*)(const char*)>(dlsym(m_x11, "XOpenDisplay"));
        m_closeDisplay = reinterpret_cast<int (*)(void*)>(dlsym(m_x11, "XCloseDisplay"));
        const auto chooseFbConfig =
            reinterpret_cast<void** (*)(void*, int, const int*, int*)>(dlsym(m_glx, "glXChooseFBConfig"));
        const auto createPbuffer =
            reinterpret_cast<unsigned long (*)(void*, void*, const int*)>(dlsym(m_glx, "glXCreatePbuffer"));
        m_destroyPbuffer = reinterpret_cast<void (*)(void*, unsigned long)>(dlsym(m_glx, "glXDestroyPbuffer"));
        m_getProc = reinterpret_cast<GetProcFn>(dlsym(m_glx, "glXGetProcAddressARB"));
        m_destroyContext = reinterpret_cast<void (*)(void*, void*)>(dlsym(m_glx, "glXDestroyContext"));
        m_makeCurrent = reinterpret_cast<int (*)(void*, unsigned long, void*)>(dlsym(m_glx, "glXMakeCurrent"));
        if (xOpenDisplay == nullptr || m_closeDisplay == nullptr || chooseFbConfig == nullptr ||
            createPbuffer == nullptr || m_destroyPbuffer == nullptr || m_getProc == nullptr ||
            m_destroyContext == nullptr || m_makeCurrent == nullptr) {
            return false;
        }
        const auto createContextAttribs = reinterpret_cast<void* (*)(void*, void*, void*, int, const int*)>(
            m_getProc(reinterpret_cast<const unsigned char*>("glXCreateContextAttribsARB")));
        if (createContextAttribs == nullptr) {
            return false;
        }
        m_display = xOpenDisplay(nullptr);
        if (m_display == nullptr) {
            return false;
        }
        const int fbAttribs[] = {drawableType, pbufferBit, renderType, rgbaBit, 8, 8, 9, 8, 10, 8, 11, 8, 12, 24, 0};
        int configCount = 0;
        void** configs = chooseFbConfig(m_display, 0, fbAttribs, &configCount);
        if (configs == nullptr || configCount == 0) {
            return false;
        }
        const int pbufferAttribs[] = {pbufferWidth, 1280, pbufferHeight, 720, 0};
        m_pbuffer = createPbuffer(m_display, configs[0], pbufferAttribs);
        if (m_pbuffer == 0) {
            return false;
        }
        const int contextAttribs[] = {contextMajor, 3, contextMinor, 3, contextProfileMask, contextCoreProfile, 0};
        m_context = createContextAttribs(m_display, configs[0], nullptr, 1, contextAttribs);
        if (m_context == nullptr) {
            return false;
        }
        return m_makeCurrent(m_display, m_pbuffer, m_context) != 0;
    }

    static Context* g_active;
    void* m_x11{};
    void* m_glx{};
    void* m_display{};
    void* m_context{};
    unsigned long m_pbuffer{};
    int (*m_closeDisplay)(void*){};
    void (*m_destroyPbuffer)(void*, unsigned long){};
    GetProcFn m_getProc{};
    void (*m_destroyContext)(void*, void*){};
    int (*m_makeCurrent)(void*, unsigned long, void*){};
};

Context* Context::g_active = nullptr;

} // namespace glx

void scenarioGpuVsCpu() {
    constexpr std::size_t width = 1280;
    constexpr std::size_t height = 720;
    constexpr int measuredFrames = 10;

    // Сцена: сетка освещённых коробок с тенями и небом — типичный 3D-кадр движка.
    core::Scene scene;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            auto box = scene.createObject<objects::ObjectType::Box3>(
                {.size = math::Vector3{{0.6, 0.6 + 0.3 * ((row + column) % 3), 0.6}}});
            CHECK(box.has_value());
            const auto transform =
                math::compose(math::translation(math::Vector3{{(column - 3.5) * 1.1, (row - 3.5) * 1.1, 6.0}}),
                              math::rotation(math::RotationAxis::Z, 0.3 * static_cast<double>(row + column)));
            CHECK(scene.setLocalTransform(*box, transform).has_value());
        }
    }
    core::PassesConfig config;
    render::PerspectiveCamera3 camera;
    camera.verticalFieldOfView = std::numbers::pi / 3;
    camera.nearPlane = 0.1;
    camera.farPlane = 100;
    config.camera3 = camera;
    config.lighting.ambient = {render::Color{1, 1, 1, 1}, 0.3};
    config.lighting.directional.push_back({math::Vector3{{0.4, -0.6, 0.7}}, render::Color{1, 1, 1, 1}, 0.8});
    config.lighting.shadows.enabled = true;
    config.lighting.shadows.resolution = 1024;
    config.lighting.shadows.extent = 10;
    config.lighting.shadows.center = math::Point3{{0, 0, 6}};
    config.lighting.sky.enabled = true;
    auto snapshotResult = core::RenderPreparation::prepareSnapshot(scene, config, width, height);
    CHECK(snapshotResult.has_value());
    const auto& snapshot = *snapshotResult;

    auto framebuffer = image::ImageBuffer::create({width, height});
    CHECK(framebuffer.has_value());
    renderer::SoftwareRenderer softwareRenderer;
    const auto renderOnceCpu = [&]() {
        const auto start = std::chrono::steady_clock::now();
        CHECK(softwareRenderer.render(snapshot, *framebuffer).has_value());
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    };
    renderOnceCpu();
    std::uint64_t cpuTotal = 0;
    for (int frame = 0; frame < measuredFrames; ++frame) {
        cpuTotal += renderOnceCpu();
    }

    auto context = glx::Context::create();
    if (context == nullptr) {
        std::cout << "------------------------------------------------------------\n"
                  << "Scenario: GPU vs CPU (skipped: OpenGL display is not available)\n"
                  << "  CPU-only SoftwareRenderer avg: " << cpuTotal / measuredFrames << " us\n"
                  << "------------------------------------------------------------\n";
        return;
    }
    const auto resolver = context->resolver();
    const auto finish = reinterpret_cast<void (*)()>(resolver("glFinish"));
    renderer::GlRenderer glRenderer(resolver);
    const auto renderOnceGpu = [&]() {
        const auto start = std::chrono::steady_clock::now();
        CHECK(glRenderer.render(snapshot).has_value());
        if (finish != nullptr) {
            finish();
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    };
    renderOnceGpu();
    std::uint64_t gpuTotal = 0;
    for (int frame = 0; frame < measuredFrames; ++frame) {
        gpuTotal += renderOnceGpu();
    }

    std::cout << "------------------------------------------------------------\n"
              << "Scenario: GPU vs CPU (64 lit boxes, shadows + sky, " << width << "x" << height << ")\n"
              << "  Avg render frame time: gpu=" << gpuTotal / measuredFrames
              << " us, cpu=" << cpuTotal / measuredFrames << " us\n"
              << "------------------------------------------------------------\n";
}

int main() {
    try {
        std::cout << "Running Huynity Benchmark Suite...\n\n";

        scenarioStatic();
        scenarioMovingInstances();
        scenarioGeometryEdits();
        scenario2DOnly();
        scenario3DWithHud();
        scenarioSlowRenderer();
        scenarioGpuVsCpu();

        std::cout << "\nAll 7 benchmark scenarios passed successfully.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failure: " << error.what() << '\n';
        return 1;
    }
}
