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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
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
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--continuous" || arg == "-c") {
            continuousMode = true;
        } else if ((arg == "--screenshot" || arg == "-s") && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
    }

    // 1. Создание окна через фабрику оконного адаптера
    auto window = window::createWindow({.size = {960, 640}, .title = "Huynity Window Demo"});
    if (!window || !window->isOpen()) {
        std::cerr << "Failed to create window adapter\n";
        return 1;
    }

    // 2. Кинетическая скульптура в выставочном пространстве.
    core::Scene scene;

    auto rotorResult = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{0.55, 0.55, 0.7}}});
    if (!rotorResult) {
        std::cerr << "Failed to create kinetic rotor: " << static_cast<int>(rotorResult.error().code) << "\n";
        return 1;
    }
    const auto rotorId = *rotorResult;
    (void)scene.setMobility(rotorId, core::Mobility::Dynamic);
    (void)scene.setLocalTransform(rotorId, math::translation(math::Vector3{{0.0, 0.0, 10.0}}));
    (void)scene.setRenderProperties(rotorId, core::RenderSpace::World, 0, 0);
    (void)scene.setMaterial(rotorId, std::make_shared<const render::Material>(render::Color{0.76, 0.49, 0.22, 1.0}));

    // Кинетический экспонат: лопасти из цельных треугольных сеток на общей оси.
    for (std::uint32_t blade = 0; blade < 9; ++blade) {
        auto created = scene.createObject<objects::ObjectType::TriangleMesh3>({
            .positions = {{{0.35, -0.12, 0}}, {{2.8, 0.15, 0.14}}, {{2.35, 0.9, -0.10}}, {{0.65, 0.4, 0}}},
            .triangles = {{{0, 2, 1}}, {{0, 3, 2}}},
        });
        if (!created) {
            return 1;
        }
        (void)scene.setParent(*created, rotorId);
        (void)scene.setLocalTransform(*created,
                                      math::rotation(math::RotationAxis::Z, blade * 2 * std::numbers::pi / 9));
        (void)scene.setBackfaceCulling(*created, false);
        (void)scene.setMaterial(*created, std::make_shared<const render::Material>(
                                              render::Color{0.35 + blade * 0.035, 0.48 + blade * 0.025, 0.46, 1}));
    }
    const auto addBox = [&](math::Vector3 size, math::Vector3 position, render::Color color) {
        auto created = scene.createObject<objects::ObjectType::Box3>({.size = size});
        if (!created) {
            throw std::runtime_error("Cannot create display stand");
        }
        (void)scene.setLocalTransform(*created, math::translation(position));
        (void)scene.setMaterial(*created, std::make_shared<const render::Material>(color));
    };
    addBox({{3.8, 0.28, 2.8}}, {{0, -3.35, 10}}, {0.58, 0.53, 0.43, 1});
    addBox({{0.24, 3.0, 0.24}}, {{0, -1.75, 10.5}}, {0.22, 0.27, 0.27, 1});
    addBox({{30, 0.18, 24}}, {{0, -3.6, 10}}, {0.32, 0.34, 0.32, 1});
    addBox({{24, 15, 0.3}}, {{0, 2, 16}}, {0.65, 0.61, 0.53, 1});
    for (int panel = -4; panel <= 4; ++panel) {
        addBox({{0.06, 12, 0.08}}, {{panel * 2.0, 2, 15.8}}, {0.42, 0.40, 0.35, 1});
    }

    core::PassesConfig passesConfig;
    passesConfig.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 16.0,
        .height = 10.67,
        .nearPlane = 1.0,
        .farPlane = 50.0,
    };
    passesConfig.hudEnabled = false;
    passesConfig.lighting = {
        .ambient = {render::Color{1.0, 1.0, 1.0, 1.0}, 0.65},
        .directional =
            {
                {
                    .direction = math::Vector3{{-0.5, -1.0, 0.8}},
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
    double rotorAngle = 0.0;
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
                headless->resize(960, 640);
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

        // 2. Fixed-step update: медленное вращение лопастей.
        constexpr double elapsed = 1.0 / 60.0;
        (void)update.advance(elapsed, [&](double dt) {
            rotorAngle += 1.5 * dt;
            const auto trans = math::translation(math::Vector3{{0.0, 0.0, 10.0}});
            const auto rotorTransform = math::compose(trans, math::rotation(math::RotationAxis::Z, rotorAngle * 0.2));
            (void)scene.setLocalTransform(rotorId, rotorTransform);
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

        if (!screenshotPath.empty() && step == DEMO_STEPS) {
            std::ofstream output(screenshotPath, std::ios::binary);
            output << "P6\n" << framebuffer->width() << ' ' << framebuffer->height() << "\n255\n";
            for (std::size_t y = 0; y < framebuffer->height(); ++y) {
                for (std::size_t x = 0; x < framebuffer->width(); ++x) {
                    output.write(reinterpret_cast<const char*>(framebuffer->row(y).data() + x * 4), 3);
                }
            }
            if (!output) {
                throw std::runtime_error("Cannot write window demo screenshot");
            }
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
