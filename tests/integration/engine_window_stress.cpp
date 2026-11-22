#include "huynity/engine.h"

#include <GL/glcorearb.h>
#include <SDL3/SDL.h>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace window = huynity::adapters::window;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int skipStatus = 77;
constexpr double fixedStep = 1.0 / 60.0;
constexpr window::WindowSize initialSize{800, 600};
constexpr std::array requestedSizes{
    window::WindowSize{1280, 720},  window::WindowSize{1920, 1080}, window::WindowSize{2560, 1440},
    window::WindowSize{3440, 1440}, window::WindowSize{3840, 2160}, window::WindowSize{1371, 793},
    window::WindowSize{1, 1},
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void reportSdlSkip(std::string_view operation, SDL_DisplayID target = 0) {
    std::cerr << "SDL_OPERATION_UNAVAILABLE," << operation << ',' << target << ',' << SDL_GetError() << '\n';
}

class VideoSession {
public:
    VideoSession() : m_isActive(SDL_InitSubSystem(SDL_INIT_VIDEO)) {}
    ~VideoSession() {
        if (m_isActive) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    VideoSession(const VideoSession&) = delete;
    VideoSession& operator=(const VideoSession&) = delete;

    [[nodiscard]] bool isActive() const noexcept { return m_isActive; }

private:
    bool m_isActive;
};

[[nodiscard]] std::vector<SDL_DisplayID> listDisplays() {
    int count{};
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> displays{SDL_GetDisplays(&count), &SDL_free};
    if (!displays || count <= 0) {
        return {};
    }
    return {displays.get(), displays.get() + count};
}

void printCsvText(std::string_view field) {
    std::cout << '"';
    for (const auto character : field) {
        if (character == '"') {
            std::cout << '"';
        }
        std::cout << character;
    }
    std::cout << '"';
}

void printInventory(const std::vector<SDL_DisplayID>& displays) {
    std::cout << "driver,display,primary,name,logical_x,logical_y,logical_width,logical_height,mode_width,"
                 "mode_height,refresh_hz,content_scale\n";
    for (const auto display : displays) {
        const auto* mode = SDL_GetCurrentDisplayMode(display);
        const auto* name = SDL_GetDisplayName(display);
        SDL_Rect bounds{};
        if (mode == nullptr || !SDL_GetDisplayBounds(display, &bounds)) {
            reportSdlSkip("display_metadata", display);
            continue;
        }
        std::cout << SDL_GetCurrentVideoDriver() << ',' << display << ',' << (display == SDL_GetPrimaryDisplay())
                  << ',';
        printCsvText(name == nullptr ? "" : name);
        std::cout << ',' << bounds.x << ',' << bounds.y << ',' << bounds.w << ',' << bounds.h << ',' << mode->w << ','
                  << mode->h << ',' << mode->refresh_rate << ',' << SDL_GetDisplayContentScale(display) << '\n';
    }
}

struct WindowDimensions {
    int logicalWidth{};
    int logicalHeight{};
    int pixelWidth{};
    int pixelHeight{};
};

[[nodiscard]] WindowDimensions readDimensions(SDL_Window* native) {
    WindowDimensions result;
    require(SDL_GetWindowSize(native, &result.logicalWidth, &result.logicalHeight), "SDL_GetWindowSize");
    require(SDL_GetWindowSizeInPixels(native, &result.pixelWidth, &result.pixelHeight), "SDL_GetWindowSizeInPixels");
    require(result.pixelWidth > 0 && result.pixelHeight > 0, "Нулевой drawable после стабилизации размеров");
    return result;
}

struct ProcessUsage {
    std::uint64_t residentKiB{};
    std::uint64_t proportionalKiB{};
    std::size_t threadCount{};
    std::size_t descriptorCount{};
};

[[nodiscard]] std::uint64_t procKiB(const char* path, std::string_view label) {
    std::ifstream file{path};
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with(label)) {
            const auto firstDigit = line.find_first_of("0123456789", label.size());
            if (firstDigit != std::string::npos) {
                std::uint64_t result{};
                (void)std::from_chars(line.data() + firstDigit, line.data() + line.size(), result);
                return result;
            }
        }
    }
    return {};
}

[[nodiscard]] std::size_t procDirectoryEntries(const char* path) {
    std::error_code error;
    std::filesystem::directory_iterator iterator{path, error};
    const std::filesystem::directory_iterator end{};
    std::size_t count{};
    while (!error && iterator != end) {
        ++count;
        iterator.increment(error);
    }
    return count;
}

