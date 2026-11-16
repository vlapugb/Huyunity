#pragma once

#include <GL/glcorearb.h>

namespace huynity::renderer::detail {

/// @brief Минимально необходимый набор функций OpenGL 3.3 Core.
///
/// Указатели загружаются через GlProcResolver активного контекста (в нативном окне —
/// SDL_GL_GetProcAddress), поэтому реализация GL-рендерера не зависит от SDL и
/// компонуется без привязки к конкретному окну или библиотеке загрузки.
struct GlFunctions {
    PFNGLGETSTRINGPROC getString{};
    PFNGLGETERRORPROC getError{};
    PFNGLGETINTEGERVPROC getIntegerv{};

    PFNGLVIEWPORTPROC viewport{};
    PFNGLENABLEPROC enable{};
    PFNGLDISABLEPROC disable{};
    PFNGLDEPTHFUNCPROC depthFunc{};
    PFNGLDEPTHMASKPROC depthMask{};
    PFNGLCULLFACEPROC cullFace{};
    PFNGLFRONTFACEPROC frontFace{};
    PFNGLBLENDFUNCPROC blendFunc{};
    PFNGLPOLYGONMODEPROC polygonMode{};
    PFNGLCLEARCOLORPROC clearColor{};
    PFNGLCLEARDEPTHPROC clearDepth{};
    PFNGLCLEARPROC clear{};
    PFNGLPIXELSTOREIPROC pixelStorei{};
    PFNGLREADPIXELSPROC readPixels{};
    PFNGLACTIVETEXTUREPROC activeTexture{};
    PFNGLDRAWBUFFERPROC drawBuffer{};
    PFNGLREADBUFFERPROC readBuffer{};
    PFNGLDRAWARRAYSPROC drawArrays{};
    PFNGLDRAWELEMENTSPROC drawElements{};

    PFNGLCREATESHADERPROC createShader{};
    PFNGLSHADERSOURCEPROC shaderSource{};
    PFNGLCOMPILESHADERPROC compileShader{};
    PFNGLGETSHADERIVPROC getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog{};
    PFNGLDELETESHADERPROC deleteShader{};
    PFNGLCREATEPROGRAMPROC createProgram{};
    PFNGLATTACHSHADERPROC attachShader{};
    PFNGLLINKPROGRAMPROC linkProgram{};
    PFNGLGETPROGRAMIVPROC getProgramiv{};
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog{};
    PFNGLDELETEPROGRAMPROC deleteProgram{};
    PFNGLUSEPROGRAMPROC useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
    PFNGLUNIFORM1IPROC uniform1i{};
    PFNGLUNIFORM1FPROC uniform1f{};
    PFNGLUNIFORM2FPROC uniform2f{};
    PFNGLUNIFORM3FPROC uniform3f{};
    PFNGLUNIFORM4FPROC uniform4f{};
    PFNGLUNIFORMMATRIX4FVPROC uniformMatrix4fv{};

    PFNGLGENVERTEXARRAYSPROC genVertexArrays{};
    PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays{};
    PFNGLBINDVERTEXARRAYPROC bindVertexArray{};
    PFNGLGENBUFFERSPROC genBuffers{};
    PFNGLDELETEBUFFERSPROC deleteBuffers{};
    PFNGLBINDBUFFERPROC bindBuffer{};
    PFNGLBUFFERDATAPROC bufferData{};
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray{};
    PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer{};

    PFNGLGENTEXTURESPROC genTextures{};
    PFNGLDELETETEXTURESPROC deleteTextures{};
    PFNGLBINDTEXTUREPROC bindTexture{};
    PFNGLTEXPARAMETERIPROC texParameteri{};
    PFNGLTEXIMAGE2DPROC texImage2D{};

    PFNGLGENFRAMEBUFFERSPROC genFramebuffers{};
    PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers{};
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer{};
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D{};
    PFNGLGENRENDERBUFFERSPROC genRenderbuffers{};
    PFNGLDELETERENDERBUFFERSPROC deleteRenderbuffers{};
    PFNGLBINDRENDERBUFFERPROC bindRenderbuffer{};
    PFNGLRENDERBUFFERSTORAGEPROC renderbufferStorage{};
    PFNGLFRAMEBUFFERRENDERBUFFERPROC framebufferRenderbuffer{};
};

/// @brief Загружает все функции таблицы через resolver активного GL-контекста.
/// @return false, если resolver отсутствует или хотя бы одна функция не найдена.
[[nodiscard]] bool loadGlFunctions(GlFunctions& functions, void* (*resolver)(const char*)) noexcept;

} // namespace huynity::renderer::detail
