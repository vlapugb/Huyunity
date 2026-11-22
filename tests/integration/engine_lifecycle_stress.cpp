#include "huynity/adapters/window/headless_window.h"
#include "huynity/engine.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace window = huynity::adapters::window;

using Clock = std::chrono::steady_clock;
using WeakOwner = std::weak_ptr<const void>;

constexpr std::size_t defaultCycles = 100;
constexpr std::size_t defaultFrames = 120;
constexpr window::WindowSize surfaceSize{64, 48};
constexpr double fixedStep = 1.0 / 60.0;

enum class Mode {
    Lifecycle,
    Churn,
    ChurnGouraud,
    Native,
};

void require(bool condition, std::string_view failure) {
    if (!condition) {
        throw std::runtime_error(std::string{failure});
    }
}

std::size_t parseCount(std::string_view argument, std::size_t maximum) {
    std::size_t count{};
    const auto end = argument.data() + argument.size();
    const auto parsed = std::from_chars(argument.data(), end, count);
    require(parsed.ec == std::errc{} && parsed.ptr == end && count > 0 && count <= maximum,
            "Число циклов или кадров вне допустимого диапазона");
    return count;
}

Mode parseMode(std::string_view name) {
    if (name == "lifecycle") {
        return Mode::Lifecycle;
    }
    if (name == "churn") {
        return Mode::Churn;
    }
    if (name == "churn-gouraud") {
        return Mode::ChurnGouraud;
    }
    require(name == "native", "Допустимые режимы: lifecycle, churn, churn-gouraud, native");
    return Mode::Native;
}

struct ProcessSample {
    std::size_t rssKiB{};
    std::size_t pssKiB{};
    std::size_t threads{};
    std::size_t fileDescriptors{};
};

ProcessSample sampleProcess() {
    ProcessSample sample;
    std::ifstream memory{"/proc/self/smaps_rollup"};
    require(memory.good(), "Невозможно прочитать /proc/self/smaps_rollup");
    bool hasPss{};
    for (std::string line; std::getline(memory, line);) {
        if (line.starts_with("Rss:")) {
            std::istringstream{line.substr(4)} >> sample.rssKiB;
        } else if (line.starts_with("Pss:")) {
            hasPss = static_cast<bool>(std::istringstream{line.substr(4)} >> sample.pssKiB);
        }
    }
    require(sample.rssKiB > 0 && hasPss, "В /proc/self/smaps_rollup отсутствуют RSS или PSS");
    memory.close();

    sample.threads = static_cast<std::size_t>(
        std::distance(std::filesystem::directory_iterator{"/proc/self/task"}, std::filesystem::directory_iterator{}));
    const auto descriptors =
        std::distance(std::filesystem::directory_iterator{"/proc/self/fd"}, std::filesystem::directory_iterator{});
    require(sample.threads > 0 && descriptors > 0, "Невозможно подсчитать потоки или файловые дескрипторы");
    // Итератор /proc/self/fd открывает собственный дескриптор.
    sample.fileDescriptors = static_cast<std::size_t>(descriptors - 1);
    return sample;
}

struct SceneProbe {
    WeakOwner geometry;
    WeakOwner mesh;
    WeakOwner material;
    WeakOwner texture;
};

struct CycleSample {
    std::size_t frames{};
    long long elapsedMicroseconds{};
    bool geometryRetained{};
    bool meshRetained{};
    std::size_t tessellationCalculations{};
    std::size_t shadingCacheMisses{};
};

huynity::Engine createEngine(Mode mode) {
    huynity::Engine engine{{
        .windowSize = surfaceSize,
        .windowTitle = "Huynity lifecycle stress",
        .fixedTimeStep = fixedStep,
        .forceHeadless = mode != Mode::Native,
    }};
    require(engine.window().hasGpuSurface() == (mode == Mode::Native),
            "Оконный адаптер не соответствует выбранному режиму");
    engine.setCamera(render::OrthographicCamera3{.width = 8, .height = 6, .nearPlane = 1, .farPlane = 20});
    engine.setLighting({.ambient = {.intensity = 1}, .directional = {}});
    engine.setClearColor({0, 0, 0, 1});
    engine.setHudEnabled(false);
    return engine;
}

