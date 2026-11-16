#include "huynity/renderer/gl_renderer.h"

#include <GL/glcorearb.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gl_functions.h"

namespace huynity::renderer {
namespace {

using detail::GlFunctions;

/// @brief RAII-владение шейдерной программой: удаляет программу при разрушении.
class GlProgram {
public:
    GlProgram(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlProgram() {
        if (m_id != 0) {
            m_functions.deleteProgram(m_id);
        }
    }
    GlProgram(const GlProgram&) = delete;
    GlProgram& operator=(const GlProgram&) = delete;
    GlProgram(GlProgram&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) { other.m_id = 0; }
    GlProgram& operator=(GlProgram&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteProgram(m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

/// @brief RAII-владение vertex array object.
class GlVertexArray {
public:
    GlVertexArray(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlVertexArray() {
        if (m_id != 0) {
            m_functions.deleteVertexArrays(1, &m_id);
        }
    }
    GlVertexArray(const GlVertexArray&) = delete;
    GlVertexArray& operator=(const GlVertexArray&) = delete;
    GlVertexArray(GlVertexArray&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) { other.m_id = 0; }
    GlVertexArray& operator=(GlVertexArray&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteVertexArrays(1, &m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

/// @brief RAII-владение буфером вершин или индексов.
class GlBuffer {
public:
    GlBuffer(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlBuffer() {
        if (m_id != 0) {
            m_functions.deleteBuffers(1, &m_id);
        }
    }
    GlBuffer(const GlBuffer&) = delete;
    GlBuffer& operator=(const GlBuffer&) = delete;
    GlBuffer(GlBuffer&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) { other.m_id = 0; }
    GlBuffer& operator=(GlBuffer&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteBuffers(1, &m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

/// @brief RAII-владение текстурой.
class GlTexture {
public:
    GlTexture(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlTexture() {
        if (m_id != 0) {
            m_functions.deleteTextures(1, &m_id);
        }
    }
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) { other.m_id = 0; }
    GlTexture& operator=(GlTexture&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteTextures(1, &m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

/// @brief RAII-владение framebuffer object.
class GlFramebuffer {
public:
    GlFramebuffer(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlFramebuffer() {
        if (m_id != 0) {
            m_functions.deleteFramebuffers(1, &m_id);
        }
    }
    GlFramebuffer(const GlFramebuffer&) = delete;
    GlFramebuffer& operator=(const GlFramebuffer&) = delete;
    GlFramebuffer(GlFramebuffer&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) { other.m_id = 0; }
    GlFramebuffer& operator=(GlFramebuffer&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteFramebuffers(1, &m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

/// @brief RAII-владение renderbuffer глубины.
class GlRenderbuffer {
public:
    GlRenderbuffer(const GlFunctions& functions, unsigned int id) : m_functions(functions), m_id(id) {}
    ~GlRenderbuffer() {
        if (m_id != 0) {
            m_functions.deleteRenderbuffers(1, &m_id);
        }
    }
    GlRenderbuffer(const GlRenderbuffer&) = delete;
    GlRenderbuffer& operator=(const GlRenderbuffer&) = delete;
    GlRenderbuffer(GlRenderbuffer&& other) noexcept : m_functions(other.m_functions), m_id(other.m_id) {
        other.m_id = 0;
    }
    GlRenderbuffer& operator=(GlRenderbuffer&& other) noexcept {
        if (m_id != 0) {
            m_functions.deleteRenderbuffers(1, &m_id);
        }
        m_id = other.m_id;
        other.m_id = 0;
        return *this;
    }

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    const GlFunctions& m_functions;
    unsigned int m_id;
};

struct Vertex {
    float position[3]{};
    float uv[2]{};
    float normal[3]{};
};

[[nodiscard]] std::array<float, 16> multiply(const std::array<float, 16>& first, const std::array<float, 16>& second) {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t index = 0; index < 4; ++index) {
                result[column * 4 + row] += first[index * 4 + row] * second[column * 4 + index];
            }
        }
    }
    return result;
}

[[nodiscard]] std::array<float, 16> modelView(const math::Transform3& model, const math::Transform3& view) {
    const auto combined = math::compose(view, model);
    return {static_cast<float>(combined.linear(0, 0)),   static_cast<float>(combined.linear(1, 0)),
            static_cast<float>(combined.linear(2, 0)),   0.0F,
            static_cast<float>(combined.linear(0, 1)),   static_cast<float>(combined.linear(1, 1)),
            static_cast<float>(combined.linear(2, 1)),   0.0F,
            static_cast<float>(combined.linear(0, 2)),   static_cast<float>(combined.linear(1, 2)),
            static_cast<float>(combined.linear(2, 2)),   0.0F,
            static_cast<float>(combined.translation(0)), static_cast<float>(combined.translation(1)),
            static_cast<float>(combined.translation(2)), 1.0F};
}

[[nodiscard]] std::array<float, 16> projection(const render_contracts::Camera3& camera, double aspect) {
    std::array<float, 16> result{};
    result[15] = 1.0F;
    return std::visit(
        [&](const auto& value) {
            using Camera = std::decay_t<decltype(value)>;
            const double near = value.nearPlane;
            const double far = value.farPlane;
            if constexpr (std::is_same_v<Camera, render_contracts::PerspectiveCamera3>) {
                const double tangent = std::tan(value.verticalFieldOfView * 0.5);
                result[0] = static_cast<float>(1.0 / (tangent * aspect));
                result[5] = static_cast<float>(1.0 / tangent);
                result[10] = static_cast<float>((far + near) / (far - near));
                result[11] = 1.0F;
                result[14] = static_cast<float>(-2.0 * far * near / (far - near));
                result[15] = 0.0F;
            } else {
                result[0] = static_cast<float>(2.0 / value.width);
                result[5] = static_cast<float>(2.0 / value.height);
                result[10] = static_cast<float>(2.0 / (far - near));
                result[14] = static_cast<float>(-(far + near) / (far - near));
            }
            return result;
        },
        camera);
}

[[nodiscard]] std::array<float, 16> viewMatrix(const math::Transform3& view) {
    return modelView(math::Transform3{}, view);
}

[[nodiscard]] math::Transform3 lookAt(const math::Vector3& forward, const math::Vector3& center) noexcept {
    const auto normalize = [](math::Vector3 value) {
        const double length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        return length > 0.0 ? math::Vector3{{value[0] / length, value[1] / length, value[2] / length}}
                            : math::Vector3{{0.0, 0.0, 1.0}};
    };
    const auto viewForward = normalize(forward);
    const auto worldUp = math::Vector3{{0.0, 1.0, 0.0}};
    const auto cross = [](const math::Vector3& first, const math::Vector3& second) {
        return math::Vector3{{first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2],
                              first[0] * second[1] - first[1] * second[0]}};
    };
    const auto right = normalize(cross(worldUp, viewForward));
    const auto up = cross(viewForward, right);
    // Transform3 хранится построчно (3x4): строки basis-векторов, перевод в values[3,7,11].
    math::Transform3 result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.values[axis] = right[axis];
        result.values[4 + axis] = up[axis];
        result.values[8 + axis] = viewForward[axis];
    }
    result.values[3] = -(right[0] * center[0] + right[1] * center[1] + right[2] * center[2]);
    result.values[7] = -(up[0] * center[0] + up[1] * center[1] + up[2] * center[2]);
    result.values[11] = -(viewForward[0] * center[0] + viewForward[1] * center[1] + viewForward[2] * center[2]);
    return result;
}

[[nodiscard]] std::array<float, 16> orthographic(double width, double height, double near, double far) {
    return {static_cast<float>(2.0 / width),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            static_cast<float>(2.0 / height),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            static_cast<float>(2.0 / (far - near)),
            0.0F,
            0.0F,
            0.0F,
            static_cast<float>(-(far + near) / (far - near)),
            1.0F};
}

constexpr const char* worldVertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in vec3 vertexNormal;
uniform mat4 modelView;
uniform mat4 projection;
out vec3 viewPosition;
out vec3 smoothNormal;
out vec2 uv;
void main() {
    vec4 view = modelView * vec4(position, 1.0);
    viewPosition = view.xyz;
    smoothNormal = mat3(modelView) * vertexNormal;
    uv = texCoord;
    gl_Position = projection * view;
})";

constexpr const char* worldFragment = R"(#version 330 core
in vec3 viewPosition;
in vec3 smoothNormal;
in vec2 uv;
out vec4 outColor;
uniform vec4 baseColor;
uniform vec3 ambient;
uniform vec3 sunColor;
uniform vec3 sunDirection;
uniform bool unlit;
uniform bool gouraud;
uniform bool textured;
uniform sampler2D image;
uniform sampler2D shadowMap;
uniform mat4 shadowMatrix;
uniform bool shadowsEnabled;
uniform bool receiveShadow;
uniform float shadowBias;
uniform float minVisibility;
uniform bool fogEnabled;
uniform vec4 fogColor;
uniform vec2 fogRange;
void main() {
    vec4 color = baseColor * (textured ? texture(image, uv) : vec4(1.0));
    // Производная нормаль грани направлена вглубь view space, а smoothNormal (как и
    // нормали SoftwareRenderer) — наружу, поэтому flat-нормаль инвертируется.
    vec3 normal = gouraud ? normalize(smoothNormal) : -normalize(cross(dFdx(viewPosition), dFdy(viewPosition)));
    if (!gl_FrontFacing) normal = -normal;
    float diffuse = max(dot(normal, normalize(-sunDirection)), 0.0);
    float visibility = 1.0;
    if (shadowsEnabled && receiveShadow) {
        vec4 projected = shadowMatrix * vec4(viewPosition, 1.0);
        vec3 shadowCoordinate = projected.xyz / projected.w * 0.5 + 0.5;
        if (shadowCoordinate.z > 0.0 && shadowCoordinate.z < 1.0 &&
            all(greaterThanEqual(shadowCoordinate.xy, vec2(0.0))) &&
            all(lessThanEqual(shadowCoordinate.xy, vec2(1.0)))) {
            float depth = shadowCoordinate.z - shadowBias;
            vec2 step = 1.0 / textureSize(shadowMap, 0);
            float litSamples = 0.0;
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    if (texture(shadowMap, shadowCoordinate.xy + vec2(float(x), float(y)) * step).r >= depth) {
                        litSamples += 1.0;
                    }
                }
            }
            visibility = mix(minVisibility, 1.0, litSamples / 9.0);
        }
    }
    vec3 light = unlit ? vec3(1.0) : ambient + sunColor * diffuse * visibility;
    color.rgb *= light;
    if (fogEnabled) {
        float amount = smoothstep(fogRange.x, fogRange.y, viewPosition.z);
        color.rgb = mix(color.rgb, fogColor.rgb, amount);
    }
    outColor = color;
})";

constexpr const char* shadowVertex = R"(#version 330 core
layout(location=0) in vec3 position;
uniform mat4 modelView;
uniform mat4 projection;
void main() { gl_Position = projection * modelView * vec4(position, 1.0); })";

constexpr const char* shadowFragment = R"(#version 330 core
void main() {})";

constexpr const char* screenVertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
uniform mat4 transform;
out vec2 uv;
void main() { uv = texCoord; gl_Position = transform * vec4(position.xy, 0.0, 1.0); })";

constexpr const char* screenFragment = R"(#version 330 core
in vec2 uv;
out vec4 outColor;
uniform vec4 baseColor;
uniform bool textured;
uniform sampler2D image;
void main() { outColor = baseColor * (textured ? texture(image, uv) : vec4(1.0)); })";

constexpr const char* skyVertex = R"(#version 330 core
out vec2 uv;
void main() { vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); uv=p; gl_Position=vec4(p*2.0-1.0,1.0,1.0); })";

constexpr const char* skyFragment = R"(#version 330 core
in vec2 uv;
out vec4 outColor;
uniform vec4 zenith;
uniform vec4 horizon;
uniform vec4 ground;
uniform vec3 rayRight;
uniform vec3 rayUp;
uniform vec3 rayForward;
uniform vec2 projectionScale;
uniform vec3 sunDirection;
uniform vec4 sunColor;
uniform float sunGlow;
void main() {
    vec2 p=uv*2.0-1.0;
    vec3 ray=normalize(rayForward + rayRight*p.x*projectionScale.x + rayUp*p.y*projectionScale.y);
    float y=clamp(ray.y,-1.0,1.0);
    vec4 color = y >= 0.0 ? mix(horizon, zenith, pow(y, 0.65)) : mix(horizon, ground, pow(-y, 0.65));
    float sun=max(dot(ray,normalize(-sunDirection)),0.0);
    color.rgb += sunColor.rgb * pow(sun, 256.0) + sunColor.rgb * pow(sun, 16.0) * sunGlow;
    outColor=color;
})";

/// @brief Кешированный mesh на GPU: VAO/VBO/IBO и версия загруженных данных.
struct GlMesh {
    GlVertexArray vao;
    GlBuffer vbo;
    GlBuffer ibo;
    std::size_t indexCount{};
    std::shared_ptr<const void> owner;
    std::shared_ptr<const void> texCoordsOwner;
    std::uint64_t version{};
    std::uint64_t lastUsedFrame{};
};

/// @brief Кешированная текстура на GPU и версия загруженных пикселей.
struct GlTextureEntry {
    GlTexture texture;
    std::uint64_t version{};
    std::uint64_t lastUsedFrame{};
};

struct MeshKey {
    std::uint64_t resourceId{};
    bool needNormals{};

    bool operator==(const MeshKey&) const = default;
};

struct MeshKeyHash {
    std::size_t operator()(const MeshKey& key) const noexcept {
        const auto first = std::hash<std::uint64_t>{}(key.resourceId);
        return first ^ (std::hash<bool>{}(key.needNormals) + 0x9e3779b9 + (first << 6) + (first >> 2));
    }
};

/// @brief Кадровый интервал, после которого неиспользуемый GPU-ресурс удаляется.
constexpr std::uint64_t unusedResourceSweepFrames = 120;

} // namespace

struct GlRenderer::Implementation {
    explicit Implementation(GlProcResolver resolver)
        : worldProgram(gl, 0), screenProgram(gl, 0), skyProgram(gl, 0), shadowProgram(gl, 0), emptyVao(gl, 0),
          shadowTexture(gl, 0), shadowFramebuffer(gl, 0), captureTexture(gl, 0), captureDepth(gl, 0),
          captureFramebuffer(gl, 0) {
        if (resolver == nullptr) {
            throw std::runtime_error("OpenGL procedure resolver is not available");
        }
        if (!detail::loadGlFunctions(gl, resolver)) {
            throw std::runtime_error("Failed to load required OpenGL 3.3 Core entry points");
        }
        const auto* versionString = reinterpret_cast<const char*>(gl.getString(GL_VERSION));
        if (versionString == nullptr) {
            throw std::runtime_error("OpenGL context is not active");
        }
        int major = 0;
        int minor = 0;
        if (std::sscanf(versionString, "%d.%d", &major, &minor) != 2 || major < 3 || (major == 3 && minor < 3)) {
            throw std::runtime_error(std::string("OpenGL 3.3 Core is required, got version: ") + versionString);
        }
        worldProgram = GlProgram(gl, makeProgram(worldVertex, worldFragment));
        screenProgram = GlProgram(gl, makeProgram(screenVertex, screenFragment));
        skyProgram = GlProgram(gl, makeProgram(skyVertex, skyFragment));
        shadowProgram = GlProgram(gl, makeProgram(shadowVertex, shadowFragment));
        emptyVao = GlVertexArray(gl, generate(gl.genVertexArrays));
    }

    [[nodiscard]] unsigned int makeProgram(const char* vertexSource, const char* fragmentSource) {
        const auto compile = [this](unsigned int type, const char* source) {
            const auto shader = gl.createShader(type);
            gl.shaderSource(shader, 1, &source, nullptr);
            gl.compileShader(shader);
            int success = 0;
            gl.getShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (success == 0) {
                char log[2048]{};
                gl.getShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "OpenGL shader compilation failed: " << log << '\n';
                gl.deleteShader(shader);
                return 0U;
            }
            return shader;
        };
        const auto vertex = compile(GL_VERTEX_SHADER, vertexSource);
        const auto fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vertex == 0 || fragment == 0) {
            gl.deleteShader(vertex);
            gl.deleteShader(fragment);
            return 0;
        }
        const auto program = gl.createProgram();
        gl.attachShader(program, vertex);
        gl.attachShader(program, fragment);
        gl.linkProgram(program);
        gl.deleteShader(vertex);
        gl.deleteShader(fragment);
        int success = 0;
        gl.getProgramiv(program, GL_LINK_STATUS, &success);
        if (success == 0) {
            char log[2048]{};
            gl.getProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "OpenGL program link failed: " << log << '\n';
            gl.deleteProgram(program);
            return 0;
        }
        return program;
    }

    template <typename Generate>
    [[nodiscard]] unsigned int generate(Generate generateFunction) {
        unsigned int id = 0;
        generateFunction(1, &id);
        return id;
    }

    /// @brief Возвращает ключ текстуры: стабильный textureId из снимка или адрес как fallback.
    [[nodiscard]] static std::uint64_t textureKey(const render_contracts::Material& material) noexcept {
        if (material.textureId != 0) {
            return material.textureId;
        }
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(material.texture.get()));
    }

