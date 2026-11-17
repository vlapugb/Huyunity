#include "huynity/adapters/renderer_actor/fake_renderer_adapter.h"
#include "huynity/core/render_preparation.h"
#include "huynity/core/scene.h"
#include "huynity/image/image_buffer.h"
#include "huynity/math/transform.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/software_renderer.h"
#include "huynity/runtime/frame_completions.h"
#include "huynity/runtime/renderer_port.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
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

struct Rgba {
    int red;
    int green;
    int blue;
    int alpha;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

constexpr Rgba BLACK{0, 0, 0, 255};
constexpr Rgba LIT_RED{199, 0, 0, 255};
constexpr Rgba LIT_GREEN{0, 255, 0, 255};
constexpr Rgba SHADED_GREEN{0, 64, 0, 255};
constexpr Rgba HUD_YELLOW{255, 255, 0, 255};

constexpr std::size_t IMAGE_WIDTH = 64;
constexpr std::size_t IMAGE_HEIGHT = 64;

render::Color makeColor(double red, double green, double blue, double alpha = 1.0) {
    return {red, green, blue, alpha};
}

std::shared_ptr<const render::Material> makeMaterial(double red, double green, double blue, double alpha = 1.0) {
    return std::make_shared<const render::Material>(makeColor(red, green, blue, alpha));
}

image::ImageBuffer makeFramebuffer(std::size_t width, std::size_t height) {
    auto created = image::ImageBuffer::create({width, height});
    if (!created) {
        throw std::runtime_error("failed to create framebuffer");
    }
    return std::move(*created);
}

Rgba pixel(const image::ImageBuffer& framebuffer, std::size_t x, std::size_t y) {
    const auto bytes = framebuffer.bytes();
    const std::size_t offset = y * framebuffer.stride() + x * 4;
    return {std::to_integer<int>(bytes[offset]), std::to_integer<int>(bytes[offset + 1]),
            std::to_integer<int>(bytes[offset + 2]), std::to_integer<int>(bytes[offset + 3])};
}

core::PassesConfig make3DPassesConfig() {
    core::PassesConfig config;
    config.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 20.0,
        .height = 20.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    config.hudEnabled = true;
    config.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.25},
        .directional =
            {
                {
                    .direction = math::Vector3{{1.0, 0.0, 1.0}},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.75,
                },
            },
    };
    return config;
}

/// @brief Создаёт 3D-сцену с shared geometry ресурсом, статическим и динамическим экземплярами и HUD.
struct SceneSetup {
    core::Scene scene;
    core::ObjectId sourceId{};
    core::ObjectId staticId{};
    core::ObjectId dynamicId{};
    core::ObjectId hudId{};
};

SceneSetup create3DScene() {
    SceneSetup setup;

    // 1. Исходный 3D-объект: куб 4x4x4 в начале координат
    auto sourceBox = setup.scene.createObject(objects::Box3::Params{.size = {4.0, 4.0, 4.0}});
    CHECK(sourceBox.has_value());
    setup.sourceId = *sourceBox;

    // 2. Статический экземпляр куба (Mobility::Static), смещённый влево, с красным материалом
    auto staticInstance = setup.scene.createInstance(setup.sourceId);
    CHECK(staticInstance.has_value());
    setup.staticId = *staticInstance;
    CHECK(setup.scene.setMobility(setup.staticId, core::Mobility::Static).has_value());
    CHECK(
        setup.scene.setLocalTransform(setup.staticId, math::translation(math::Vector3{{-5.0, 0.0, 10.0}})).has_value());
    CHECK(setup.scene.setRenderProperties(setup.staticId, core::RenderSpace::World, 0, 0).has_value());
    CHECK(setup.scene.setMaterial(setup.staticId, makeMaterial(1.0, 0.0, 0.0)).has_value());

    // 3. Динамический экземпляр куба (Mobility::Dynamic), смещённый вправо и повёрнутый на 45°, зелёный материал
    auto dynamicInstance = setup.scene.createInstance(setup.sourceId);
    CHECK(dynamicInstance.has_value());
    setup.dynamicId = *dynamicInstance;
    CHECK(setup.scene.setMobility(setup.dynamicId, core::Mobility::Dynamic).has_value());
    const auto rotY = math::rotation(math::RotationAxis::Y, std::numbers::pi / 4.0);
    const auto transDynamic = math::translation(math::Vector3{{5.0, 0.0, 10.0}});
    CHECK(setup.scene.setLocalTransform(setup.dynamicId, math::compose(transDynamic, rotY)).has_value());
    CHECK(setup.scene.setRenderProperties(setup.dynamicId, core::RenderSpace::World, 0, 0).has_value());
    CHECK(setup.scene.setMaterial(setup.dynamicId, makeMaterial(0.0, 1.0, 0.0)).has_value());

    // Исходный шаблонный объект не рендерится сам по себе
    CHECK(setup.scene.setVisibility(setup.sourceId, false).has_value());

    // 4. Элемент HUD (RenderSpace::Hud) в экранных пикселях: жёлтый прямоугольник 10x6 с центром в (16, 32)
    auto hudElement = setup.scene.createObject(objects::Rectangle2::Params{.size = {10.0, 6.0}});
    CHECK(hudElement.has_value());
    setup.hudId = *hudElement;
    CHECK(setup.scene.setLocalTransform(setup.hudId, math::translation(math::Vector2{{16.0, 32.0}})).has_value());
    CHECK(setup.scene.setRenderProperties(setup.hudId, core::RenderSpace::Hud, 0, 0).has_value());
    CHECK(setup.scene.setMaterial(setup.hudId, makeMaterial(1.0, 1.0, 0.0)).has_value());

    return setup;
}

