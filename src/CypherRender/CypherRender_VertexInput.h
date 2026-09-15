//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_VertexInput.h
//  Purpose: Declares immutable geometry-stream binding objects.
//  Details: A vertex input joins a layout to live vertex and optional index
//           buffers. OpenGL represents it with a VAO; other backends may not.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_VERTEX_INPUT_H
#define CYPHER_ENGINE_RENDER_VERTEX_INPUT_H
#pragma once

#include "CypherRender_Buffer.h"
#include "CypherRender_VertexLayout.h"

namespace cypher::engine::render
{

using render_vertex_input_handle_t = render_handle_t;
inline constexpr render_vertex_input_handle_t R_INVALID_VERTEX_INPUT{};

/*
================
Vertex-buffer binding

The binding identity must match one render_vertex_binding_desc_t in the layout.
Offset selects the first byte of vertex or instance zero inside the buffer.
================
*/
struct render_vertex_buffer_binding_t {
    render_buffer_handle_t buffer{};       // Live buffer with R_BUFFER_USAGE_VERTEX.
    ::cypher::common::u64 offset{ 0u };    // Base byte offset for this stream.
    ::cypher::common::u8 binding{ 0u };    // Layout binding supplied by this buffer.
};

/*
================
Index-buffer binding

An invalid buffer means the vertex input is non-indexed. The index type belongs
to draw interpretation rather than buffer storage, because buffers are raw bytes.
================
*/
struct render_index_buffer_binding_t {
    render_buffer_handle_t buffer{};       // Live buffer with R_BUFFER_USAGE_INDEX.
    ::cypher::common::u64 offset{ 0u };    // First index byte available to draws.
    render_index_type_t type{ render_index_type_t::UINT16 }; // Width of one index.
};

/*
================
Vertex-input descriptor

The renderer copies every active-prefix value during creation. debugName is
borrowed only for the duration of R_CreateVertexInput and is never retained.
================
*/
struct render_vertex_input_desc_t {
    render_vertex_layout_t layout{}; // Attribute interpretation shared with the pipeline.
    render_vertex_buffer_binding_t vertexBuffers[R_MAX_VERTEX_BINDINGS]{};
    ::cypher::common::u32 vertexBufferCount{ 0u }; // Active prefix in vertexBuffers.
    render_index_buffer_binding_t indexBuffer{};   // Invalid buffer selects non-indexed drawing.
    const char *debugName{ nullptr };               // Optional borrowed UTF-8 diagnostic label.
};

struct render_vertex_input_info_t {
    render_vertex_layout_t layout{};
    render_vertex_buffer_binding_t vertexBuffers[R_MAX_VERTEX_BINDINGS]{};
    ::cypher::common::u32 vertexBufferCount{ 0u };
    render_index_buffer_binding_t indexBuffer{};
};

// Resolves all buffers and verifies layout, usage, offsets, ranges, and alignment.
CYPHER_NODISCARD render_error_t R_ValidateVertexInputDesc(
    const render_vertex_input_desc_t &description ) noexcept;

// Creates one frontend object and one backend-specific input representation.
CYPHER_NODISCARD render_error_t R_CreateVertexInput(
    const render_vertex_input_desc_t &description,
    CY_OUT render_vertex_input_handle_t *vertexInputOut ) noexcept;

// Releases the backend object and all buffer references held by this input.
CYPHER_NODISCARD render_error_t R_DestroyVertexInput(
    render_vertex_input_handle_t vertexInput ) noexcept;

CYPHER_NODISCARD bool R_IsVertexInputValid(
    render_vertex_input_handle_t vertexInput ) noexcept;

CYPHER_NODISCARD render_error_t R_GetVertexInputInfo(
    render_vertex_input_handle_t vertexInput,
    CY_OUT render_vertex_input_info_t *infoOut ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_VERTEX_INPUT_H