    [[nodiscard]] std::expected<unsigned int, RenderError> uploadTexture(const render_contracts::Material& material) {
        if (!material.texture) {
            return 0;
        }
        const auto key = textureKey(material);
        const auto found = textures.find(key);
        if (found != textures.end() && found->second.version == material.textureVersion) {
            found->second.lastUsedFrame = frameCounter;
            return found->second.texture.id();
        }
        if (found != textures.end()) {
            textures.erase(found);
        }
        const auto id = generate(gl.genTextures);
        gl.bindTexture(GL_TEXTURE_2D, id);
        const auto& image = *material.texture;
        const auto packedStride = image.width() * 4;
        std::vector<std::byte> packed;
        const auto* pixels = image.bytes().data();
        if (image.stride() % 4 != 0) {
            packed.resize(packedStride * image.height());
            for (std::size_t row = 0; row < image.height(); ++row) {
                std::memcpy(packed.data() + row * packedStride, pixels + row * image.stride(), packedStride);
            }
            pixels = packed.data();
        }
        gl.pixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<int>((packed.empty() ? image.stride() : packedStride) / 4));
        gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(material.texture->width()),
                      static_cast<int>(material.texture->height()), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        gl.pixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        auto [stored, inserted] =
            textures.emplace(key, GlTextureEntry{GlTexture(gl, id), material.textureVersion, frameCounter});
        (void)inserted;
        return stored->second.texture.id();
    }

