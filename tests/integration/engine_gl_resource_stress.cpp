#include "huynity/adapters/window/window_factory.h"
#include "huynity/engine.h"

#include <GL/glcorearb.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

extern "C" SDL_FunctionPointer __real_SDL_GL_GetProcAddress(const char* name);
extern "C" bool __real_SDL_GL_DestroyContext(SDL_GLContext context);

namespace {

namespace render = huynity::render_contracts;
namespace image = huynity::image;

enum class Resource {
    Texture,
    Buffer,
    VertexArray,
    Framebuffer,
    Renderbuffer,
    Shader,
    Program,
    Count,
};

constexpr std::size_t resourceCount = static_cast<std::size_t>(Resource::Count);
constexpr std::array names{"texture", "buffer", "vertex_array", "framebuffer", "renderbuffer", "shader", "program"};

struct ObjectCounts {
    std::size_t created{};
    std::size_t deleted{};
    std::set<GLuint> live;
};

using ContextCounts = std::array<ObjectCounts, resourceCount>;

struct ResourceLog {
    std::map<SDL_GLContext, ContextCounts> contexts;
    SDL_GLContext expectedReleaseContext{};
    std::array<std::size_t, resourceCount> created{};
    std::array<std::size_t, resourceCount> deleted{};
    std::array<std::size_t, resourceCount> liveAtDestroy{};
    std::size_t invalidDeletes{};
    std::size_t duplicateCreates{};
    std::size_t destroyedContexts{};
};

ResourceLog resourceLog;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void recordCreation(Resource resource, GLuint name) {
    if (name != 0) {
        const auto index = static_cast<std::size_t>(resource);
        auto& counts = resourceLog.contexts[SDL_GL_GetCurrentContext()][index];
        if (!counts.live.insert(name).second) {
            ++resourceLog.duplicateCreates;
        }
        ++counts.created;
        ++resourceLog.created[index];
    }
}

void recordDeletion(Resource resource, GLuint name) {
    if (name == 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(resource);
    const auto current = SDL_GL_GetCurrentContext();
    auto context = resourceLog.contexts.find(current);
    if (current == nullptr || context == resourceLog.contexts.end() ||
        (resourceLog.expectedReleaseContext != nullptr && resourceLog.expectedReleaseContext != current) ||
        context->second[index].live.erase(name) != 1) {
        ++resourceLog.invalidDeletes;
        return;
    }
    ++context->second[index].deleted;
    ++resourceLog.deleted[index];
}

template <Resource type>
struct ObjectFunctions {
    static inline PFNGLGENTEXTURESPROC generate{};
    static inline PFNGLDELETETEXTURESPROC destroy{};

    static void APIENTRY generateTracked(GLsizei count, GLuint* objects) {
        generate(count, objects);
        for (GLsizei index = 0; index < count; ++index) {
            recordCreation(type, objects[index]);
        }
    }

    static void APIENTRY destroyTracked(GLsizei count, const GLuint* objects) {
        for (GLsizei index = 0; index < count; ++index) {
            recordDeletion(type, objects[index]);
        }
        destroy(count, objects);
    }
};

PFNGLCREATESHADERPROC createShader{};
PFNGLCREATEPROGRAMPROC createProgram{};
PFNGLDELETESHADERPROC deleteShader{};
PFNGLDELETEPROGRAMPROC deleteProgram{};

GLuint APIENTRY createTrackedShader(GLenum type) {
    const auto shader = createShader(type);
    recordCreation(Resource::Shader, shader);
    return shader;
}

GLuint APIENTRY createTrackedProgram() {
    const auto program = createProgram();
    recordCreation(Resource::Program, program);
    return program;
}

void APIENTRY deleteTrackedShader(GLuint shader) {
    recordDeletion(Resource::Shader, shader);
    deleteShader(shader);
}

void APIENTRY deleteTrackedProgram(GLuint program) {
    recordDeletion(Resource::Program, program);
    deleteProgram(program);
}

template <Resource resource>
SDL_FunctionPointer instrumentObjects(std::string_view request, std::string_view generation, std::string_view deletion,
                                      SDL_FunctionPointer original) noexcept {
    using Functions = ObjectFunctions<resource>;
    if (request == generation) {
        Functions::generate = reinterpret_cast<PFNGLGENTEXTURESPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&Functions::generateTracked);
    }
    if (request == deletion) {
        Functions::destroy = reinterpret_cast<PFNGLDELETETEXTURESPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&Functions::destroyTracked);
    }
    return nullptr;
}

