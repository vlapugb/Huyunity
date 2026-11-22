#include "huynity/adapters/window/headless_window.h"
#include "huynity/adapters/window/window_factory.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace window = huynity::adapters::window;
namespace image = huynity::image;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

namespace {

void testNativeWindowSupportQuery() {
#if defined(HUYNITY_HAS_NATIVE_WINDOW)
    CHECK(window::hasNativeWindowSupport());
#else
    CHECK(!window::hasNativeWindowSupport());
#endif
}

void testWindowConfigDefaults() {
    window::WindowConfig defaultConfig;
    CHECK(defaultConfig.size == window::WindowSize{.width = 800, .height = 600});
    CHECK(defaultConfig.title == "Huynity Window");
    CHECK(!defaultConfig.forceHeadless);

    window::WindowConfig sameConfig;
    CHECK(defaultConfig == sameConfig);

    window::WindowConfig modifiedConfig{
        .size = window::WindowSize{.width = 1024, .height = 768},
        .title = "Changed Title",
        .forceHeadless = true,
    };
    CHECK(defaultConfig != modifiedConfig);
}

void testDefaultCreateWindow() {
    auto win = window::createWindow();
    CHECK(win != nullptr);
    CHECK(win->isOpen());
    if (dynamic_cast<window::HeadlessWindow*>(win.get()) != nullptr) {
        CHECK(win->size() == window::WindowSize{.width = 800, .height = 600});
    } else {
        CHECK(win->size().width >= 800 && win->size().height >= 600);
    }

    std::vector<window::InputEvent> events;
    win->pollEvents(events);
    if (dynamic_cast<window::HeadlessWindow*>(win.get()) != nullptr) {
        CHECK(events.empty());
    } else {
        // При создании нативного окна композитор может одновременно доставить события ввода.
        const auto lastResize = std::find_if(events.crbegin(), events.crend(), [](const auto& event) {
            return std::holds_alternative<window::WindowResizedEvent>(event);
        });
        if (lastResize != events.crend()) {
            const auto& resize = std::get<window::WindowResizedEvent>(*lastResize);
            CHECK(win->size() == window::WindowSize{.width = resize.width, .height = resize.height});
        }
    }

    win->close();
    CHECK(!win->isOpen());
}

void testCustomCreateWindow() {
    const window::WindowConfig config{
        .size = window::WindowSize{.width = 1280, .height = 720},
        .title = "Custom App Window",
        .forceHeadless = false,
    };

    auto win = window::createWindow(config);
    CHECK(win != nullptr);
    CHECK(win->isOpen());
    if (dynamic_cast<window::HeadlessWindow*>(win.get()) != nullptr) {
        CHECK(win->size() == window::WindowSize{.width = 1280, .height = 720});
    } else {
        CHECK(win->size().width >= 1280 && win->size().height >= 720);
    }

    // In environment without native backend, transparent fallback to HeadlessWindow occurs
    if (!window::hasNativeWindowSupport()) {
        auto* headless = dynamic_cast<window::HeadlessWindow*>(win.get());
        CHECK(headless != nullptr);
        CHECK(headless->title() == "Custom App Window");
    }

    win->close();
    CHECK(!win->isOpen());
}

void testForceHeadlessCreateWindow() {
    const window::WindowConfig config{
        .size = window::WindowSize{.width = 640, .height = 480},
        .title = "Forced Headless Mode",
        .forceHeadless = true,
    };

    auto win = window::createWindow(config);
    CHECK(win != nullptr);
    CHECK(win->isOpen());
    CHECK(win->size() == window::WindowSize{.width = 640, .height = 480});

    // When forceHeadless is set, must strictly be a HeadlessWindow
    auto* headless = dynamic_cast<window::HeadlessWindow*>(win.get());
    CHECK(headless != nullptr);
    CHECK(headless->title() == "Forced Headless Mode");
    CHECK(headless->presentedCount() == 0);
    CHECK(headless->rejectedPresents() == 0);

    // Emulate event and poll via WindowAdapter interface
    headless->pushEvent(window::KeyPressedEvent{.key = window::KeyCode::Escape});
    std::vector<window::InputEvent> events;
    win->pollEvents(events);
    CHECK(events.size() == 1);
    CHECK(std::holds_alternative<window::KeyPressedEvent>(events[0]));

    // Present buffer
    auto bufferDesc = image::ImageBufferDesc{.width = 640, .height = 480};
    auto bufferRes = image::ImageBuffer::create(bufferDesc);
    CHECK(bufferRes.has_value());
    auto returned = win->present(std::make_unique<image::ImageBuffer>(std::move(*bufferRes)));
    CHECK(returned != nullptr);
    CHECK(headless->presentedCount() == 1);
    CHECK(headless->lastPresentedFrame() != nullptr);

    win->close();
    CHECK(!win->isOpen());
}

void testInvalidNativeSizeFallsBackToHeadless() {
    auto win = window::createWindow({.size = {0, 600}, .title = "Invalid native size"});
    CHECK(win != nullptr);
    auto* headless = dynamic_cast<window::HeadlessWindow*>(win.get());
    CHECK(headless != nullptr);
    CHECK(win->isOpen());
    win->close();
}

} // namespace

int main() {
    try {
        testNativeWindowSupportQuery();
        testWindowConfigDefaults();
        testDefaultCreateWindow();
        testCustomCreateWindow();
        testForceHeadlessCreateWindow();
        testInvalidNativeSizeFallsBackToHeadless();
        std::cout << "All window factory tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failed: " << ex.what() << '\n';
        return 1;
    }
}
