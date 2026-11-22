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
#include "huynity/objectlib/shapes/sphere3.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/software_renderer.h"
#include "huynity/runtime/fixed_step_update.h"
#include "huynity/runtime/frame_pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
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

namespace {

/**
 * @brief Создаёт процедурную RGBA-текстуру 2D-спрайта с полностью прозрачными углами.
 *
 * @param width ширина текстуры в пикселях.
 * @param height высота текстуры в пикселях.
 * @return разделяемый указатель на неизменяемый ImageBuffer с альфа-каналом.
 */
[[nodiscard]] std::shared_ptr<const huynity::image::ImageBuffer> createAlphaPatternTexture(std::size_t width,
                                                                                           std::size_t height) {
    auto bufferResult = huynity::image::ImageBuffer::create({.width = width, .height = height});
    if (!bufferResult) {
        throw std::runtime_error("Failed to create texture ImageBuffer");
    }
    auto buffer = std::move(*bufferResult);

    // Заполняем узор: цветная фигура с полностью прозрачными углами (alpha = 0)
    for (std::size_t y = 0; y < height; ++y) {
        auto row = buffer.row(y);
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t offset = x * 4;
            const bool isCorner = (x < width / 4 || x >= width * 3 / 4) && (y < height / 4 || y >= height * 3 / 4);
            if (isCorner) {
                row[offset + 0] = std::byte{0};
                row[offset + 1] = std::byte{0};
                row[offset + 2] = std::byte{0};
                row[offset + 3] = std::byte{0}; // Полностью прозрачные углы
            } else {
                const bool isBorder =
                    (x == width / 4 || x == width * 3 / 4 - 1 || y == height / 4 || y == height * 3 / 4 - 1);
                if (isBorder) {
                    row[offset + 0] = std::byte{255};
                    row[offset + 1] = std::byte{255};
                    row[offset + 2] = std::byte{255};
                    row[offset + 3] = std::byte{255}; // Белая рамка
                } else {
                    row[offset + 0] = std::byte{240};
                    row[offset + 1] = std::byte{180};
                    row[offset + 2] = std::byte{40};
                    row[offset + 3] = std::byte{220}; // Полупрозрачный золотистый узор
                }
            }
        }
    }

    return std::make_shared<const huynity::image::ImageBuffer>(std::move(buffer));
}

