//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Local.h
//  Purpose: Declares renderer-frontend state shared by private modules.
//  Details: Buffer, shader, pipeline, texture, and draw implementations use
//           this header to reach the selected backend. Host and gameplay code
//           must include CypherRender_Public.h instead.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_LOCAL_H
#define CYPHER_ENGINE_RENDER_LOCAL_H
#pragma once

#include "CypherRender_Backend.h"

namespace cypher::engine::render
{

/*
===============================================================================

    Private renderer frontend state

The frontend owns policy and call ordering. The selected backend owns native
graphics objects through backend_api_t::state, while Host continues to own the
window referenced here. Renderer work remains confined to the render thread.

===============================================================================
*/
struct renderer_state_t {
    const backend_api_t *backend{ nullptr };            // Selected immutable dispatch table.
    ::cypher::engine::sys::window_t *window{ nullptr }; // Borrowed Host-owned presentation window.
    render_config_t config{};                           // Validated renderer policy requested by Host.
    render_info_t info{};                               // Actual device and capability snapshot.
    bool initialized{ false };                          // Backend initialization completed successfully.
    bool frameActive{ false };                          // BeginFrame succeeded without a matching EndFrame.
};

// Quake-family renderers traditionally call the frontend process state `tr`.
// The symbol is private to the CypherRender target despite being shared by its
// implementation translation units.
extern renderer_state_t tr;

// Resource modules initialize only after backend startup and release all
// native objects before the graphics context/device is destroyed.
CYPHER_NODISCARD render_error_t R_BufferSystemInit() noexcept;
CYPHER_NODISCARD render_error_t R_BufferSystemShutdown() noexcept;

// Vertex-input objects retain buffer references so native VAOs can never keep
// names of buffers that the frontend has already destroyed.
CYPHER_NODISCARD render_error_t R_BufferAcquireReference(
    render_buffer_handle_t buffer,
    render_buffer_usage_flags_t requiredUsage,
    CY_OUT backend_buffer_t *nativeOut,
    CY_OUT render_buffer_info_t *infoOut ) noexcept;
CYPHER_NODISCARD render_error_t R_BufferReleaseReference(
    render_buffer_handle_t buffer ) noexcept;

CYPHER_NODISCARD render_error_t R_VertexInputSystemInit() noexcept;
CYPHER_NODISCARD render_error_t R_VertexInputSystemShutdown() noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_LOCAL_H
