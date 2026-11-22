#include "huynity/renderer/software_renderer.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>

int main(int argc, char**) {
    constexpr std::size_t width = 1920;
    constexpr std::size_t height = 1080;
    const std::size_t padding = argc > 1 ? 1 : 0;
    auto framebuffer =
        huynity::image::ImageBuffer::create({.width = width, .height = height, .stride = width * 4 + padding});
    huynity::render_contracts::RenderSnapshot snapshot{};
    snapshot.imageWidth = width;
    snapshot.imageHeight = height;
    snapshot.clearColor = {0.2, 0.4, 0.6, 1};
    huynity::renderer::SoftwareRenderer renderer;
    if (!framebuffer) {
        return EXIT_FAILURE;
    }
    for (int frame = -20; frame < 101; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = renderer.render(snapshot, *framebuffer);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (!result) {
            return EXIT_FAILURE;
        }
        if (frame >= 0) {
            std::cout << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() << '\n';
        }
    }
}