std::shared_ptr<const huynity::image::ImageBuffer> createRedTexture() {
    auto texture = huynity::image::ImageBuffer::create({16, 16});
    require(texture.has_value(), "Не удалось создать текстуру");
    auto pixels = texture->bytes();
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
        pixels[offset] = std::byte{255};
        pixels[offset + 3] = std::byte{255};
    }
    return std::make_shared<const huynity::image::ImageBuffer>(std::move(*texture));
}

void verifyFrame(const huynity::image::ImageBuffer& frame, bool containsScene) {
    require(frame.width() > 0 && frame.height() > 0, "Кадр пуст");
    const auto pixel = frame.bytes().subspan(frame.height() / 2 * frame.stride() + frame.width() / 2 * 4, 4);
    const auto red = containsScene ? std::byte{255} : std::byte{};
    require(pixel[0] == red && pixel[1] == std::byte{} && pixel[2] == std::byte{} && pixel[3] == std::byte{255},
            containsScene ? "Центральный пиксель сцены не соответствует текстуре" : "Кадр сохранил удалённую сцену");
}

CycleSample runScene(huynity::Engine& engine, std::size_t frames, render::ShadingModel shading, SceneProbe& probe) {
    auto* headless = dynamic_cast<window::HeadlessWindow*>(&engine.window());
    const auto presentedBefore = headless ? headless->presentedCount() : 0;
    const auto tessellationsBefore = engine.scene().tessellationCalculations();
    const auto shadingMissesBefore = engine.renderer().shadingCacheMisses();

    auto& scene = engine.scene();
    const auto box = engine.create<objects::ObjectType::Box3>({.size = {2, 2, 2}});
    require(scene.setLocalTransform(box, math::translation(math::Vector3{{0, 0, 5}})).has_value(),
            "Не удалось поставить объект перед камерой");
    require(scene.setBackfaceCulling(box, false).has_value(), "Не удалось отключить отсечение граней");
    if (shading != render::ShadingModel::Flat) {
        require(scene.setShading(box, shading).has_value(), "Не удалось назначить режим затенения");
    }
    {
        const auto texture = createRedTexture();
        const auto material = std::make_shared<const render::Material>(
            render::Material{.color = {1, 1, 1, 1}, .texture = texture, .unlit = true});
        require(scene.setMaterial(box, material).has_value(), "Не удалось назначить материал объекта");
        require(scene.setTexCoords(box, std::vector<math::Point2>(8)).has_value(),
                "Не удалось назначить текстурные координаты объекта");
        const auto* instance = scene.find(box);
        require(instance != nullptr, "Только что созданный объект отсутствует в сцене");
        const auto tessellation = scene.tessellation(box);
        require(tessellation.has_value(), "Не удалось тесселировать объект");
        probe = {instance->geometry, *tessellation, material, texture};
    }

    const auto start = Clock::now();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        engine.step(fixedStep);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
    {
        const auto captured = engine.captureFrame();
        require(captured != nullptr, "Не удалось захватить кадр сцены");
        verifyFrame(*captured, true);
    }
    require(scene.removeSubtree(box).has_value() && scene.find(box) == nullptr && scene.size() == 0,
            "После удаления объекта сцена должна быть пустой");
    engine.step(fixedStep);
    {
        const auto captured = engine.captureFrame();
        require(captured != nullptr, "Не удалось захватить кадр после выгрузки сцены");
        verifyFrame(*captured, false);
    }
    require(probe.material.expired(), "Выгруженная сцена удерживает материал");
    if (headless) {
        require(probe.texture.expired(), "Выгруженная сцена удерживает текстуру");
        require(headless->presentedCount() == presentedBefore + frames + 1 && headless->rejectedPresents() == 0,
                "Число успешно представленных кадров отличается от ожидаемого");
        require(engine.renderer().shadingCacheMisses() == shadingMissesBefore + 1,
                "Нормали геометрии не переиспользуются между кадрами");
    }
    require(scene.tessellationCalculations() == tessellationsBefore + 1,
            "Тесселяция геометрии не переиспользуется между кадрами");

    return {frames,
            elapsed.count(),
            !probe.geometry.expired(),
            !probe.mesh.expired(),
            scene.tessellationCalculations(),
            engine.renderer().shadingCacheMisses()};
}

