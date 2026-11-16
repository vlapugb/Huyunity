#include "huynity/renderer/gl_renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "native_window.h"

namespace window = huynity::adapters::window;
namespace image = huynity::image;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

namespace {

std::unique_ptr<image::ImageBuffer> makeBuffer(std::size_t width, std::size_t height, std::size_t stride = 0) {
    auto created = image::ImageBuffer::create({.width = width, .height = height, .stride = stride});
    if (!created) {
        throw std::runtime_error("Failed to create test ImageBuffer");
    }
    auto buffer = std::make_unique<image::ImageBuffer>(std::move(*created));
    for (std::size_t index = 0; index < buffer->sizeBytes(); ++index) {
        buffer->bytes()[index] = static_cast<std::byte>(index % 251);
    }
    return buffer;
}

void testLifecycle() {
    window::NativeWindow native({64, 48}, "SDL lifecycle");
    CHECK(native.isOpen());
    CHECK(native.size() == window::WindowSize{64, 48});
    native.setTitle("Updated title");
    native.close();
    CHECK(!native.isOpen());
    native.setTitle("Ignored title");
    native.close();
    CHECK(!native.isOpen());
}

void testPaddedPresent() {
    window::NativeWindow native({64, 48}, "SDL padded present");
    const auto size = native.size();
    const auto rowBytes = static_cast<std::size_t>(size.width) * 4;
    auto padded = makeBuffer(size.width, size.height, rowBytes + 1);
    padded = native.present(std::move(padded));
    using ReadTexture = void (*)(GLenum, GLint, GLenum, GLenum, void*);
    const auto readTexture = reinterpret_cast<ReadTexture>(native.glProcResolver()("glGetTexImage"));
    CHECK(readTexture != nullptr);
    std::vector<std::byte> pixels(rowBytes * size.height);
    readTexture(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (std::size_t row = 0; row < size.height; ++row) {
        CHECK(std::equal(pixels.begin() + row * rowBytes, pixels.begin() + (row + 1) * rowBytes,
                         padded->bytes().begin() + row * padded->stride()));
    }
    std::cout << "Padded native presentation passed\n";
}

void testPresent() {
    window::NativeWindow native({2, 2}, "SDL present");
    auto framebuffer = makeBuffer(2, 2, 12);
    const auto* original = framebuffer.get();
    const auto originalByte = framebuffer->bytes()[0];
    framebuffer = native.present(std::move(framebuffer));
    CHECK(framebuffer.get() == original);
    CHECK(framebuffer->bytes()[0] == originalByte);

    framebuffer = native.present(std::move(framebuffer));
    CHECK(framebuffer.get() == original);
    CHECK(native.present(nullptr) == nullptr);
    CHECK(native.hasGpuSurface());
    CHECK(native.makeCurrent());
    huynity::renderer::GlRenderer renderer{native.glProcResolver()};
    huynity::render_contracts::RenderSnapshot snapshot;
    snapshot.imageWidth = 2;
    snapshot.imageHeight = 2;
    snapshot.clearColor = {1.0, 0.0, 0.0, 1.0};
    auto captured = renderer.capture(snapshot);
    CHECK(captured.has_value());
    CHECK((*captured)->bytes()[0] == std::byte{255});
    CHECK((*captured)->bytes()[1] == std::byte{0});

    auto mismatched = makeBuffer(1, 2);
    const auto* mismatchedPointer = mismatched.get();
    mismatched = native.present(std::move(mismatched));
    CHECK(mismatched.get() == mismatchedPointer);

    native.close();
    framebuffer = native.present(std::move(framebuffer));
    CHECK(framebuffer.get() == original);
}

void testPollEvents() {
    window::NativeWindow native({64, 48}, "SDL events");
    std::vector<window::InputEvent> events;
    native.pollEvents(events);
    events.clear();
    auto initialFrame = makeBuffer(64, 48);
    initialFrame = native.present(std::move(initialFrame));

    SDL_Event foreign{};
    foreign.type = SDL_EVENT_KEY_DOWN;
    foreign.key.windowID = 999;
    foreign.key.scancode = SDL_SCANCODE_A;
    CHECK(SDL_PushEvent(&foreign));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_D;
    CHECK(SDL_PushEvent(&key));

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    resize.window.data1 = 80;
    resize.window.data2 = 50;
    CHECK(SDL_PushEvent(&resize));

    SDL_Event close{};
    close.type = SDL_EVENT_QUIT;
    CHECK(SDL_PushEvent(&close));

    native.pollEvents(events);
    CHECK(events.size() == 3);
    CHECK(std::holds_alternative<window::KeyPressedEvent>(events[0]));
    CHECK(std::get<window::KeyPressedEvent>(events[0]).key == window::KeyCode::D);
    CHECK(std::holds_alternative<window::WindowResizedEvent>(events[1]));
    CHECK(std::get<window::WindowResizedEvent>(events[1]).width == 80);
    CHECK(std::get<window::WindowResizedEvent>(events[1]).height == 50);
    CHECK(std::holds_alternative<window::WindowCloseRequestedEvent>(events[2]));
    CHECK(native.size() == window::WindowSize{80, 50});

    auto resizedFrame = makeBuffer(80, 50);
    resizedFrame = native.present(std::move(resizedFrame));
    CHECK(resizedFrame != nullptr);

    events.clear();
    native.pollEvents(events);
    events.clear();

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 8.0F;
    motion.motion.y = 8.0F;
    motion.motion.xrel = 4.0F;
    motion.motion.yrel = 2.0F;
    CHECK(SDL_PushEvent(&motion));
    native.pollEvents(events);
    CHECK(events.size() == 1);
    const auto moved = std::get<window::MouseMovedEvent>(events.front());
    CHECK(std::abs(moved.x - 10.0) < 0.0001);
    CHECK(std::abs(moved.y - (50.0 / 48.0 * 8.0)) < 0.0001);
    CHECK(std::abs(moved.deltaX - 5.0) < 0.0001);
    CHECK(std::abs(moved.deltaY - (50.0 / 48.0 * 2.0)) < 0.0001);
}

} // namespace

int main() {
    try {
        const auto* driver = SDL_GetCurrentVideoDriver();
        const auto* requestedDriver = SDL_getenv("SDL_VIDEO_DRIVER");
        const auto* legacyDriver = SDL_getenv("SDL_VIDEODRIVER");
        if ((driver != nullptr && std::string_view{driver} == "dummy") ||
            (requestedDriver != nullptr && std::string_view{requestedDriver} == "dummy") ||
            (legacyDriver != nullptr && std::string_view{legacyDriver} == "dummy")) {
            std::cout << "OpenGL native tests skipped on SDL dummy driver\n";
            return 0;
        }
        testPaddedPresent();
        testLifecycle();
        testPresent();
        testPollEvents();
        std::cout << "All SDL native window tests passed successfully!\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