[[nodiscard]] ProcessUsage processUsage() {
    return {procKiB("/proc/self/status", "VmRSS:"), procKiB("/proc/self/smaps_rollup", "Pss:"),
            procDirectoryEntries("/proc/self/task"), procDirectoryEntries("/proc/self/fd")};
}

[[nodiscard]] std::vector<core::ObjectId> createObjects(huynity::Engine& engine, int side) {
    const auto prototype = engine.create<objects::ObjectType::Box3>({.size = math::Vector3{{0.55, 0.55, 0.55}}});
    std::vector<core::ObjectId> ids;
    ids.reserve(static_cast<std::size_t>(side * side));
    ids.push_back(prototype);
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            if (row != 0 || column != 0) {
                const auto instance = engine.scene().createInstance(prototype);
                require(instance.has_value(), "Не удалось создать экземпляр коробки");
                ids.push_back(*instance);
            }
            const double center = static_cast<double>(side - 1) * 0.5;
            const auto transform =
                math::translation(math::Vector3{{(column - center) * 0.65, (row - center) * 0.65, 10.0}});
            require(engine.scene().setLocalTransform(ids.back(), transform).has_value(),
                    "Не удалось установить трансформацию коробки");
        }
    }
    return ids;
}

void setSceneConfiguration(huynity::Engine& engine) {
    engine.setCamera(render::PerspectiveCamera3{
        .verticalFieldOfView = std::numbers::pi / 3,
        .nearPlane = 0.1,
        .farPlane = 100,
    });
    render::Lighting3 lighting;
    lighting.ambient = {{1, 1, 1, 1}, 0.3};
    lighting.directional.push_back({math::Vector3{{0.4, -0.6, 0.7}}, {1, 1, 1, 1}, 0.8});
    lighting.shadows.enabled = true;
    lighting.shadows.resolution = 512;
    lighting.shadows.center = math::Point3{{0, 0, 10}};
    lighting.sky.enabled = true;
    engine.setLighting(std::move(lighting));
}

struct FrameSample {
    std::int64_t wallNs{};
    double processCpuNs{};
    std::size_t resizeEvents{};
};

class WindowTest {
public:
    explicit WindowTest(huynity::Engine& engine)
        : m_engine(engine), m_native(SDL_GL_GetCurrentWindow()), m_context(SDL_GL_GetCurrentContext()) {
        require(m_native != nullptr && m_context != nullptr, "Нет активного SDL OpenGL-контекста");
        const auto resolve = engine.window().glProcResolver();
        require(resolve != nullptr, "Нет resolver для OpenGL");
        m_getInteger = reinterpret_cast<PFNGLGETINTEGERVPROC>(resolve("glGetIntegerv"));
        m_getError = reinterpret_cast<PFNGLGETERRORPROC>(resolve("glGetError"));
        require(m_getInteger != nullptr && m_getError != nullptr, "Не удалось разрешить функции OpenGL");
        require(m_engine.captureFrame() != nullptr, "Нативный Engine не вернул кадр OpenGL");
        m_engine.onEvent([this](const window::InputEvent& event) {
            if (std::holds_alternative<window::WindowResizedEvent>(event)) {
                ++m_resizeEvents;
            }
        });
    }

    ~WindowTest() { m_engine.onEvent({}); }

    WindowTest(const WindowTest&) = delete;
    WindowTest& operator=(const WindowTest&) = delete;

    [[nodiscard]] bool windowStress() {
        bool tested{};
        for (const auto requested : requestedSizes) {
            if (resize(requested)) {
                measure("resolution", 0, requested, 90, 15, true);
                tested = true;
            }
        }
        for (int iteration = 0; iteration < 3; ++iteration) {
            for (const auto requested : {initialSize, window::WindowSize{1371, 793}, window::WindowSize{1, 1},
                                         window::WindowSize{1280, 720}}) {
                if (resize(requested)) {
                    measure("resize", 0, requested, 5, 0, false);
                }
            }
        }
        return tested;
    }

