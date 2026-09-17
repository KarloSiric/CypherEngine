//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Public.h
//  Purpose: Declares the stable engine-facing renderer lifecycle contract.
//  Details: Host calls this frontend API. OpenGL, software, and Vulkan details
//           are selected and dispatched internally by the renderer module.
//
//  History:
//  - Created by Karlo Siric on 2026-09-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_PUBLIC_H
#define CYPHER_ENGINE_RENDER_PUBLIC_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherRender_Error.h"
#include "CypherRender_Buffer.h"
#include "CypherRender_Draw.h"
#include "CypherRender_HostSurface.h"
#include "CypherRender_Pipeline.h"
#include "CypherRender_Shader.h"
#include "CypherRender_Texture.h"
#include "CypherRender_Types.h"
#include "CypherRender_VertexInput.h"
#include "CypherSystem/CypherSystem_Window.h"

namespace cypher::engine::render
{

// Binary contract version shared by Host and renderer implementations. This is
// deliberately independent from the CypherEngine product version.
inline constexpr ::cypher::common::u32 RENDER_API_VERSION = 1u;

/*
===============================================================================

    Renderer configuration

R_ConfigureWindow runs before Sys_CreateWindow because graphics APIs require
some native window attributes to be selected before the window exists. Host
still owns the title, dimensions, and fullscreen policy in windowDescription;
the renderer may alter only graphics-related fields.

===============================================================================
*/
CYPHER_NODISCARD render_config_t R_DefaultConfig() noexcept;

CYPHER_NODISCARD render_error_t R_ValidateConfig(
    const render_config_t &config ) noexcept;

CYPHER_NODISCARD render_error_t R_ConfigureWindow(
    const render_config_t &config,
    ::cypher::engine::sys::window_desc_t &windowDescription ) noexcept;

/*
===============================================================================

    Renderer lifecycle

For a standalone surface, Host creates the System window after
R_ConfigureWindow, then passes that owning window record to R_Init. The renderer
borrows the window and owns its graphics context/device.

For an embedded surface, the tool creates and owns the context, then passes a
callback descriptor to R_InitHostSurface. In both paths the owning host object
must outlive R_Shutdown.

===============================================================================
*/
CYPHER_NODISCARD render_error_t R_Init(
    ::cypher::engine::sys::window_t &window,
    const render_config_t &config ) noexcept;

// Initializes against a graphics context owned by an editor or embedding host.
// The host must destroy that context only after R_Shutdown has returned.
CYPHER_NODISCARD render_error_t R_ValidateHostSurface(
    const render_host_surface_desc_t &surface ) noexcept;

CYPHER_NODISCARD render_error_t R_InitHostSurface(
    const render_host_surface_desc_t &surface,
    const render_config_t &config ) noexcept;

CYPHER_NODISCARD render_error_t R_Shutdown() noexcept;

CYPHER_NODISCARD bool R_IsInitialized() noexcept;
CYPHER_NODISCARD bool R_IsFrameActive() noexcept;

// The returned record is renderer-owned and remains valid until R_Shutdown.
CYPHER_NODISCARD const render_info_t *R_GetInfo() noexcept;

/*
===============================================================================

    Frame lifecycle

Exactly one R_BeginFrame/R_EndFrame pair is legal at a time. Resize is explicit
so minimized windows, zero-sized drawables, and backend storage recreation can
be handled without hiding policy inside a draw call.

===============================================================================
*/
CYPHER_NODISCARD render_error_t R_BeginFrame(
    const render_frame_info_t &frameInfo ) noexcept;

CYPHER_NODISCARD render_error_t R_Resize(
    const render_extent_t &drawableExtent ) noexcept;

CYPHER_NODISCARD render_error_t R_EndFrame() noexcept;

// Presentation policy changes are serialized with frame submission; callers
// must invoke these operations outside an active BeginFrame/EndFrame pair.
CYPHER_NODISCARD render_error_t R_SetPresentMode(
    render_present_mode_t presentMode ) noexcept;

CYPHER_NODISCARD render_error_t R_WaitIdle() noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_PUBLIC_H