void configureScene(huynity::Engine& engine) {
    engine.setClearColor({0, 0, 0, 1});
    engine.setCamera(render::OrthographicCamera3{.width = 4, .height = 3, .nearPlane = 0.1, .farPlane = 50});
    const auto box = engine.create<huynity::objectlib::ObjectType::Box3>({.size = {{1, 1, 1}}});
    require(engine.scene()
                .setLocalTransform(box, huynity::math::translation(huynity::math::Vector3{{0, 0, 5}}))
                .has_value(),
            "box transform");
    auto pixels = image::ImageBuffer::create({4, 4});
    require(pixels.has_value(), "texture allocation");
    std::ranges::fill(pixels->bytes(), std::byte{255});
    auto texture = std::make_shared<const image::ImageBuffer>(std::move(*pixels));
    auto material = std::make_shared<const render::Material>(
        render::Material{.color = {0.8, 0.2, 0.1, 1}, .texture = std::move(texture), .unlit = true});
    require(engine.scene().setMaterial(box, std::move(material)).has_value(), "box texture");
}

SDL_GLContext captureScene(huynity::Engine& engine) {
    const auto frame = engine.captureFrame();
    require(frame != nullptr, "native scene capture failed");
    const auto size = engine.window().size();
    require(size.width == frame->width() && size.height == frame->height(), "capture pixel dimensions");
    const auto center = frame->height() / 2 * frame->stride() + frame->width() / 2 * 4;
    const auto rgba = frame->bytes().subspan(center, 4);
    require(std::to_integer<int>(rgba[0]) > 150 && std::to_integer<int>(rgba[1]) < 100 &&
                std::to_integer<int>(rgba[2]) < 100 && rgba[3] == std::byte{255},
            "capture lost textured geometry");
    const auto current = SDL_GL_GetCurrentContext();
    require(current != nullptr && resourceLog.contexts.contains(current), "capture context is not tracked");
    return current;
}

enum class Release {
    Stop,
    Destructor,
    MoveAssignment,
    DirectClose,
};

constexpr huynity::EngineConfig makeConfig() {
    return {.windowSize = {96, 72}, .windowTitle = "Huynity GL resources"};
}

