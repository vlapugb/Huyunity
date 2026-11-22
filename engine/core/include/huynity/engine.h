#pragma once

#include "huynity/adapters/window/input_state.h"
#include "huynity/adapters/window/window_adapter.h"
#include "huynity/core/scene.h"
#include "huynity/objectlib/geometry.h"
#include "huynity/objectlib/objects.h"
#include "huynity/render_contracts/render_snapshot.h"
#include "huynity/renderer/software_renderer.h"
#include "huynity/runtime/fixed_step_update.h"
#include "huynity/runtime/frame_pipeline.h"

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace huynity::objectlib {

/**
 * @brief Псевдоним типизированных параметров фигуры по её виду ObjectType.
 */
template <SupportedObject T>
using TypedParams = T::Params;

} // namespace huynity::objectlib

namespace huynity {

/**
 * @brief Псевдоним обработчика шага фиксированной симуляции.
 */
using UpdateCallback = std::function<void(double fixedDt)>;

/**
 * @brief Псевдоним обработчика подготовки кадра с коэффициентом интерполяции.
 */
using FrameCallback = std::function<void(double alphaRemainder)>;

/**
 * @brief Псевдоним обработчика системных событий окна и ввода.
 */
using EventCallback = std::function<void(const adapters::window::InputEvent& event)>;

/**
 * @brief Параметры конфигурации высокоуровневого фасада Engine.
 */
struct EngineConfig {
    adapters::window::WindowSize windowSize{800, 600};
    std::string windowTitle{"Huynity Game"};
    double fixedTimeStep{1.0 / 60.0};
    bool forceHeadless{false};

    bool operator==(const EngineConfig&) const = default;
};

/**
 * @brief Высокоуровневый фасад игрового движка Huynity.
 *
 * Связывает сцену, оконный адаптер, конвейер кадра, опрос ввода и программный рендерер.
 */
class Engine {
public:
    using UpdateCallback = huynity::UpdateCallback;
    using FrameCallback = huynity::FrameCallback;
    using EventCallback = huynity::EventCallback;

    /**
     * @brief Создаёт экземпляр движка с заданной конфигурацией.
     * @param config параметры окна, шага симуляции и режима headless.
     */
    explicit Engine(const EngineConfig& config = {});

    /**
     * @brief Деструктор.
     */
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    // --- Настройка сцены и камеры ---

    /**
     * @brief Возвращает активную сцену движка.
     */
    [[nodiscard]] core::Scene& scene() noexcept { return m_scene; }

    /**
     * @brief Возвращает активную сцену движка (константная версия).
     */
    [[nodiscard]] const core::Scene& scene() const noexcept { return m_scene; }

    /**
     * @brief Устанавливает активную 3D-камеру сцены.
     * @param camera 3D-камера (перспективная или ортографическая).
     */
    void setCamera(render_contracts::Camera3 camera);

    /**
     * @brief Устанавливает активную 2D-камеру сцены.
     * @param camera 2D-камера мира.
     */
    void setCamera(render_contracts::Camera2 camera);

    /**
     * @brief Сбрасывает текущую 3D-камеру сцены.
     */
    void clearCamera3() noexcept;

    /**
     * @brief Сбрасывает текущую 2D-камеру сцены.
     */
    void clearCamera2() noexcept;

    /**
     * @brief Устанавливает параметры глобального 3D-освещения.
     * @param lighting фоновое и направленное освещение.
     */
    void setLighting(render_contracts::Lighting3 lighting);

    /**
     * @brief Возвращает текущую установленную 3D-камеру, если она задана.
     */
    [[nodiscard]] const std::optional<render_contracts::Camera3>& camera3() const noexcept { return m_camera3; }

    /**
     * @brief Возвращает текущую установленную 2D-камеру, если она задана.
     */
    [[nodiscard]] const std::optional<render_contracts::Camera2>& camera2() const noexcept { return m_camera2; }

    /**
     * @brief Возвращает текущее освещение сцены.
     */
    [[nodiscard]] const render_contracts::Lighting3& lighting() const noexcept { return m_lighting; }

    /**
     * @brief Задаёт порядок отрисовки 2D-мира относительно 3D: true, если 2D рисуется до 3D.
     * @param world2DFirst true, если 2D-проход должен предшествовать 3D.
     */
    void setWorld2DFirst(bool world2DFirst) noexcept;

    /**
     * @brief Возвращает флаг отрисовки 2D-мира перед 3D.
     */
    [[nodiscard]] bool isWorld2DFirst() const noexcept;

    /**
     * @brief Включает или выключает проход отрисовки экранного интерфейса (HUD).
     * @param enabled true для включения HUD.
     */
    void setHudEnabled(bool enabled) noexcept;

    /**
     * @brief Возвращает флаг активности HUD.
     */
    [[nodiscard]] bool isHudEnabled() const noexcept;

    /**
     * @brief Устанавливает цвет очистки кадрового буфера перед отрисовкой.
     * @param color цвет очистки RGBA.
     */
    void setClearColor(render_contracts::Color color) noexcept;

    /**
     * @brief Возвращает текущий цвет очистки кадрового буфера.
     */
    [[nodiscard]] render_contracts::Color clearColor() const noexcept;

    // --- Фабричные методы создания объектов ---