    [[nodiscard]] bool fullscreenStress(const std::vector<SDL_DisplayID>& displays) {
        measure("initial-display", SDL_GetDisplayForWindow(m_native), initialSize, 90, 15, false);
        bool tested{};
        for (const auto display : displays) {
            const auto centered = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display));
            if (!SDL_SetWindowPosition(m_native, centered, centered)) {
                reportSdlSkip("window_move", display);
            } else {
                if (!SDL_SyncWindow(m_native)) {
                    reportSdlSkip("window_move_sync", display);
                }
                measure("move", display, initialSize, 90, 15, false);
            }
            const auto activeDisplay = SDL_GetDisplayForWindow(m_native);
            if (activeDisplay != display) {
                std::cerr << "DISPLAY_NOT_REACHED," << display << ',' << activeDisplay << '\n';
                continue;
            }
            if (!setFullscreen(true)) {
                continue;
            }
            tested = true;
            measure("desktop-fullscreen", display, {}, 90, 15, true);
            require(setFullscreen(false), "Не удалось выйти из полноэкранного режима");
            measure("windowed-after-fullscreen", display, initialSize, 90, 15, false);
        }
        return tested;
    }

    void longRun(int framesPerCase) {
        auto currentObjects = createObjects(m_engine, 16);
        measure("long-many", 0, initialSize, framesPerCase, 15, false);
        erase(currentObjects);
        measure("long-empty", 0, initialSize, framesPerCase, 15, false);
        for (int stage = 0; stage < 4; ++stage) {
            const auto requested = requestedSizes[static_cast<std::size_t>(stage)];
            if (resize(requested)) {
                currentObjects = createObjects(m_engine, 8);
                measure("long-scene-recreate", 0, requested, framesPerCase / 4, 5, false);
                erase(currentObjects);
            }
        }
    }

private:
    [[nodiscard]] FrameSample sampleStep() {
        m_resizeEvents = 0;
        const auto cpuStart = std::clock();
        const auto start = Clock::now();
        m_engine.step(fixedStep);
        const auto elapsed = Clock::now() - start;
        const auto cpuEnd = std::clock();
        const auto cpuNs = 1.0e9 * static_cast<double>(cpuEnd - cpuStart) / CLOCKS_PER_SEC;
        require(m_engine.window().isOpen(), "Тестовое окно закрылось в процессе рендеринга");
        return {std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(), cpuNs, m_resizeEvents};
    }

    [[nodiscard]] bool resize(window::WindowSize size) const {
        if (!SDL_SetWindowSize(m_native, static_cast<int>(size.width), static_cast<int>(size.height))) {
            reportSdlSkip("window_resize");
            return false;
        }
        if (!SDL_SyncWindow(m_native)) {
            reportSdlSkip("window_resize_sync");
        }
        return true;
    }

    [[nodiscard]] bool setFullscreen(bool requested) const {
        if (!SDL_SetWindowFullscreen(m_native, requested)) {
            reportSdlSkip("desktop_fullscreen", SDL_GetDisplayForWindow(m_native));
            return false;
        }
        if (!SDL_SyncWindow(m_native)) {
            reportSdlSkip("desktop_fullscreen_sync", SDL_GetDisplayForWindow(m_native));
        }
        const bool actual = (SDL_GetWindowFlags(m_native) & SDL_WINDOW_FULLSCREEN) != 0;
        if (actual != requested) {
            reportSdlSkip("fullscreen_not_applied", SDL_GetDisplayForWindow(m_native));
            return false;
        }
        require(SDL_GetWindowFullscreenMode(m_native) == nullptr, "Неожиданный эксклюзивный полноэкранный режим");
        return true;
    }

    void erase(const std::vector<core::ObjectId>& objectsToRemove) const {
        for (const auto id : objectsToRemove) {
            require(m_engine.scene().removeSubtree(id).has_value(), "Не удалось удалить объект");
        }
    }

    void verifyFrame(const WindowDimensions& size, bool capture) const {
        require(SDL_GL_GetCurrentContext() == m_context && SDL_GL_GetCurrentWindow() == m_native,
                "Потерян OpenGL-контекст тестового окна");
        const auto adapterSize = m_engine.window().size();
        require(adapterSize.width == static_cast<std::uint32_t>(size.pixelWidth) &&
                    adapterSize.height == static_cast<std::uint32_t>(size.pixelHeight),
                "Размер WindowAdapter расходится с размером drawable SDL");
        std::array<GLint, 4> viewport{};
        m_getInteger(GL_VIEWPORT, viewport.data());
        require(viewport[2] == size.pixelWidth && viewport[3] == size.pixelHeight,
                "OpenGL viewport расходится с размером drawable SDL");
        if (capture) {
            const auto frame = m_engine.captureFrame();
            require(frame && frame->width() == adapterSize.width && frame->height() == adapterSize.height,
                    "Размер кадра захвата расходится с размером drawable SDL");
        }
        require(m_getError() == GL_NO_ERROR, "После кадра обнаружена ошибка OpenGL");
    }

    void measure(std::string_view stage, SDL_DisplayID requestedDisplay, window::WindowSize requestedSize,
                 int frameCount, int warmupCount, bool capture) {
        for (int frame = 0; frame < warmupCount; ++frame) {
            (void)sampleStep();
        }
        std::vector<FrameSample> samples;
        samples.reserve(static_cast<std::size_t>(frameCount));
        for (int frame = 0; frame < frameCount; ++frame) {
            samples.push_back(sampleStep());
        }
        const auto size = readDimensions(m_native);
        const auto actualDisplay = SDL_GetDisplayForWindow(m_native);
        const auto scale = SDL_GetWindowDisplayScale(m_native);
        const auto density = SDL_GetWindowPixelDensity(m_native);
        int swapInterval{};
        const bool isIntervalKnown = SDL_GL_GetSwapInterval(&swapInterval);
        const auto process = processUsage();
        for (std::size_t frame = 0; frame < samples.size(); ++frame) {
            const auto& sample = samples[frame];
            std::cout << SDL_GetCurrentVideoDriver() << ',' << stage << ',' << frame << ',' << requestedDisplay << ','
                      << actualDisplay << ',' << requestedSize.width << ',' << requestedSize.height << ','
                      << size.logicalWidth << ',' << size.logicalHeight << ',' << size.pixelWidth << ','
                      << size.pixelHeight << ',' << scale << ',' << density << ',' << isIntervalKnown << ','
                      << swapInterval << ',' << sample.wallNs << ',' << sample.processCpuNs << ','
                      << process.residentKiB << ',' << process.proportionalKiB << ',' << process.threadCount << ','
                      << process.descriptorCount << ',' << m_engine.scene().size() << ',' << sample.resizeEvents
                      << '\n';
        }
        std::cout << std::flush;
        verifyFrame(size, capture);
    }

    huynity::Engine& m_engine;
    SDL_Window* m_native;
    SDL_GLContext m_context;
    PFNGLGETINTEGERVPROC m_getInteger{};
    PFNGLGETERRORPROC m_getError{};
    std::size_t m_resizeEvents{};
};