    /// @brief Возвращает кешированный mesh; при новой версии перезагружает буферы.
    ///
    /// Все индексы проверяются на попадание в диапазон вершин до загрузки в GL:
    /// некорректный ресурс даёт InvalidResource до любой отрисовки.
    [[nodiscard]] std::expected<const GlMesh*, RenderError> uploadMesh(const render_contracts::MeshResource3& resource,
                                                                       bool needNormals) {
        const MeshKey key{resource.resourceId, needNormals};
        const auto found = meshes.find(key);
        if (resource.resourceId != 0 && found != meshes.end() && found->second.version == resource.version) {
            found->second.lastUsedFrame = frameCounter;
            return &found->second;
        }
        const std::size_t triangleCount = resource.triangles.size() / sizeof(render_contracts::TriangleIndices);
        if (resource.triangles.size() % sizeof(render_contracts::TriangleIndices) != 0) {
            return std::unexpected(RenderError::InvalidResource);
        }
        const auto* triangles = reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
        std::vector<Vertex> vertices(resource.positions.size());
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const auto& point = resource.positions[index];
            vertices[index].position[0] = static_cast<float>(point[0]);
            vertices[index].position[1] = static_cast<float>(point[1]);
            vertices[index].position[2] = static_cast<float>(point[2]);
            if (index < resource.texCoords.size()) {
                vertices[index].uv[0] = static_cast<float>(resource.texCoords[index][0]);
                vertices[index].uv[1] = static_cast<float>(resource.texCoords[index][1]);
            }
        }
        for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
            const auto& indices = triangles[triangle];
            if (indices.a >= vertices.size() || indices.b >= vertices.size() || indices.c >= vertices.size()) {
                return std::unexpected(RenderError::InvalidResource);
            }
            if (needNormals) {
                const auto& a = vertices[indices.a].position;
                const auto& b = vertices[indices.b].position;
                const auto& c = vertices[indices.c].position;
                const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const std::array<float, 3> normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                                  ab[0] * ac[1] - ab[1] * ac[0]};
                for (const auto index : {indices.a, indices.b, indices.c}) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        vertices[index].normal[axis] += normal[axis];
                    }
                }
            }
        }
        if (found != meshes.end()) {
            meshes.erase(found);
        }
        GlVertexArray vao(gl, generate(gl.genVertexArrays));
        gl.bindVertexArray(vao.id());
        GlBuffer vbo(gl, generate(gl.genBuffers));
        gl.bindBuffer(GL_ARRAY_BUFFER, vbo.id());
        gl.bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                      GL_STATIC_DRAW);
        GlBuffer ibo(gl, generate(gl.genBuffers));
        gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo.id());
        gl.bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.triangles.size()),
                      resource.triangles.data(), GL_STATIC_DRAW);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                               reinterpret_cast<void*>(offsetof(Vertex, position)));
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
        gl.enableVertexAttribArray(2);
        gl.vertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                               reinterpret_cast<void*>(offsetof(Vertex, normal)));
        gl.bindVertexArray(0);
        auto [stored, inserted] =
            meshes.emplace(key, GlMesh{std::move(vao), std::move(vbo), std::move(ibo), triangleCount * 3,
                                       resource.owner, resource.texCoordsOwner, resource.version, frameCounter});
        (void)inserted;
        return &stored->second;
    }

    /// @brief Удаляет GPU-ресурсы, не встречавшиеся дольше unusedResourceSweepFrames кадров.
    void sweepUnusedResources() {
        for (auto it = meshes.begin(); it != meshes.end();) {
            if (frameCounter - it->second.lastUsedFrame > unusedResourceSweepFrames) {
                it = meshes.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = textures.begin(); it != textures.end();) {
            if (frameCounter - it->second.lastUsedFrame > unusedResourceSweepFrames) {
                it = textures.erase(it);
            } else {
                ++it;
            }
        }
    }

    void uniforms(unsigned int program, const render_contracts::Material& material) {
        const auto& color = material.color;
        gl.uniform4f(gl.getUniformLocation(program, "baseColor"), static_cast<float>(color.red),
                     static_cast<float>(color.green), static_cast<float>(color.blue), static_cast<float>(color.alpha));
        gl.uniform1i(gl.getUniformLocation(program, "textured"), material.texture ? 1 : 0);
        gl.uniform1i(gl.getUniformLocation(program, "image"), 0);
        gl.activeTexture(GL_TEXTURE0);
        const auto textureId = uploadTexture(material);
        gl.bindTexture(GL_TEXTURE_2D, textureId ? *textureId : 0);
        if (material.texture) {
            const auto filter = material.filter == render_contracts::TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
            const auto wrap = material.wrap == render_contracts::TextureWrap::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
            gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        }
    }

    std::expected<void, RenderError> world3D(const render_contracts::RenderSnapshot& snapshot,
                                             const render_contracts::World3DPass& pass) {
        const auto& camera =
            std::visit([](const auto& value) -> const auto& { return value.worldToView; }, pass.camera);
        const auto projectionMatrix =
            projection(pass.camera, static_cast<double>(snapshot.imageWidth) / snapshot.imageHeight);
        std::array<float, 16> shadowMatrix = {};
        // Фиксируем привязку framebuffer до любой работы с shadow FBO: первое создание
        // shadow FBO переключает привязку, и её нужно восстановить после прохода.
        int previousFramebuffer = 0;
        gl.getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        if (pass.lighting.shadows.enabled && !pass.lighting.directional.empty()) {
            const auto& config = pass.lighting.shadows;
            const auto& lightDirection = pass.lighting.directional.front().direction;
            const math::Vector3 center{{config.center[0], config.center[1], config.center[2]}};
            const auto worldToLight = lookAt(lightDirection, center);
            const auto lightProjection = orthographic(config.extent * 2.0, config.extent * 2.0,
                                                      -config.depthRange * 0.5, config.depthRange * 0.5);
            const auto cameraInverse = math::inverse(camera);
            if (!cameraInverse) {
                return std::unexpected(RenderError::InvalidResource);
            }
            const auto viewToWorld = viewMatrix(*cameraInverse);
            if (shadowResolution != config.resolution) {
                shadowFramebuffer = GlFramebuffer(gl, 0);
                shadowTexture = GlTexture(gl, 0);
                const auto framebufferId = generate(gl.genFramebuffers);
                gl.bindFramebuffer(GL_FRAMEBUFFER, framebufferId);
                shadowFramebuffer = GlFramebuffer(gl, framebufferId);
                const auto textureId = generate(gl.genTextures);
                shadowTexture = GlTexture(gl, textureId);
                gl.bindTexture(GL_TEXTURE_2D, textureId);
                gl.texImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, static_cast<int>(config.resolution),
                              static_cast<int>(config.resolution), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textureId, 0);
                gl.drawBuffer(GL_NONE);
                gl.readBuffer(GL_NONE);
                shadowResolution = config.resolution;
            }
            gl.bindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer.id());
            gl.viewport(0, 0, static_cast<GLsizei>(config.resolution), static_cast<GLsizei>(config.resolution));
            gl.enable(GL_DEPTH_TEST);
            gl.depthFunc(GL_LESS);
            gl.disable(GL_BLEND);
            gl.clearDepth(1.0);
            gl.clear(GL_DEPTH_BUFFER_BIT);
            gl.enable(GL_CULL_FACE);
            gl.cullFace(GL_BACK);
            gl.frontFace(GL_CW);
            gl.useProgram(shadowProgram.id());
            gl.uniformMatrix4fv(gl.getUniformLocation(shadowProgram.id(), "projection"), 1, GL_FALSE,
                                lightProjection.data());
            for (const auto& item : pass.drawItems) {
                if (!item.castShadow || item.wireframe || item.resource >= snapshot.resources3.size()) {
                    continue;
                }
                if (item.material < snapshot.materials.size() && snapshot.materials[item.material] != nullptr &&
                    snapshot.materials[item.material]->unlit) {
                    continue;
                }
                const auto& resource = snapshot.resources3[item.resource];
                const auto mesh = uploadMesh(resource, false);
                if (!mesh) {
                    return std::unexpected(mesh.error());
                }
                gl.bindVertexArray((*mesh)->vao.id());
                gl.uniformMatrix4fv(gl.getUniformLocation(shadowProgram.id(), "modelView"), 1, GL_FALSE,
                                    modelView(item.localToWorld, worldToLight).data());
                gl.drawElements(GL_TRIANGLES, static_cast<GLsizei>((*mesh)->indexCount), GL_UNSIGNED_INT, nullptr);
            }
            gl.disable(GL_CULL_FACE);
            gl.bindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
            gl.viewport(0, 0, static_cast<GLsizei>(snapshot.imageWidth), static_cast<GLsizei>(snapshot.imageHeight));
            shadowMatrix = multiply(lightProjection, multiply(viewMatrix(worldToLight), viewToWorld));
        }
        gl.enable(GL_DEPTH_TEST);
        gl.depthFunc(GL_LESS);
        gl.depthMask(GL_TRUE);
        gl.disable(GL_BLEND);
        gl.useProgram(worldProgram.id());
        gl.uniformMatrix4fv(gl.getUniformLocation(worldProgram.id(), "projection"), 1, GL_FALSE,
                            projectionMatrix.data());
        gl.uniformMatrix4fv(gl.getUniformLocation(worldProgram.id(), "shadowMatrix"), 1, GL_FALSE, shadowMatrix.data());
        gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "shadowsEnabled"), pass.lighting.shadows.enabled ? 1 : 0);
        gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "shadowMap"), 1);
        gl.activeTexture(GL_TEXTURE1);
        gl.bindTexture(GL_TEXTURE_2D, shadowTexture.id());
        gl.activeTexture(GL_TEXTURE0);
        gl.uniform1f(gl.getUniformLocation(worldProgram.id(), "shadowBias"),
                     static_cast<float>(pass.lighting.shadows.bias * 0.01));
        gl.uniform1f(gl.getUniformLocation(worldProgram.id(), "minVisibility"),
                     static_cast<float>(pass.lighting.shadows.minVisibility));
        gl.uniform3f(gl.getUniformLocation(worldProgram.id(), "ambient"),
                     static_cast<float>(pass.lighting.ambient.color.red * pass.lighting.ambient.intensity),
                     static_cast<float>(pass.lighting.ambient.color.green * pass.lighting.ambient.intensity),
                     static_cast<float>(pass.lighting.ambient.color.blue * pass.lighting.ambient.intensity));
        if (!pass.lighting.directional.empty()) {
            const auto& light = pass.lighting.directional.front();
            const auto direction = math::transformVector(camera, light.direction);
            gl.uniform3f(gl.getUniformLocation(worldProgram.id(), "sunDirection"), static_cast<float>(direction[0]),
                         static_cast<float>(direction[1]), static_cast<float>(direction[2]));
            gl.uniform3f(gl.getUniformLocation(worldProgram.id(), "sunColor"),
                         static_cast<float>(light.color.red * light.intensity),
                         static_cast<float>(light.color.green * light.intensity),
                         static_cast<float>(light.color.blue * light.intensity));
        } else {
            gl.uniform3f(gl.getUniformLocation(worldProgram.id(), "sunColor"), 0, 0, 0);
            gl.uniform3f(gl.getUniformLocation(worldProgram.id(), "sunDirection"), 0, 0, 1);
        }
        gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "fogEnabled"), pass.lighting.fog.enabled ? 1 : 0);
        gl.uniform4f(gl.getUniformLocation(worldProgram.id(), "fogColor"),
                     static_cast<float>(pass.lighting.fog.color.red), static_cast<float>(pass.lighting.fog.color.green),
                     static_cast<float>(pass.lighting.fog.color.blue),
                     static_cast<float>(pass.lighting.fog.color.alpha));
        gl.uniform2f(gl.getUniformLocation(worldProgram.id(), "fogRange"), static_cast<float>(pass.lighting.fog.start),
                     static_cast<float>(pass.lighting.fog.end));
        for (const auto& item : pass.drawItems) {
            if (item.resource >= snapshot.resources3.size()) {
                return std::unexpected(RenderError::InvalidResource);
            }
            if (item.material >= snapshot.materials.size() || !snapshot.materials[item.material]) {
                return std::unexpected(RenderError::InvalidMaterial);
            }
            const auto& resource = snapshot.resources3[item.resource];
            const bool isGouraud = item.shading == render_contracts::ShadingModel::Gouraud;
            const auto mesh = uploadMesh(resource, isGouraud);
            if (!mesh) {
                return std::unexpected(mesh.error());
            }
            const auto mv = modelView(item.localToWorld, camera);
            gl.uniformMatrix4fv(gl.getUniformLocation(worldProgram.id(), "modelView"), 1, GL_FALSE, mv.data());
            uniforms(worldProgram.id(), *snapshot.materials[item.material]);
            gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "unlit"),
                         snapshot.materials[item.material]->unlit ? 1 : 0);
            gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "gouraud"), isGouraud ? 1 : 0);
            gl.uniform1i(gl.getUniformLocation(worldProgram.id(), "receiveShadow"), item.receiveShadow ? 1 : 0);
            if (item.backfaceCulling) {
                gl.enable(GL_CULL_FACE);
                gl.cullFace(GL_BACK);
                gl.frontFace(GL_CW);
            } else {
                gl.disable(GL_CULL_FACE);
            }
            gl.polygonMode(GL_FRONT_AND_BACK, item.wireframe ? GL_LINE : GL_FILL);
            gl.bindVertexArray((*mesh)->vao.id());
            gl.drawElements(GL_TRIANGLES, static_cast<GLsizei>((*mesh)->indexCount), GL_UNSIGNED_INT, nullptr);
        }
        gl.polygonMode(GL_FRONT_AND_BACK, GL_FILL);
        gl.disable(GL_CULL_FACE);
        if (pass.lighting.sky.enabled) {
            const auto skyViewToWorld = math::inverse(camera);
            if (!skyViewToWorld) {
                return std::unexpected(RenderError::InvalidResource);
            }
            const auto& sky = pass.lighting.sky;
            gl.useProgram(skyProgram.id());
            gl.depthFunc(GL_LEQUAL);
            gl.depthMask(GL_FALSE);
            gl.uniform4f(gl.getUniformLocation(skyProgram.id(), "zenith"), static_cast<float>(sky.zenithColor.red),
                         static_cast<float>(sky.zenithColor.green), static_cast<float>(sky.zenithColor.blue), 1);
            gl.uniform4f(gl.getUniformLocation(skyProgram.id(), "horizon"), static_cast<float>(sky.horizonColor.red),
                         static_cast<float>(sky.horizonColor.green), static_cast<float>(sky.horizonColor.blue), 1);
            gl.uniform4f(gl.getUniformLocation(skyProgram.id(), "ground"), static_cast<float>(sky.groundColor.red),
                         static_cast<float>(sky.groundColor.green), static_cast<float>(sky.groundColor.blue), 1);
            gl.uniform3f(
                gl.getUniformLocation(skyProgram.id(), "rayRight"), static_cast<float>(skyViewToWorld->linear(0, 0)),
                static_cast<float>(skyViewToWorld->linear(1, 0)), static_cast<float>(skyViewToWorld->linear(2, 0)));
            gl.uniform3f(
                gl.getUniformLocation(skyProgram.id(), "rayUp"), static_cast<float>(skyViewToWorld->linear(0, 1)),
                static_cast<float>(skyViewToWorld->linear(1, 1)), static_cast<float>(skyViewToWorld->linear(2, 1)));
            gl.uniform3f(
                gl.getUniformLocation(skyProgram.id(), "rayForward"), static_cast<float>(skyViewToWorld->linear(0, 2)),
                static_cast<float>(skyViewToWorld->linear(1, 2)), static_cast<float>(skyViewToWorld->linear(2, 2)));
            double scaleX = 0.0;
            double scaleY = 0.0;
            if (const auto* perspective = std::get_if<render_contracts::PerspectiveCamera3>(&pass.camera)) {
                scaleY = std::tan(perspective->verticalFieldOfView * 0.5);
                scaleX = scaleY * static_cast<double>(snapshot.imageWidth) / snapshot.imageHeight;
            }
            gl.uniform2f(gl.getUniformLocation(skyProgram.id(), "projectionScale"), static_cast<float>(scaleX),
                         static_cast<float>(scaleY));
            if (!pass.lighting.directional.empty()) {
                const auto& light = pass.lighting.directional.front();
                gl.uniform3f(gl.getUniformLocation(skyProgram.id(), "sunDirection"),
                             static_cast<float>(light.direction[0]), static_cast<float>(light.direction[1]),
                             static_cast<float>(light.direction[2]));
                gl.uniform4f(gl.getUniformLocation(skyProgram.id(), "sunColor"), static_cast<float>(sky.sunColor.red),
                             static_cast<float>(sky.sunColor.green), static_cast<float>(sky.sunColor.blue), 1.0F);
            } else {
                gl.uniform4f(gl.getUniformLocation(skyProgram.id(), "sunColor"), 0, 0, 0, 1);
                gl.uniform3f(gl.getUniformLocation(skyProgram.id(), "sunDirection"), 0, 0, 1);
            }
            gl.uniform1f(gl.getUniformLocation(skyProgram.id(), "sunGlow"), static_cast<float>(sky.sunGlow));
            gl.bindVertexArray(emptyVao.id());
            gl.drawArrays(GL_TRIANGLES, 0, 3);
            gl.depthMask(GL_TRUE);
            gl.depthFunc(GL_LESS);
        }
        return {};
    }

    template <typename Pass>
    std::expected<void, RenderError> pass2D(const render_contracts::RenderSnapshot& snapshot, const Pass& pass,
                                            const render_contracts::Camera2* camera) {
        gl.disable(GL_DEPTH_TEST);
        gl.enable(GL_BLEND);
        gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.useProgram(screenProgram.id());
        const auto width = camera == nullptr ? static_cast<double>(snapshot.imageWidth) : camera->width;
        const auto height = camera == nullptr ? static_cast<double>(snapshot.imageHeight) : camera->height;
        std::array<float, 16> projection2{2.0F / static_cast<float>(width),
                                          0,
                                          0,
                                          0,
                                          0,
                                          2.0F / static_cast<float>(height),
                                          0,
                                          0,
                                          0,
                                          0,
                                          1,
                                          0,
                                          0,
                                          0,
                                          0,
                                          1};
        if (camera == nullptr) {
            projection2[5] = -projection2[5];
            projection2[0] = 2.0F / static_cast<float>(snapshot.imageWidth);
            projection2[12] = -1.0F;
            projection2[13] = 1.0F;
        }
        gl.uniformMatrix4fv(gl.getUniformLocation(screenProgram.id(), "transform"), 1, GL_FALSE, projection2.data());
        std::vector<const render_contracts::DrawItem2*> sorted;
        sorted.reserve(pass.drawItems.size());
        for (const auto& item : pass.drawItems) {
            sorted.push_back(&item);
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto* first, const auto* second) {
            return std::tie(first->layer, first->order) < std::tie(second->layer, second->order);
        });
        for (const auto* item : sorted) {
            if (item->resource >= snapshot.resources2.size()) {
                return std::unexpected(RenderError::InvalidResource);
            }
            if (item->material >= snapshot.materials.size() || !snapshot.materials[item->material]) {
                return std::unexpected(RenderError::InvalidMaterial);
            }
            const auto& resource = snapshot.resources2[item->resource];
            const std::size_t triangleCount = resource.triangles.size() / sizeof(render_contracts::TriangleIndices);
            if (resource.triangles.size() % sizeof(render_contracts::TriangleIndices) != 0) {
                return std::unexpected(RenderError::InvalidResource);
            }
            const auto* triangles =
                reinterpret_cast<const render_contracts::TriangleIndices*>(resource.triangles.data());
            std::vector<Vertex> vertices;
            vertices.reserve(resource.positions.size());
            for (std::size_t index = 0; index < resource.positions.size(); ++index) {
                // World2D переводит мировые координаты через камеру; Hud2D использует
                // экранные пиксели напрямую — ортографическая проекция выше уже
                // масштабирует (0..width, 0..height) в NDC.
                auto point = math::transformPoint(item->localToWorld, resource.positions[index]);
                if (camera != nullptr) {
                    point = math::transformPoint(camera->worldToView, point);
                }
                Vertex vertex{};
                vertex.position[0] = static_cast<float>(point[0]);
                vertex.position[1] = static_cast<float>(point[1]);
                if (index < resource.texCoords.size()) {
                    vertex.uv[0] = static_cast<float>(resource.texCoords[index][0]);
                    vertex.uv[1] = static_cast<float>(resource.texCoords[index][1]);
                }
                vertices.push_back(vertex);
            }
            for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
                const auto& indices = triangles[triangle];
                if (indices.a >= vertices.size() || indices.b >= vertices.size() || indices.c >= vertices.size()) {
                    return std::unexpected(RenderError::InvalidResource);
                }
            }
            GlVertexArray vao(gl, generate(gl.genVertexArrays));
            gl.bindVertexArray(vao.id());
            GlBuffer vbo(gl, generate(gl.genBuffers));
            gl.bindBuffer(GL_ARRAY_BUFFER, vbo.id());
            gl.bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                          GL_STREAM_DRAW);
            GlBuffer ibo(gl, generate(gl.genBuffers));
            gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo.id());
            gl.bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(resource.triangles.size()),
                          resource.triangles.data(), GL_STREAM_DRAW);
            gl.enableVertexAttribArray(0);
            gl.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, position)));
            gl.enableVertexAttribArray(1);
            gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, uv)));
            gl.enableVertexAttribArray(2);
            gl.vertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, normal)));
            uniforms(screenProgram.id(), *snapshot.materials[item->material]);
            gl.polygonMode(GL_FRONT_AND_BACK, item->wireframe ? GL_LINE : GL_FILL);
            gl.drawElements(GL_TRIANGLES, static_cast<GLsizei>(resource.triangles.size() / sizeof(std::uint32_t)),
                            GL_UNSIGNED_INT, nullptr);
            gl.bindVertexArray(0);
        }
        gl.polygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return {};
    }

    /// @brief Таблица функций объявлена первой, чтобы пережить RAII-члены при разрушении.
    GlFunctions gl{};
    GlProgram worldProgram;
    GlProgram screenProgram;
    GlProgram skyProgram;
    GlProgram shadowProgram;
    GlVertexArray emptyVao;
    GlTexture shadowTexture;
    GlFramebuffer shadowFramebuffer;
    GlTexture captureTexture;
    GlRenderbuffer captureDepth;
    GlFramebuffer captureFramebuffer;
    std::size_t captureWidth{};
    std::size_t captureHeight{};
    std::size_t shadowResolution{};
    std::uint64_t frameCounter{};
    std::unordered_map<MeshKey, GlMesh, MeshKeyHash> meshes;
    std::unordered_map<std::uint64_t, GlTextureEntry> textures;
};

