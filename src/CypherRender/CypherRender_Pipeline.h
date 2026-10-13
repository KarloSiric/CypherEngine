//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Pipeline.h
//  Purpose: Declares immutable indexed graphics-pipeline state.
//  Details: Pipelines retain a live shader and describe vertex interpretation,
//           depth, color writes, rasterization, blending, and resource slots.
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

enum class render_polygon_mode_t : ::cypher::common::u8 {
    FILL = 0, // Rasterize triangle interiors.
    LINE,     // Rasterize triangle edges at the driver's one-pixel line width.
    COUNT
};

enum class render_depth_compare_t : ::cypher::common::u8 {
    NEVER = 0,
    LESS,
    EQUAL,
    LESS_EQUAL,
    GREATER,
    NOT_EQUAL,
    GREATER_EQUAL,
    ALWAYS,
    COUNT
};

enum class render_blend_mode_t : ::cypher::common::u8 {
    DISABLED = 0,  // Replace destination color with the shader output.
    STRAIGHT_ALPHA, // RGB: source*sourceAlpha + destination*(1-sourceAlpha).
    ADDITIVE,       // RGB: source*sourceAlpha + destination; preserve destination alpha.
    COUNT
};

inline constexpr ::cypher::common::u32 R_MAX_UNIFORM_BLOCK_BINDINGS = 2u;
inline constexpr ::cypher::common::u32 R_MAX_SAMPLED_TEXTURE_BINDINGS = 2u;

enum class render_sampled_texture_kind_t : ::cypher::common::u8 {
    SCALAR_2D = 0,       // Active scalar sampler2D; RGBA8, RGBA16F, or raw D24 depth.
    DEPTH_COMPARE_2D,    // Active sampler2DShadow; D24 and an explicit compare sampler.
    COUNT
};

struct render_uniform_block_binding_t {
    const char *name{ nullptr };
    ::cypher::common::u32 byteSize{ 0u };
};

struct render_sampled_texture_binding_t {
    const char *name{ nullptr }; // Active GLSL sampler with the declared kind.
    render_sampled_texture_kind_t kind{ render_sampled_texture_kind_t::SCALAR_2D };
};

// First-draw scope: triangle or line lists, fill or triangle wireframe, selectable depth test,
// no stencil, and optional straight-alpha or additive blending into color output zero.
// Lines use the driver's one-pixel rasterization; culling and front-face state
// have no effect on them. Polygon LINE mode is valid only for TRIANGLE_LIST;
// LINE_LIST geometry is already lines. All calls execute on the renderer thread.
// Names are borrowed only by Create.
struct render_pipeline_desc_t {
    render_shader_handle_t shader{};
    render_vertex_layout_t vertexLayout{};
    bool depthTest{ true };
    bool depthWrite{ true };
    render_depth_compare_t depthCompare{ render_depth_compare_t::LESS };
    bool colorWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    render_blend_mode_t blendMode{ render_blend_mode_t::DISABLED };
    ::cypher::common::u32 uniformBlockCount{ 0u };
    render_uniform_block_binding_t uniformBlocks[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    const char *debugName{ nullptr };
    ::cypher::common::u32 sampledTextureCount{ 0u };
    render_sampled_texture_binding_t sampledTextures[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    render_primitive_topology_t topology{ render_primitive_topology_t::TRIANGLE_LIST };
    render_polygon_mode_t polygonMode{ render_polygon_mode_t::FILL };
    // Filled triangles only. Positive values increase window-space depth;
    // useful for a depth-writing shadow caster pass with a LESS depth test. These are
    // backend-neutral slope and constant terms, not clip-space depth values.
    bool depthBiasEnable{ false };
    ::cypher::common::f32 depthBiasSlopeFactor{ 0.0f };
    ::cypher::common::f32 depthBiasConstantUnits{ 0.0f };
};

// Owns no pointers. A live pipeline retains its shader until destruction.
struct render_pipeline_info_t {
    render_shader_handle_t shader{};
    render_vertex_layout_t vertexLayout{};
    bool depthTest{ true };
    bool depthWrite{ true };
    render_depth_compare_t depthCompare{ render_depth_compare_t::LESS };
    bool colorWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    render_blend_mode_t blendMode{ render_blend_mode_t::DISABLED };
    ::cypher::common::u32 uniformBlockCount{ 0u };
    ::cypher::common::u32 uniformBlockBytes[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    ::cypher::common::u32 sampledTextureCount{ 0u };
    render_sampled_texture_kind_t sampledTextureKinds[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    render_primitive_topology_t topology{ render_primitive_topology_t::TRIANGLE_LIST };
    render_polygon_mode_t polygonMode{ render_polygon_mode_t::FILL };
    bool depthBiasEnable{ false };
    ::cypher::common::f32 depthBiasSlopeFactor{ 0.0f };
    ::cypher::common::f32 depthBiasConstantUnits{ 0.0f };
};

// Declared slots are contiguous from zero and map to GL uniform-buffer binding
// points and texture units of the same index. Shader names and exact block sizes
// and sampler kinds are reflected against the linked program. SCALAR_2D uses
// GLSL sampler2D; DEPTH_COMPARE_2D uses sampler2DShadow. Unused array entries
// must be empty. Other ordinary uniforms remain unsupported.
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
