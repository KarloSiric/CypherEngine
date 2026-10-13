//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Prepares visible World objects as backend-neutral camera and shadow draws.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_WORLD_DRAW_H
#define CYPHER_ENGINE_RENDER_WORLD_DRAW_H
#pragma once

#include "CypherRender_MaterialBridge.h"
#include "CypherRender_MeshBridge.h"
#include "CypherRender_ScenePasses.h"
#include "CypherRender_UniformBatch.h"
#include "CypherWorld/Submission/CypherWorld_Submission.h"

namespace cypher::engine::render
{

// Selects the shader-visible Model uniform record for camera color draws.
// MODEL_ONLY is one column-major mat4 (64 bytes). MODEL_NORMAL adds three
// std140-padded columns of the exact inverse-transpose linear transform at
// byte offsets 64, 80, and 96 (112 bytes total). A vertex shader can then
// transform normals under shear/nonuniform scale without recomputing the
// inverse for every vertex. MODEL_CUTOUT appends a std140 vec4 at byte 64,
// while MODEL_NORMAL_CUTOUT appends it at byte 112. Its x component is the
// authored MASK cutoff, or -1 for an unmasked material. A fragment shader
// samples texture slot zero's alpha and discards values below that cutoff.
// The inverse must fit finite f32 components; the shadow adapter keeps its
// separate 64-byte Model contract.
enum class render_world_model_layout_t : ::cypher::common::u8 {
    MODEL_ONLY = 0u,
    MODEL_NORMAL,
    MODEL_CUTOUT,
    MODEL_NORMAL_CUTOUT
};

// Host owns every borrowed table, World submission, batch, and output. Logical
// World asset slots index the two tables; no World object stores a GPU handle.
// The shader's block at modelUniformSlot must match modelLayout exactly. Other
// uniform slots in drawTemplate are caller-owned view/pass data. MASK is
// accepted only with a CUTOUT layout and a sampled alpha texture in slot zero.
// The fragment shader's discard behavior is an authoring contract: reflection
// can verify block size, but cannot prove it reads the cutoff or samples alpha.
// Other accepted materials use a -1 cutoff so they never discard. This slice
// accepts OPAQUE and opt-in MASK materials,
// nonsingular transforms, and one instance per World object. A negative
// determinant reverses authored mesh winding, so the material's mirrored
// pipeline is selected; a collapsed transform remains unsupported.
struct render_world_opaque_prepare_desc_t {
    const ::cypher::engine::world::world_submission_t *submission{ nullptr };
    ::cypher::engine::world::world_submission_token_t token{};
    const render_static_mesh_bundle_t *meshes{ nullptr };
    ::cypher::common::u32 meshCount{ 0u };
    const render_material_bundle_t *materials{ nullptr };
    ::cypher::common::u32 materialCount{ 0u };
    render_uniform_batch_t *modelUniforms{ nullptr };
    ::cypher::common::u32 modelUniformSlot{ 0u };
    render_world_model_layout_t modelLayout{ render_world_model_layout_t::MODEL_ONLY };
    render_draw_indexed_desc_t drawTemplate{};
};

struct render_world_opaque_draw_t {
    ::cypher::engine::world::world_object_id_t objectId{};
    render_draw_indexed_desc_t draw{};
};

// Single-threaded on the renderer thread. The active submission and resource
// tables must not be changed during this call. Preflights all visible records,
// output and uniform capacity, transforms, and resource handles before staging
// any model matrix. Then commits the caller's batch in one upload and publishes
// ordered output records/count. On validation failure, output/count and batch
// are unchanged. If the upload fails, output/count remain unchanged but the
// batch contains staged records and is sealed; the caller may retry its commit
// or reset it between frames. The adapter never mutates a draw queue.
// Existing batch records, if any, are included in the upload. Output must not
// overlap the borrowed tables or the batch staging memory. Successful draws
// retain borrowed renderer handles; keep their bundles and batch alive through
// submission, and do not reset the batch before drawing.
CYPHER_NODISCARD render_error_t R_PrepareOpaqueWorldDraws(
    const render_world_opaque_prepare_desc_t &description,
    CY_OUT render_world_opaque_draw_t *output,
    ::cypher::common::u32 outputCapacity,
    CY_OUT ::cypher::common::u32 *outCount ) noexcept;

// Prepares one visible World submission in camera draw order. Callers own the
// view matrix, borrowed resource tables, uniform batch, and disposable scratch.
// The viewFromWorld matrix must be a finite affine right-handed camera view
// matching the World submission. BLEND order uses each world-space AABB center's
// positive view depth; a visible box crossing the eye sorts at the near end.
// Intersecting translucent surfaces cannot be ordered exactly by one object
// depth. MASK enters the opaque span only with a CUTOUT Model layout; its
// sampled alpha source and discard shader must obey the contract above.
struct render_world_camera_prepare_desc_t {
    const ::cypher::engine::world::world_submission_t *submission{ nullptr };
    ::cypher::engine::world::world_submission_token_t token{};
    const render_static_mesh_bundle_t *meshes{ nullptr };
    ::cypher::common::u32 meshCount{ 0u };
    const render_material_bundle_t *materials{ nullptr };
    ::cypher::common::u32 materialCount{ 0u };
    render_uniform_batch_t *modelUniforms{ nullptr };
    ::cypher::common::u32 modelUniformSlot{ 0u };
    render_world_model_layout_t modelLayout{ render_world_model_layout_t::MODEL_ONLY };
    render_draw_indexed_desc_t drawTemplate{};
    ::cypher::math::mat4_t viewFromWorld{ ::cypher::math::CY_MAT4_IDENTITY };
    render_scene_item_t *sceneItemsScratch{ nullptr };
    ::cypher::common::u32 sceneItemCapacity{ 0u };
    ::cypher::common::u32 *passIndicesScratch{ nullptr };
    ::cypher::common::u32 passIndexCapacity{ 0u };
};

using render_world_camera_draw_t = render_world_opaque_draw_t;

struct render_world_camera_pass_counts_t {
    ::cypher::common::u32 opaqueCount{ 0u };
    ::cypher::common::u32 blendCount{ 0u };
    ::cypher::common::u32 additiveCount{ 0u };
};

// Allocation-free and single-threaded on the renderer thread. Scratch needs
// capacity for every visible object in both arrays and must not overlap the
// public output, resource tables, or uniform staging storage. On success,
// output has opaque draws in input order, BLEND far-to-near with stable ties,
// then ADDITIVE in input order. The three counts partition that output. All
// visible records, resource handles, pipeline state, scratch/output capacity,
// and model storage are preflighted before staging. Validation failure leaves
// public output/counts and the batch unchanged; scratch may be modified. A
// failed upload leaves output/counts unchanged while the staged batch is sealed
// for retry or reset, matching R_PrepareOpaqueWorldDraws. Keep all referenced
// bundles and the batch alive until the resulting draws have executed.
CYPHER_NODISCARD render_error_t R_PrepareWorldCameraDraws(
    const render_world_camera_prepare_desc_t &description,
    CY_OUT render_world_camera_draw_t *output,
    ::cypher::common::u32 outputCapacity,
    CY_OUT render_world_camera_pass_counts_t *outCounts ) noexcept;

// The host builds a separate World submission using the light's view query,
// including casters that are outside the camera view. It owns the light-view
// uniform and two depth-only shadow pipelines, selected by model winding.
// Each selected pipeline must have the matching front-face convention.
// drawTemplate carries the light uniform and any viewport/scissor.
// This first shadow slice uses a 64-byte Model block, no sampled textures,
// and OPAQUE casters only. Noncasting materials are skipped; a casting MASK,
// BLEND, or ADDITIVE material is rejected instead of casting a solid shadow.
struct render_world_shadow_prepare_desc_t {
    const ::cypher::engine::world::world_submission_t *submission{ nullptr };
    ::cypher::engine::world::world_submission_token_t token{};
    const render_static_mesh_bundle_t *meshes{ nullptr };
    ::cypher::common::u32 meshCount{ 0u };
    const render_material_bundle_t *materials{ nullptr };
    ::cypher::common::u32 materialCount{ 0u };
    render_uniform_batch_t *modelUniforms{ nullptr };
    ::cypher::common::u32 modelUniformSlot{ 0u };
    render_pipeline_handle_t shadowPipeline{}; // CCW front for ordinary models.
    render_pipeline_handle_t mirroredShadowPipeline{}; // CW front for mirrored models.
    render_draw_indexed_desc_t drawTemplate{};
};

using render_world_shadow_draw_t = render_world_opaque_draw_t;

// Single-threaded and allocation-free. The output contains only casting
// objects in light-submission order. The caller may reserve output capacity
// for just the caster count. All selected draws, batch storage, and output
// capacity are preflighted before staging one model record per caster. On
// validation failure output/count and batch are unchanged. On GPU upload
// failure output/count are unchanged and the batch remains sealed for retry or
// reset. Keep the batch, mesh bundles and pipelines alive through drawing.
CYPHER_NODISCARD render_error_t R_PrepareWorldShadowDraws(
    const render_world_shadow_prepare_desc_t &description,
    CY_OUT render_world_shadow_draw_t *output,
    ::cypher::common::u32 outputCapacity,
    CY_OUT ::cypher::common::u32 *outCount ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_WORLD_DRAW_H
