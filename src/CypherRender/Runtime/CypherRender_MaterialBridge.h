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
    render_texture_handle_t textures[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    ::cypher::common::u32 textureCount{ 0u };
    ::cypher::common::render_material_alpha_mode_t alphaMode{
        ::cypher::common::render_material_alpha_mode_t::OPAQUE
    }; // Caller uses this to place BLEND draws after opaque draws.
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

// Accepts CYMT V2 / CYSH V3 with no features, material parameters or named
// surface; SURFACE + OPAQUE or BLEND state; zero to two 2D color textures with identity
// UV0 and no sampler preset. All shader resource bindings must be represented
// by those textures or caller-declared uniform blocks. The current renderer
// must support the selected shader and textures. Resource views are borrowed
// until synchronous GPU creation finishes, then all acquired CPU references
// are released. Successful output owns no CPU resource handle or borrowed view.
// BLEND uses straight-alpha blending with depth testing and no depth writes.
// Its shader must emit straight (unassociated) alpha. The caller submits BLEND
// draws after opaque draws, back-to-front; this bridge does not sort them.
// Ordinary failure leaves bundleOut empty. If GPU rollback itself fails,
// CLEANUP_FAILED reports that error and publishes remaining live handles in
// bundleOut; the caller must retry R_DestroyCookedMaterialBundle before shutdown.
CYPHER_NODISCARD render_material_bridge_result_t R_CreateCookedMaterialBundle(
    const render_material_bundle_desc_t &description,
    CY_OUT render_material_bundle_t *bundleOut ) noexcept;

// Destroys pipeline before shader and all independent textures. On a backend
// destroy failure, keeps the still-live handles in bundle for a later retry.
CYPHER_NODISCARD render_error_t R_DestroyCookedMaterialBundle(
    render_material_bundle_t *bundle ) noexcept;

// Copies pipeline and sampled-texture slots into an existing draw record and
// clears both sampler slots to use the textures' default sampling policy. The
// caller supplies vertex input, uniform buffers, geometry and frame state.
CYPHER_NODISCARD render_error_t R_ApplyCookedMaterialBundleToDraw(
    const render_material_bundle_t &bundle,
    render_draw_indexed_desc_t *draw ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_MATERIAL_BRIDGE_H
