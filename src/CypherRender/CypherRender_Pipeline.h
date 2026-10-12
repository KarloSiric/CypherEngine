//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Pipeline.h
//  Purpose: Declares immutable indexed graphics-pipeline state.
//  Details: Pipelines retain a live shader and describe vertex interpretation,
//           depth, rasterization, blending, and one optional uniform block.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_PIPELINE_H
#define CYPHER_ENGINE_RENDER_PIPELINE_H
#pragma once

#include "CypherRender_Shader.h"
#include "CypherRender_VertexLayout.h"

namespace cypher::engine::render
{

using render_pipeline_handle_t = render_handle_t;
inline constexpr render_pipeline_handle_t R_INVALID_PIPELINE{};

enum class render_primitive_topology_t : ::cypher::common::u8 {
    TRIANGLE_LIST = 0,
    LINE_LIST,
    COUNT
};

// First-draw scope: triangle or line lists, solid fill, LESS depth,
// no stencil, and optional source-alpha blending into the presentation target.
// Lines use the driver's one-pixel rasterization; culling and front-face state
// have no effect on them. All calls execute on the renderer thread.
// Names are borrowed only by Create.
struct render_pipeline_desc_t {
    render_shader_handle_t shader{};
    render_vertex_layout_t vertexLayout{};
    bool depthTest{ true };
    bool depthWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    bool alphaBlend{ false };
    const char *uniformBlockName{ nullptr }; // Optional GLSL block, assigned binding zero.
    ::cypher::common::u32 uniformBlockBytes{ 0u }; // Exact reflected block size, including padding.
    const char *debugName{ nullptr };
    const char *sampledTextureName{ nullptr }; // Optional active sampler2D, unit zero.
    render_primitive_topology_t topology{ render_primitive_topology_t::TRIANGLE_LIST };
};

// Owns no pointers. A live pipeline retains its shader until destruction.
struct render_pipeline_info_t {
    render_shader_handle_t shader{};
    render_vertex_layout_t vertexLayout{};
    bool depthTest{ true };
    bool depthWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    bool alphaBlend{ false };
    ::cypher::common::u32 uniformBlockBytes{ 0u };
    bool sampledTextureRequired{ false };
    render_primitive_topology_t topology{ render_primitive_topology_t::TRIANGLE_LIST };
};

// Shader input locations and the optional block are checked against the linked
// program. Name and byte size must both be present or both absent. The first
// contract supports one active uniform block and one optional sampler2D. Other
// ordinary uniforms remain unsupported.
CYPHER_NODISCARD render_error_t R_CreateGraphicsPipeline(
    const render_pipeline_desc_t &description,
    CY_OUT render_pipeline_handle_t *pipelineOut ) noexcept;
CYPHER_NODISCARD render_error_t R_DestroyGraphicsPipeline(
    render_pipeline_handle_t pipeline ) noexcept;
CYPHER_NODISCARD bool R_IsGraphicsPipelineValid(
    render_pipeline_handle_t pipeline ) noexcept;
CYPHER_NODISCARD render_error_t R_GetGraphicsPipelineInfo(
    render_pipeline_handle_t pipeline,
    CY_OUT render_pipeline_info_t *infoOut ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_PIPELINE_H
