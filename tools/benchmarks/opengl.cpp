#include "huynity/core/render_preparation.h"
#include "huynity/renderer/gl_renderer.h"

#include <GL/glcorearb.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;
namespace render = huynity::render_contracts;
namespace renderer = huynity::renderer;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

struct Counts {
    int uniformLookups{};
    int drawElements{};
    int drawArrays{};
    int uploadCalls{};
    std::size_t uploadedBytes{};
    int texturesBound{};
    int programsUsed{};
    int cullModeSets{};
    int polygonModeSets{};
    int textureParameterSets{};
    int vertexArraysGenerated{};
};

Counts counts;

template <typename Fn>
Fn proc(const char* name) {
    return reinterpret_cast<Fn>(SDL_GL_GetProcAddress(name));
}

#define COUNTED(NAME, TYPE, SIGNATURE, CALL, COUNTER)                                                                  \
    auto observe##NAME SIGNATURE {                                                                                     \
        static const auto underlying = proc<TYPE>("gl" #NAME);                                                         \
        COUNTER;                                                                                                       \
        return underlying CALL;                                                                                        \
    }

COUNTED(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC, (GLuint id, const GLchar* name), (id, name),
        ++counts.uniformLookups)
COUNTED(DrawElements, PFNGLDRAWELEMENTSPROC, (GLenum mode, GLsizei n, GLenum type, const void* offset),
        (mode, n, type, offset), ++counts.drawElements)
COUNTED(DrawArrays, PFNGLDRAWARRAYSPROC, (GLenum mode, GLint first, GLsizei n), (mode, first, n), ++counts.drawArrays)
COUNTED(BufferData, PFNGLBUFFERDATAPROC, (GLenum target, GLsizeiptr size, const void* data, GLenum usage),
        (target, size, data, usage), ++counts.uploadCalls;
        counts.uploadedBytes += size)
COUNTED(BindTexture, PFNGLBINDTEXTUREPROC, (GLenum target, GLuint id), (target, id), ++counts.texturesBound)
COUNTED(UseProgram, PFNGLUSEPROGRAMPROC, (GLuint id), (id), ++counts.programsUsed)
COUNTED(CullFace, PFNGLCULLFACEPROC, (GLenum face), (face), ++counts.cullModeSets)
COUNTED(PolygonMode, PFNGLPOLYGONMODEPROC, (GLenum face, GLenum mode), (face, mode), ++counts.polygonModeSets)
COUNTED(TexParameteri, PFNGLTEXPARAMETERIPROC, (GLenum target, GLenum name, GLint parameter), (target, name, parameter),
        ++counts.textureParameterSets)
COUNTED(GenVertexArrays, PFNGLGENVERTEXARRAYSPROC, (GLsizei n, GLuint* ids), (n, ids),
        counts.vertexArraysGenerated += n)

