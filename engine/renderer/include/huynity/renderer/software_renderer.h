#pragma once

#include "huynity/image/image_buffer.h"
#include "huynity/render_contracts/render_snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace huynity::renderer {

enum class RenderError {
    SizeMismatch,    // размеры framebuffer не совпадают с размерами снимка
    InvalidResource, // draw item ссылается на отсутствующий mesh-ресурс или вершину
    InvalidMaterial, // draw item ссылается на отсутствующий материал
};

using render_contracts::TextureFilter;
using render_contracts::TextureWrap;

namespace detail {

struct ProjectedVertex {
    double ndcX{};
    double ndcY{};
    double invZ{};   // 1/z для перспективы, 1 для ортографической камеры
    double zOverW{}; // z * invZ: даёт z = sum(w_i * zOverW_i) / sum(w_i * invZ_i)
};

/// @brief Параметры проекции одной камеры снимка.
///
/// View space: +z направлен вглубь сцены, камера в начале координат. Недопустимая
/// камера (нечисловые или противоречивые параметры) не проецирует ни одной вершины,
/// что согласовано с culling на стороне подготовки кадра.
struct Projection {
    double tanHalfFov{};
    double aspect{};
    double halfWidth{};
    double halfHeight{};
    double nearPlane{};
    double farPlane{};
    bool isPerspective{};

    [[nodiscard]] bool isValid() const noexcept;

    /// @return вершину в NDC или nullopt, если она вне [nearPlane, farPlane];
    /// частичное отсечение треугольников не выполняется.
    [[nodiscard]] std::optional<ProjectedVertex> project(const math::Point3& view) const noexcept;
};

[[nodiscard]] Projection makeProjection(const render_contracts::Camera3& camera, std::size_t imageWidth,
                                        std::size_t imageHeight) noexcept;

[[nodiscard]] math::Vector3 normalized(const math::Vector3& value) noexcept;

[[nodiscard]] render_contracts::Color scaleColor(const render_contracts::Color& color, double factor) noexcept;

void addTo(render_contracts::Color& target, const render_contracts::Color& addition) noexcept;

[[nodiscard]] render_contracts::Color multiplyColors(const render_contracts::Color& first,
                                                     const render_contracts::Color& second) noexcept;

/// @brief Направленный свет, перенесённый в view space.
///
/// DirectionalLight::direction задаёт направление распространения света в мировых
/// координатах; грань освещена, если её нормаль смотрит навстречу (к источнику).
struct ViewDirectionalLight {
    math::Vector3 towardLight;
    render_contracts::Color color;
    double intensity{};
};

/// @brief Кеш производных shading-данных на версию mesh-ресурса.
///
/// Нормали граней вычисляются один раз для неизменного ресурса: ключом служат
/// владелец ресурса и адрес данных вершин. Запись удерживает shared_ptr владельца,
/// поэтому адрес не может быть переиспользован другим ресурсом, пока запись жива.
class ShadingDataCache {
public:
    [[nodiscard]] std::expected<const std::vector<math::Vector3>*, RenderError>
    faceNormals(const render_contracts::MeshResource3& resource);

    [[nodiscard]] std::expected<const std::vector<math::Vector3>*, RenderError>
    vertexNormals(const render_contracts::MeshResource3& resource);

    [[nodiscard]] std::size_t hits() const noexcept { return m_hits; }
    [[nodiscard]] std::size_t misses() const noexcept { return m_misses; }

private:
    struct Entry {
        std::shared_ptr<const void> owner;
        const math::Point3* positions;
        std::size_t triangleCount;
        std::vector<math::Vector3> normals;
    };

    struct VertexEntry {
        std::shared_ptr<const void> owner;
        const math::Point3* positions;
        const std::byte* triangles;
        std::size_t vertexCount;
        std::size_t triangleCount;
        std::vector<math::Vector3> normals;
    };

    [[nodiscard]] static std::expected<std::vector<math::Vector3>, RenderError>
    computeNormals(const render_contracts::MeshResource3& resource, std::size_t triangleCount);

    [[nodiscard]] static std::expected<std::vector<math::Vector3>, RenderError>
    computeVertexNormals(const render_contracts::MeshResource3& resource, std::size_t triangleCount,
                         std::size_t vertexCount);