GlRenderer::GlRenderer(GlProcResolver resolver) : m_implementation{std::make_unique<Implementation>(resolver)} {}
GlRenderer::~GlRenderer() = default;
GlRenderer::GlRenderer(GlRenderer&&) noexcept = default;
GlRenderer& GlRenderer::operator=(GlRenderer&&) noexcept = default;

std::expected<void, RenderError> GlRenderer::render(const render_contracts::RenderSnapshot& snapshot) {
    ++m_implementation->frameCounter;
    auto& gl = m_implementation->gl;
    gl.viewport(0, 0, static_cast<GLsizei>(snapshot.imageWidth), static_cast<GLsizei>(snapshot.imageHeight));
    gl.clearColor(static_cast<float>(snapshot.clearColor.red), static_cast<float>(snapshot.clearColor.green),
                  static_cast<float>(snapshot.clearColor.blue), static_cast<float>(snapshot.clearColor.alpha));
    gl.clearDepth(1.0);
    gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (const auto& renderPass : snapshot.passes) {
        auto result = std::visit(
            [&](const auto& pass) -> std::expected<void, RenderError> {
                using Pass = std::decay_t<decltype(pass)>;
                if constexpr (std::is_same_v<Pass, render_contracts::World3DPass>) {
                    return m_implementation->world3D(snapshot, pass);
                } else if constexpr (std::is_same_v<Pass, render_contracts::World2DPass>) {
                    return m_implementation->pass2D(snapshot, pass, &pass.camera);
                } else {
                    return m_implementation->pass2D(snapshot, pass, nullptr);
                }
            },
            renderPass);
        if (!result) {
            return result;
        }
    }
    gl.disable(GL_BLEND);
    m_implementation->sweepUnusedResources();
    return {};
}

