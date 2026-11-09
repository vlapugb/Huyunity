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
                      const std::shared_ptr<const render::Material>& mat) {
    const auto id = scene.createObject<objects::ObjectType::Box3>({.size = size});
    (void)scene.setMobility(*id, core::Mobility::Static);
    (void)scene.setLocalTransform(*id, transform);
    (void)scene.setMaterial(*id, mat);
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

    std::cout << "Starting Huynity: First-Person Corridor ("
              << (continuous ? "continuous interactive mode" : "10 CI steps") << ")...\n";
    if (continuous) {
        std::cout << "Controls: W/S move forward/backward, A/D strafe, Mouse drag to look around, Space sprint, Esc "
                     "exit.\n";
    }

    // 1. Окно
    auto window = window::createWindow(
        {.size = {800, 600}, .title = "Huynity: First-Person Corridor", .forceHeadless = !continuous});
    if (!window || !window->isOpen()) {
        std::cerr << "Failed to create window\n";
        return 1;
    }

    // 2. Сцена
    core::Scene scene;

    // Материалы стен, пола, потолка и светильников
    auto floorMat = makeMat(0.22, 0.24, 0.28);     // темный каменный пол
    auto ceilingMat = makeMat(0.18, 0.20, 0.24);   // перекрытие потолка
    auto outerWallMat = makeMat(0.35, 0.38, 0.42); // внешняя стена
    auto innerWallMat = makeMat(0.28, 0.31, 0.36); // внутренняя стена
    auto archMat = makeMat(0.50, 0.42, 0.30);      // бронзовые пилоны/арки
    auto lanternMat = makeMat(1.0, 0.85, 0.30);    // светящиеся фонари на стенах

    // 3. Генерация кольцевого 3D-коридора
    // Радиус центральной траектории коридора R = 12.0
    constexpr double corridorRadius = 12.0;
    constexpr double corridorWidth = 3.6;
    constexpr double corridorHeight = 3.2;
    constexpr double halfWidth = corridorWidth * 0.5;
    constexpr int segmentCount = 32;

    const double deltaAngle = (2.0 * std::numbers::pi) / static_cast<double>(segmentCount);
    const double segChordLength = corridorRadius * deltaAngle * 1.05;

    for (int i = 0; i < segmentCount; ++i) {
        const double angle = static_cast<double>(i) * deltaAngle;
        const double midAngle = angle + deltaAngle * 0.5;

        // Позиция центра сегмента
        const double midX = std::cos(midAngle) * corridorRadius;
        const double midZ = std::sin(midAngle) * corridorRadius;

        // Поворот сегмента по касательной к кольцу
        const auto segmentRot = math::rotation(math::RotationAxis::Y, -midAngle + std::numbers::pi * 0.5);
        const auto segmentTrans = math::translation(math::Vector3{{midX, 0.0, midZ}});
        const auto segmentBase = math::compose(segmentTrans, segmentRot);

        // 3.1. Пол
        const auto floorOffset = math::translation(math::Vector3{{0.0, -corridorHeight * 0.5, 0.0}});
        addBox(scene, math::Vector3{{corridorWidth + 0.4, 0.3, segChordLength}},
               math::compose(segmentBase, floorOffset), floorMat);

        // 3.2. Потолок
        const auto ceilOffset = math::translation(math::Vector3{{0.0, corridorHeight * 0.5, 0.0}});
        addBox(scene, math::Vector3{{corridorWidth + 0.4, 0.3, segChordLength}}, math::compose(segmentBase, ceilOffset),
               ceilingMat);

        // 3.3. Внутренняя стена (ближе к центру)
        const auto innerOffset = math::translation(math::Vector3{{-halfWidth, 0.0, 0.0}});
        addBox(scene, math::Vector3{{0.35, corridorHeight, segChordLength}}, math::compose(segmentBase, innerOffset),
               innerWallMat);

        // 3.4. Внешняя стена (дальше от центра)
        const auto outerOffset = math::translation(math::Vector3{{halfWidth, 0.0, 0.0}});
        addBox(scene, math::Vector3{{0.35, corridorHeight, segChordLength}}, math::compose(segmentBase, outerOffset),
               outerWallMat);

        // 3.5. Опорные арки на стыках сегментов
        if (i % 2 == 0) {
            // Левый пилон
            addBox(scene, math::Vector3{{0.45, corridorHeight, 0.45}},
                   math::compose(segmentBase, math::translation(math::Vector3{{-halfWidth + 0.15, 0.0, 0.0}})),
                   archMat);
            // Правый пилон
            addBox(scene, math::Vector3{{0.45, corridorHeight, 0.45}},
                   math::compose(segmentBase, math::translation(math::Vector3{{halfWidth - 0.15, 0.0, 0.0}})), archMat);
            // Верхняя балка арки
            addBox(
                scene, math::Vector3{{corridorWidth, 0.35, 0.45}},
                math::compose(segmentBase, math::translation(math::Vector3{{0.0, corridorHeight * 0.5 - 0.15, 0.0}})),
                archMat);

            // Фонари на стенах (Gouraud сферы)
            addSphere(scene, 0.22,
                      math::compose(segmentBase, math::translation(math::Vector3{{-halfWidth + 0.28, 0.4, 0.0}})),
                      lanternMat);
            addSphere(scene, 0.22,
                      math::compose(segmentBase, math::translation(math::Vector3{{halfWidth - 0.28, 0.4, 0.0}})),
                      lanternMat);
        }
    }

    // 4. Прицел HUD в центре экрана
    // Вертикальная и горизонтальная риски перекрестия
    auto crosshairMat = makeMat(0.0, 0.95, 1.0, 0.8);
    const auto chH = scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{14.0, 2.0}}});
    const auto chV = scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{2.0, 14.0}}});
    if (chH && chV) {
        (void)scene.setRenderProperties(*chH, core::RenderSpace::Hud, 0, 10);
        (void)scene.setLocalTransform(*chH, math::translation(math::Vector2{{400.0, 300.0}}));
        (void)scene.setMaterial(*chH, crosshairMat);

        (void)scene.setRenderProperties(*chV, core::RenderSpace::Hud, 0, 10);
        (void)scene.setLocalTransform(*chV, math::translation(math::Vector2{{400.0, 300.0}}));
        (void)scene.setMaterial(*chV, crosshairMat);
    }

    // 5. Освещение коридора: тёплый направленный свет вдоль тоннеля + рассеянный сумрак
    render::Lighting3 lighting{
        .ambient = {.color = {0.70, 0.75, 0.85, 1.0}, .intensity = 0.30},
        .directional =
            {
                {
                    .direction = math::Vector3{{0.5, -0.7, 0.8}},
                    .color = {1.0, 0.92, 0.75, 1.0},
                    .intensity = 0.80,
                },
                {
                    .direction = math::Vector3{{-0.5, 0.3, -0.6}},
                    .color = {0.35, 0.55, 0.95, 1.0},
                    .intensity = 0.25,
                },
            },
    };

    // 6. Параметры игрока от первого лица
    double playerAngle = 0.0;   // положение по окружности
    double playerRadius = 12.0; // смещение от центра (радиальное стрейф-положение)
    double lookYaw = 0.0;       // поворот головы влево/вправо
    double lookPitch = 0.0;     // наклон головы вверх/вниз
    double headBobTimer = 0.0;

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

        // Обзор мышкой от первого лица: при зажатой левой кнопке или свободном перемещении
        if (inputState.isMouseButtonPressed(window::MouseButton::Left)) {
            constexpr double mouseSensitivity = 0.005;
            lookYaw += inputState.mouseDeltaX() * mouseSensitivity;
            lookPitch = std::clamp(lookPitch - inputState.mouseDeltaY() * mouseSensitivity, -1.1, 1.1);
        }

        fixedUpdate.advance(1.0 / 60.0, [&](double dt) {
            ++stepCount;

            const double runSpeed = inputState.isKeyPressed(window::KeyCode::Space) ? 2.2 : 1.0;

            // Движение вперед/назад по кольцу
            double moveForward = 0.0;
            if (inputState.isKeyPressed(window::KeyCode::W) || inputState.isKeyPressed(window::KeyCode::Up)) {
                moveForward += 1.0;
            }
            if (inputState.isKeyPressed(window::KeyCode::S) || inputState.isKeyPressed(window::KeyCode::Down)) {
                moveForward -= 1.0;
            }

            // Автоматическое плавное скольжение по коридору вперед, если клавиши движения не зажаты
            if (std::abs(moveForward) < 0.01 && continuous) {
                moveForward = 0.5;
            }

            // Стрейф влево/вправо между внутренними и внешними стенами
            double strafe = 0.0;
            if (inputState.isKeyPressed(window::KeyCode::A) || inputState.isKeyPressed(window::KeyCode::Left)) {
                strafe -= 1.0;
            }
            if (inputState.isKeyPressed(window::KeyCode::D) || inputState.isKeyPressed(window::KeyCode::Right)) {
                strafe += 1.0;
            }

            // Продвижение по углу кольца (длина дуги dl = R * dAngle -> dAngle = dl / R)
            constexpr double linearSpeed = 4.2;
            const double arcDistance = moveForward * linearSpeed * runSpeed * dt;
            playerAngle += arcDistance / playerRadius;

            // Стрейф по радиусу коридора с ограничением расстояния до стен
            playerRadius = std::clamp(playerRadius + strafe * 2.8 * dt, 10.8, 13.2);

            if (std::abs(moveForward) > 0.05 || std::abs(strafe) > 0.05) {
                headBobTimer += dt * 9.0 * runSpeed;
            }
        });

        // 7. Расчёт камеры от первого лица (First-Person Perspective Camera)
        // Координаты глаз игрока в мировом пространстве:
        const double eyeX = std::cos(playerAngle) * playerRadius;
        const double eyeZ = std::sin(playerAngle) * playerRadius;
        const double eyeY = -0.1 + std::sin(headBobTimer) * 0.04; // легкое покачивание головы при ходьбе

        // Базовый азимут направления коридора: вектор касательной к окружности (+pi/2)
        const double corridorForwardYaw = -playerAngle + std::numbers::pi * 0.5;
        const double totalYaw = corridorForwardYaw + lookYaw;

        // Матрица перехода World -> View:
        // Сначала перенос в точку глаз игрока (-eyeX, -eyeY, -eyeZ),
        // затем вращение по Yaw (горизонт) и Pitch (наклон головы вверх/вниз)
        const auto eyeTrans = math::translation(math::Vector3{{-eyeX, -eyeY, -eyeZ}});
        const auto camRotY = math::rotation(math::RotationAxis::Y, -totalYaw);
        const auto camRotX = math::rotation(math::RotationAxis::X, -lookPitch);
        const auto worldToView = math::compose(camRotX, math::compose(camRotY, eyeTrans));

        render::PerspectiveCamera3 camera3{
            .worldToView = worldToView,
            .verticalFieldOfView = 1.15, // ~66 градусов FOV для естественного обзора от первого лица
            .nearPlane = 0.08,
            .farPlane = 50.0,
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
        snapshot.clearColor = render::Color{0.04, 0.05, 0.07, 1.0};

        // Включаем Gouraud shading для фонарей на стенах
        for (auto& pass : snapshot.passes) {
            if (auto* world3 = std::get_if<render::World3DPass>(&pass)) {
                for (auto& item : world3->drawItems) {
                    if (item.material < snapshot.materials.size()) {
                        const auto& mat = *snapshot.materials[item.material];
                        if (mat.color.red > 0.9 && mat.color.green > 0.7 && mat.color.blue < 0.4) {
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
                    savePpm(*frame, "/tmp/corridor_f_" + std::to_string(stepCount) + ".ppm");
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

    std::cout << "Finished First-Person Corridor successfully.\n";
    return 0;
}