void replaceTextureAtReusedAddress() {
    using Pixel = std::array<std::byte, 4>;
    constexpr Pixel red{std::byte{255}, std::byte{}, std::byte{}, std::byte{255}};
    constexpr Pixel green{std::byte{}, std::byte{255}, std::byte{}, std::byte{255}};

    alignas(image::ImageBuffer) std::array<std::byte, sizeof(image::ImageBuffer)> storage{};
    huynity::Engine engine(makeConfig());
    engine.setClearColor({0, 0, 0, 1});
    engine.setCamera(render::OrthographicCamera3{.width = 4, .height = 3, .nearPlane = 0.1, .farPlane = 50});
    const auto triangle = engine.create<huynity::objectlib::ObjectType::TriangleMesh3>({
        .positions = {{{-1.5, -1.3, 5}}, {{0, 1.3, 5}}, {{1.5, -1.3, 5}}},
        .triangles = {{{0, 1, 2}}},
    });
    require(engine.scene().setBackfaceCulling(triangle, false).has_value(), "texture fixture backface culling");
    require(engine.scene().setTexCoords(triangle, {{{0, 0}}, {{0.5, 1}}, {{1, 0}}}).has_value(), "texture fixture UVs");

    const auto assignTexture = [&](const Pixel& pixel) {
        auto pixels = image::ImageBuffer::create({1, 1});
        require(pixels.has_value(), "texture allocation");
        std::ranges::copy(pixel, pixels->bytes().begin());
        auto* imageBuffer =
            std::construct_at(reinterpret_cast<image::ImageBuffer*>(storage.data()), std::move(*pixels));
        const std::shared_ptr<const image::ImageBuffer> texture{
            imageBuffer, [](const image::ImageBuffer* released) { std::destroy_at(released); }};
        require(engine.scene()
                    .setMaterial(triangle, std::make_shared<const render::Material>(
                                               render::Material{.texture = texture, .unlit = true}))
                    .has_value(),
                "set texture on triangle");
        return std::weak_ptr<const image::ImageBuffer>{texture};
    };
    const auto captureCenter = [&]() -> Pixel {
        const auto frame = engine.captureFrame();
        require(frame != nullptr, "texture fixture capture failed");
        const auto center = frame->height() / 2 * frame->stride() + frame->width() / 2 * 4;
        const auto pixel = frame->bytes().subspan(center, 4);
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    };

    const auto firstOwner = assignTexture(red);
    require(captureCenter() == red, "first texture should be red");
    const auto textureIndex = static_cast<std::size_t>(Resource::Texture);
    const auto texturesCreated = resourceLog.created[textureIndex];
    require(captureCenter() == red && resourceLog.created[textureIndex] == texturesCreated,
            "unchanged texture should reuse its GL object");
    require(engine.scene().setMaterial(triangle, {}).has_value(), "remove first textured material");
    require(firstOwner.expired(), "GPU cache retained the previous CPU texture");
    (void)assignTexture(green); // Новый ImageBuffer занимает тот же адрес после истечения первого владельца.
    require(captureCenter() == green, "stale texture at reused ImageBuffer address");
    require(resourceLog.created[textureIndex] == texturesCreated + 1, "replacement texture should upload once");
}

void exerciseRelease(Release operation) {
    auto first = std::make_unique<huynity::Engine>(makeConfig());
    configureScene(*first);
    const auto released = captureScene(*first);
    auto second = std::make_unique<huynity::Engine>(makeConfig());
    configureScene(*second);
    const auto surviving = captureScene(*second);
    require(released != surviving, "expected independent contexts");

    // При освобождении первого окна текущим остаётся контекст второго.
    resourceLog.expectedReleaseContext = released;
    switch (operation) {
    case Release::Stop: {
        first->stop();
        first.reset();
        break;
    }
    case Release::Destructor: {
        first.reset();
        break;
    }
    case Release::MoveAssignment: {
        *first = std::move(*second);
        second.reset();
        second = std::move(first);
        break;
    }
    case Release::DirectClose: {
        first->window().close();
        first.reset();
        break;
    }
    }
    resourceLog.expectedReleaseContext = surviving;
    if (resourceLog.invalidDeletes == 0) {
        require(captureScene(*second) == surviving, "surviving window lost its OpenGL context");
    }
    second.reset();
    resourceLog.expectedReleaseContext = nullptr;
}