    std::vector<Entry> m_entries;
    std::vector<VertexEntry> m_vertexEntries;
    std::size_t m_hits{};
    std::size_t m_misses{};
};

[[nodiscard]] std::byte toByte(double channel) noexcept;

void writePixel(image::ImageBuffer& framebuffer, std::size_t x, std::size_t y,
                const render_contracts::Color& color) noexcept;

[[nodiscard]] render_contracts::Color readPixel(const image::ImageBuffer& buffer, std::size_t x,
                                                std::size_t y) noexcept;

[[nodiscard]] double applyWrap(double coordinate, TextureWrap wrap) noexcept;

/// @brief Выборка цвета из текстуры методом ближайшего соседа по UV-координатам.
[[nodiscard]] render_contracts::Color sampleTextureNearest(const image::ImageBuffer& texture, double u, double v,
                                                           TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Выборка цвета из текстуры методом билинейной интерполяции по UV-координатам.
[[nodiscard]] render_contracts::Color sampleTextureBilinear(const image::ImageBuffer& texture, double u, double v,
                                                            TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Выборка цвета из текстуры с заданным методом фильтрации и режимом наложения.
[[nodiscard]] render_contracts::Color sampleTexture(const image::ImageBuffer& texture, double u, double v,
                                                    TextureFilter filter = TextureFilter::Nearest,
                                                    TextureWrap wrap = TextureWrap::Clamp) noexcept;

void clear(image::ImageBuffer& framebuffer, const render_contracts::Color& color) noexcept;

[[nodiscard]] constexpr double edgeFunction(double pointX, double pointY, double otherX, double otherY, double sampleX,
                                            double sampleY) noexcept {
    return (otherX - pointX) * (sampleY - pointY) - (sampleX - pointX) * (otherY - pointY);
}

/// @brief Заполняет треугольник с depth-тестом; меньшая view-глубина перекрывает большую.
void rasterizeTriangle(const ProjectedVertex& first, const ProjectedVertex& second, const ProjectedVertex& third,
                       const render_contracts::Color& color, image::ImageBuffer& framebuffer,
                       std::vector<double>& depth) noexcept;

/// @brief Заполняет 3D-треугольник с затенением Гуро: барицентрическая интерполяция цвета по вершинам и depth-тест.
void rasterizeTriangleGouraud(const ProjectedVertex& first, const ProjectedVertex& second, const ProjectedVertex& third,
                              const render_contracts::Color& c0, const render_contracts::Color& c1,
                              const render_contracts::Color& c2, image::ImageBuffer& framebuffer,
                              std::vector<double>& depth) noexcept;

/// @brief Заполняет 3D-треугольник с наложением текстуры, перспективно-корректной интерполяцией UV,
/// затенением Гуро и Z-буфером.
void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const std::array<math::Point2, 3>& uv,
                                 const image::ImageBuffer& texture, const render_contracts::Color& c0,
                                 const render_contracts::Color& c1, const render_contracts::Color& c2,
                                 image::ImageBuffer& framebuffer, std::vector<double>& depth,
                                 TextureFilter filter = TextureFilter::Nearest,
                                 TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Заполняет 3D-треугольник с наложением текстуры, перспективно-корректной интерполяцией UV,
/// единым цветом освещения/материала и Z-буфером.
void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const std::array<math::Point2, 3>& uv,
                                 const image::ImageBuffer& texture, const render_contracts::Color& color,
                                 image::ImageBuffer& framebuffer, std::vector<double>& depth,
                                 TextureFilter filter = TextureFilter::Nearest,
                                 TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Перегрузка rasterizeTriangleTextured3D, принимающая UV-координаты отдельными точками.
void rasterizeTriangleTextured3D(const ProjectedVertex& first, const ProjectedVertex& second,
                                 const ProjectedVertex& third, const math::Point2& uv0, const math::Point2& uv1,
                                 const math::Point2& uv2, const image::ImageBuffer& texture,
                                 const render_contracts::Color& color, image::ImageBuffer& framebuffer,
                                 std::vector<double>& depth, TextureFilter filter = TextureFilter::Nearest,
                                 TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Смешивает цвет с содержимым framebuffer по правилу source-over.
void blendPixel(image::ImageBuffer& framebuffer, std::size_t x, std::size_t y,
                const render_contracts::Color& color) noexcept;

/// @brief Растеризует 2D-отрезок между двумя точками целочисленным алгоритмом Брезенхэма.
///
/// Обрабатывает все 8 октантов с накоплением ошибки. Пиксели вне границ кадрового буфера отсекаются.
/// При alpha < 1.0 используется blendPixel, иначе прямое writePixel.
void rasterizeLine2D(int x0, int y0, int x1, int y1, const render_contracts::Color& color,
                     image::ImageBuffer& framebuffer) noexcept;

/// @brief Растеризует 3D-отрезок целочисленным алгоритмом Брезенхэма с линейной интерполяцией глубины и Z-буфером.
///
/// Пиксель рисуется и глубина обновляется, если вычисленное z < depth[y * width + x].
void rasterizeLine3D(int x0, int y0, double z0, int x1, int y1, double z1, const render_contracts::Color& color,
                     image::ImageBuffer& framebuffer, std::vector<double>& depth) noexcept;

/// @brief Минимальный flat shading: ambient плюс диффузная составляющая направленных
/// источников по нормали грани. Обратная грань (при выключенном culling) освещается
/// с перевёрнутой нормалью.
[[nodiscard]] render_contracts::Color shadeFlat(const render_contracts::Material& material,
                                                const render_contracts::AmbientLight& ambient,
                                                const std::vector<ViewDirectionalLight>& lights,
                                                const math::Vector3& viewNormal, bool isFront) noexcept;

/// @brief Растеризует World3D-пасс: camera transforms, backface culling, depth buffer
/// и flat shading с кешированными нормалями граней.
///
/// Backface culling выполняется в view space: лицевой считается грань, чья нормаль
/// cross(b - a, c - a) направлена к камере (для ортографической камеры — в -z).
/// Кешированная локальная нормаль переносится в view space линейной частью
/// преобразования с нормализацией; для неоднородного масштаба это приближение.
[[nodiscard]] std::expected<void, RenderError>
renderWorld3D(const render_contracts::RenderSnapshot& snapshot, const render_contracts::World3DPass& pass,
              image::ImageBuffer& framebuffer, std::vector<double>& depth, ShadingDataCache& shadingCache);

/// @brief Заполняет 2D-треугольник в экранных координатах (пиксели, y вниз) с alpha blending.
void rasterizeTriangle2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                         const render_contracts::Color& color, image::ImageBuffer& framebuffer) noexcept;

/// @brief Заполняет 2D-треугольник в экранных координатах с барицентрической интерполяцией цвета (Гуро) и alpha
/// blending.
void rasterizeTriangleGouraud2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                                const render_contracts::Color& c0, const render_contracts::Color& c1,
                                const render_contracts::Color& c2, image::ImageBuffer& framebuffer) noexcept;

/// @brief Перегрузка rasterizeTriangleGouraud2D, принимающая точки math::Point2.
void rasterizeTriangleGouraud2D(const math::Point2& p0, const math::Point2& p1, const math::Point2& p2,
                                const render_contracts::Color& c0, const render_contracts::Color& c1,
                                const render_contracts::Color& c2, image::ImageBuffer& framebuffer) noexcept;

/// @brief Заполняет 2D-треугольник в экранных координатах с наложением текстуры и alpha blending.
void rasterizeTriangleTextured2D(const std::array<double, 3>& screenX, const std::array<double, 3>& screenY,
                                 const std::array<math::Point2, 3>& uv, const image::ImageBuffer& texture,
                                 const render_contracts::Color& tintColor, image::ImageBuffer& framebuffer,
                                 TextureFilter filter = TextureFilter::Nearest,
                                 TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Перегрузка rasterizeTriangleTextured2D, принимающая точки и UV-координаты math::Point2.
void rasterizeTriangleTextured2D(const math::Point2& p0, const math::Point2& p1, const math::Point2& p2,
                                 const math::Point2& uv0, const math::Point2& uv1, const math::Point2& uv2,
                                 const image::ImageBuffer& texture, const render_contracts::Color& tintColor,
                                 image::ImageBuffer& framebuffer, TextureFilter filter = TextureFilter::Nearest,
                                 TextureWrap wrap = TextureWrap::Clamp) noexcept;

/// @brief Растеризует 2D-проход (World2D или Hud2D) алгоритмом художника с alpha blending.
///
/// Внутри прохода draw items исполняются в стабильном порядке (layer, order) независимо
/// от порядка подачи. World2D проецирует мировые координаты через собственную камеру
/// с соглашениями ортографической проекции; Hud2D (camera == nullopt) использует
/// экранные координаты в пикселях с началом в левом верхнем углу, ось y вниз.
/// Недопустимая камера даёт пустой проход, что согласовано с culling при подготовке.
[[nodiscard]] std::expected<void, RenderError> render2DPass(const render_contracts::RenderSnapshot& snapshot,
                                                            std::span<const render_contracts::DrawItem2> items,
                                                            const std::optional<render_contracts::Camera2>& camera,
                                                            image::ImageBuffer& framebuffer);

} // namespace detail

/// @brief Синхронный программный рендерер снимка с кешем shading-данных.
///
/// Один экземпляр переиспользует нормали граней неизменной геометрии между кадрами;
/// статистика hits/misses показывает работу кеша.
class SoftwareRenderer {
public:
    /// @brief Растеризует снимок в framebuffer (headless-потребитель RenderSnapshot).
    ///
    /// Passes исполняются в порядке снимка: World3D — с общим depth buffer, World2D и
    /// Hud2D — алгоритмом художника с alpha blending. Зависит только от Render
    /// Contracts, Image и Math.
    [[nodiscard]] std::expected<void, RenderError> render(const render_contracts::RenderSnapshot& snapshot,
                                                          image::ImageBuffer& framebuffer);

    [[nodiscard]] std::size_t shadingCacheHits() const noexcept { return m_shadingCache.hits(); }
    [[nodiscard]] std::size_t shadingCacheMisses() const noexcept { return m_shadingCache.misses(); }

private:
    detail::ShadingDataCache m_shadingCache;
    std::vector<double> m_depthBuffer;
};

/// @brief Одноразовый вызов без сохранения кеша между кадрами.
/// Для переиспользования shading-данных неизменной геометрии используйте экземпляр SoftwareRenderer.
[[nodiscard]] inline std::expected<void, RenderError> render(const render_contracts::RenderSnapshot& snapshot,
                                                             image::ImageBuffer& framebuffer) {
    SoftwareRenderer renderer;
    return renderer.render(snapshot, framebuffer);
}

/// @brief Растеризует 2D-отрезок целочисленным алгоритмом Брезенхэма.
using detail::rasterizeLine2D;

/// @brief Растеризует 3D-отрезок целочисленным алгоритмом Брезенхэма с Z-буфером.
using detail::rasterizeLine3D;

/// @brief Растеризует 3D-треугольник с барицентрической интерполяцией цвета вершин (Гуро) и Z-буфером.
using detail::rasterizeTriangleGouraud;

/// @brief Растеризует 2D-треугольник с барицентрической интерполяцией цвета вершин (Гуро) и alpha blending.
using detail::rasterizeTriangleGouraud2D;

/// @brief Растеризует 2D-треугольник с наложением текстуры и alpha blending.
using detail::rasterizeTriangleTextured2D;

/// @brief Растеризует 3D-треугольник с наложением текстуры, перспективно-корректной интерполяцией UV и Z-буфером.
using detail::rasterizeTriangleTextured3D;

/// @brief Кеш производных shading-данных (нормали граней и вершин) на версию mesh-ресурса.
using detail::ShadingDataCache;

/// @brief Выборка цвета из текстуры методом ближайшего соседа.
using detail::sampleTextureNearest;

/// @brief Выборка цвета из текстуры методом билинейной интерполяции.
using detail::sampleTextureBilinear;

/// @brief Выборка цвета из текстуры с заданным методом фильтрации и режимом наложения.
using detail::sampleTexture;

} // namespace huynity::renderer