/// @brief 1 & 2: Воспроизводимый 3D-сценарий, shared geometry, свет, HUD и контрольные пиксели изображения.
void scenario3DImageCheckAndSharedGeometry() {
    auto [scene, sourceId, staticId, dynamicId, hudId] = create3DScene();

    // Проверка разделения геометрии (shared geometry) в сцене:
    const auto* sourceInst = scene.find(sourceId);
    const auto* staticInst = scene.find(staticId);
    const auto* dynamicInst = scene.find(dynamicId);
    CHECK(sourceInst != nullptr && staticInst != nullptr && dynamicInst != nullptr);
    CHECK(sourceInst->geometry != nullptr);
    CHECK(sourceInst->geometry == staticInst->geometry);
    CHECK(sourceInst->geometry == dynamicInst->geometry);

    CHECK(staticInst->mobility == core::Mobility::Static);
    CHECK(dynamicInst->mobility == core::Mobility::Dynamic);
    CHECK(scene.find(hudId)->renderSpace == core::RenderSpace::Hud);

    // Подготовка снимка
    const auto config = make3DPassesConfig();
    auto snapshotResult = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshotResult.has_value());
    auto snapshot = std::move(*snapshotResult);
    snapshot.clearColor = makeColor(0.0, 0.0, 0.0);

    // Проверка структуры снимка: 2 пасса (World3DPass, затем Hud2DPass)
    CHECK(snapshot.passes.size() == 2);
    CHECK(std::holds_alternative<render::World3DPass>(snapshot.passes[0]));
    CHECK(std::holds_alternative<render::Hud2DPass>(snapshot.passes[1]));

    // Shared geometry на уровне снимка: ровно один 3D mesh-ресурс, разделяемый обоими draw items
    CHECK(snapshot.resources3.size() == 1);
    const auto& worldPass = std::get<render::World3DPass>(snapshot.passes[0]);
    CHECK(worldPass.drawItems.size() == 2);
    CHECK(worldPass.drawItems[0].resource == 0);
    CHECK(worldPass.drawItems[1].resource == 0);
    CHECK(worldPass.drawItems[0].material != worldPass.drawItems[1].material);

    const auto& hudPass = std::get<render::Hud2DPass>(snapshot.passes[1]);
    CHECK(hudPass.drawItems.size() == 1);

    // Рендеринг кадра через SoftwareRenderer
    renderer::SoftwareRenderer softwareRenderer;
    auto framebuffer = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(softwareRenderer.render(snapshot, framebuffer).has_value());

    // Контроль изображения:
    // (2, 2): фон за пределами объектов — чёрный
    CHECK(pixel(framebuffer, 2, 2) == BLACK);

    // (16, 27): освещённая грань статического красного куба выше зоны перекрытия HUD
    // Направленный свет под 45° даёт освещённость ambient(0.25) + 0.75 * cos(45°) = 0.7803 -> red 199
    CHECK(pixel(framebuffer, 16, 27) == LIT_RED);

    // (16, 32): наложение HUD (жёлтый прямоугольник) поверх статического красного куба
    CHECK(pixel(framebuffer, 16, 32) == HUD_YELLOW);

    // (44, 32): освещённая грань динамического куба (повернута навстречу свету, полное освещение 1.0 -> green 255)
    CHECK(pixel(framebuffer, 44, 32) == LIT_GREEN);

    // (52, 32): затенённая грань динамического куба (отвернута от направленного света, только ambient 0.25 -> green 64)
    CHECK(pixel(framebuffer, 52, 32) == SHADED_GREEN);
}

