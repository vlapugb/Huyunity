// Тесты OpenGL-рендерера. Проверки загрузчика функций не требуют дисплея;
// кадровые проверки создают скрытый GLX-pbuffer контекст через dlopen и явно
// пропускаются (код 77), если GL-дисплей недоступен.

#include "huynity/renderer/gl_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gl_functions.h"

namespace render = huynity::render_contracts;
namespace image = huynity::image;
namespace math = huynity::math;
namespace renderer = huynity::renderer;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

namespace {

// --- Проверки загрузчика функций без дисплея ---

void testLoaderRejectsNullResolver() {
    renderer::detail::GlFunctions functions;
    CHECK(!renderer::detail::loadGlFunctions(functions, nullptr));
}

void* emptyResolver(const char*) {
    return nullptr;
}

char g_fakeStorage[8];
std::vector<std::string> g_requestedNames;

void* recordingResolver(const char* name) {
    g_requestedNames.emplace_back(name);
    return g_fakeStorage;
}

void* partialResolver(const char* name) {
    if (std::strcmp(name, "glCreateShader") == 0) {
        return nullptr;
    }
    return g_fakeStorage;
}

void testLoaderRejectsMissingEntryPoints() {
    renderer::detail::GlFunctions functions;
    CHECK(!renderer::detail::loadGlFunctions(functions, &emptyResolver));
}

void testLoaderPopulatesAllEntryPoints() {
    g_requestedNames.clear();
    renderer::detail::GlFunctions functions;
    CHECK(renderer::detail::loadGlFunctions(functions, &recordingResolver));
    CHECK(functions.createShader != nullptr);
    CHECK(functions.genFramebuffers != nullptr);
    CHECK(functions.uniformMatrix4fv != nullptr);
    CHECK(functions.texImage2D != nullptr);
    CHECK(functions.readPixels != nullptr);
    CHECK(g_requestedNames.size() >= 50);
    std::vector<std::string> unique = g_requestedNames;
    std::sort(unique.begin(), unique.end());
    CHECK(std::adjacent_find(unique.begin(), unique.end()) == unique.end());
}

void testLoaderFailureLeavesPartialTable() {
    renderer::detail::GlFunctions functions;
    CHECK(!renderer::detail::loadGlFunctions(functions, &partialResolver));
}

void testGlRendererRejectsNullResolver() {
    bool thrown = false;
    try {
        renderer::GlRenderer glRenderer(nullptr);
        (void)glRenderer;
    } catch (const std::exception&) {
        thrown = true;
    }
    CHECK(thrown);
}

// --- GLX-pbuffer контекст через dlopen (без зависимостей сборки от X11) ---

constexpr int glxDrawableType = 0x8010;
constexpr int glxRenderType = 0x8011;
constexpr int glxRgbaBit = 0x00000001;
constexpr int glxPbufferBit = 0x00000004;
constexpr int glxPbufferWidth = 0x8041;
constexpr int glxPbufferHeight = 0x8042;
constexpr int glxDepthSize = 12;
constexpr int glxContextMajorVersion = 0x2091;
constexpr int glxContextMinorVersion = 0x2092;
constexpr int glxContextProfileMask = 0x9126;
constexpr int glxContextCoreProfileBit = 0x00000001;
constexpr int glxNone = 0;

class GlxContext {
public:
    static std::unique_ptr<GlxContext> create() {
        void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        void* glx = dlopen("libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
        if (glx == nullptr) {
            glx = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        }
        if (x11 == nullptr || glx == nullptr) {
            if (x11 != nullptr) {
                dlclose(x11);
            }
            if (glx != nullptr) {
                dlclose(glx);
            }
            return nullptr;
        }
        auto context = std::unique_ptr<GlxContext>(new GlxContext(x11, glx));
        if (!context->initialize()) {
            return nullptr;
        }
        g_activeContext = context.get();
        return context;
    }

    ~GlxContext() {
        if (m_display != nullptr) {
            m_glxMakeCurrent(m_display, 0, nullptr);
        }
        if (m_display != nullptr && m_pbuffer != 0) {
            m_glxDestroyPbuffer(m_display, m_pbuffer);
        }
        if (m_display != nullptr && m_context != nullptr) {
            m_glxDestroyContext(m_display, m_context);
        }
        if (m_display != nullptr) {
            m_xCloseDisplay(m_display);
        }
        if (m_x11 != nullptr) {
            dlclose(m_x11);
        }
        if (m_glx != nullptr) {
            dlclose(m_glx);
        }
    }

    GlxContext(const GlxContext&) = delete;
    GlxContext& operator=(const GlxContext&) = delete;

    /// @brief Возвращает resolver entry points для huynity::renderer::GlRenderer.
    [[nodiscard]] renderer::GlRenderer::GlProcResolver procResolver() const noexcept { return &resolveProc; }

    [[nodiscard]] std::size_t uniformLookupCount() const noexcept { return m_uniformLookupCount; }

private:
    using XOpenDisplayFn = void* (*)(const char*);
    using XCloseDisplayFn = int (*)(void*);
    using GlxChooseFbConfigFn = void** (*)(void*, int, const int*, int*);
    using GlxCreatePbufferFn = unsigned long (*)(void*, void*, const int*);
    using GlxDestroyPbufferFn = void (*)(void*, unsigned long);
    using GlxCreateContextAttribsFn = void* (*)(void*, void*, void*, int, const int*);
    using GlxDestroyContextFn = void (*)(void*, void*);
    using GlxMakeCurrentFn = int (*)(void*, unsigned long, void*);
    using GlxGetProcAddressFn = void* (*)(const unsigned char*);

    static GLint APIENTRY countUniformLookup(GLuint program, const GLchar* name) {
        ++g_activeContext->m_uniformLookupCount;
        return g_activeContext->m_getUniformLocation(program, name);
    }

    static void* resolveProc(const char* name) {
        auto* address = g_activeContext->m_glxGetProcAddress(reinterpret_cast<const unsigned char*>(name));
        if (address != nullptr && std::strcmp(name, "glGetUniformLocation") == 0) {
            g_activeContext->m_getUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(address);
            return reinterpret_cast<void*>(&countUniformLookup);
        }
        return address;
    }

    GlxContext(void* x11, void* glx) : m_x11(x11), m_glx(glx) {}

    bool initialize() {
        const auto xOpenDisplay = reinterpret_cast<XOpenDisplayFn>(dlsym(m_x11, "XOpenDisplay"));
        m_xCloseDisplay = reinterpret_cast<XCloseDisplayFn>(dlsym(m_x11, "XCloseDisplay"));
        const auto glxChooseFbConfig = reinterpret_cast<GlxChooseFbConfigFn>(dlsym(m_glx, "glXChooseFBConfig"));
        const auto glxCreatePbuffer = reinterpret_cast<GlxCreatePbufferFn>(dlsym(m_glx, "glXCreatePbuffer"));
        m_glxDestroyPbuffer = reinterpret_cast<GlxDestroyPbufferFn>(dlsym(m_glx, "glXDestroyPbuffer"));
        // GLX_ARB_create_context экспортируется не как обычный символ, а только через
        // glXGetProcAddressARB, поэтому сначала резолвим базовый доступ к entry points.
        const auto glxGetProcAddressRaw = reinterpret_cast<GlxGetProcAddressFn>(dlsym(m_glx, "glXGetProcAddressARB"));
        if (glxGetProcAddressRaw == nullptr) {
            return false;
        }
        const auto glxCreateContextAttribs = reinterpret_cast<GlxCreateContextAttribsFn>(
            glxGetProcAddressRaw(reinterpret_cast<const unsigned char*>("glXCreateContextAttribsARB")));
        m_glxDestroyContext = reinterpret_cast<GlxDestroyContextFn>(dlsym(m_glx, "glXDestroyContext"));
        m_glxMakeCurrent = reinterpret_cast<GlxMakeCurrentFn>(dlsym(m_glx, "glXMakeCurrent"));
        m_glxGetProcAddress = reinterpret_cast<GlxGetProcAddressFn>(dlsym(m_glx, "glXGetProcAddressARB"));
        if (xOpenDisplay == nullptr || m_xCloseDisplay == nullptr || glxChooseFbConfig == nullptr ||
            glxCreatePbuffer == nullptr || m_glxDestroyPbuffer == nullptr || glxCreateContextAttribs == nullptr ||
            m_glxDestroyContext == nullptr || m_glxMakeCurrent == nullptr || m_glxGetProcAddress == nullptr) {
            return false;
        }
        m_display = xOpenDisplay(nullptr);
        if (m_display == nullptr) {
            return false;
        }
        // Ограничения разрядности отсекают вспомогательные конфигурации без цвета/глубины.
        const int fbAttribs[] = {
            glxDrawableType, glxPbufferBit, glxRenderType, glxRgbaBit, 8, 8, 9, 8, 10, 8, 11, 8, glxDepthSize, 24,
            glxNone};
        const int pbufferAttribs[] = {glxPbufferWidth, 64, glxPbufferHeight, 64, glxNone};
        const int contextAttribs[] = {
            glxContextMajorVersion,   3,      glxContextMinorVersion, 3, glxContextProfileMask,
            glxContextCoreProfileBit, glxNone};
        int configCount = 0;
        void** configs = glxChooseFbConfig(m_display, 0, fbAttribs, &configCount);
        if (configs == nullptr || configCount == 0) {
            return false;
        }
        m_pbuffer = glxCreatePbuffer(m_display, configs[0], pbufferAttribs);
        if (m_pbuffer == 0) {
            return false;
        }
        m_context = glxCreateContextAttribs(m_display, configs[0], nullptr, 1, contextAttribs);
        if (m_context == nullptr) {
            return false;
        }
        return m_glxMakeCurrent(m_display, m_pbuffer, m_context) != 0;
    }

    static GlxContext* g_activeContext;

    void* m_x11{};
    void* m_glx{};
    void* m_display{};
    void* m_context{};
    unsigned long m_pbuffer{};
    XCloseDisplayFn m_xCloseDisplay{};
    GlxDestroyPbufferFn m_glxDestroyPbuffer{};
    GlxDestroyContextFn m_glxDestroyContext{};
    GlxMakeCurrentFn m_glxMakeCurrent{};
    GlxGetProcAddressFn m_glxGetProcAddress{};
    PFNGLGETUNIFORMLOCATIONPROC m_getUniformLocation{};
    std::size_t m_uniformLookupCount{};
};

GlxContext* GlxContext::g_activeContext = nullptr;

// --- Построение контрольных снимков ---

render::Color color(double red, double green, double blue, double alpha = 1) {
    return {red, green, blue, alpha};
}

struct Mesh3 {
    std::vector<math::Point3> positions;
    std::vector<render::TriangleIndices> triangles;
    std::shared_ptr<const void> owner = std::make_shared<const int>(0);
};

struct Mesh2 {
    std::vector<math::Point2> positions;
    std::vector<render::TriangleIndices> triangles;
    std::shared_ptr<const void> owner = std::make_shared<const int>(0);
};

// Лицевая грань: нормаль cross(b - a, c - a) направлена в -z (к камере).
Mesh3 frontTriangle(double size, double z) {
    Mesh3 mesh;
    mesh.positions = {{0, size, z}, {size, -size, z}, {-size, -size, z}};
    mesh.triangles = {{0, 1, 2}};
    return mesh;
}

Mesh3 screenQuad(double halfExtent, double z) {
    Mesh3 mesh;
    mesh.positions = {{-halfExtent, halfExtent, z},
                      {halfExtent, halfExtent, z},
                      {halfExtent, -halfExtent, z},
                      {-halfExtent, -halfExtent, z}};
    // Намотка с нормалью в -z (лицевая по соглашению рендерера).
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    return mesh;
}

render::MeshResource3 resource3(const Mesh3& mesh, std::uint64_t resourceId, std::uint64_t version) {
    render::MeshResource3 resource;
    resource.owner = mesh.owner;
    resource.positions = mesh.positions;
    resource.triangles = std::as_bytes(std::span{mesh.triangles});
    resource.resourceId = resourceId;
    resource.version = version;
    return resource;
}

std::shared_ptr<const render::Material> makeMaterial(render::Color baseColor) {
    return std::make_shared<const render::Material>(baseColor);
}

render::Lighting3 ambientLighting(double intensity) {
    return {{render::Color{1, 1, 1, 1}, intensity}, {}};
}

int channel(const image::ImageBuffer& buffer, std::size_t x, std::size_t y, std::size_t offset) {
    return std::to_integer<int>(buffer.bytes()[y * buffer.stride() + x * 4 + offset]);
}

bool nearColor(const image::ImageBuffer& buffer, std::size_t x, std::size_t y, render::Color expected,
               int tolerance = 8) {
    const int wanted[3] = {static_cast<int>(std::lround(expected.red * 255)),
                           static_cast<int>(std::lround(expected.green * 255)),
                           static_cast<int>(std::lround(expected.blue * 255))};
    for (std::size_t c = 0; c < 3; ++c) {
        if (std::abs(channel(buffer, x, y, c) - wanted[c]) > tolerance) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    testLoaderRejectsNullResolver();
    testLoaderRejectsMissingEntryPoints();
    testLoaderPopulatesAllEntryPoints();
    testLoaderFailureLeavesPartialTable();
    testGlRendererRejectsNullResolver();

    auto context = GlxContext::create();
    if (context == nullptr) {
        std::cout << "SKIP: OpenGL display is not available, frame tests skipped\n";
        return 77;
    }
    const auto resolver = context->procResolver();

    renderer::detail::GlFunctions gl;
    CHECK(renderer::detail::loadGlFunctions(gl, resolver));
    try {
        // Контрольный кадр: clearColor покрывает весь readback.
        {
            renderer::GlRenderer glRenderer(resolver);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0.2, 0.4, 0.6);
            int previousDraw = 0;
            int previousRead = 0;
            gl.getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
            gl.getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
            auto captured = glRenderer.capture(snapshot);
            CHECK(captured.has_value());
            for (const auto size : {64, 32}) {
                snapshot.imageWidth = size;
                snapshot.imageHeight = size;
                CHECK(glRenderer.capture(snapshot).has_value());
                int draw = 0;
                int read = 0;
                gl.getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                gl.getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                CHECK(draw == previousDraw);
                CHECK(read == previousRead);
            }
            CHECK(nearColor(**captured, 0, 0, snapshot.clearColor));
            CHECK(nearColor(**captured, 63, 63, snapshot.clearColor));
        }

        // Flat triangle под белым ambient даёт цвет материала.
        {
            const auto lookupsBeforeLink = context->uniformLookupCount();
            renderer::GlRenderer glRenderer(resolver);
            const auto lookupsAfterLink = context->uniformLookupCount();
            CHECK(lookupsAfterLink > lookupsBeforeLink);
            Mesh3 mesh = frontTriangle(0.7, 5);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {makeMaterial(color(0.9, 0.1, 0.1))};
            snapshot.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting = ambientLighting(1.0);
            pass.drawItems.push_back({0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, color(0.9, 0.1, 0.1)));
            CHECK(context->uniformLookupCount() == lookupsAfterLink);
        }

        // Depth test: ближний треугольник перекрывает дальний независимо от порядка.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 nearMesh = frontTriangle(0.9, 3);
            Mesh3 farMesh = frontTriangle(0.9, 8);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {makeMaterial(color(1, 0, 0)), makeMaterial(color(0, 0, 1))};
            snapshot.resources3 = {resource3(nearMesh, 0, 0), resource3(farMesh, 0, 0)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting = ambientLighting(1.0);
            pass.drawItems.push_back({1, math::Transform3{}, 1, true, false, render::ShadingModel::Flat, true, true});
            pass.drawItems.push_back({0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, color(1, 0, 0)));
            farMesh.triangles[0].c = 99;
            snapshot.resources3[1] = resource3(farMesh, 0, 0);
            CHECK(glRenderer.render(snapshot).error() == renderer::RenderError::InvalidResource);
        }

        // Текстурированный unlit-треугольник даёт цвет текселя.
        {
            renderer::GlRenderer glRenderer(resolver);
            auto textureStorage = std::make_unique<image::ImageBuffer>(
                *image::ImageBuffer::create({.width = 2, .height = 2, .stride = 9}));
            for (std::size_t y = 0; y < 2; ++y) {
                for (std::size_t x = 0; x < 2; ++x) {
                    auto* pixel = textureStorage->bytes().data() + y * textureStorage->stride() + x * 4;
                    pixel[0] = std::byte{10};
                    pixel[1] = std::byte{200};
                    pixel[2] = std::byte{30};
                    pixel[3] = std::byte{255};
                }
            }
            auto material = std::make_shared<render::Material>();
            material->color = color(1, 1, 1);
            material->texture = std::shared_ptr<const image::ImageBuffer>(std::move(textureStorage));
            material->unlit = true;

            Mesh3 mesh = frontTriangle(0.7, 5);
            render::MeshResource3 resource = resource3(mesh, 1, 1);
            static const std::array<math::Point2, 3> uvs{math::Point2{0, 0}, math::Point2{1, 0}, math::Point2{0.5, 1}};
            resource.texCoords = std::span<const math::Point2>{uvs};

            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {std::shared_ptr<const render::Material>(material)};
            snapshot.resources3 = {resource};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.drawItems.push_back({0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, color(10.0 / 255, 200.0 / 255, 30.0 / 255)));
            // Общая текстура сохраняет настройки каждого материала, включая смену между кадрами.
            const auto getTextureParameter =
                reinterpret_cast<PFNGLGETTEXPARAMETERIVPROC>(resolver("glGetTexParameteriv"));
            CHECK(getTextureParameter != nullptr);
            auto smoothMaterial = std::make_shared<render::Material>(*material);
            smoothMaterial->filter = render::TextureFilter::Bilinear;
            smoothMaterial->wrap = render::TextureWrap::Repeat;
            snapshot.materials.push_back(smoothMaterial);
            std::get<render::World3DPass>(snapshot.passes[0])
                .drawItems.push_back({0, math::Transform3{}, 1, true, false, render::ShadingModel::Flat, true, true});
            CHECK(glRenderer.capture(snapshot).has_value());
            int filter = 0;
            int wrap = 0;
            getTextureParameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
            getTextureParameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
            CHECK(filter == GL_LINEAR);
            CHECK(wrap == GL_REPEAT);
            smoothMaterial->filter = render::TextureFilter::Nearest;
            smoothMaterial->wrap = render::TextureWrap::Clamp;
            CHECK(glRenderer.capture(snapshot).has_value());
            getTextureParameter(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
            getTextureParameter(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
            CHECK(filter == GL_NEAREST);
            CHECK(wrap == GL_CLAMP_TO_EDGE);
        }

        // Wireframe не заполняет внутренность треугольника.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 mesh = frontTriangle(0.7, 5);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0.05, 0.05, 0.05);
            snapshot.materials = {makeMaterial(color(1, 1, 1))};
            snapshot.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting = ambientLighting(1.0);
            pass.drawItems.push_back({0, math::Transform3{}, 0, true, true, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, snapshot.clearColor));
        }

        // Процедурное небо: верх кадра — zenith, низ — ground.
        {
            renderer::GlRenderer glRenderer(resolver);
            const auto lookupsAfterLink = context->uniformLookupCount();
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            render::World3DPass pass;
            render::PerspectiveCamera3 camera;
            camera.verticalFieldOfView = std::numbers::pi / 2;
            camera.nearPlane = 0.1;
            camera.farPlane = 100;
            pass.camera = camera;
            pass.lighting.sky.enabled = true;
            pass.lighting.sky.zenithColor = color(0.1, 0.1, 0.8);
            pass.lighting.sky.horizonColor = color(0.9, 0.9, 0.9);
            pass.lighting.sky.groundColor = color(0.8, 0.2, 0.2);
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(channel(**frame, 32, 2, 2) > channel(**frame, 32, 2, 0) + 60);
            CHECK(channel(**frame, 32, 62, 0) > channel(**frame, 32, 62, 2) + 60);
            CHECK(context->uniformLookupCount() == lookupsAfterLink);
        }

        // HUD: source-over alpha blending и стабильный порядок (layer, order).
        {
            renderer::GlRenderer glRenderer(resolver);
            const auto lookupsAfterLink = context->uniformLookupCount();
            Mesh2 quad;
            quad.positions = {{4, 4}, {60, 4}, {60, 60}, {4, 60}};
            quad.triangles = {{0, 1, 2}, {0, 2, 3}};
            render::MeshResource2 resource;
            resource.owner = quad.owner;
            resource.positions = quad.positions;
            resource.triangles = std::as_bytes(std::span{quad.triangles});
            resource.resourceId = 1;
            resource.version = 1;

            auto opaqueGreen = std::make_shared<render::Material>();
            opaqueGreen->color = color(0, 1, 0, 1);
            auto halfRed = std::make_shared<render::Material>();
            halfRed->color = color(1, 0, 0, 0.5);

            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {std::shared_ptr<const render::Material>(opaqueGreen),
                                  std::shared_ptr<const render::Material>(halfRed)};
            snapshot.resources2 = {resource};
            render::Hud2DPass pass;
            // Несмотря на обратный порядок подачи, layer 1 (красный 50%) рисуется поверх.
            pass.drawItems.push_back({0, math::Transform2{}, 1, 1, 0, false});
            pass.drawItems.push_back({0, math::Transform2{}, 0, 0, 0, false});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, color(0.5, 0.5, 0), 12));
            CHECK(context->uniformLookupCount() == lookupsAfterLink);
        }

        // Направленная тень: область за caster по направлению света темнеет
        // освещённой области. Свет (0.25, 0.25, 1) смещает тень quad-кастера
        // на (~1, ~1) мировых единиц по экрану от его силуэта.
        {
            renderer::GlRenderer glRenderer(resolver);
            const auto lookupsAfterLink = context->uniformLookupCount();
            Mesh3 caster = screenQuad(0.5, 6);
            Mesh3 receiver = screenQuad(4, 10);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {makeMaterial(color(1, 1, 1))};
            snapshot.resources3 = {resource3(caster, 1, 1), resource3(receiver, 2, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting.ambient = {render::Color{1, 1, 1, 1}, 0.35};
            pass.lighting.directional.push_back({math::Vector3{{0.25, 0.25, 1}}, render::Color{1, 1, 1, 1}, 0.9});
            pass.lighting.shadows.enabled = true;
            pass.lighting.shadows.resolution = 512;
            pass.lighting.shadows.extent = 8;
            pass.lighting.shadows.center = math::Point3{{0, 0, 0}};
            pass.lighting.shadows.depthRange = 40;
            pass.lighting.shadows.bias = 0.06;
            pass.lighting.shadows.minVisibility = 0.2;
            pass.drawItems.push_back({1, math::Transform3{}, 0, false, false, render::ShadingModel::Flat, false, true});
            pass.drawItems.push_back({0, math::Transform3{}, 0, false, false, render::ShadingModel::Flat, true, false});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            // Мировая точка тени (~1, ~1, 10) -> экранный пиксель около (48, 16);
            // контрольная освещённая точка (-1.5, -1.5, 10) -> пиксель около (8, 56).
            CHECK(channel(**frame, 48, 16, 0) < channel(**frame, 8, 56, 0) - 25);
            // Камера меняет экранные координаты, но не освещённость мировых точек.
            auto& world = std::get<render::World3DPass>(snapshot.passes[0]);
            const auto movedView = math::compose(math::translation(math::Vector3{{0.25, 0.125, 1}}),
                                                 math::rotation(math::RotationAxis::Z, std::numbers::pi / 2));
            std::get<render::OrthographicCamera3>(world.camera).worldToView = movedView;
            auto moved = glRenderer.capture(snapshot);
            CHECK(moved.has_value());
            for (const auto point : {math::Point3{{1, 1, 10}}, math::Point3{{-1.5, -1.5, 10}}}) {
                const auto view = math::transformPoint(movedView, point);
                const auto x = static_cast<std::size_t>((view[0] / 4 + 0.5) * 64);
                const auto y = static_cast<std::size_t>((0.5 - view[1] / 4) * 64);
                const auto originalX = static_cast<std::size_t>((point[0] / 4 + 0.5) * 64);
                const auto originalY = static_cast<std::size_t>((0.5 - point[1] / 4) * 64);
                CHECK(std::abs(channel(**moved, x, y, 0) - channel(**frame, originalX, originalY, 0)) < 12);
            }
            // Center задаёт середину объёма, поэтому caster перед центром тоже отбрасывает тень.
            std::get<render::OrthographicCamera3>(world.camera).worldToView = math::Transform3{};
            world.lighting.shadows.center = {{0, 0, 10}};
            auto centered = glRenderer.capture(snapshot);
            CHECK(centered.has_value());
            CHECK(channel(**centered, 48, 16, 0) < channel(**centered, 8, 56, 0) - 25);
            CHECK(context->uniformLookupCount() == lookupsAfterLink);
        }

        // Гуро: сглаженные вершинные нормали освещаются в вершинном шейдере.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 mesh = frontTriangle(0.7, 5);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {makeMaterial(color(1, 1, 1))};
            snapshot.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting.ambient = {render::Color{1, 1, 1, 1}, 0.3};
            pass.lighting.directional.push_back({math::Vector3{{0, 0, 1}}, render::Color{1, 1, 1, 1}, 0.7});
            pass.drawItems.push_back(
                {0, math::Transform3{}, 0, true, false, render::ShadingModel::Gouraud, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            CHECK(nearColor(**frame, 32, 32, color(1, 1, 1), 16));
        }

        // Дистанционная дымка: дальний unlit объект окрашивается в цвет тумана.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 mesh = frontTriangle(0.7, 5);
            auto fogMaterial = std::make_shared<render::Material>();
            fogMaterial->color = color(1, 0, 0);
            fogMaterial->unlit = true;
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.clearColor = color(0, 0, 0);
            snapshot.materials = {std::shared_ptr<const render::Material>(fogMaterial)};
            snapshot.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.lighting.fog.enabled = true;
            pass.lighting.fog.color = color(0, 0, 1);
            pass.lighting.fog.start = 1;
            pass.lighting.fog.end = 8;
            pass.drawItems.push_back({0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            auto frame = glRenderer.capture(snapshot);
            CHECK(frame.has_value());
            // smoothstep(1, 8, 5) ~= 0.6: красный канал падает, синий растёт.
            const int red = channel(**frame, 32, 32, 0);
            const int blue = channel(**frame, 32, 32, 2);
            CHECK(blue > red + 30);
            CHECK(red > 40);
        }

        // Солнце закреплено в мире: его экранная позиция следует проекции при наклоне камеры.
        {
            renderer::GlRenderer glRenderer(resolver);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 128;
            snapshot.imageHeight = 128;
            render::World3DPass pass;
            pass.camera = render::PerspectiveCamera3{math::Transform3{}, std::numbers::pi / 2, 0.1, 100};
            pass.lighting.directional.push_back({{{0, 0, -1}}, color(1, 1, 1), 1});
            pass.lighting.sky.enabled = true;
            pass.lighting.sky.zenithColor = color(0, 0, 0);
            pass.lighting.sky.horizonColor = color(0, 0, 0);
            pass.lighting.sky.groundColor = color(0, 0, 0);
            pass.lighting.sky.sunColor = color(1, 1, 1);
            pass.lighting.sky.sunGlow = 0;
            snapshot.passes.push_back(pass);
            auto initial = glRenderer.capture(snapshot);
            CHECK(initial.has_value());
            CHECK(channel(**initial, 64, 64, 0) > 220);
            for (const double pitch : {-0.3, 0.3}) {
                auto& camera =
                    std::get<render::PerspectiveCamera3>(std::get<render::World3DPass>(snapshot.passes[0]).camera);
                camera.worldToView = math::compose(math::rotation(math::RotationAxis::X, pitch),
                                                   math::translation(math::Vector3{{2, 1, -3}}));
                const auto towardSun = math::transformVector(camera.worldToView, math::Vector3{{0, 0, 1}});
                const auto y = static_cast<std::size_t>((0.5 - towardSun[1] / towardSun[2] * 0.5) * 128);
                auto tilted = glRenderer.capture(snapshot);
                CHECK(tilted.has_value());
                CHECK(channel(**tilted, 64, y, 0) > 220);
                CHECK(channel(**tilted, 64, 64, 0) < 30);
            }
        }

        // Ошибочные ссылки возвращают RenderError до отрисовки.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 mesh = frontTriangle(0.7, 5);
            render::RenderSnapshot snapshot;
            snapshot.imageWidth = 64;
            snapshot.imageHeight = 64;
            snapshot.materials = {makeMaterial(color(1, 0, 0))};
            snapshot.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass pass;
            pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            pass.drawItems.push_back({5, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            snapshot.passes.push_back(pass);
            CHECK(glRenderer.render(snapshot).error() == renderer::RenderError::InvalidResource);

            render::RenderSnapshot badMaterial;
            badMaterial.imageWidth = 64;
            badMaterial.imageHeight = 64;
            badMaterial.materials = {makeMaterial(color(1, 0, 0))};
            badMaterial.resources3 = {resource3(mesh, 1, 1)};
            render::World3DPass materialPass;
            materialPass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            materialPass.drawItems.push_back(
                {0, math::Transform3{}, 7, true, false, render::ShadingModel::Flat, true, true});
            badMaterial.passes.push_back(materialPass);
            CHECK(glRenderer.render(badMaterial).error() == renderer::RenderError::InvalidMaterial);

            Mesh3 broken = frontTriangle(0.7, 5);
            broken.triangles = {{0, 1, 9}};
            render::RenderSnapshot badIndex;
            badIndex.imageWidth = 64;
            badIndex.imageHeight = 64;
            badIndex.materials = {makeMaterial(color(1, 0, 0))};
            badIndex.resources3 = {resource3(broken, 1, 1)};
            render::World3DPass indexPass;
            indexPass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
            indexPass.drawItems.push_back(
                {0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
            badIndex.passes.push_back(indexPass);
            CHECK(glRenderer.render(badIndex).error() == renderer::RenderError::InvalidResource);
        }

        // Повторные кадры детерминированы; смена версии ресурса перезагружает буферы.
        {
            renderer::GlRenderer glRenderer(resolver);
            Mesh3 mesh = frontTriangle(0.7, 5);
            const auto buildSnapshot = [&mesh](std::uint64_t version) {
                render::RenderSnapshot snapshot;
                snapshot.imageWidth = 64;
                snapshot.imageHeight = 64;
                snapshot.clearColor = color(0, 0, 0);
                snapshot.materials = {makeMaterial(color(0.2, 0.7, 0.4))};
                snapshot.resources3 = {resource3(mesh, 1, version)};
                render::World3DPass pass;
                pass.camera = render::OrthographicCamera3{math::Transform3{}, 4, 4, 0.1, 100};
                pass.lighting = ambientLighting(1.0);
                pass.drawItems.push_back(
                    {0, math::Transform3{}, 0, true, false, render::ShadingModel::Flat, true, true});
                snapshot.passes.push_back(pass);
                return snapshot;
            };
            auto first = buildSnapshot(1);
            auto frameA = glRenderer.capture(first);
            auto second = buildSnapshot(1);
            auto frameB = glRenderer.capture(second);
            CHECK(frameA.has_value() && frameB.has_value());
            CHECK(std::equal((*frameA)->bytes().begin(), (*frameA)->bytes().end(), (*frameB)->bytes().begin()));
            auto third = buildSnapshot(2);
            auto frameC = glRenderer.capture(third);
            CHECK(frameC.has_value());
            CHECK(nearColor(**frameC, 32, 32, color(0.2, 0.7, 0.4)));
        }
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All OpenGL renderer tests passed\n";
    return 0;
}
