//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_HostSurface.h
//  Purpose: Declares the renderer contract for host-owned graphics surfaces.
//  Details: Editor frameworks may own a graphics context and presentation
//           lifecycle. This contract lets CypherRender borrow that context
//           without depending on Qt, SDL, or another host toolkit.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_HOST_SURFACE_H
#define CYPHER_ENGINE_RENDER_HOST_SURFACE_H
#pragma once

#include "CypherRender_Types.h"

#include <type_traits>

namespace cypher::engine::render
{

/*
===============================================================================

    Host-owned render surface

An editor can own the native context and presentation lifecycle while the
renderer continues to own GPU resources and draw submission. The descriptor is
copied during initialization; userData and the objects reached by its callbacks
must remain valid until R_Shutdown returns.

The current renderer frontend is a singleton, so one process may initialize one
standalone window or one host surface at a time. All calls stay on the host's
graphics thread. Resource creation and destruction must run while the borrowed
context is current; frame, resize, wait, and shutdown operations activate it
through ActivateContext themselves.

ActivateContext and ResolveProcAddress are required by the OpenGL backend.
PrepareFrame is optional and runs after activation at the beginning of every
frame; Qt hosts use it to restore the QOpenGLWidget framebuffer. Present is
optional because widgets normally composite after paintGL returns. When Present
is absent, R_EndFrame flushes commands and leaves presentation to the host.

===============================================================================
*/
using render_host_proc_t = void ( * )( void );

using render_host_activate_context_fn_t = bool ( * )(
    void *userData ) noexcept;

using render_host_resolve_proc_address_fn_t = render_host_proc_t ( * )(
    const char *procedureName,
    void *userData ) noexcept;

using render_host_prepare_frame_fn_t = bool ( * )(
    void *userData ) noexcept;

using render_host_present_fn_t = bool ( * )(
    void *userData ) noexcept;

using render_host_set_present_mode_fn_t = bool ( * )(
    render_present_mode_t requestedMode,
    render_present_mode_t *actualModeOut,
    void *userData ) noexcept;

struct render_host_surface_desc_t {
    render_backend_t backend{ render_backend_t::AUTO }; // Concrete API already created by the host.
    render_extent_t drawableExtent{};                   // Initial target size in physical pixels.
    render_present_mode_t presentMode{ render_present_mode_t::FIFO }; // Actual host presentation policy.

    ::cypher::common::u16 apiMajorVersion{ 0u }; // Version of the current host-owned graphics context.
    ::cypher::common::u16 apiMinorVersion{ 0u };
    ::cypher::common::u8 sampleCount{ 0u };      // Samples in the host's active default target.
    bool sRGBFramebuffer{ false };               // Active default target accepts sRGB conversion.
    bool accelerated{ false };                   // Host reports a hardware-backed context.

    void *userData{ nullptr }; // Borrowed opaque host object passed to every callback.
    render_host_activate_context_fn_t ActivateContext{ nullptr };
    render_host_resolve_proc_address_fn_t ResolveProcAddress{ nullptr };
    render_host_prepare_frame_fn_t PrepareFrame{ nullptr };
    render_host_present_fn_t Present{ nullptr };
    render_host_set_present_mode_fn_t SetPresentMode{ nullptr };
};

static_assert( std::is_trivially_copyable_v<render_host_surface_desc_t> );
static_assert( std::is_standard_layout_v<render_host_surface_desc_t> );

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_HOST_SURFACE_H