void* resolve(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}
void* resolveCounting(const char* name) {
#define LOOKUP(NAME)                                                                                                   \
    if (std::string_view{name} == "gl" #NAME) {                                                                        \
        return reinterpret_cast<void*>(&observe##NAME);                                                                \
    }
    LOOKUP(GetUniformLocation);
    LOOKUP(DrawElements);
    LOOKUP(DrawArrays);
    LOOKUP(BufferData);
    LOOKUP(BindTexture);
    LOOKUP(UseProgram);
    LOOKUP(CullFace);
    LOOKUP(PolygonMode);
    LOOKUP(TexParameteri);
    LOOKUP(GenVertexArrays);
#undef LOOKUP
    return resolve(name);
}

namespace {

int positiveArgument(const char* argument, int limit) {
    const std::string_view text{argument};
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    CHECK(error == std::errc{} && end == text.data() + text.size() && value > 0 && value <= limit);
    return value;
}

void logMemory() {
    std::ifstream map{"/proc/self/smaps_rollup"};
    for (std::string line; std::getline(map, line);) {
        if (line.starts_with("Rss:") || line.starts_with("Pss:")) {
            std::clog << line << '\n';
        }
    }
}

using GpuClients = std::map<std::pair<std::string, std::uint64_t>, std::uint64_t>;

GpuClients renderEngineTimes() {
    GpuClients clients;
    for (const auto& descriptor : std::filesystem::directory_iterator{"/proc/self/fdinfo"}) {
        std::ifstream info{descriptor.path()};
        std::string device;
        std::uint64_t client{};
        std::uint64_t renderTime{};
        bool hasClient{};
        bool hasRenderTime{};
        for (std::string line; std::getline(info, line);) {
            std::istringstream field{line};
            std::string key;
            field >> key;
            if (key == "drm-client-id:") {
                hasClient = static_cast<bool>(field >> client);
            } else if (key == "drm-pdev:") {
                field >> device;
            } else if (key == "drm-engine-render:") {
                std::string unit;
                hasRenderTime = static_cast<bool>(field >> renderTime >> unit) && unit == "ns";
            }
        }
        if (!device.empty() && hasClient && hasRenderTime) {
            clients.insert_or_assign({device, client}, renderTime);
        }
    }
    return clients;
}

void measurePresentation(renderer::GlRenderer& backend, const render::RenderSnapshot& snapshot, SDL_Window* window,
                         int frameCount) {
    using Clock = std::chrono::steady_clock;
    const auto nanoseconds = [](Clock::duration duration) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    };
    const auto gen = proc<PFNGLGENQUERIESPROC>("glGenQueries");
    const auto begin = proc<PFNGLBEGINQUERYPROC>("glBeginQuery");
    const auto end = proc<PFNGLENDQUERYPROC>("glEndQuery");
    const auto get = proc<PFNGLGETQUERYOBJECTUI64VPROC>("glGetQueryObjectui64v");
    const auto release = proc<PFNGLDELETEQUERIESPROC>("glDeleteQueries");
    const auto finish = proc<PFNGLFINISHPROC>("glFinish");
    const auto getError = proc<PFNGLGETERRORPROC>("glGetError");
    const auto queryInfo = proc<PFNGLGETQUERYIVPROC>("glGetQueryiv");
    GLint timerBits{};
    queryInfo(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &timerBits);
    CHECK(timerBits > 0);

    struct Frame {
        std::int64_t submit{};
        std::int64_t swap{};
        GLuint64 gpu{};
    };
    std::vector<Frame> frames(static_cast<std::size_t>(frameCount));
    std::vector<GLuint> queries(frames.size());
    gen(frameCount, queries.data());
    finish();
    const auto gpuStart = renderEngineTimes();
    const auto processStart = std::clock();
    const auto wallStart = Clock::now();
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto frameStart = Clock::now();
        begin(GL_TIME_ELAPSED, queries[index]);
        CHECK(backend.render(snapshot).has_value());
        end(GL_TIME_ELAPSED);
        const auto submitEnd = Clock::now();
        CHECK(SDL_GL_SwapWindow(window));
        frames[index].submit = nanoseconds(submitEnd - frameStart);
        frames[index].swap = nanoseconds(Clock::now() - submitEnd);
    }
    finish();
    const auto wall = std::chrono::duration<double>(Clock::now() - wallStart).count();
    const auto process = static_cast<double>(std::clock() - processStart) / CLOCKS_PER_SEC;
    std::clog << "wall_seconds=" << wall << " process_cpu_seconds=" << process
              << " cpu_percent_one_core=" << 100 * process / wall << '\n';
    std::uint64_t busyNanoseconds{};
    bool hasCounters{};
    const auto gpuEnd = renderEngineTimes();
    for (const auto& [client, start] : gpuStart) {
        if (const auto found = gpuEnd.find(client); found != gpuEnd.end() && found->second >= start) {
            busyNanoseconds += found->second - start;
            hasCounters = true;
        }
    }
    if (hasCounters) {
        std::clog << "drm_render_engine_percent=" << 100e-9 * static_cast<double>(busyNanoseconds) / wall << '\n';
    } else {
        std::clog << "drm_render_engine_percent=unavailable\n";
    }
    CHECK(getError() == GL_NO_ERROR);
    logMemory();
    for (std::size_t index = 0; index < frames.size(); ++index) {
        get(queries[index], GL_QUERY_RESULT, &frames[index].gpu);
    }
    release(frameCount, queries.data());
    CHECK(getError() == GL_NO_ERROR);
    std::cout << "frame,cpu_submit_ns,swap_ns,frame_ns,gpu_ns\n";
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        std::cout << index << ',' << frame.submit << ',' << frame.swap << ',' << frame.submit + frame.swap << ','
                  << frame.gpu << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc >= 2);
    const auto mode = std::string_view{argv[1]};
    const auto fixture = argc > 2 ? std::string_view{argv[2]} : "3d";
    const auto isDisplayRun = mode == "display";
    const auto fixture2D = fixture == "2d";
    CHECK(fixture2D || fixture == "3d" || fixture == "dense" || fixture == "empty");
    CHECK(argc <= 6 && argc != 4);

    const int width = argc >= 5 ? positiveArgument(argv[3], 8192) : 1280;
    const int height = argc >= 5 ? positiveArgument(argv[4], 8192) : 720;
    const int frameCount = argc >= 6 ? positiveArgument(argv[5], 100000) : 180;

    // Сцена: сетка освещённых коробок с тенями и небом — типичный 3D-кадр движка.
    core::Scene scene;
    const int rows = fixture == "empty" ? 0 : fixture == "dense" ? 24 : 8;
    const auto spacing = fixture == "dense" ? 0.29 : 1.1;
    const auto center = (rows - 1) * 0.5;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < rows; ++column) {
            auto box = scene.createObject<objects::ObjectType::Box3>(
                {.size = math::Vector3{{0.6, 0.6 + 0.3 * ((row + column) % 3), 0.6}}});
            CHECK(box.has_value());
            const auto transform = math::compose(
                math::translation(math::Vector3{{(column - center) * spacing, (row - center) * spacing, 6.0}}),
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
    if (fixture == "empty") {
        config.camera3.reset();
        config.hudEnabled = false;
    }
    if (fixture2D) {
        config.camera3.reset();
        config.camera2 = render::Camera2{.width = 16, .height = 9};
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                auto rectangle =
                    scene.createObject<objects::ObjectType::Rectangle2>({.size = math::Vector2{{0.6, 0.6}}});
                CHECK(rectangle.has_value());
                const auto transform = math::translation(math::Vector2{{(column - 3.5) * 0.8, (row - 3.5) * 0.8}});
                CHECK(scene.setLocalTransform(*rectangle, transform).has_value());
            }
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL video unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    const std::string_view videoDriver{SDL_GetCurrentVideoDriver()};
    if (isDisplayRun && (videoDriver == "offscreen" || videoDriver == "dummy")) {
        std::cerr << "A native SDL display driver is required: " << videoDriver << '\n';
        SDL_Quit();
        return 77;
    }
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3));
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3));
    CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE));
    CHECK(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));
    const auto windowFlags = SDL_WINDOW_OPENGL | (isDisplayRun ? 0 : SDL_WINDOW_HIDDEN);
    auto* window = SDL_CreateWindow("Huynity audit", width, height, windowFlags);
    if (window == nullptr) {
        std::cerr << "SDL window unavailable: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 77;
    }
    const auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << "SDL OpenGL context unavailable: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    const bool intervalSet = SDL_GL_SetSwapInterval(0);
    int interval{};
    const bool intervalRead = SDL_GL_GetSwapInterval(&interval);
    const auto version = proc<PFNGLGETSTRINGPROC>("glGetString");
    std::cerr << version(GL_RENDERER) << ", " << version(GL_VERSION) << "\n";
    int pixelWidth{};
    int pixelHeight{};
    int logicalWidth{};
    int logicalHeight{};
    CHECK(SDL_SyncWindow(window));
    CHECK(SDL_GetWindowSize(window, &logicalWidth, &logicalHeight));
    CHECK(SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight) && pixelWidth > 0 && pixelHeight > 0);
    std::clog << "driver=" << SDL_GetCurrentVideoDriver() << " requested=" << width << 'x' << height
              << " logical=" << logicalWidth << 'x' << logicalHeight << " pixels=" << pixelWidth << 'x' << pixelHeight
              << " display=" << SDL_GetDisplayForWindow(window) << " scale=" << SDL_GetWindowDisplayScale(window)
              << " interval_set=" << intervalSet << " interval_known=" << intervalRead << " swap_interval=" << interval
              << '\n';
    const auto snapshotResult = core::RenderPreparation::prepareSnapshot(
        scene, config, static_cast<std::size_t>(pixelWidth), static_cast<std::size_t>(pixelHeight));
    CHECK(snapshotResult.has_value());
    const auto& snapshot = *snapshotResult;
    {
        renderer::GlRenderer backend(mode == "counts" ? resolveCounting : resolve);
        const auto render = [&]() { CHECK(backend.render(snapshot).has_value()); };
        for (int frame = 0; frame < 15; ++frame) {
            SDL_PumpEvents();
            render();
            CHECK(SDL_GL_SwapWindow(window));
        }
        const auto finish = proc<PFNGLFINISHPROC>("glFinish");
        const auto getError = proc<PFNGLGETERRORPROC>("glGetError");
        finish();
        CHECK(getError() == GL_NO_ERROR);
        if (isDisplayRun) {
            measurePresentation(backend, snapshot, window, frameCount);
        } else if (mode == "counts") {
            counts = {};
            render();
            CHECK(getError() == GL_NO_ERROR);
            std::cout << "lookups=" << counts.uniformLookups << " drawElements=" << counts.drawElements
                      << " drawArrays=" << counts.drawArrays << " bufferData=" << counts.uploadCalls
                      << " uploadedBytes=" << counts.uploadedBytes << " bindTexture=" << counts.texturesBound
                      << " useProgram=" << counts.programsUsed << " cullFace=" << counts.cullModeSets
                      << " polygonMode=" << counts.polygonModeSets << " texParameteri=" << counts.textureParameterSets
                      << " genVertexArrays=" << counts.vertexArraysGenerated << '\n';
        } else if (mode == "pixels") {
            const auto capture = backend.capture(snapshot);
            CHECK(capture.has_value());
            const auto pixels = (*capture)->bytes();
            std::cout.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
            CHECK(std::cout.good() && getError() == GL_NO_ERROR);
        } else if (mode == "timing") {
            const auto gen = proc<PFNGLGENQUERIESPROC>("glGenQueries");
            const auto begin = proc<PFNGLBEGINQUERYPROC>("glBeginQuery");
            const auto end = proc<PFNGLENDQUERYPROC>("glEndQuery");
            const auto get = proc<PFNGLGETQUERYOBJECTUI64VPROC>("glGetQueryObjectui64v");
            const auto del = proc<PFNGLDELETEQUERIESPROC>("glDeleteQueries");
            std::array<GLuint, 120> queries{};
            gen(queries.size(), queries.data());
            for (const auto id : queries) {
                begin(GL_TIME_ELAPSED, id);
                const auto start = std::chrono::steady_clock::now();
                render();
                const auto cpu = std::chrono::steady_clock::now() - start;
                end(GL_TIME_ELAPSED);
                // Каждая итерация начинает с пустой очередью, чтобы отделить submit от wait.
                finish();
                const auto elapsed = std::chrono::steady_clock::now() - start;
                GLuint64 gpu{};
                get(id, GL_QUERY_RESULT, &gpu);
                const auto toNs = [](auto value) {
                    return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
                };
                std::cout << toNs(cpu) << ',' << gpu << ',' << toNs(elapsed) << '\n';
            }
            del(queries.size(), queries.data());
            CHECK(getError() == GL_NO_ERROR);
        } else {
            return 2;
        }
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