CycleSample runLifecycle(Mode mode, std::size_t frames) {
    SceneProbe sceneOwners;
    WeakOwner callbackOwner;
    CycleSample sample;
    {
        std::size_t updates{};
        auto engine = createEngine(mode);
        auto* headless = dynamic_cast<window::HeadlessWindow*>(&engine.window());
        {
            auto token = std::make_shared<int>();
            callbackOwner = token;
            engine.onUpdate([&updates](double) { ++updates; });
            engine.onFrame([token](double) {});
            engine.onEvent([token](const window::InputEvent&) { ++*token; });
        }
        if (headless) {
            require(headless->queuedEventCount() == 0, "Новое окно содержит события прошлого цикла");
            headless->pushEvent(window::KeyPressedEvent{.key = window::KeyCode::Space});
        }

        sample = runScene(engine, frames, render::ShadingModel::Flat, sceneOwners);
        require(updates == frames + 1, "Число шагов симуляции отличается от числа кадров");
        if (headless) {
            require(headless->queuedEventCount() == 0 && engine.input().isKeyPressed(window::KeyCode::Space),
                    "Событие ввода не обработано");
            const auto token = std::static_pointer_cast<const int>(callbackOwner.lock());
            require(token && *token == 1, "Обработчик должен получить ровно одно событие");
        }
        engine.stop();
        require(!engine.isRunning() && !engine.window().isOpen(), "Остановка не закрыла окно");
    }
    sample.geometryRetained = !sceneOwners.geometry.expired();
    sample.meshRetained = !sceneOwners.mesh.expired();
    require(sceneOwners.material.expired() && sceneOwners.texture.expired(),
            "После разрушения Engine сохранился материал или текстура");
    require(callbackOwner.expired(), "После разрушения Engine сохранился владелец обработчика");
    return sample;
}

void printSample(std::size_t cycle, const CycleSample& sample, const ProcessSample& process) {
    std::cout << cycle << ',' << sample.frames << ',' << sample.elapsedMicroseconds << ',' << process.rssKiB << ','
              << process.pssKiB << ',' << process.threads << ',' << process.fileDescriptors << ','
              << sample.geometryRetained << ',' << sample.meshRetained << ',' << sample.tessellationCalculations << ','
              << sample.shadingCacheMisses << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc <= 4,
                "Использование: engine_lifecycle_stress [lifecycle|churn|churn-gouraud|native] [cycles] [frames]");
        const auto mode = argc > 1 ? parseMode(argv[1]) : Mode::Lifecycle;
        const bool isChurn = mode == Mode::Churn || mode == Mode::ChurnGouraud;
        const auto cycles = argc > 2 ? parseCount(argv[2], 10000) : defaultCycles;
        auto frames = defaultFrames;
        if (argc > 3) {
            frames = parseCount(argv[3], 10000);
        } else if (isChurn) {
            frames = 10;
        } else if (mode == Mode::Native) {
            frames = 2;
        }
        const auto baseline = sampleProcess();
        std::cout << "cycle,frames,elapsed_us,rss_kib,pss_kib,threads,fd_count,geometry_retained,mesh_retained,"
                     "tessellations,shading_misses\n";
        printSample(0, {}, baseline);
        std::size_t retainedGeometries{};
        std::size_t retainedMeshes{};
        if (isChurn) {
            auto engine = createEngine(mode);
            const auto shading =
                mode == Mode::ChurnGouraud ? render::ShadingModel::Gouraud : render::ShadingModel::Flat;
            for (std::size_t cycle = 1; cycle <= cycles; ++cycle) {
                SceneProbe probe;
                const auto sample = runScene(engine, frames, shading, probe);
                retainedGeometries += sample.geometryRetained;
                retainedMeshes += sample.meshRetained;
                probe = {};
                printSample(cycle, sample, sampleProcess());
            }
        } else {
            for (std::size_t cycle = 1; cycle <= cycles; ++cycle) {
                const auto sample = runLifecycle(mode, frames);
                retainedGeometries += sample.geometryRetained;
                retainedMeshes += sample.meshRetained;
                const auto process = sampleProcess();
                printSample(cycle, sample, process);
                if (mode == Mode::Lifecycle) {
                    require(process.threads == baseline.threads && process.fileDescriptors == baseline.fileDescriptors,
                            "Цикл оставил активные потоки или открытые файловые дескрипторы");
                }
            }
        }
        if (retainedGeometries != 0 || retainedMeshes != 0) {
            std::cerr << "Геометрия удерживается после завершения " << retainedGeometries
                      << " циклов, тесселированные меши после " << retainedMeshes << " циклов\n";
            return 1;
        }
    } catch (const std::exception& failure) {
        std::cerr << "Ошибка стресс-теста жизненного цикла: " << failure.what() << '\n';
        return 1;
    }
}