[[nodiscard]] int parseFrameCount(int argc, char** argv) {
    constexpr int defaultFrames = 3600;
    if (argc == 2) {
        return defaultFrames;
    }
    const std::string_view argument{argv[2]};
    int frames{};
    const auto [next, error] = std::from_chars(argument.data(), argument.data() + argument.size(), frames);
    require(error == std::errc{} && next == argument.data() + argument.size() && frames >= 100 && frames <= 36000,
            "Число кадров должно быть в диапазоне 100–36000");
    return frames;
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3,
                "Укажите --inventory, --window-stress, --fullscreen-stress или --long-run [frames]");
        const std::string_view mode{argv[1]};
        require(mode == "--inventory" || mode == "--window-stress" || mode == "--fullscreen-stress" ||
                    mode == "--long-run",
                "Неизвестный режим окна");
        require(argc == 2 || mode == "--long-run", "Число кадров применяется только к --long-run");
        const VideoSession video;
        if (!video.isActive()) {
            reportSdlSkip("video_initialization");
            return skipStatus;
        }
        const auto displays = listDisplays();
        if (displays.empty()) {
            reportSdlSkip("display_enumeration");
            return skipStatus;
        }
        if (mode == "--inventory") {
            printInventory(displays);
            return EXIT_SUCCESS;
        }

        huynity::Engine engine{{.windowSize = initialSize, .windowTitle = "Huynity display stress"}};
        if (!engine.window().hasGpuSurface() || !engine.window().makeCurrent()) {
            reportSdlSkip("native_opengl_window");
            return skipStatus;
        }
        WindowTest test{engine};
        setSceneConfiguration(engine);
        std::cout
            << "driver,stage,frame,requested_display,actual_display,requested_width,requested_height,logical_width,"
               "logical_height,framebuffer_width,framebuffer_height,content_scale,pixel_density,"
               "swap_interval_known,swap_interval,step_ns,process_cpu_ns,rss_kib,pss_kib,threads,fds,objects,"
               "resize_events\n";

        if (mode == "--long-run") {
            test.longRun(parseFrameCount(argc, argv));
        } else {
            (void)createObjects(engine, 6);
            const bool tested = mode == "--window-stress" ? test.windowStress() : test.fullscreenStress(displays);
            if (!tested) {
                return skipStatus;
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "engine_window_stress: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
