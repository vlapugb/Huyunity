#include "huynity/adapters/renderer_actor/fake_renderer_adapter.h"
#include "huynity/adapters/window/headless_window.h"
#include "huynity/adapters/window/input_events.h"
#include "huynity/adapters/window/input_state.h"
#include "huynity/adapters/window/window_adapter.h"
#include "huynity/adapters/window/window_factory.h"
#include "huynity/core/render_preparation.h"
#include "huynity/core/scene.h"
#include "huynity/image/image_buffer.h"
#include "huynity/math/transform.h"
#include "huynity/objectlib/shapes/box3.h"
#include "huynity/objectlib/shapes/rectangle2.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/software_renderer.h"
#include "huynity/runtime/fixed_step_update.h"
#include "huynity/runtime/frame_completions.h"
#include "huynity/runtime/frame_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace window = huynity::adapters::window;
namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace renderer = huynity::renderer;
namespace runtime = huynity::runtime;
namespace actor = huynity::adapters::renderer_actor;

int main(int argc, char* argv[]) {
    bool continuousMode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--continuous" || arg == "-c") {
            continuousMode = true;
        }
    }

    // 1. Создание окна через фабрику оконного адаптера
    auto window = window::createWindow({.size = {64, 64}, .title = "Huynity Window Demo"});
    if (!window || !window->isOpen()) {
        std::cerr << "Failed to create window adapter\n";
        return 1;
    }

    // 2. Создание сцены Core: 3D вращающийся куб со светом и материалом + экранный элемент HUD
    core::Scene scene;

    auto cubeResult = scene.createObject(objects::Box3::Params{.size = {4.0, 4.0, 4.0}});
    if (!cubeResult) {
        std::cerr << "Failed to create cube object: " << static_cast<int>(cubeResult.error().code) << "\n";
        return 1;
    }
    const auto cubeId = *cubeResult;
    (void)scene.setMobility(cubeId, core::Mobility::Dynamic);
    (void)scene.setLocalTransform(cubeId, math::translation(math::Vector3{0.0, 0.0, 10.0}));
    (void)scene.setRenderProperties(cubeId, core::RenderSpace::World, 0, 0);
    (void)scene.setMaterial(cubeId, std::make_shared<const render::Material>(render::Color{0.2, 0.8, 0.3, 1.0}));

    auto hudResult = scene.createObject(objects::Rectangle2::Params{.size = {16.0, 8.0}});
    if (!hudResult) {
        std::cerr << "Failed to create HUD object: " << static_cast<int>(hudResult.error().code) << "\n";
        return 1;
    }
    const auto hudId = *hudResult;
    (void)scene.setRenderProperties(hudId, core::RenderSpace::Hud, 0, 0);
    (void)scene.setLocalTransform(hudId, math::translation(math::Vector2{{16.0, 16.0}}));
    (void)scene.setMaterial(hudId, std::make_shared<const render::Material>(render::Color{1.0, 0.9, 0.1, 0.8}));

    core::PassesConfig passesConfig;
    passesConfig.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 16.0,
        .height = 16.0,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    passesConfig.hudEnabled = true;
    passesConfig.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.25},
        .directional =
            {
                {
                    .direction = math::Vector3{1.0, 1.0, 1.0},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.75,
                },
            },
    };

    // 3. Создание инфраструктуры Runtime и Renderer
    runtime::CompletionChannel channel;
    actor::FakeRendererAdapter rendererAdapter{channel};
    runtime::FramePipeline pipeline;
    runtime::FixedStepUpdate update{1.0 / 60.0, 5};

    const auto initialSize = window->size();
    pipeline.resize(runtime::SurfaceSize{.width = initialSize.width, .height = initialSize.height});

    window::InputState inputState;
    renderer::SoftwareRenderer softwareRenderer;

    constexpr std::size_t DEMO_STEPS = 10;
    auto* headless = dynamic_cast<window::HeadlessWindow*>(window.get());
    std::size_t step = 0;
    std::uint64_t frameId = 0;
    double cubeAngle = 0.0;
    bool closeRequested = false;

    std::cout << "Starting Huynity Window Demo (" << (continuousMode ? "continuous mode" : "10 demo steps") << ")...\n";

    while (window->isOpen() && !closeRequested) {
        ++step;
        inputState.resetDeltas();

        // В headless-режиме генерируем тестовые события для демонстрации обработки ввода
        if (headless != nullptr) {
            if (step == 1) {
                headless->pushEvent(window::KeyPressedEvent{.key = window::KeyCode::Space});
                headless->pushEvent(window::MouseMovedEvent{.x = 32.0, .y = 32.0, .deltaX = 2.0, .deltaY = 1.0});
            } else if (step == 3) {
                headless->pushEvent(
                    window::MouseButtonPressedEvent{.button = window::MouseButton::Left, .x = 32.0, .y = 32.0});
            } else if (step == 5) {
                headless->pushEvent(window::KeyReleasedEvent{.key = window::KeyCode::Space});
                headless->pushEvent(
                    window::MouseButtonReleasedEvent{.button = window::MouseButton::Left, .x = 32.0, .y = 32.0});
            } else if (step == 7) {
                headless->resize(80, 80);
            } else if (step == DEMO_STEPS + 1 && !continuousMode) {
                headless->pushEvent(window::WindowCloseRequestedEvent{});
            }
        }

        // 1. Опрос событий окна и ввода
        std::vector<window::InputEvent> events;
        window->pollEvents(events);
        for (const auto& event : events) {
            inputState.processEvent(event);
            if (std::holds_alternative<window::WindowCloseRequestedEvent>(event)) {
                std::cout << "  [Event] WindowCloseRequestedEvent received. Closing window.\n";
                closeRequested = true;
                window->close();
                break;
            }
            if (const auto* resized = std::get_if<window::WindowResizedEvent>(&event)) {
                std::cout << "  [Event] WindowResizedEvent: " << resized->width << "x" << resized->height << "\n";
                pipeline.resize(runtime::SurfaceSize{.width = resized->width, .height = resized->height});
            }
        }

        if (closeRequested || !window->isOpen()) {
            break;
        }

        if (inputState.isKeyPressed(window::KeyCode::Escape)) {
            std::cout << "  [Input] Escape key pressed. Closing window.\n";
            window->close();
            break;
        }

        // 2. Fixed-step update: вращение 3D-куба
        constexpr double elapsed = 1.0 / 60.0;
        (void)update.advance(elapsed, [&](double dt) {
            cubeAngle += 1.5 * dt;
            const auto rotX = math::rotation(math::RotationAxis::X, cubeAngle * 0.7);
            const auto rotY = math::rotation(math::RotationAxis::Y, cubeAngle);
            const auto trans = math::translation(math::Vector3{0.0, 0.0, 10.0});
            const auto cubeTransform = math::compose(trans, math::compose(rotX, rotY));
            (void)scene.setLocalTransform(cubeId, cubeTransform);
        });

        // 3. Подготовка RenderSnapshot
        const auto currentSize = window->size();
        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, passesConfig, currentSize.width, currentSize.height);
        if (!snapshotResult) {
            std::cerr << "Failed to prepare snapshot: " << static_cast<int>(snapshotResult.error().code) << "\n";
            return 1;
        }
        auto snapshot = std::move(*snapshotResult);
        snapshot.clearColor = render::Color{0.08, 0.08, 0.12, 1.0};

        // 4. Рендеринг через SoftwareRenderer в ImageBuffer из пула FramePipeline
        auto framebuffer = pipeline.acquireFramebuffer();
        if (!framebuffer) {
            std::cerr << "Failed to acquire framebuffer\n";
            return 1;
        }
        auto renderResult = softwareRenderer.render(snapshot, *framebuffer);
        if (!renderResult) {
            std::cerr << "Failed to render snapshot: " << static_cast<int>(renderResult.error()) << "\n";
            return 1;
        }

        // 5. Отправка кадра в конвейер и презентация через window->present()
        auto frame = runtime::RenderFrame{
            .frameId = ++frameId,
            .snapshot = std::make_unique<render::RenderSnapshot>(std::move(snapshot)),
            .framebuffer = std::move(framebuffer),
        };

        if (pipeline.submit(rendererAdapter, frame) == runtime::SubmitResult::Accepted) {
            (void)rendererAdapter.completeReady();
            auto completed = pipeline.complete(channel);
            pipeline.handleCompletedFrame(std::move(completed), *window);
        }

        std::cout << "[Step " << step << "] Rendered and presented frame " << frameId << " (" << currentSize.width
                  << "x" << currentSize.height << "), space=" << inputState.isKeyPressed(window::KeyCode::Space)
                  << ", mouse=(" << inputState.mouseX() << ", " << inputState.mouseY() << ")\n";

        if (!continuousMode && step >= DEMO_STEPS && headless == nullptr) {
            window->close();
            break;
        }
    }

    std::cout << "Huynity Window Demo finished successfully (" << (step > DEMO_STEPS ? DEMO_STEPS : step)
              << " frames rendered and presented).\n";
    return 0;
}
