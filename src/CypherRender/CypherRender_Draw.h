//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Draw.h
//  Purpose: Declares immediate backend-neutral indexed triangle submission.
//  Details: Draw records reference validated renderer handles and numeric
//           ranges. They never contain native OpenGL names or pointers to
//           transient caller-owned geometry.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_DRAW_H
#define CYPHER_ENGINE_RENDER_DRAW_H
#pragma once

#include "CypherRender_Pipeline.h"
#include "CypherRender_Texture.h"
#include "CypherRender_VertexInput.h"

namespace cypher::engine::render
{

struct render_draw_indexed_desc_t {
    render_pipeline_handle_t pipeline{};
    render_vertex_input_handle_t vertexInput{};
    render_buffer_handle_t uniformBuffer{}; // Whole buffer at binding zero, when required.
    ::cypher::common::u32 firstIndex{ 0u }; // Relative to the vertex input's index offset.
    ::cypher::common::u32 indexCount{ 0u }; // Positive multiple of three.
    ::cypher::common::u32 vertexCount{ 0u }; // Addressable vertices beginning at vertex zero.
    render_texture_handle_t sampledTexture{}; // Required exactly when the pipeline declares a sampler.
};

// Executes immediately inside an active frame on the renderer thread. The
// layout must match the pipeline; no referenced buffer may be CPU-mapped.
// Buffer ranges are checked without reading indices back from the GPU: the
// caller guarantees every selected index is strictly less than vertexCount.
// No instancing, base vertex, primitive restart, or deferred pointer borrowing.
CYPHER_NODISCARD render_error_t R_DrawIndexed(
    const render_draw_indexed_desc_t &description ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_DRAW_H
