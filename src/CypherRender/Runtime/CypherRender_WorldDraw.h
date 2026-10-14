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

#include <cstddef>

namespace cypher::engine::render
{

// One host-supplied point light. Position is in world space, range is a
// positive distance, and RGB color/intensity are linear nonnegative values.
struct render_world_point_light_desc_t {
    ::cypher::math::vec3_t position{};
    ::cypher::common::f32 range{ 0.0f };
    ::cypher::math::vec3_t color{};
    ::cypher::common::f32 intensity{ 0.0f };
};

// World currently owns geometry, not authored lights. The host supplies these
// view-wide values for a small ambient + directional + two-point-light shader.
// directionToLight points from a surface toward the directional light. The
// camera matrix must also be used as WorldCameraDraws.viewFromWorld when
// translucent World objects are sorted for this view.
struct render_world_view_lighting_desc_t {
    ::cypher::math::mat4_t viewFromWorld{ ::cypher::math::CY_MAT4_IDENTITY };
    ::cypher::math::mat4_t clipFromView{ ::cypher::math::CY_MAT4_IDENTITY };
    ::cypher::math::vec3_t ambientLight{};
    ::cypher::math::vec3_t directionToLight{ ::cypher::math::CY_VEC3_UP };
    ::cypher::math::vec3_t directionalRadiance{};
    render_world_point_light_desc_t pointLights[2]{};
    ::cypher::common::u32 pointLightCount{ 0u };
};

// GLSL std140 `View` block. All matrices are column-major, vec4 lanes and
// inactive point-light entries are zero-filled, and pointLightControl.x is
// the number of active lights. There is no shadow matrix in this record.
struct alignas( 16 ) render_world_view_lighting_record_t {
    ::cypher::math::mat4_t view{};
    ::cypher::math::mat4_t projection{};
    ::cypher::common::f32 ambientLight[4]{};
    ::cypher::common::f32 lightDirection[4]{};
    ::cypher::common::f32 lightColor[4]{};
    ::cypher::common::f32 pointLightPositionRange[2][4]{};
    ::cypher::common::f32 pointLightColorIntensity[2][4]{};
    ::cypher::common::i32 pointLightControl[4]{};
};
static_assert( sizeof( render_world_view_lighting_record_t ) == 256u );
static_assert( offsetof( render_world_view_lighting_record_t, ambientLight ) == 128u );
static_assert( offsetof( render_world_view_lighting_record_t, lightDirection ) == 144u );
static_assert( offsetof( render_world_view_lighting_record_t, lightColor ) == 160u );
static_assert( offsetof( render_world_view_lighting_record_t, pointLightPositionRange ) == 176u );
static_assert( offsetof( render_world_view_lighting_record_t, pointLightColorIntensity ) == 208u );
static_assert( offsetof( render_world_view_lighting_record_t, pointLightControl ) == 240u );

// Pure, allocation-free packer. Validates finite matrices (affine camera),
// nonnegative finite light values, a normalizable direction, at most two
// points, and positive ranges for active points. Inactive descriptions are
// ignored. On failure output is unchanged. The caller owns GPU upload and
// must declare an exact 256-byte View block in its lighting pipeline.
CYPHER_NODISCARD render_error_t R_PackWorldViewLighting(
    const render_world_view_lighting_desc_t &description,
    CY_OUT render_world_view_lighting_record_t *output ) noexcept;

// Selects the shader-visible Model uniform record for camera color draws.
// MODEL_ONLY is one column-major mat4 (64 bytes). MODEL_NORMAL adds three
// std140-padded columns of the exact inverse-transpose linear transform at
// byte offsets 64, 80, and 96 (112 bytes total). A vertex shader can then
// transform normals under shear/nonuniform scale without recomputing the
// inverse for every vertex. MODEL_CUTOUT appends a std140 vec4 at byte 64,
// while MODEL_NORMAL_CUTOUT appends it at byte 112. Its x component is the
// authored MASK cutoff, or -1 for an unmasked material. A fragment shader
// samples texture slot zero's alpha and discards values below that cutoff.
// The inverse must fit finite f32 components. Shadow draws use a separate
// 64-byte opaque or 80-byte masked Model contract.
enum class render_world_model_layout_t : ::cypher::common::u8 {
    MODEL_ONLY = 0u,
    MODEL_NORMAL,
    MODEL_CUTOUT,
    MODEL_NORMAL_CUTOUT
};

// Host owns every borrowed table, World submission, batch, and output. Logical
// World asset slots index the two tables; no World object stores a GPU handle.
// The shader's block at modelUniformSlot must match modelLayout exactly. Other
// uniform slots in drawTemplate are caller-owned view/pass data. A material
// declaring passShadowSlot preserves the matching caller-owned D24 texture and
// explicit comparison sampler from drawTemplate through camera publication;
// both are validated for every visible object before staging model uniforms.
// Other unused sampled slots are cleared. MASK is
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
// uniform and depth-only shadow pipelines, selected by alpha mode and model
// winding. Each selected pipeline must have the matching front-face convention.
// drawTemplate carries the light uniform and any viewport/scissor.
// OPAQUE uses a 64-byte Model block and no textures. MASK uses an 80-byte
// Model block containing a vec4 cutoff at byte 64, and samples alpha from the
// material's RGBA texture in slot zero. Its shader must discard fragments
// below that cutoff; reflection cannot prove the discard is implemented.
// Noncasting materials are skipped; casting BLEND/ADDITIVE are rejected.
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
    render_pipeline_handle_t maskShadowPipeline{}; // CCW, alpha-tested depth.
    render_pipeline_handle_t mirroredMaskShadowPipeline{}; // CW, alpha-tested depth.
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
