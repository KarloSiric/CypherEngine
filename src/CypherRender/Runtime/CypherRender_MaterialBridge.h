//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Adopts a small, explicit subset of cooked materials for drawing.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_MATERIAL_BRIDGE_H
#define CYPHER_ENGINE_RENDER_MATERIAL_BRIDGE_H
#pragma once

#include "CypherRender/CypherRender_Draw.h"
#include "CypherResource/CypherResource_RenderAssets.h"

namespace cypher::engine::render
{

// The caller owns the resource manager and all renderer handles in the resulting
// bundle. The cooked material path is a normalized .cymat_c virtual path.
// Referenced authored .cyshader/.cytex paths are resolved to cooked paths by
// appending _c. Names and paths are borrowed only for the synchronous call.
struct render_material_bundle_desc_t {
    resource::resource_manager_t *pResources{ nullptr };
    ::cypher::common::string_view_t cookedMaterialPath{};
    render_vertex_layout_t vertexLayout{};
    ::cypher::common::u32 uniformBlockCount{ 0u };
    render_uniform_block_binding_t uniformBlocks[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    const char *debugName{ nullptr };
};

struct render_material_bundle_t {
    render_shader_handle_t shader{};
    render_pipeline_handle_t pipeline{};
    render_pipeline_handle_t mirroredPipeline{};
    render_texture_handle_t textures[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    render_buffer_handle_t materialUniformBuffer{};
    ::cypher::common::u32 materialUniformSlot{ R_MAX_UNIFORM_BLOCK_BINDINGS };
    ::cypher::common::u32 textureCount{ 0u };
    ::cypher::common::render_material_alpha_mode_t alphaMode{
        ::cypher::common::render_material_alpha_mode_t::OPAQUE
    }; // Caller uses this to place BLEND draws after opaque draws.
    ::cypher::common::f32 alphaCutoff{ 0.5F }; // MASK threshold for caller-owned shader constants.
    ::cypher::common::flags32_t materialFlags{ 0u }; // Preserved for scene shadow policy.
};

enum class render_material_bridge_status_t : ::cypher::common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    RESOURCE_ERROR,
    MATERIAL_UNSUPPORTED,
    SHADER_INTERFACE_MISMATCH,
    RENDER_ERROR,
    CLEANUP_FAILED
};

struct render_material_bridge_result_t {
    render_material_bridge_status_t status{ render_material_bridge_status_t::OK };
    resource::resource_error_t resourceError{ resource::resource_error_t::OK };
    render_error_t renderError{ render_error_t::OK };
    render_error_t cleanupRenderError{ render_error_t::OK };
};

// Accepts CYMT V2 / CYSH V3 with no features or named
// surface; SURFACE + OPAQUE, MASK, BLEND, or ADDITIVE state; zero to two
// 2D color textures (linear/sRGB RGBA8 or linear RGBA32_FLOAT) with identity
// UV0 and no sampler preset. The only supported material value is one F32X4
// `tint` at byte offset zero. Its shader must declare a matching MATERIAL VALUE
// and a 16-byte MATERIAL uniform block named `Material` with GLSL std140
// member `vec4 tint`. The caller declares View at slot 0, 64-byte Model at
// slot 1, and Material at slot 2; the bundle owns and binds only slot 2.
// GLSL block member names and offsets are a shader-authoring convention here:
// native pipeline reflection currently verifies the block size, not members.
// All shader
// resource bindings must be represented by those textures or caller-declared
// uniform blocks. The renderer must support the selected shader and textures.
// Resource views are borrowed until synchronous GPU creation finishes. All
// acquired CPU references are then released; successful output owns no CPU
// resource handle or borrowed view.
// Both pipelines share the shader and every state except front-face winding:
// pipeline treats CCW as front, mirroredPipeline treats CW as front. The
// mirrored variant also preserves gl_FrontFacing on two-sided materials.
// BLEND uses straight-alpha blending with depth testing and no depth writes.
// Its shader must emit unassociated RGB and a separate alpha. The caller
// submits BLEND draws after opaque draws, back-to-front; this bridge does not
// sort them.
// ADDITIVE uses source alpha as its RGB weight, adds to destination RGB, and
// preserves destination alpha. It tests depth without writing it; the caller
// submits it after opaque geometry. The bridge does not schedule draws.
// MASK keeps opaque depth testing and writes with blending disabled. The caller
// must supply the cooked alphaCutoff through its existing uniform-block contract
// to both color and shadow shaders; each shader must sample alpha and discard
// fragments below that cutoff. The bridge cannot verify shader discard behavior
// or choose an alpha-source texture for the caller.
// Ordinary failure leaves bundleOut empty. If rollback of a successfully
// created renderer handle fails, CLEANUP_FAILED reports that error and
// publishes remaining live handles in bundleOut; the caller must retry
// R_DestroyCookedMaterialBundle before shutdown. A backend that returns an
// error plus a native token must be able to retire that token immediately;
// the renderer frontend cannot publish it if that native cleanup also fails.
CYPHER_NODISCARD render_material_bridge_result_t R_CreateCookedMaterialBundle(
    const render_material_bundle_desc_t &description,
    CY_OUT render_material_bundle_t *bundleOut ) noexcept;

// Destroys both pipelines before shader, independent textures, and the
// optional material uniform buffer. On a backend destroy failure, keeps the
// still-live handles in bundle for a later retry.
CYPHER_NODISCARD render_error_t R_DestroyCookedMaterialBundle(
    render_material_bundle_t *bundle ) noexcept;

// Copies the selected pipeline and sampled-texture slots into an existing draw record,
// clears both sampler slots to use the textures' default sampling policy, and
// binds the bundle-owned Material uniform in slot 2 when present. The caller
// supplies vertex input, View/Model uniform buffers, geometry and frame state.
// mirrored=true selects the CW-front variant for a negative-determinant model.
// Failure leaves draw unchanged. The two-argument form selects the CCW-front
// pipeline for ordinary models.
CYPHER_NODISCARD render_error_t R_ApplyCookedMaterialBundleToDraw(
    const render_material_bundle_t &bundle,
    bool mirrored,
    render_draw_indexed_desc_t *draw ) noexcept;
CYPHER_NODISCARD render_error_t R_ApplyCookedMaterialBundleToDraw(
    const render_material_bundle_t &bundle,
    render_draw_indexed_desc_t *draw ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_MATERIAL_BRIDGE_H
