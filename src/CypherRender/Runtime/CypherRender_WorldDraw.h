//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Prepares visible World objects as backend-neutral camera draws.
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

// Host owns every borrowed table, World submission, batch, and output. Logical
// World asset slots index the two tables; no World object stores a GPU handle.
// The shader's block at modelUniformSlot must contain exactly one column-major
// mat4 model transform (64 bytes). Other uniform slots in drawTemplate are
// caller-owned view/pass data. The first slice accepts only OPAQUE materials,
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
// depth. MASK is not yet accepted because the alpha-cutoff shader convention is
// not established for World submissions.
struct render_world_camera_prepare_desc_t {
    const ::cypher::engine::world::world_submission_t *submission{ nullptr };
    ::cypher::engine::world::world_submission_token_t token{};
    const render_static_mesh_bundle_t *meshes{ nullptr };
    ::cypher::common::u32 meshCount{ 0u };
    const render_material_bundle_t *materials{ nullptr };
    ::cypher::common::u32 materialCount{ 0u };
    render_uniform_batch_t *modelUniforms{ nullptr };
    ::cypher::common::u32 modelUniformSlot{ 0u };
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

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_WORLD_DRAW_H