    /**
     * @brief Создаёт геометрический объект в активной сцене по строго типизированным параметрам.
     *
     * @tparam kind тип геометрического объекта ObjectType.
     * @param params структура параметров для создания объекта.
     * @param mobility режим подвижности (статический по умолчанию).
     * @return идентификатор созданной сущности в сцене ObjectId.
     * @throws std::runtime_error при ошибке валидации параметров или создания геометрии.
     */
    template <objectlib::SupportedObject T>
    core::ObjectId create(const T::Params& params, core::Mobility mobility = core::Mobility::Static) {
        auto result = m_scene.createObject<T>(params);
        if (!result) {
            throw std::runtime_error("Failed to create object in scene");
        }
        const auto id = *result;
        (void)m_scene.setMobility(id, mobility);
        return id;
    }

    /**
     * @brief Создаёт геометрический объект в активной сцене по спецификации ObjectSpec.
     *
     * @param spec динамическая спецификация объекта с типом и параметрами.
     * @param mobility режим подвижности (статический по умолчанию).
     * @return идентификатор созданной сущности в сцене ObjectId.
     * @throws std::runtime_error при ошибке валидации спецификации или создания геометрии.
     */
    core::ObjectId create(const objectlib::GeometrySpec& spec, core::Mobility mobility = core::Mobility::Static);

    // --- Ввод и окно ---

    /**
     * @brief Возвращает текущее состояние клавиатуры и мыши.
     */
    [[nodiscard]] const adapters::window::InputState& input() const noexcept { return m_inputState; }

    /**
     * @brief Возвращает системный оконный адаптер.
     */
    [[nodiscard]] adapters::window::WindowAdapter& window() noexcept { return *m_window; }

    /**
     * @brief Возвращает системный оконный адаптер (константная версия).
     */
    [[nodiscard]] const adapters::window::WindowAdapter& window() const noexcept { return *m_window; }

    // --- Обратные вызовы игры ---

    /**
     * @brief Регистрирует обработчик фиксированного шага симуляции.
     * @param callback функция обратного вызова с фиксированным шагом времени fixedDt.
     */
    void onUpdate(UpdateCallback callback);

    /**
     * @brief Регистрирует обработчик отрисовки кадра с остатком интерполяции.
     * @param callback функция обратного вызова с остатком времени alphaRemainder в диапазоне [0, 1).
     */
    void onFrame(FrameCallback callback);

    /**
     * @brief Регистрирует обработчик системных событий окна и ввода.
     * @param callback функция обратного вызова, принимающая событие InputEvent.
     */
    void onEvent(EventCallback callback);

    // --- Жизненный цикл ---

    /**
     * @brief Выполняет одну итерацию кадра движка с заданной дельтой времени.
     *
     * @param dt прошедшее время в секундах.
     */
    void step(double dt);

    /**
     * @brief Запускает главный цикл выполнения движка.
     */
    void run();

    /**
     * @brief Останавливает главный цикл и закрывает окно.
     */
    void stop() noexcept;

    /**
     * @brief Проверяет, выполняется ли в данный момент главный цикл движка.
     * @return true, если главный цикл активен.
     */
    [[nodiscard]] bool isRunning() const noexcept;

    // --- Быстрый предварительный просмотр (Quick Preview) ---

    /**
     * @brief Отображает существующий объект в сцене, автоматически настраивая камеру и свет по его AABB.
     *
     * @param id идентификатор объекта в активной сцене.
     * @throws std::runtime_error если объект не найден или невозможно вычислить его габариты.
     */
    void show(core::ObjectId id);

    /**
     * @brief Статический метод быстрого предварительного просмотра произвольной геометрии в отдельном окне.
     *
     * @param geometry геометрический объект Object Library (2D или 3D).
     * @param config конфигурация окна и движка (по умолчанию стандартные 800x600).
     */
    static void preview(const objectlib::Geometry& geometry, const EngineConfig& config = {});

    // --- Конфигурация и внутренние подсистемы ---

    /**
     * @brief Возвращает конфигурацию, с которой был инициализирован движок.
     */
    [[nodiscard]] const EngineConfig& config() const noexcept { return m_config; }

    /**
     * @brief Возвращает программный рендерер движка.
     */
    [[nodiscard]] renderer::SoftwareRenderer& renderer() noexcept { return m_renderer; }

    /**
     * @brief Возвращает программный рендерер движка (константная версия).
     */
    [[nodiscard]] const renderer::SoftwareRenderer& renderer() const noexcept { return m_renderer; }

    /**
     * @brief Возвращает конвейер кадров движка.
     */
    [[nodiscard]] runtime::FramePipeline& pipeline() noexcept { return m_pipeline; }

    /**
     * @brief Возвращает конвейер кадров движка (константная версия).
     */
    [[nodiscard]] const runtime::FramePipeline& pipeline() const noexcept { return m_pipeline; }

private:
    EngineConfig m_config;
    core::Scene m_scene;
    std::unique_ptr<adapters::window::WindowAdapter> m_window;
    runtime::FramePipeline m_pipeline;
    runtime::FixedStepUpdate m_fixedUpdate;
    renderer::SoftwareRenderer m_renderer;
    adapters::window::InputState m_inputState;
    std::optional<render_contracts::Camera3> m_camera3;
    std::optional<render_contracts::Camera2> m_camera2;
    render_contracts::Lighting3 m_lighting;
    bool m_hudEnabled{true};
    bool m_world2DFirst{false};
    render_contracts::Color m_clearColor{};
    bool m_isRunning{false};
    UpdateCallback m_onUpdate;
    FrameCallback m_onFrame;
    EventCallback m_onEvent;
};

} // namespace huynity
