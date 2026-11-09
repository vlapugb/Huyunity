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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace renderer = huynity::renderer;
namespace runtime = huynity::runtime;
namespace window = huynity::adapters::window;

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

std::shared_ptr<const render::Material> makeMat(double r, double g, double b, double a = 1.0) {
    return std::make_shared<const render::Material>(render::Color{r, g, b, a});
}

core::ObjectId addBox(core::Scene& scene, const math::Vector3& size, const math::Transform3& transform,
                      const std::shared_ptr<const render::Material>& mat, bool wireframe = false) {
    const auto id = scene.createObject<objects::ObjectType::Box3>({.size = size});
    (void)scene.setMobility(*id, core::Mobility::Static);
    (void)scene.setLocalTransform(*id, transform);
    (void)scene.setMaterial(*id, mat);
    if (wireframe) {
        (void)scene.setWireframe(*id, true);
    }
    return *id;
}

core::ObjectId addSphere(core::Scene& scene, double radius, const math::Transform3& transform,
                         const std::shared_ptr<const render::Material>& mat) {
    const auto id = scene.createObject<objects::ObjectType::Sphere3>({.radius = radius});
    (void)scene.setMobility(*id, core::Mobility::Static);
    (void)scene.setLocalTransform(*id, transform);
    (void)scene.setMaterial(*id, mat);
    return *id;
}

} // namespace

