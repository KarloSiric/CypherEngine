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
#include "CypherSystem/CypherSystem_OpenGL.h"
#include "CypherSystem/CypherSystem_Window.h"

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
    bool usesHostSurface{ false };                      // Context/presentation are borrowed from a tool host.

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
