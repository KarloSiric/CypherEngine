//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_OpenGL.h
//  Purpose: Declares the platform-facing OpenGL context bridge.
//  Details: CypherRender owns OpenGL policy and rendering state. CypherSystem
//           performs the native window/context operations required by that
//           policy without exposing SDL or OpenGL loader declarations here.
//
//  History:
//  - Created by Karlo Siric on 2026-05-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_OPENGL_H
#define CYPHER_ENGINE_SYSTEM_OPENGL_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSystem_Error.h"
#include "CypherCommon_Annotations.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::engine::sys
{

struct window_t;

/*
===============================================================================

    OpenGL context configuration

The renderer fills this record before window creation. CypherSystem translates
the values into native SDL attributes, but does not choose the OpenGL version,
profile, framebuffer format, or debugging policy.

===============================================================================
*/
enum class gl_profile_t : common::u8 {
    NONE = 0u,      // Invalid profile; catches a missing renderer decision.
    CORE,           // Modern OpenGL core profile without removed fixed-function APIs.
    COMPATIBILITY,  // Compatibility profile for explicit legacy experiments only.
    ES,             // OpenGL ES profile when a future target requires it.
    COUNT
};

using gl_context_flags_t = ::cypher::common::flags32_t;

enum gl_context_flag_t : gl_context_flags_t {
    GLIMP_CONTEXT_NONE               = 0u,
    GLIMP_CONTEXT_DEBUG              = CYPHER_BIT32( 0 ), // Requests driver debug-message support.
    GLIMP_CONTEXT_FORWARD_COMPATIBLE = CYPHER_BIT32( 1 ), // Removes functionality deprecated by the requested version.
    GLIMP_CONTEXT_ROBUST_ACCESS      = CYPHER_BIT32( 2 ), // Requests robust out-of-bounds access handling.
    GLIMP_CONTEXT_RESET_ISOLATION    = CYPHER_BIT32( 3 )  // Isolates reset notification from other contexts.
};

constexpr gl_context_flags_t GLIMP_CONTEXT_FLAG_MASK =
    GLIMP_CONTEXT_DEBUG |
    GLIMP_CONTEXT_FORWARD_COMPATIBLE |
    GLIMP_CONTEXT_ROBUST_ACCESS |
    GLIMP_CONTEXT_RESET_ISOLATION;

struct gl_context_desc_t {
    common::u8 majorVersion{ 0u }; // Required OpenGL major version; zero is intentionally invalid.
    common::u8 minorVersion{ 0u }; // Required OpenGL minor version.
    gl_profile_t profile{ gl_profile_t::NONE }; // Core, compatibility, or ES context profile.
    gl_context_flags_t flags{ GLIMP_CONTEXT_NONE }; // Debug, forward-compatible, and robustness requests.

    common::u8 redBits{ 8u };      // Minimum red-channel bits in the default framebuffer.
    common::u8 greenBits{ 8u };    // Minimum green-channel bits in the default framebuffer.
    common::u8 blueBits{ 8u };     // Minimum blue-channel bits in the default framebuffer.
    common::u8 alphaBits{ 8u };    // Minimum alpha-channel bits in the default framebuffer.
    common::u8 depthBits{ 24u };   // Minimum depth-buffer precision.
    common::u8 stencilBits{ 8u };  // Minimum stencil-buffer precision.
    common::u8 sampleCount{ 0u };  // Zero disables MSAA; otherwise use a supported power of two.

    bool doubleBuffered{ true };   // Creates front/back buffers suitable for window presentation.
    bool sRGBFramebuffer{ true };  // Requests an sRGB-capable default framebuffer.
    bool requireAcceleration{ true }; // Rejects a software OpenGL implementation when true.
};

struct gl_context_info_t {
    common::u8 majorVersion{ 0u }; // Actual context major version reported by the window system.
    common::u8 minorVersion{ 0u }; // Actual context minor version reported by the window system.
    gl_profile_t profile{ gl_profile_t::NONE }; // Actual profile selected by the driver.
    gl_context_flags_t flags{ GLIMP_CONTEXT_NONE }; // Context flags actually granted.

    common::u8 redBits{ 0u };      // Actual red-channel precision.
    common::u8 greenBits{ 0u };    // Actual green-channel precision.
    common::u8 blueBits{ 0u };     // Actual blue-channel precision.
    common::u8 alphaBits{ 0u };    // Actual alpha-channel precision.
    common::u8 depthBits{ 0u };    // Actual depth-buffer precision.
    common::u8 stencilBits{ 0u };  // Actual stencil-buffer precision.
    common::u8 sampleCount{ 0u };  // Actual default-framebuffer sample count.

    bool doubleBuffered{ false };  // Driver created distinct front and back buffers.
    bool sRGBFramebuffer{ false }; // Default framebuffer supports sRGB conversion.
    bool accelerated{ false };     // Context uses a hardware-accelerated visual.
};

/*
================
OpenGL context handle

The native pointer is owned by the renderer but created and destroyed through
CypherSystem. It must never be cast to an SDL type outside CypherSystem.
================
*/
struct gl_context_t {
    void *nativeContext{ nullptr }; // Opaque SDL_GLContext value.
};

using gl_proc_t = void ( * )( void ); // Generic OpenGL entry point returned to GLAD.

enum class gl_swap_interval_t : common::i8 {
    ADAPTIVE = -1,    // Synchronize unless the frame has already missed retrace.
    IMMEDIATE = 0,    // Present without vertical synchronization.
    SYNCHRONIZED = 1  // Present on vertical retrace.
};

/*
===============================================================================

    OpenGL platform operations

All operations are main-thread only. A context is associated with exactly one
System window in V1. The renderer must destroy the context before that window.

===============================================================================
*/
CYPHER_NODISCARD bool GLimp_ContextDescIsValid(
    const gl_context_desc_t &description ) noexcept;

CYPHER_NODISCARD bool GLimp_IsContextValid(
    const gl_context_t &context ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_CreateContext(
    window_t &window,
    gl_context_t &contextOut ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_MakeCurrent(
    window_t &window,
    const gl_context_t &context ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_DestroyContext(
    gl_context_t &context ) noexcept;

CYPHER_NODISCARD gl_proc_t GLimp_GetProcAddress(
    const char *procedureName ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_QueryContextInfo(
    gl_context_info_t &infoOut ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_SetSwapInterval(
    gl_swap_interval_t interval ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_GetSwapInterval(
    gl_swap_interval_t &intervalOut ) noexcept;

CYPHER_NODISCARD sys_error_t GLimp_SwapWindow(
    window_t &window ) noexcept;

} // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_OPENGL_H