void savePpm(const huynity::image::ImageBuffer& buffer, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    file << "P6\n" << buffer.width() << " " << buffer.height() << "\n255\n";
    for (std::size_t y = 0; y < buffer.height(); ++y) {
        const auto row = buffer.row(y);
        for (std::size_t x = 0; x < buffer.width(); ++x) {
            file.put(static_cast<char>(row[x * 4 + 0]));
            file.put(static_cast<char>(row[x * 4 + 1]));
            file.put(static_cast<char>(row[x * 4 + 2]));
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    bool continuous = false;
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--continuous" || arg == "-c") {
            continuous = true;
        } else if ((arg == "--screenshot" || arg == "-s") && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
    }

    std::cout << "Starting Huynity: Renderer Gallery (" << (continuous ? "continuous mode" : "10 CI steps") << ")...\n";

    // 1. Создание окна через фабрику оконного адаптера
    auto window = window::createWindow({.size = {800, 600}, .title = "Huynity: Renderer Gallery"});
    if (!window || !window->isOpen()) {
        std::cerr << "Failed to create window adapter\n";
        return 1;
    }

    // 2. Создание сцены Core: каркасный куб (wireframe), сфера (Gouraud) и спрайт с альфа-текстурой
    core::Scene scene;

    // Каркасный объект (wireframe = true): 3D куб Box3
    auto cubeResult = scene.createObject(objects::Box3::Params{.size = {2.0, 2.0, 2.0}});
    if (!cubeResult) {
        std::cerr << "Failed to create cube object\n";
        return 1;
    }
    const auto cube = *cubeResult;
    (void)scene.setMobility(cube, core::Mobility::Dynamic);
    (void)scene.setWireframe(cube, true);
    (void)scene.setLocalTransform(cube, math::translation(math::Vector3{-2.5, 0.0, 10.0}));
    (void)scene.setMaterial(cube, std::make_shared<const render::Material>(render::Material{
                                      .color = render::Color{0.1, 0.5, 1.0, 1.0},
                                  }));

    // Сглаженный объект с затенением Гуро: 3D сфера Sphere3
    auto sphereResult = scene.createObject(objects::Sphere3::Params{.radius = 1.2});
    if (!sphereResult) {
        std::cerr << "Failed to create sphere object\n";
        return 1;
    }
    const auto sphere = *sphereResult;
    (void)scene.setMobility(sphere, core::Mobility::Dynamic);
    (void)scene.setLocalTransform(sphere, math::translation(math::Vector3{2.5, 0.0, 10.0}));
    (void)scene.setMaterial(sphere, std::make_shared<const render::Material>(render::Material{
                                        .color = render::Color{0.9, 0.25, 0.2, 1.0},
                                    }));

    // Текстурированный 2D-спрайт: прямоугольник Rectangle2 с альфа-прозрачной текстурой
    auto spriteResult = scene.createObject(objects::Rectangle2::Params{.size = {80.0, 80.0}});
    if (!spriteResult) {
        std::cerr << "Failed to create sprite object\n";
        return 1;
    }
    const auto sprite = *spriteResult;
    (void)scene.setRenderProperties(sprite, core::RenderSpace::Hud, 0, 0);
    (void)scene.setLocalTransform(sprite, math::translation(math::Vector2{{60.0, 60.0}}));
    auto spriteTexture = createAlphaPatternTexture(32, 32);
    (void)scene.setMaterial(sprite, std::make_shared<const render::Material>(render::Material{
                                        .color = render::Color{1.0, 1.0, 1.0, 1.0},
                                        .texture = std::move(spriteTexture),
                                        .filter = render::TextureFilter::Nearest,
                                        .wrap = render::TextureWrap::Clamp,
                                    }));

    // 3. Конфигурация проходов рендеринга: камера, освещение и HUD
    core::PassesConfig passesConfig;
    passesConfig.camera3 = render::OrthographicCamera3{
        .worldToView = math::Transform3{},
        .width = 10.0,
        .height = 7.5,
        .nearPlane = 0.1,
        .farPlane = 50.0,
    };
    passesConfig.hudEnabled = true;
    passesConfig.lighting = render::Lighting3{
        .ambient =
            {
                .color = render::Color{1.0, 1.0, 1.0, 1.0},
                .intensity = 0.25,
            },
        .directional =
            {
                {
                    .direction = math::Vector3{1.0, 1.0, 1.0},
                    .color = render::Color{1.0, 1.0, 1.0, 1.0},
                    .intensity = 0.75,
                },
            },
    };

    // 4. Инфраструктура конвейера кадра и программного рендерера
    runtime::FramePipeline pipeline;
    const auto initialSize = window->size();
    pipeline.resize(runtime::SurfaceSize{.width = initialSize.width, .height = initialSize.height});

    runtime::FixedStepUpdate fixedUpdate{1.0 / 60.0, 5};
    renderer::SoftwareRenderer softwareRenderer;
    window::InputState inputState;

    static constexpr std::array<math::Point2, 4> spriteUvs{
        math::Point2{{0.0, 0.0}},
        math::Point2{{1.0, 0.0}},
        math::Point2{{1.0, 1.0}},
        math::Point2{{0.0, 1.0}},
    };

    double angle = 0.0;
    std::size_t step = 0;
    constexpr std::size_t maxSteps = 10;
    bool closeRequested = false;

    // 5. Главный цикл кадра
    while (window->isOpen() && !closeRequested) {
        ++step;
        inputState.resetDeltas();

        // Опрос событий окна и ввода
        std::vector<window::InputEvent> events;
        window->pollEvents(events);
        for (const auto& event : events) {
            inputState.processEvent(event);
            if (std::holds_alternative<window::WindowCloseRequestedEvent>(event)) {
                closeRequested = true;
                window->close();
                break;
            }
            if (const auto* resized = std::get_if<window::WindowResizedEvent>(&event)) {
                pipeline.resize(runtime::SurfaceSize{.width = resized->width, .height = resized->height});
            }
        }

        if (closeRequested || !window->isOpen()) {
            break;
        }

        if (inputState.isKeyPressed(window::KeyCode::Escape)) {
            window->close();
            break;
        }

        // Fixed-step update: вращение 3D-куба и 3D-сферы
        const double speed = inputState.isKeyPressed(window::KeyCode::Space) ? 3.0 : 1.0;
        constexpr double elapsed = 1.0 / 60.0;
        (void)fixedUpdate.advance(elapsed, [&](double dt) {
            angle += dt * speed;

            const auto cubeRot = math::compose(math::rotation(math::RotationAxis::X, angle * 0.7),
                                               math::rotation(math::RotationAxis::Y, angle));
            (void)scene.setLocalTransform(cube,
                                          math::compose(math::translation(math::Vector3{-2.5, 0.0, 10.0}), cubeRot));

            const auto sphereRot = math::compose(math::rotation(math::RotationAxis::Y, angle * 0.8),
                                                 math::rotation(math::RotationAxis::Z, angle * 0.5));
            (void)scene.setLocalTransform(sphere,
                                          math::compose(math::translation(math::Vector3{2.5, 0.0, 10.0}), sphereRot));
        });

        // Подготовка RenderSnapshot
        const auto currentSize = window->size();
        if (currentSize.width == 0 || currentSize.height == 0) {
            continue;
        }

        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, passesConfig, currentSize.width, currentSize.height);
        if (!snapshotResult) {
            std::cerr << "Failed to prepare snapshot: " << static_cast<int>(snapshotResult.error().code) << "\n";
            return 1;
        }
        auto snapshot = std::move(*snapshotResult);
        snapshot.clearColor = render::Color{0.08, 0.08, 0.12, 1.0};

        // 1) Для сферы в 3D-проходе явно выставляем ShadingModel::Gouraud
        for (auto& pass : snapshot.passes) {
            if (auto* world3 = std::get_if<render::World3DPass>(&pass)) {
                for (auto& item : world3->drawItems) {
                    if (!item.wireframe) {
                        item.shading = render::ShadingModel::Gouraud;
                    }
                }
            }
        }

        // 2) Для 2D-спрайта в snapshot.resources2 задаём UV-координаты для четырёх вершин прямоугольника
        for (auto& resource : snapshot.resources2) {
            if (resource.positions.size() == 4) {
                resource.texCoords = spriteUvs;
            }
        }

        // Рендеринг кадра через SoftwareRenderer в ImageBuffer из пула конвейера
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

        // Презентация готового кадра окну и возврат буфера в пул
        framebuffer = window->present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));

        if (!continuous && step >= maxSteps) {
            window->close();
            break;
        }
    }

    if (!screenshotPath.empty()) {
        if (const auto* headless = dynamic_cast<const window::HeadlessWindow*>(window.get())) {
            if (const auto* frame = headless->lastPresentedFrame()) {
                savePpm(*frame, screenshotPath);
                std::cout << "Saved screenshot to " << screenshotPath << "\n";
            }
        }
    }

    std::cout << "Finished Renderer Gallery successfully (" << (step > maxSteps ? maxSteps : step)
              << " frames rendered and presented).\n";
    return 0;
}
