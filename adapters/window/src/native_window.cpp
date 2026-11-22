#include "native_window.h"

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "sdl_event_translation.h"

namespace huynity::adapters::window {
namespace {

void* resolveGlProcedure(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

[[nodiscard]] int sdlDimension(std::uint32_t value) {
    if (value == 0 || value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Window dimensions must be positive and fit SDL integer range");
    }
    return static_cast<int>(value);
}

[[nodiscard]] std::runtime_error sdlError(std::string_view operation) {
    return std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

[[nodiscard]] SDL_WindowID eventWindowId(const SDL_Event& event) noexcept {
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        return event.window.windowID;
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    default:
        return 0;
    }
}

[[nodiscard]] bool isForWindow(const SDL_Event& event, SDL_Window* window) noexcept {
    if (event.type == SDL_EVENT_QUIT) {
        return true;
    }
    const auto windowId = eventWindowId(event);
    return windowId == 0 || windowId == SDL_GetWindowID(window);
}

[[nodiscard]] PixelScale pixelScale(SDL_Window* window, WindowSize pixelSize) noexcept {
    int logicalWidth = 0;
    int logicalHeight = 0;
    if (!SDL_GetWindowSize(window, &logicalWidth, &logicalHeight) || logicalWidth <= 0 || logicalHeight <= 0 ||
        pixelSize.width == 0 || pixelSize.height == 0) {
        return {};
    }
    return {
        .x = static_cast<double>(pixelSize.width) / static_cast<double>(logicalWidth),
        .y = static_cast<double>(pixelSize.height) / static_cast<double>(logicalHeight),
    };
}

} // namespace

NativeWindow::NativeWindow(WindowSize size, std::string_view title) : m_size{size}, m_isOpen{true} {
    try {
        const int width = sdlDimension(size.width);
        const int height = sdlDimension(size.height);
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            throw sdlError("SDL_InitSubSystem(SDL_INIT_VIDEO) failed");
        }
        m_videoInitialized = true;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        m_window = SDL_CreateWindow(std::string(title).c_str(), width, height,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL);
        if (m_window == nullptr) {
            throw sdlError("SDL_CreateWindow(OpenGL) failed");
        }
        (void)SDL_SyncWindow(m_window);
        m_context = SDL_GL_CreateContext(m_window);
        if (m_context == nullptr) {
            throw sdlError("SDL_GL_CreateContext failed");
        }
        if (!SDL_GL_SetSwapInterval(1)) {
            std::cerr << "SDL_GL_SetSwapInterval(1) failed: " << SDL_GetError() << '\n';
        }

        int pixelWidth = 0;
        int pixelHeight = 0;
        if (!SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight)) {
            throw sdlError("SDL_GetWindowSizeInPixels failed");
        }
        m_size = WindowSize{
            .width = static_cast<std::uint32_t>(pixelWidth),
            .height = static_cast<std::uint32_t>(pixelHeight),
        };
    } catch (...) {
        close();
        throw;
    }
}

NativeWindow::~NativeWindow() {
    close();
}

bool NativeWindow::isOpen() const noexcept {
    return m_isOpen;
}

void NativeWindow::close() noexcept {
    m_isOpen = false;
    if (m_context != nullptr) {
        const auto hasBlitResources = m_blitTexture != 0 || m_blitVao != 0 || m_blitProgram != 0;
        if (hasBlitResources && SDL_GL_MakeCurrent(m_window, m_context)) {
            if (m_blitTexture != 0) {
                glDeleteTextures(1, &m_blitTexture);
            }
            if (m_blitVao != 0) {
                glDeleteVertexArrays(1, &m_blitVao);
            }
            if (m_blitProgram != 0) {
                glDeleteProgram(m_blitProgram);
            }
        }
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_videoInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_videoInitialized = false;
    }
}

WindowSize NativeWindow::size() const noexcept {
    return m_size;
}

