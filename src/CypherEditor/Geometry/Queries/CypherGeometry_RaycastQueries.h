//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_RaycastQueries.h
//  Purpose: Declares brute-force authoring ray casts for brushes and meshes.
//  Details: These queries are the correctness oracle for future spatial indexes.
//           They keep hit range, culling, precision, scratch, and provenance
//           explicit and do not own selection or acceleration state.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_RAYCAST_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_RAYCAST_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Scratch.h"

namespace cypher::editor::geometry
{

// Authoring geometry is stored in binary64, so editor picking remains in the
// same precision. The origin must lie inside the active authoring coordinate
// magnitude, and the direction must be unit length within the numerical policy;
// accepted hit parameters are therefore world-space distances.
struct geometry_raycast_ray_t {
    math::vec3d_t origin{};
    math::vec3d_t direction{};
};

enum class geometry_raycast_cull_mode_t : common::u8 {
    NONE = 0u,
    BACK_FACE,
    FRONT_FACE,
    COUNT
};

struct geometry_raycast_options_t {
    // The lower bound is inclusive after the query raises it to the policy's
    // absolute distance tolerance. Consequently every reported hit is strictly
    // in front of the ray origin, including when fMinimumDistance is zero.
    common::f64 fMinimumDistance{ 0.0 };
    common::f64 fMaximumDistance{ 1.0e6 };
    geometry_raycast_cull_mode_t cullMode{
        geometry_raycast_cull_mode_t::NONE };
};

// OK plus bHit=false is the ordinary no-intersection result. Every error also
// leaves the complete output in this neutral state.
struct brush_raycast_hit_t {
    common::bool_t bHit{ false };
    common::f64 fDistance{ 0.0 };
    math::vec3d_t position{};
    math::vec3d_t normal{};       // Authored outward unit normal.
    math::vec3d_t barycentric{};  // Weights for iVertex0/1/2.

    geometry_source_id_t brushSourceId{};
    geometry_source_id_t sideSourceId{};
    common::u32 iFace{ CY_INVALID_INDEX };
    common::u32 iSide{ CY_INVALID_INDEX };
    common::u32 iTriangleInFace{ CY_INVALID_INDEX };
    common::u32 iVertex0{ CY_INVALID_INDEX };
    common::u32 iVertex1{ CY_INVALID_INDEX };
    common::u32 iVertex2{ CY_INVALID_INDEX };
};

// Mesh source identity is document-owned rather than embedded in editable_mesh_t,
// so callers pass it explicitly. Native mesh faces currently have no persistent
// component ID; hFace is the live component identity and iSourceSide preserves
// brush-conversion provenance when such provenance exists.
struct mesh_raycast_hit_t {
    common::bool_t bHit{ false };
    common::f64 fDistance{ 0.0 };
    math::vec3d_t position{};
    math::vec3d_t normal{};       // Winding-derived outward unit normal.
    math::vec3d_t barycentric{};  // Weights for hVertex0/1/2.

    geometry_source_id_t meshSourceId{};
    geometry_mesh_shell_handle_t hShell{};
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_loop_handle_t hLoop{};
    geometry_mesh_vertex_handle_t hVertex0{};
    geometry_mesh_vertex_handle_t hVertex1{};
    geometry_mesh_vertex_handle_t hVertex2{};
    common::u32 iSourceSide{ CY_INVALID_INDEX };
    common::u32 iTriangleInFace{ CY_INVALID_INDEX };
};

// Tests every triangle in every convex boundary polygon. Faces are fan
// triangulated from their first ring vertex; no quad assumption is made and no
// temporary allocation is performed. Near-equal hits use stable side source ID,
// then face/triangle index, as the deterministic tie-break key.
//
// The supplied boundary must be the canonical boundary derived from pBrush:
// vertices must lie inside all brush planes, face i must map to side i with an
// exhaustive packed ring on that side's plane, and edge records must use valid
// ordered vertex indices. This correspondence check is allocation-free and uses
// the policy's coordinate and coplanar tolerances. A structurally usable brush
// and boundary that do not correspond return INVALID_ARGUMENT; every failure
// leaves pHitOut neutral.
CYPHER_NODISCARD geometry_status_t BrushQueries_TryRaycast(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_policy_t &policy,
    brush_raycast_hit_t *pHitOut ) noexcept;

// Returns a conservative byte count for one MeshQueries_TryRaycast invocation.
// The count includes worst-case alignment padding and storage for the largest
// face's positions, handles, projection, ear-clipping work list, and triangles.
// A canonical empty mesh requires zero bytes. The output is zero on failure.
CYPHER_NODISCARD geometry_status_t
MeshQueries_TryGetRaycastScratchSize(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy,
    common::usize *pBytesOut ) noexcept;

// Triangulates every simple planar mesh face with the shared binary64 polygon
// ear clipper, then tests all emitted triangles. The query performs no heap
// allocation: non-empty meshes require initialized Geometry scratch. All scratch
// allocations are rewound before return, including failure paths. Near-equal
// hits use face handle slot/generation and triangle index as the deterministic
// tie-break key.
CYPHER_NODISCARD geometry_status_t MeshQueries_TryRaycast(
    const editable_mesh_t *pMesh,
    geometry_source_id_t meshSourceId,
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_policy_t &policy,
    geometry_scratch_t *pScratch,
    mesh_raycast_hit_t *pHitOut ) noexcept;

static_assert( std::is_trivially_copyable_v<geometry_raycast_ray_t> );
static_assert( std::is_trivially_copyable_v<brush_raycast_hit_t> );
static_assert( std::is_trivially_copyable_v<mesh_raycast_hit_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_RAYCAST_QUERIES_H