void resizeCaptureTargets() {
    huynity::Engine engine(makeConfig());
    configureScene(engine);
    const auto context = captureScene(engine);
    auto* window = SDL_GL_GetCurrentWindow();
    require(window != nullptr, "no current SDL window");
    std::array sizes{std::pair{81, 63}, std::pair{112, 84}};
    for (int iteration = 0; iteration < 16; ++iteration) {
        const auto [width, height] = sizes[static_cast<std::size_t>(iteration) % sizes.size()];
        const auto previous = engine.window().size();
        require(SDL_SetWindowSize(window, width, height), "SDL_SetWindowSize failed");
        require(SDL_SyncWindow(window), "SDL_SyncWindow failed");
        std::vector<huynity::adapters::window::InputEvent> events;
        engine.window().pollEvents(events);
        const auto size = engine.window().size();
        require(size != previous, "no drawable size change received");
        require(captureScene(engine) == context, "resize replaced the OpenGL context");
        const auto created = resourceLog.created;
        require(captureScene(engine) == context, "capture replaced the OpenGL context");
        require(resourceLog.created == created, "same-size capture recreated GL resources");
        for (const auto& counts : resourceLog.contexts.at(context)) {
            require(counts.created - counts.deleted == counts.live.size(), "invalid GL object accounting");
        }
        require(resourceLog.contexts.at(context)[static_cast<std::size_t>(Resource::Framebuffer)].live.size() == 1,
                "capture framebuffer leak during resize");
        require(resourceLog.contexts.at(context)[static_cast<std::size_t>(Resource::Renderbuffer)].live.size() == 1,
                "capture depth attachment leak during resize");
        require(resourceLog.contexts.at(context)[static_cast<std::size_t>(Resource::Texture)].live.size() == 2,
                "capture color attachment leak during resize");
    }
}

bool preserveBlitOnClose() {
    namespace window = huynity::adapters::window;
    const window::WindowConfig config{.size = {96, 72}, .title = "Huynity blit resources"};
    auto first = window::createWindow(config);
    auto second = window::createWindow(config);
    for (auto* adapter : {first.get(), second.get()}) {
        require(adapter != nullptr && adapter->hasGpuSurface(), "blit window has no OpenGL context");
        const auto size = adapter->size();
        auto pixels = image::ImageBuffer::create({size.width, size.height});
        require(pixels.has_value(), "blit image allocation failed");
        (void)adapter->present(std::make_unique<image::ImageBuffer>(std::move(*pixels)));
    }

    const auto resolve = second->glProcResolver();
    require(resolve != nullptr, "blit window has no OpenGL resolver");
    const auto query = reinterpret_cast<PFNGLGETINTEGERVPROC>(resolve("glGetIntegerv"));
    const auto useProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(resolve("glUseProgram"));
    const auto isTexture = reinterpret_cast<PFNGLISTEXTUREPROC>(resolve("glIsTexture"));
    const auto isVertexArray = reinterpret_cast<PFNGLISVERTEXARRAYPROC>(resolve("glIsVertexArray"));
    const auto isProgram = reinterpret_cast<PFNGLISPROGRAMPROC>(resolve("glIsProgram"));
    require(query && useProgram && isTexture && isVertexArray && isProgram, "blit resource query unavailable");
    GLint texture{};
    GLint vertexArray{};
    GLint program{};
    query(GL_TEXTURE_BINDING_2D, &texture);
    query(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    query(GL_CURRENT_PROGRAM, &program);
    require(texture != 0 && vertexArray != 0 && program != 0, "blit resources were not bound");

    first->close();
    require(second->makeCurrent(), "surviving blit window lost its context");
    useProgram(0); // Завершает отложенное удаление, если программа была ошибочно удалена.
    const auto textureAlive = isTexture(static_cast<GLuint>(texture)) == GL_TRUE;
    const auto vertexArrayAlive = isVertexArray(static_cast<GLuint>(vertexArray)) == GL_TRUE;
    const auto programAlive = isProgram(static_cast<GLuint>(program)) == GL_TRUE;
    std::cerr << "blit_survivor texture=" << textureAlive << " vertex_array=" << vertexArrayAlive
              << " program=" << programAlive << '\n';
    return textureAlive && vertexArrayAlive && programAlive;
}

bool report(std::string_view scenario, bool ownsContext = true) {
    bool passed = resourceLog.destroyedContexts != 0 && resourceLog.contexts.empty() &&
                  resourceLog.invalidDeletes == 0 && resourceLog.duplicateCreates == 0;
    for (std::size_t index = 0; index < resourceCount; ++index) {
        std::cout << scenario << ',' << names[index] << ',' << resourceLog.created[index] << ','
                  << resourceLog.deleted[index] << ',' << resourceLog.liveAtDestroy[index] << ','
                  << resourceLog.invalidDeletes << '\n';
        passed = passed && resourceLog.created[index] != 0 &&
                 resourceLog.created[index] == resourceLog.deleted[index] + resourceLog.liveAtDestroy[index] &&
                 (!ownsContext || resourceLog.liveAtDestroy[index] == 0);
    }
    resourceLog = {};
    return passed;
}

} // namespace

