#pragma once

#include "huynity/renderer/software_renderer.h"

#include <expected>
#include <memory>

namespace huynity {
class Engine;
}

namespace huynity::renderer {

class GlRenderer {
public:
    using GlProcResolver = void* (*)(const char*);

    explicit GlRenderer(GlProcResolver resolver);
    ~GlRenderer();

    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;
    GlRenderer(GlRenderer&&) noexcept;
    GlRenderer& operator=(GlRenderer&&) noexcept;

    [[nodiscard]] std::expected<void, RenderError> render(const render_contracts::RenderSnapshot& snapshot);
    [[nodiscard]] std::expected<std::unique_ptr<image::ImageBuffer>, RenderError>
    capture(const render_contracts::RenderSnapshot& snapshot);

private:
    friend class huynity::Engine;

    struct Implementation;

    /// @brief Освобождает состояние CPU, когда контекст недоступен. GPU-объекты освобождает владелец контекста.
    void abandonContext() noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace huynity::renderer
