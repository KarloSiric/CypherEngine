//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Local.h
//  Purpose: Reserves state shared by private OpenGL implementation files.
//  Details: This header is never included by the renderer frontend, Host, or
//           gameplay. It will own native object records, translation helpers,
//           state caching, and OpenGL-only diagnostics as modules are split.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_OPENGL_LOCAL_H
#define CYPHER_ENGINE_RENDER_OPENGL_LOCAL_H
#pragma once

#include "CypherCommon/Formats/CypherCommon_CookedShader.h"
#include "CypherRender/CypherRender_Error.h"
#include "CypherRender/CypherRender_HostSurface.h"
#include "CypherRender/CypherRender_Types.h"
#include "CypherSystem/Platform/CypherSystem_OpenGL.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <glad/gl.h>

namespace cypher::engine::render
{

/*
===============================================================================

    OpenGL private state

Only OpenGL implementation files may include this header. The renderer frontend
sees the opaque pointer stored in backend_api_t and never depends on this layout.

===============================================================================
*/

inline constexpr ::cypher::common::usize GL_STRING_CAPACITY = 256u;

// The host lends a live GL context. A frame may clear or draw in it, but must
// return every state item changed by this backend before control returns to
// the host. Hosted draws use indexed slot zero so the other draw buffers and
// viewports remain untouched; standalone draws retain their prior behavior.
struct gl_host_draw_buffer_state_t {
    GLboolean blend{ GL_FALSE };
    GLboolean colorMask[4]{ GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLint blendEquationRGB{ GL_FUNC_ADD };
    GLint blendEquationAlpha{ GL_FUNC_ADD };
    GLint blendSourceRGB{ GL_ONE };
    GLint blendDestinationRGB{ GL_ZERO };
    GLint blendSourceAlpha{ GL_ONE };
    GLint blendDestinationAlpha{ GL_ZERO };
};

struct gl_host_frame_state_t {
    gl_host_draw_buffer_state_t drawBuffer0{};
    GLint drawFramebuffer{ 0 };
    GLint readFramebuffer{ 0 };
    GLfloat viewport[4]{};
    GLint program{ 0 };
    GLint vertexArray{ 0 };
    GLint activeTexture{ GL_TEXTURE0 };
    GLint texture2DUnit0{ 0 };
    GLint samplerUnit0{ 0 };
    GLint texture2DUnit1{ 0 };
    GLint samplerUnit1{ 0 };
    GLint uniformBuffer{ 0 };       // Generic target, changed by BindBufferBase.
    GLint uniformBufferIndex0{ 0 }; // Indexed binding, with its range below.
    GLint64 uniformBufferStart0{ 0 };
    GLint64 uniformBufferSize0{ 0 };
    GLint uniformBufferIndex1{ 0 };
    GLint64 uniformBufferStart1{ 0 };
    GLint64 uniformBufferSize1{ 0 };
    GLint depthFunction{ GL_LESS };
    GLint cullFaceMode{ GL_BACK };
    GLint frontFace{ GL_CCW };
    GLint polygonMode[2]{ GL_FILL, GL_FILL };
    GLint stencilWriteMaskFront{ 0 };
    GLint stencilWriteMaskBack{ 0 };
    GLint clearStencil{ 0 };
    GLfloat lineWidth{ 1.0f };
    GLfloat polygonOffsetFactor{ 0.0f };
    GLfloat polygonOffsetUnits{ 0.0f };
    GLfloat clearColor[4]{};
    GLdouble clearDepth{ 1.0 };
    GLboolean depthWriteMask{ GL_TRUE };
    GLboolean scissorTest{ GL_FALSE };
    GLint scissorBox[4]{};
    GLboolean depthTest{ GL_FALSE };
    GLboolean cullFace{ GL_FALSE };
    GLboolean stencilTest{ GL_FALSE };
    GLboolean primitiveRestart{ GL_FALSE };
    GLboolean rasterizerDiscard{ GL_FALSE };
    GLboolean polygonOffsetFill{ GL_FALSE };
    GLboolean polygonOffsetLine{ GL_FALSE };
    GLboolean lineSmooth{ GL_FALSE };
    GLboolean framebufferSRGB{ GL_FALSE };
    GLboolean multisample{ GL_FALSE };
    bool captured{ false };
};

struct gl_state_t {
    ::cypher::engine::sys::window_t *window{ nullptr }; // Borrowed Host-owned presentation window.
    ::cypher::engine::sys::gl_context_t context{};      // Native context owned by this backend.
    render_host_surface_desc_t hostSurface{};           // Copied callbacks for a host-owned context.
    render_config_t config{};                           // Validated frontend startup policy.
    render_limits_t limits{};                           // Device limits queried after GLAD startup.
    render_extent_t drawableExtent{};                   // Current default-framebuffer dimensions.
    render_present_mode_t presentMode{ render_present_mode_t::FIFO }; // Actual swap policy.
    ::cypher::common::u64 activeFrameIndex{ 0u };        // Diagnostic identity of the active frame.
    bool initialized{ false };                          // Context and entry points are ready.
    bool frameActive{ false };                          // BeginFrame has no matching EndFrame.
    bool targetPassActive{ false };                     // One non-nested offscreen pass is open.
    render_extent_t targetPassExtent{};                 // Valid only during an offscreen pass.
    bool usesHostSurface{ false };                      // Context/presentation are borrowed from a tool host.

    // PrepareFrame may bind a host-owned, nonzero presentation FBO. A target
    // pass restores these actual bindings and the frame viewport, never an
    // assumed default framebuffer.
    GLint presentationDrawFramebuffer{ 0 };
    GLint presentationReadFramebuffer{ 0 };
    GLfloat presentationViewport[4]{};
    gl_host_frame_state_t hostFrameState{}; // Valid only while a hosted frame is active.

    char apiName[GL_STRING_CAPACITY]{};                 // Stable strings borrowed by render_info_t.
    char deviceName[GL_STRING_CAPACITY]{};
    char vendorName[GL_STRING_CAPACITY]{};
    char driverVersion[GL_STRING_CAPACITY]{};
    char shadingLanguageVersion[GL_STRING_CAPACITY]{};
};

extern gl_state_t glState;

// Establishes and checks explicit native-error boundaries around GL operations.
void GL_ClearErrors() noexcept;
CYPHER_NODISCARD render_error_t GL_CheckErrors(
    render_validation_t validation ) noexcept;

// Compiles one cooked stage on the renderer thread with this context current.
// Failure leaves shaderOut zero. Success transfers a temporary native stage to
// the caller, which must delete it after linking or rolling back program creation.
CYPHER_NODISCARD render_error_t GL_CompileShaderStage(
    const ::cypher::common::cooked_shader_stage_view_t &stage,
    const char *debugName,
    GLuint &shaderOut ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_LOCAL_H