extern "C" SDL_FunctionPointer __wrap_SDL_GL_GetProcAddress(const char* name) {
    const auto original = __real_SDL_GL_GetProcAddress(name);
    if (original == nullptr) {
        return nullptr;
    }
    const std::string_view function{name};
    const std::array objectWrappers{
        instrumentObjects<Resource::Texture>(function, "glGenTextures", "glDeleteTextures", original),
        instrumentObjects<Resource::Buffer>(function, "glGenBuffers", "glDeleteBuffers", original),
        instrumentObjects<Resource::VertexArray>(function, "glGenVertexArrays", "glDeleteVertexArrays", original),
        instrumentObjects<Resource::Framebuffer>(function, "glGenFramebuffers", "glDeleteFramebuffers", original),
        instrumentObjects<Resource::Renderbuffer>(function, "glGenRenderbuffers", "glDeleteRenderbuffers", original),
    };
    for (const auto wrapper : objectWrappers) {
        if (wrapper != nullptr) {
            return wrapper;
        }
    }
    if (function == "glCreateShader") {
        createShader = reinterpret_cast<PFNGLCREATESHADERPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&createTrackedShader);
    }
    if (function == "glCreateProgram") {
        createProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&createTrackedProgram);
    }
    if (function == "glDeleteShader") {
        deleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&deleteTrackedShader);
    }
    if (function == "glDeleteProgram") {
        deleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(original);
        return reinterpret_cast<SDL_FunctionPointer>(&deleteTrackedProgram);
    }
    return original;
}

extern "C" bool __wrap_SDL_GL_DestroyContext(SDL_GLContext context) {
    if (!__real_SDL_GL_DestroyContext(context)) {
        std::cerr << "SDL_GL_DestroyContext failed: " << SDL_GetError() << '\n';
        return false;
    }
    const auto counts = resourceLog.contexts.find(context);
    if (counts != resourceLog.contexts.end()) {
        for (std::size_t index = 0; index < resourceCount; ++index) {
            resourceLog.liveAtDestroy[index] += counts->second[index].live.size();
        }
        resourceLog.contexts.erase(counts);
        ++resourceLog.destroyedContexts;
    }
    return true;
}

int main() {
    try {
        {
            const huynity::Engine probe(makeConfig());
            if (!probe.window().hasGpuSurface()) {
                std::cerr << "SKIP: native OpenGL context unavailable\n";
                return 77;
            }
        }
        resourceLog = {};
        std::cout << "scenario,resource,created,deleted,live_at_context_destroy,invalid_deletes\n";
        resizeCaptureTargets();
        bool passed = report("resize");
        exerciseRelease(Release::Stop);
        passed = report("stop") && passed;
        exerciseRelease(Release::Destructor);
        passed = report("destructor") && passed;
        exerciseRelease(Release::MoveAssignment);
        passed = report("move_assignment") && passed;
        exerciseRelease(Release::DirectClose);
        // При прямом закрытии окна объекты первого рендерера освобождает контекст.
        passed = report("direct_window_close", false) && passed;
        replaceTextureAtReusedAddress();
        passed = report("reused_texture_address") && passed;
        passed = preserveBlitOnClose() && passed;
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
