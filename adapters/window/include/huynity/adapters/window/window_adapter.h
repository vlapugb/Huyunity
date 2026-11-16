#pragma once

#include "huynity/adapters/window/input_events.h"
#include "huynity/image/image_buffer.h"
#include "huynity/runtime/presentation_port.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace huynity::adapters::window {

/**
 * @brief Габариты клиентской области окна в пикселях.
 */
struct WindowSize {
    std::uint32_t width{0};
    std::uint32_t height{0};

    bool operator==(const WindowSize&) const = default;
};

/**
 * @brief Базовый интерфейс системного окна и адаптера вывода кадров.
 *
 * Наследует runtime::PresentationPort для интеграции с главным циклом движка.
 * Не зависит от подсистем Core и Renderer.
 */
class WindowAdapter : public runtime::PresentationPort {
public:
    ~WindowAdapter() override = default;

    /**
     * @brief Проверяет, открыто ли окно.
     * @return true, если окно открыто и активно; false в противном случае.
     */
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /**
     * @brief Запрашивает закрытие окна и освобождение его системных ресурсов.
     */
    virtual void close() noexcept = 0;

    /**
     * @brief Возвращает текущие размеры клиентской области окна.
     * @return структура WindowSize с шириной и высотой в пикселях.
     */
    [[nodiscard]] virtual WindowSize size() const noexcept = 0;

    /**
     * @brief Устанавливает заголовок системного окна.
     * @param title новый заголовок окна.
     */
    virtual void setTitle(std::string_view title) = 0;

    /**
     * @brief Опрашивает накопившиеся системные события окна и ввода.
     * @param[out] outEvents вектор для добавления полученных событий.
     */
    virtual void pollEvents(std::vector<InputEvent>& outEvents) = 0;

    /**
     * @brief Отображает готовый кадровый буфер (реализация PresentationPort).
     * @param framebuffer буфер пикселей для отображения.
     * @return буфер при синхронном показе (возврат в пул), либо nullptr при асинхронном.
     */
    [[nodiscard]] virtual std::unique_ptr<image::ImageBuffer>
    present(std::unique_ptr<image::ImageBuffer> framebuffer) override = 0;

    using GlProcResolver = void* (*)(const char*);

    [[nodiscard]] virtual bool hasGpuSurface() const noexcept { return false; }
    [[nodiscard]] virtual bool makeCurrent() noexcept { return false; }
    virtual void swapBuffers() noexcept {}
    [[nodiscard]] virtual GlProcResolver glProcResolver() const noexcept { return nullptr; }
};

} // namespace huynity::adapters::window