int main(int argc, char* argv[]) {
    bool continuous = false;
    std::string screenshotPath;
    int recordFrames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--continuous" || arg == "-c") {
            continuous = true;
        } else if ((arg == "--screenshot" || arg == "-s") && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--record" && i + 1 < argc) {
            recordFrames = std::stoi(argv[++i]);
        }
    }

    std::cout << "Starting Huynity: Third-Person Arena ("
              << (continuous ? "continuous interactive mode" : "10 CI steps") << ")...\n";
    if (continuous) {
        std::cout << "Controls: W/S throttle, A/D steer, Mouse drag orbit camera, Space turbo boost, Esc exit.\n";
    }

    // 1. Окно
    auto window = window::createWindow(
        {.size = {800, 600}, .title = "Huynity: Third-Person Arena", .forceHeadless = !continuous});
    if (!window || !window->isOpen()) {
        std::cerr << "Failed to create window\n";
        return 1;
    }

    // 2. Сцена
    core::Scene scene;

    // Материалы арены
    auto floorMat = makeMat(0.20, 0.22, 0.26);       // асфальт/плиты арены
    auto curbMat = makeMat(0.85, 0.65, 0.15);        // бордюры
    auto rampMat = makeMat(0.32, 0.35, 0.40);        // пандус
    auto columnMat = makeMat(0.38, 0.42, 0.48);      // колонны
    auto crystalMat = makeMat(0.2, 0.85, 1.0);       // светящиеся кристаллы
    auto roverBodyMat = makeMat(0.95, 0.35, 0.05);   // оранжевый кузов ровера
    auto roverCockpitMat = makeMat(0.12, 0.15, 0.2); // тонированное стекло кабины
    auto roverWheelMat = makeMat(0.1, 0.1, 0.1);     // колеса
    auto speedBarMat = makeMat(0.1, 0.85, 0.4, 0.85);

    // 2.1. Земля / главная арена 26x26
    addBox(scene, math::Vector3{{26.0, 0.4, 26.0}}, math::translation(math::Vector3{{0.0, -0.2, 0.0}}), floorMat);

    // Бордюры по краям арены
    addBox(scene, math::Vector3{{26.0, 0.6, 0.4}}, math::translation(math::Vector3{{0.0, 0.1, 13.0}}), curbMat);
    addBox(scene, math::Vector3{{26.0, 0.6, 0.4}}, math::translation(math::Vector3{{0.0, 0.1, -13.0}}), curbMat);
    addBox(scene, math::Vector3{{0.4, 0.6, 26.0}}, math::translation(math::Vector3{{13.0, 0.1, 0.0}}), curbMat);
    addBox(scene, math::Vector3{{0.4, 0.6, 26.0}}, math::translation(math::Vector3{{-13.0, 0.1, 0.0}}), curbMat);

    // 2.2. Пандус / рампа для заезда на верхний ярус
    const auto rampRot = math::rotation(math::RotationAxis::X, -0.22);
    const auto rampTrans = math::translation(math::Vector3{{-6.0, 0.8, -4.0}});
    addBox(scene, math::Vector3{{4.0, 0.3, 8.0}}, math::compose(rampTrans, rampRot), rampMat);

    // Верхняя платформа
    addBox(scene, math::Vector3{{8.0, 0.4, 8.0}}, math::translation(math::Vector3{{-6.0, 1.7, -10.0}}), rampMat);

    // Колонны на арене
    const std::array<math::Vector3, 4> columnPositions{
        math::Vector3{{5.0, 2.0, 5.0}},
        math::Vector3{{5.0, 2.0, -5.0}},
        math::Vector3{{-2.0, 2.0, 7.0}},
        math::Vector3{{-8.0, 2.0, 5.0}},
    };
    for (const auto& pos : columnPositions) {
        addBox(scene, math::Vector3{{0.9, 4.0, 0.9}}, math::translation(pos), columnMat);
        // Кристалл наверху колонны
        addSphere(scene, 0.45, math::translation(math::Vector3{{pos[0], pos[1] + 2.3, pos[2]}}), crystalMat);
    }

    // 2.3. Персонаж / Ровер (управляемый объект от третьего лица)
    // Корпус ровера (динамический)
    const auto roverIdResult = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{1.4, 0.5, 2.2}}});
    if (!roverIdResult) {
        return 1;
    }
    const auto rover = *roverIdResult;
    (void)scene.setMobility(rover, core::Mobility::Dynamic);
    (void)scene.setMaterial(rover, roverBodyMat);

    // Кабина ровера
    const auto cockpitResult = scene.createObject<objects::ObjectType::Box3>({.size = math::Vector3{{0.9, 0.4, 1.1}}});
    if (cockpitResult) {
        (void)scene.setMobility(*cockpitResult, core::Mobility::Dynamic);
        (void)scene.setMaterial(*cockpitResult, roverCockpitMat);
    }
    const auto cockpit = cockpitResult ? *cockpitResult : core::ObjectId{};

    // 2.4. HUD спидометр / индикатор скорости
    const auto speedBgResult =
        scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{160.0, 14.0}}});
    if (speedBgResult) {
        (void)scene.setRenderProperties(*speedBgResult, core::RenderSpace::Hud, 0, 1);
        (void)scene.setLocalTransform(*speedBgResult, math::translation(math::Vector2{{110.0, 570.0}}));
        (void)scene.setMaterial(*speedBgResult, makeMat(0.08, 0.10, 0.12, 0.85));
    }

    const auto speedBarResult =
        scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{156.0, 10.0}}});
    if (speedBarResult) {
        (void)scene.setRenderProperties(*speedBarResult, core::RenderSpace::Hud, 0, 2);
        (void)scene.setLocalTransform(*speedBarResult, math::translation(math::Vector2{{110.0, 570.0}}));
        (void)scene.setMaterial(*speedBarResult, speedBarMat);
    }

    // 3. Освещение арены
    render::Lighting3 lighting{
        .ambient = {.color = {0.75, 0.82, 0.95, 1.0}, .intensity = 0.35},
        .directional =
            {
                {
                    .direction = math::Vector3{{0.6, -1.0, 0.5}},
                    .color = {1.0, 0.96, 0.88, 1.0},
                    .intensity = 0.85,
                },
                {
                    .direction = math::Vector3{{-0.8, -0.4, -0.6}},
                    .color = {0.4, 0.6, 0.9, 1.0},
                    .intensity = 0.20,
                },
            },
    };

    // 4. Физика ровера и плавной следящей камеры (Third-Person Chase Camera)
    double roverX = 0.0;
    double roverZ = 0.0;
    double roverYaw = 0.0;
    double currentSpeed = 0.0;

    // Плавная следящая камера (Spring arm camera)
    double camOrbitYaw = 0.0;
    double camOrbitPitch = -0.38; // наклон камеры вниз на ровер
    double camDistance = 8.5;

    // Плавные координаты камеры (chase camera)
    double smoothCamX = 0.0;
    double smoothCamY = 4.0;
    double smoothCamZ = -8.5;

    runtime::FramePipeline pipeline;
    pipeline.resize({800, 600});
    runtime::FixedStepUpdate fixedUpdate(1.0 / 60.0, 5);
    renderer::SoftwareRenderer softwareRenderer;
    window::InputState inputState;

    std::vector<window::InputEvent> events;
    int stepCount = 0;
    constexpr int maxCiSteps = 10;

    while (window->isOpen()) {
        events.clear();
        window->pollEvents(events);
        for (const auto& ev : events) {
            inputState.processEvent(ev);
            if (std::holds_alternative<window::WindowCloseRequestedEvent>(ev)) {
                window->close();
                break;
            }
            if (const auto* resized = std::get_if<window::WindowResizedEvent>(&ev)) {
                pipeline.resize({resized->width, resized->height});
            }
        }

        if (!window->isOpen()) {
            break;
        }

        if (inputState.isKeyPressed(window::KeyCode::Escape)) {
            window->close();
            break;
        }

        // Вращение камеры вокруг ровера мышкой при зажатой левой кнопке
        if (inputState.isMouseButtonPressed(window::MouseButton::Left)) {
            constexpr double sensitivity = 0.006;
            camOrbitYaw += inputState.mouseDeltaX() * sensitivity;
            camOrbitPitch = std::clamp(camOrbitPitch - inputState.mouseDeltaY() * sensitivity, -1.1, -0.1);
        }

        fixedUpdate.advance(1.0 / 60.0, [&](double dt) {
            ++stepCount;

            const double maxDriveSpeed = inputState.isKeyPressed(window::KeyCode::Space) ? 9.5 : 5.5;

            // Управление ровером (A/D руль, W/S газ)
            double steer = 0.0;
            if (inputState.isKeyPressed(window::KeyCode::A) || inputState.isKeyPressed(window::KeyCode::Left)) {
                steer -= 1.0;
            }
            if (inputState.isKeyPressed(window::KeyCode::D) || inputState.isKeyPressed(window::KeyCode::Right)) {
                steer += 1.0;
            }

            double throttle = 0.0;
            if (inputState.isKeyPressed(window::KeyCode::W) || inputState.isKeyPressed(window::KeyCode::Up)) {
                throttle += 1.0;
            }
            if (inputState.isKeyPressed(window::KeyCode::S) || inputState.isKeyPressed(window::KeyCode::Down)) {
                throttle -= 1.0;
            }

            // Авто-движение в CI-режиме, если клавиши не зажаты
            if (std::abs(throttle) < 0.01 && continuous) {
                throttle = 0.6;
                steer = 0.3; // легкий поворот по кругу
            }

            // Угловая скорость зависит от движения
            roverYaw += steer * 2.2 * dt;

            // Ускорение и торможение (инерция)
            const double targetSpeed = throttle * maxDriveSpeed;
            currentSpeed += (targetSpeed - currentSpeed) * std::clamp(dt * 4.0, 0.0, 1.0);

            // Вектор движения ровера вперед
            const double forwardX = std::sin(roverYaw);
            const double forwardZ = std::cos(roverYaw);

            roverX = std::clamp(roverX + forwardX * currentSpeed * dt, -11.5, 11.5);
            roverZ = std::clamp(roverZ + forwardZ * currentSpeed * dt, -11.5, 11.5);

            // Обновление положения ровера
            const auto roverRot = math::rotation(math::RotationAxis::Y, roverYaw);
            const auto roverTrans = math::translation(math::Vector3{{roverX, 0.35, roverZ}});
            (void)scene.setLocalTransform(rover, math::compose(roverTrans, roverRot));

            if (cockpit.generation > 0) {
                const auto cockpitLocal = math::translation(math::Vector3{{0.0, 0.45, -0.2}});
                (void)scene.setLocalTransform(cockpit,
                                              math::compose(roverTrans, math::compose(roverRot, cockpitLocal)));
            }
        });

        // 5. Расчёт камеры от третьего лица (Third-Person Chase Camera)
        // Камера следует за ровером сзади и сверху:
        const double totalCamAngle = roverYaw + camOrbitYaw;
        const double targetCamX = roverX - std::sin(totalCamAngle) * camDistance;
        const double targetCamZ = roverZ - std::cos(totalCamAngle) * camDistance;
        const double targetCamY = 0.35 + 3.2;

        // Плавная интерполяция позиции камеры (Spring arm lerp) для кинематографичного движения
        constexpr double cameraFollowSharpness = 0.18;
        smoothCamX += (targetCamX - smoothCamX) * cameraFollowSharpness;
        smoothCamY += (targetCamY - smoothCamY) * cameraFollowSharpness;
        smoothCamZ += (targetCamZ - smoothCamZ) * cameraFollowSharpness;

        // Матрица перехода World -> View
        const auto camTrans = math::translation(math::Vector3{{-smoothCamX, -smoothCamY, -smoothCamZ}});
        const auto camRotY = math::rotation(math::RotationAxis::Y, -totalCamAngle);
        const auto camRotX = math::rotation(math::RotationAxis::X, -camOrbitPitch);
        const auto worldToView = math::compose(camRotX, math::compose(camRotY, camTrans));

        render::PerspectiveCamera3 camera3{
            .worldToView = worldToView,
            .verticalFieldOfView = 1.10, // ~63 градуса
            .nearPlane = 0.1,
            .farPlane = 80.0,
        };

        const auto winSize = window->size();
        core::PassesConfig passesConfig;
        passesConfig.camera3 = camera3;
        passesConfig.lighting = lighting;
        passesConfig.hudEnabled = true;

        auto snapshotResult =
            core::RenderPreparation::prepareSnapshot(scene, passesConfig, winSize.width, winSize.height);
        if (!snapshotResult) {
            std::cerr << "Prepare snapshot failed\n";
            return 1;
        }

        auto snapshot = std::move(*snapshotResult);
        snapshot.clearColor = render::Color{0.15, 0.18, 0.24, 1.0}; // атласное небо

        // Включаем Gouraud shading для кристаллов
        for (auto& pass : snapshot.passes) {
            if (auto* world3 = std::get_if<render::World3DPass>(&pass)) {
                for (auto& item : world3->drawItems) {
                    if (item.material < snapshot.materials.size()) {
                        const auto& mat = *snapshot.materials[item.material];
                        if (mat.color.blue > 0.8 && mat.color.green > 0.7) {
                            item.shading = render::ShadingModel::Gouraud;
                        }
                    }
                }
            }
        }

        auto framebuffer = pipeline.acquireFramebuffer();
        if (!framebuffer) {
            continue;
        }

        auto renderRes = softwareRenderer.render(snapshot, *framebuffer);
        if (!renderRes) {
            std::cerr << "Render failed\n";
            return 1;
        }

        framebuffer = window->present(std::move(framebuffer));
        pipeline.releaseFromPresentation(std::move(framebuffer));

        if (recordFrames > 0) {
            if (const auto* headless = dynamic_cast<const window::HeadlessWindow*>(window.get())) {
                if (const auto* frame = headless->lastPresentedFrame()) {
                    savePpm(*frame, "/tmp/arena_f_" + std::to_string(stepCount) + ".ppm");
                }
            }
            if (stepCount >= recordFrames) {
                window->close();
                break;
            }
        }

        inputState.resetDeltas();

        if (recordFrames == 0 && !continuous && stepCount >= maxCiSteps) {
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

    std::cout << "Finished Third-Person Arena successfully.\n";
    return 0;
}