/// @brief 3: Удаление объекта при активном снимке (lifetime test).
void scenario3DActiveSnapshotObjectRemoval() {
    auto [scene, sourceId, staticId, dynamicId, hudId] = create3DScene();

    const auto config = make3DPassesConfig();
    auto snapshotResult = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshotResult.has_value());
    auto snapshot = std::move(*snapshotResult);
    snapshot.clearColor = makeColor(0.0, 0.0, 0.0);

    // Фиксируем исходный рендер кадра
    renderer::SoftwareRenderer softwareRenderer;
    auto initialFramebuffer = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(softwareRenderer.render(snapshot, initialFramebuffer).has_value());

    // Сохраняем указатели и свойства ресурсов снимка до удаления объектов сцены
    const auto ownerBefore = snapshot.resources3[0].owner;
    const auto* const positionsDataBefore = snapshot.resources3[0].positions.data();
    const auto positionsSizeBefore = snapshot.resources3[0].positions.size();
    const auto* const trianglesDataBefore = snapshot.resources3[0].triangles.data();
    const auto trianglesSizeBefore = snapshot.resources3[0].triangles.size();

    CHECK(ownerBefore != nullptr);
    CHECK(positionsDataBefore != nullptr);
    CHECK(trianglesDataBefore != nullptr);

    // Удаляем все объекты из сцены, удерживая RenderSnapshot живым
    CHECK(scene.removeSubtree(dynamicId).has_value());
    CHECK(scene.removeSubtree(staticId).has_value());
    CHECK(scene.removeSubtree(sourceId).has_value());
    CHECK(scene.removeSubtree(hudId).has_value());
    CHECK(scene.size() == 0);

    // Проверяем, что RenderSnapshot сохраняет владение ресурсами через owner и указатели валидны
    CHECK(snapshot.resources3[0].owner != nullptr);
    CHECK(snapshot.resources3[0].owner == ownerBefore);
    CHECK(snapshot.resources3[0].positions.data() == positionsDataBefore);
    CHECK(snapshot.resources3[0].positions.size() == positionsSizeBefore);
    CHECK(snapshot.resources3[0].triangles.data() == trianglesDataBefore);
    CHECK(snapshot.resources3[0].triangles.size() == trianglesSizeBefore);

    // Отрендерим снимок ещё раз после удаления объектов из сцены
    auto framebufferAfterRemoval = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(softwareRenderer.render(snapshot, framebufferAfterRemoval).has_value());

    // Проверка побайтовой идентичности изображения до и после удаления объектов сцены
    CHECK(framebufferAfterRemoval.sizeBytes() == initialFramebuffer.sizeBytes());
    CHECK(std::ranges::equal(framebufferAfterRemoval.bytes(), initialFramebuffer.bytes()));
    CHECK(std::memcmp(framebufferAfterRemoval.bytes().data(), initialFramebuffer.bytes().data(),
                      initialFramebuffer.sizeBytes()) == 0);
}

