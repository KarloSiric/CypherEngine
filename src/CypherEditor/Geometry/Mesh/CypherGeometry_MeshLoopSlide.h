//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshLoopSlide.h
//  Purpose: Declares a source-identity addressed, baseline-relative slide of
//           one regular closed edge loop.
//  Details: Building a plan captures the selected loop's baseline and its
//           two neighbouring rails. Reapplying that plan computes positions
//           from the captured baseline, so interactive previews never
//           accumulate floating-point drift. The directed seed decides the
//           sign: positive factors move toward the face on the seed's left;
//           reversing the seed and negating the factor is equivalent.
//
//           The operation is position-only. It preserves topology, handles,
//           source IDs, materials, corner UVs/colors, smoothing groups,
//           hard/seam flags, and crease weights exactly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_LOOP_SLIDE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_LOOP_SLIDE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshLoopTraversal.h"
#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

struct mesh_loop_slide_sample_t {
    geometry_mesh_half_edge_handle_t hDirectedHalfEdge{};
    geometry_mesh_vertex_handle_t hVertex{};
    geometry_mesh_vertex_handle_t hPositiveRailVertex{};
    geometry_mesh_vertex_handle_t hNegativeRailVertex{};
    geometry_source_id_t vertexId{};
    geometry_source_id_t positiveRailVertexId{};
    geometry_source_id_t negativeRailVertexId{};
    math::vec3d_t baselinePosition{};
    math::vec3d_t positiveRailPosition{};
    math::vec3d_t negativeRailPosition{};
};

struct mesh_loop_slide_plan_t {
    common::vector_t<mesh_loop_slide_sample_t> samples{};
    common::vector_t<geometry_mesh_vertex_handle_t> moveHandles{};
    // Last position successfully published for every moved vertex. This is
    // also the plan's optimistic-concurrency precondition: an intervening
    // component edit makes the plan stale instead of being overwritten by a
    // later preview update.
    common::vector_t<math::vec3d_t> targetPositions{};
    // Allocation-free apply scratch. Candidates are copied into
    // targetPositions only after MeshVertices_TryMove succeeds.
    common::vector_t<math::vec3d_t> candidatePositions{};
    const common::allocator_t *pAllocator{ nullptr };
    geometry_source_id_t meshId{};
    geometry_source_id_t directedSeedA{};
    geometry_source_id_t directedSeedB{};
    bool bClosed{ false };
};

struct mesh_loop_slide_result_t {
    common::u32 cVerticesMoved{ 0u };
    common::u32 cFacesUpdated{ 0u };
    bool bClosed{ false };
};

CYPHER_NODISCARD geometry_status_t MeshLoopSlidePlan_Init(
    mesh_loop_slide_plan_t *pPlan,
    const common::allocator_t *pAllocator ) noexcept;

void MeshLoopSlidePlan_Shutdown(
    mesh_loop_slide_plan_t *pPlan ) noexcept;

CYPHER_NODISCARD bool MeshLoopSlidePlan_IsInitialized(
    const mesh_loop_slide_plan_t *pPlan ) noexcept;

// Builds a plan from the directed source-ID edge A -> B. The first slice
// accepts only the regular closed quad-loop domain documented by
// MeshOps_TryTraceRegularClosedEdgeLoop. Failure leaves a prior plan intact.
CYPHER_NODISCARD geometry_status_t MeshSourceLoopSlidePlan_TryBuild(
    mesh_loop_slide_plan_t *pPlan,
    const mesh_source_t *pSource,
    geometry_source_id_t directedVertexA,
    geometry_source_id_t directedVertexB ) noexcept;

// Applies factor in the open interval (-1, 1). Positive slides toward the
// seed's left face; negative slides toward its right face. Targets always
// come from the captured baseline, even when this is called repeatedly on
// the same preview mesh. A stale plan returns STALE_HANDLE. All positions are
// validated together by MeshVertices_TryMove before any write occurs.
CYPHER_NODISCARD geometry_status_t MeshSourceLoopSlidePlan_TryApply(
    mesh_loop_slide_plan_t *pPlan,
    mesh_source_t *pSource,
    common::f64 factor,
    mesh_loop_slide_result_t *pResultOut ) noexcept;

// Convenience for one non-interactive operation. Interactive hosts should
// keep a plan for the drag and apply absolute factors to their private
// transaction working copy.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySlideEdgeLoop(
    mesh_source_t *pSource,
    geometry_source_id_t directedVertexA,
    geometry_source_id_t directedVertexB,
    common::f64 factor,
    mesh_loop_slide_result_t *pResultOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_LOOP_SLIDE_H
