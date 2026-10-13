//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Draw.h
//  Purpose: Declares immediate backend-neutral indexed geometry submission.
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
#include "CypherRender_Sampler.h"
#include "CypherRender_Texture.h"
#include "CypherRender_VertexInput.h"

namespace cypher::engine::render
{

struct render_draw_indexed_desc_t {
    render_pipeline_handle_t pipeline{};
    render_vertex_input_handle_t vertexInput{};
    render_buffer_handle_t uniformBuffers[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    ::cypher::common::u32 firstIndex{ 0u }; // Relative to the vertex input's index offset.
    ::cypher::common::u32 indexCount{ 0u }; // Complete pairs or triples for pipeline topology.
    ::cypher::common::u32 vertexCount{ 0u }; // Addressable vertices beginning at vertex zero.
    render_texture_handle_t sampledTextures[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    render_sampler_handle_t samplers[R_MAX_SAMPLED_TEXTURE_BINDINGS]{}; // Optional per declared texture slot.
    ::cypher::common::u32 instanceCount{ 1u }; // Positive; one preserves ordinary indexed drawing.
};

// Executes immediately inside an active frame on the renderer thread. The
// layout must match the pipeline; no referenced buffer may be CPU-mapped.
// Buffer ranges are checked without reading indices back from the GPU: the
// caller guarantees every selected index is strictly less than vertexCount.
// Per-instance attributes advance according to their layout divisor. An
// explicit sampler overrides texture-owned sampling for its paired image; a
// null sampler retains the original policy. Mip samplers require generated
// mips. No base instance, base vertex, primitive restart, or deferred pointers.
CYPHER_NODISCARD render_error_t R_DrawIndexed(
    const render_draw_indexed_desc_t &description ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_DRAW_H