/// @brief 4: Проверка sync/async эквивалентности через SoftwareRenderer и FakeRendererAdapter.
void scenario3DSyncAsyncEquivalence() {
    auto [scene, sourceId, staticId, dynamicId, hudId] = create3DScene();

    const auto config = make3DPassesConfig();
    auto snapshotResult = core::RenderPreparation::prepareSnapshot(scene, config, IMAGE_WIDTH, IMAGE_HEIGHT);
    CHECK(snapshotResult.has_value());
    auto snapshot = std::move(*snapshotResult);
    snapshot.clearColor = makeColor(0.0, 0.0, 0.0);

    // 1. Синхронный эталонный рендеринг через SoftwareRenderer
    auto syncFramebuffer = makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT);
    renderer::SoftwareRenderer syncRenderer;
    CHECK(syncRenderer.render(snapshot, syncFramebuffer).has_value());

    // 2. Асинхронная доставка кадра через FakeRendererAdapter + CompletionChannel + RenderFrame
    runtime::CompletionChannel channel;
    adapter::FakeRendererAdapter fakeAdapter{channel};

    auto asyncSnapshot = std::make_unique<render::RenderSnapshot>(snapshot);
    auto asyncFramebufferPtr = std::make_unique<image::ImageBuffer>(makeFramebuffer(IMAGE_WIDTH, IMAGE_HEIGHT));

    runtime::RenderFrame frame{
        .frameId = 101,
        .snapshot = std::move(asyncSnapshot),
        .framebuffer = std::move(asyncFramebufferPtr),
    };

    // Передача кадра в порт адаптера
    const auto submitResult = fakeAdapter.trySubmit(frame);
    CHECK(submitResult == runtime::SubmitResult::Accepted);
    CHECK(frame.snapshot == nullptr);    // владение снимком передано адаптеру
    CHECK(frame.framebuffer == nullptr); // владение буфером передано адаптеру
    CHECK(fakeAdapter.isBusy());
    CHECK(channel.hasReservedSlot());

    // Симуляция актора: извлечение кадра, растеризация и публикация FrameReady в CompletionChannel
    auto activeFrame = fakeAdapter.complete();
    CHECK(!fakeAdapter.isBusy());
    CHECK(activeFrame.frameId == 101);
    CHECK(activeFrame.snapshot != nullptr);
    CHECK(activeFrame.framebuffer != nullptr);

    renderer::SoftwareRenderer asyncRenderer;
    CHECK(asyncRenderer.render(*activeFrame.snapshot, *activeFrame.framebuffer).has_value());

    CHECK(channel.publish(runtime::FrameReady{activeFrame.frameId, std::move(activeFrame)}));
    CHECK(channel.hasCompletion());

    // Получение завершения из канала
    auto completion = channel.take();
    CHECK(completion.has_value());
    CHECK(!channel.hasReservedSlot());
    CHECK(!channel.hasCompletion());
    CHECK(std::holds_alternative<runtime::FrameReady>(*completion));

    const auto& ready = std::get<runtime::FrameReady>(*completion);
    CHECK(ready.frameId == 101);
    CHECK(ready.frame.framebuffer != nullptr);
    const auto& asyncFramebuffer = *ready.frame.framebuffer;

    // 3. Побайтовое сравнение содержимого буферов (sync.bytes() == async.bytes())
    CHECK(syncFramebuffer.width() == asyncFramebuffer.width());
    CHECK(syncFramebuffer.height() == asyncFramebuffer.height());
    CHECK(syncFramebuffer.stride() == asyncFramebuffer.stride());
    CHECK(syncFramebuffer.format() == asyncFramebuffer.format());
    CHECK(syncFramebuffer.sizeBytes() == asyncFramebuffer.sizeBytes());
    CHECK(syncFramebuffer.bytes().size() == asyncFramebuffer.bytes().size());

    CHECK(std::ranges::equal(syncFramebuffer.bytes(), asyncFramebuffer.bytes()));
    CHECK(std::memcmp(syncFramebuffer.bytes().data(), asyncFramebuffer.bytes().data(), syncFramebuffer.sizeBytes()) ==
          0);

    // 4. Построчная и попиксельная проверка
    for (std::size_t y = 0; y < IMAGE_HEIGHT; ++y) {
        CHECK(std::ranges::equal(syncFramebuffer.row(y), asyncFramebuffer.row(y)));
        for (std::size_t x = 0; x < IMAGE_WIDTH; ++x) {
            CHECK(pixel(syncFramebuffer, x, y) == pixel(asyncFramebuffer, x, y));
        }
    }
}

int main() {
    try {
        scenario3DImageCheckAndSharedGeometry();
        scenario3DActiveSnapshotObjectRemoval();
        scenario3DSyncAsyncEquivalence();
        std::cout << "scenario_3d_tests: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
