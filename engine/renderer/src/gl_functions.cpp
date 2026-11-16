#include "gl_functions.h"

namespace huynity::renderer::detail {

bool loadGlFunctions(GlFunctions& functions, void* (*resolver)(const char*)) noexcept {
    if (resolver == nullptr) {
        return false;
    }
    const auto load = [resolver](const char* name) noexcept { return resolver(name); };
    bool complete = true;
    const auto require = [&complete](void* pointer) noexcept {
        if (pointer == nullptr) {
            complete = false;
        }
        return pointer;
    };

    functions.getString = reinterpret_cast<PFNGLGETSTRINGPROC>(require(load("glGetString")));
    functions.getError = reinterpret_cast<PFNGLGETERRORPROC>(require(load("glGetError")));
    functions.getIntegerv = reinterpret_cast<PFNGLGETINTEGERVPROC>(require(load("glGetIntegerv")));

    functions.viewport = reinterpret_cast<PFNGLVIEWPORTPROC>(require(load("glViewport")));
    functions.enable = reinterpret_cast<PFNGLENABLEPROC>(require(load("glEnable")));
    functions.disable = reinterpret_cast<PFNGLDISABLEPROC>(require(load("glDisable")));
    functions.depthFunc = reinterpret_cast<PFNGLDEPTHFUNCPROC>(require(load("glDepthFunc")));
    functions.depthMask = reinterpret_cast<PFNGLDEPTHMASKPROC>(require(load("glDepthMask")));
    functions.cullFace = reinterpret_cast<PFNGLCULLFACEPROC>(require(load("glCullFace")));
    functions.frontFace = reinterpret_cast<PFNGLFRONTFACEPROC>(require(load("glFrontFace")));
    functions.blendFunc = reinterpret_cast<PFNGLBLENDFUNCPROC>(require(load("glBlendFunc")));
    functions.polygonMode = reinterpret_cast<PFNGLPOLYGONMODEPROC>(require(load("glPolygonMode")));
    functions.clearColor = reinterpret_cast<PFNGLCLEARCOLORPROC>(require(load("glClearColor")));
    functions.clearDepth = reinterpret_cast<PFNGLCLEARDEPTHPROC>(require(load("glClearDepth")));
    functions.clear = reinterpret_cast<PFNGLCLEARPROC>(require(load("glClear")));
    functions.pixelStorei = reinterpret_cast<PFNGLPIXELSTOREIPROC>(require(load("glPixelStorei")));
    functions.readPixels = reinterpret_cast<PFNGLREADPIXELSPROC>(require(load("glReadPixels")));
    functions.activeTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(require(load("glActiveTexture")));
    functions.drawBuffer = reinterpret_cast<PFNGLDRAWBUFFERPROC>(require(load("glDrawBuffer")));
    functions.readBuffer = reinterpret_cast<PFNGLREADBUFFERPROC>(require(load("glReadBuffer")));
    functions.drawArrays = reinterpret_cast<PFNGLDRAWARRAYSPROC>(require(load("glDrawArrays")));
    functions.drawElements = reinterpret_cast<PFNGLDRAWELEMENTSPROC>(require(load("glDrawElements")));

    functions.createShader = reinterpret_cast<PFNGLCREATESHADERPROC>(require(load("glCreateShader")));
    functions.shaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(require(load("glShaderSource")));
    functions.compileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(require(load("glCompileShader")));
    functions.getShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(require(load("glGetShaderiv")));
    functions.getShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(require(load("glGetShaderInfoLog")));
    functions.deleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(require(load("glDeleteShader")));
    functions.createProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(require(load("glCreateProgram")));
    functions.attachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(require(load("glAttachShader")));
    functions.linkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(require(load("glLinkProgram")));
    functions.getProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(require(load("glGetProgramiv")));
    functions.getProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(require(load("glGetProgramInfoLog")));
    functions.deleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(require(load("glDeleteProgram")));
    functions.useProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(require(load("glUseProgram")));
    functions.getUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(require(load("glGetUniformLocation")));
    functions.uniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(require(load("glUniform1i")));
    functions.uniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(require(load("glUniform1f")));
    functions.uniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(require(load("glUniform2f")));
    functions.uniform3f = reinterpret_cast<PFNGLUNIFORM3FPROC>(require(load("glUniform3f")));
    functions.uniform4f = reinterpret_cast<PFNGLUNIFORM4FPROC>(require(load("glUniform4f")));
    functions.uniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(require(load("glUniformMatrix4fv")));

    functions.genVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(require(load("glGenVertexArrays")));
    functions.deleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(require(load("glDeleteVertexArrays")));
    functions.bindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(require(load("glBindVertexArray")));
    functions.genBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(require(load("glGenBuffers")));
    functions.deleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(require(load("glDeleteBuffers")));
    functions.bindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(require(load("glBindBuffer")));
    functions.bufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(require(load("glBufferData")));
    functions.enableVertexAttribArray =
        reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(require(load("glEnableVertexAttribArray")));
    functions.vertexAttribPointer =
        reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(require(load("glVertexAttribPointer")));

    functions.genTextures = reinterpret_cast<PFNGLGENTEXTURESPROC>(require(load("glGenTextures")));
    functions.deleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC>(require(load("glDeleteTextures")));
    functions.bindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC>(require(load("glBindTexture")));
    functions.texParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC>(require(load("glTexParameteri")));
    functions.texImage2D = reinterpret_cast<PFNGLTEXIMAGE2DPROC>(require(load("glTexImage2D")));

    functions.genFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(require(load("glGenFramebuffers")));
    functions.deleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(require(load("glDeleteFramebuffers")));
    functions.bindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(require(load("glBindFramebuffer")));
    functions.framebufferTexture2D =
        reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(require(load("glFramebufferTexture2D")));
    functions.genRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(require(load("glGenRenderbuffers")));
    functions.deleteRenderbuffers =
        reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(require(load("glDeleteRenderbuffers")));
    functions.bindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(require(load("glBindRenderbuffer")));
    functions.renderbufferStorage =
        reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(require(load("glRenderbufferStorage")));
    functions.framebufferRenderbuffer =
        reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(require(load("glFramebufferRenderbuffer")));

    return complete;
}

} // namespace huynity::renderer::detail
