//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Prepares visible World objects as backend-neutral opaque draws.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_WORLD_DRAW_H
#define CYPHER_ENGINE_RENDER_WORLD_DRAW_H
#pragma once

#include "CypherRender_MaterialBridge.h"
#include "CypherRender_MeshBridge.h"
#include "CypherRender_UniformBatch.h"
#include "CypherWorld/Submission/CypherWorld_Submission.h"

namespace cypher::engine::render
{

// Host owns every borrowed table, World submission, batch, and output. Logical
// World asset slots index the two tables; no World object stores a GPU handle.
// The shader's block at modelUniformSlot must contain exactly one column-major
// mat4 model transform (64 bytes). Other uniform slots in drawTemplate are
// caller-owned view/pass data. The first slice accepts only OPAQUE materials,
// positive nonsingular transforms, and one instance per World object. A
// positive determinant preserves the authored mesh winding: the material's
// pipeline still chooses whether its front face is clockwise or counterclockwise.
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

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_WORLD_DRAW_H