std::expected<std::unique_ptr<image::ImageBuffer>, RenderError>
GlRenderer::capture(const render_contracts::RenderSnapshot& snapshot) {
    auto& implementation = *m_implementation;
    auto& gl = implementation.gl;

    int previousFramebuffer = 0;
    gl.getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    int previousReadFramebuffer = 0;
    gl.getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    // Readback выполняется из собственного FBO: это работает даже там, где default
    // framebuffer недоступен для чтения (например, у GLX-pbuffer контекстов).
    if (implementation.captureFramebuffer.id() == 0 || implementation.captureWidth != snapshot.imageWidth ||
        implementation.captureHeight != snapshot.imageHeight) {
        const auto textureId = implementation.generate(gl.genTextures);
        implementation.captureTexture = GlTexture(gl, textureId);
        gl.bindTexture(GL_TEXTURE_2D, textureId);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(snapshot.imageWidth),
                      static_cast<int>(snapshot.imageHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        const auto depthId = implementation.generate(gl.genRenderbuffers);
        implementation.captureDepth = GlRenderbuffer(gl, depthId);
        gl.bindRenderbuffer(GL_RENDERBUFFER, depthId);
        gl.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<int>(snapshot.imageWidth),
                               static_cast<int>(snapshot.imageHeight));
        const auto framebufferId = implementation.generate(gl.genFramebuffers);
        implementation.captureFramebuffer = GlFramebuffer(gl, framebufferId);
        gl.bindFramebuffer(GL_FRAMEBUFFER, framebufferId);
        gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
        gl.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthId);
        implementation.captureWidth = snapshot.imageWidth;
        implementation.captureHeight = snapshot.imageHeight;
    }

    gl.bindFramebuffer(GL_FRAMEBUFFER, implementation.captureFramebuffer.id());

    auto rendered = render(snapshot);
    if (!rendered) {
        gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousReadFramebuffer));
        return std::unexpected(rendered.error());
    }
    auto framebuffer = image::ImageBuffer::create({snapshot.imageWidth, snapshot.imageHeight});
    if (!framebuffer) {
        gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousReadFramebuffer));
        return std::unexpected(RenderError::SizeMismatch);
    }
    gl.pixelStorei(GL_PACK_ROW_LENGTH, static_cast<int>(framebuffer->stride() / 4));
    gl.readPixels(0, 0, static_cast<int>(snapshot.imageWidth), static_cast<int>(snapshot.imageHeight), GL_RGBA,
                  GL_UNSIGNED_BYTE, framebuffer->bytes().data());
    gl.pixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
    gl.bindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousReadFramebuffer));
    std::vector<std::byte> row(framebuffer->stride());
    for (std::size_t y = 0; y < snapshot.imageHeight / 2; ++y) {
        auto* top = framebuffer->bytes().data() + y * framebuffer->stride();
        auto* bottom = framebuffer->bytes().data() + (snapshot.imageHeight - y - 1) * framebuffer->stride();
        std::memcpy(row.data(), top, row.size());
        std::memcpy(top, bottom, row.size());
        std::memcpy(bottom, row.data(), row.size());
    }
    return std::make_unique<image::ImageBuffer>(std::move(*framebuffer));
}

} // namespace huynity::renderer