void NativeWindow::setTitle(std::string_view title) {
    if (!m_isOpen || m_window == nullptr) {
        return;
    }
    const std::string terminatedTitle{title};
    if (!SDL_SetWindowTitle(m_window, terminatedTitle.c_str())) {
        std::cerr << "SDL_SetWindowTitle failed: " << SDL_GetError() << '\n';
    }
}

void NativeWindow::pollEvents(std::vector<InputEvent>& outEvents) {
    if (!m_isOpen || m_window == nullptr) {
        return;
    }

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (!isForWindow(event, m_window)) {
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            m_size = WindowSize{
                .width = static_cast<std::uint32_t>(std::max<Sint32>(event.window.data1, 0)),
                .height = static_cast<std::uint32_t>(std::max<Sint32>(event.window.data2, 0)),
            };
        }

        auto translated = translateSdlEvent(event, pixelScale(m_window, m_size));
        if (translated) {
            outEvents.push_back(*translated);
        }
    }
}

std::unique_ptr<image::ImageBuffer> NativeWindow::present(std::unique_ptr<image::ImageBuffer> framebuffer) {
    if (!framebuffer || !makeCurrent() || framebuffer->format() != image::PixelFormat::Rgba8Unorm ||
        framebuffer->width() != m_size.width || framebuffer->height() != m_size.height) {
        return framebuffer;
    }
    if (m_blitProgram == 0) {
        const char* vertexSource = R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
        const char* fragmentSource = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D image;
void main() { color = texture(image, vec2(uv.x, 1.0 - uv.y)); })";
        const auto vertex = glCreateShader(GL_VERTEX_SHADER);
        const auto fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(vertex, 1, &vertexSource, nullptr);
        glShaderSource(fragment, 1, &fragmentSource, nullptr);
        glCompileShader(vertex);
        glCompileShader(fragment);
        m_blitProgram = glCreateProgram();
        glAttachShader(m_blitProgram, vertex);
        glAttachShader(m_blitProgram, fragment);
        glLinkProgram(m_blitProgram);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        glGetProgramiv(m_blitProgram, GL_LINK_STATUS, &linked);
        if (linked == 0) {
            std::cerr << "OpenGL software present shader link failed\n";
            glDeleteProgram(m_blitProgram);
            m_blitProgram = 0;
            return framebuffer;
        }
        glGenVertexArrays(1, &m_blitVao);
        glGenTextures(1, &m_blitTexture);
        glBindTexture(GL_TEXTURE_2D, m_blitTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glViewport(0, 0, static_cast<GLsizei>(m_size.width), static_cast<GLsizei>(m_size.height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, m_blitTexture);
    const auto packedStride = framebuffer->width() * 4;
    std::vector<std::byte> packed;
    const auto* pixels = framebuffer->bytes().data();
    if (framebuffer->stride() % 4 != 0) {
        packed.resize(packedStride * framebuffer->height());
        for (std::size_t row = 0; row < framebuffer->height(); ++row) {
            std::memcpy(packed.data() + row * packedStride, pixels + row * framebuffer->stride(), packedStride);
        }
        pixels = packed.data();
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                  static_cast<GLint>((packed.empty() ? framebuffer->stride() : packedStride) / 4));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_size.width), static_cast<GLsizei>(m_size.height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glUseProgram(m_blitProgram);
    glBindVertexArray(m_blitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    swapBuffers();
    return framebuffer;
}

bool NativeWindow::makeCurrent() noexcept {
    return m_isOpen && m_context != nullptr && SDL_GL_MakeCurrent(m_window, m_context);
}

void NativeWindow::swapBuffers() noexcept {
    if (m_isOpen && m_context != nullptr) {
        SDL_GL_SwapWindow(m_window);
    }
}

WindowAdapter::GlProcResolver NativeWindow::glProcResolver() const noexcept {
    return m_context == nullptr ? nullptr : &resolveGlProcedure;
}

} // namespace huynity::adapters::window
