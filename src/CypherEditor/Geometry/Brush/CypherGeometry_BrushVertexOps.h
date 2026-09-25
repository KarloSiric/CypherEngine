//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexOps.h
//  Purpose: Declares vertex-level manipulation operations on convex brushes.
//  Details: These operations let the user grab individual vertices, edges,
//           or faces of a brush and drag them while maintaining convexity.
//           This is the equivalent of TrenchBroom's vertex manipulation
//           mode and Hammer's vertex tool.
//
//           Every operation follows the same pattern:
//             1. Validate the brush and cached boundary contracts.
//             2. Stage the modified point set in policy-bounded workspace.
//             3. Rebuild the brush via convex hull from the modified set.
//             4. Deep-validate and transfer retained side provenance.
//             5. Publish through an ownership move or leave all live state
//                and the source-ID allocator exactly unchanged.
//
//           On success, the brush_solid_t is replaced in-place with the
//           new plane set. The caller must invalidate any cached boundary.
//
//           Attribute transfer: when the hull rebuild produces new planes,
//           each new plane is matched at most once to the closest original
//           plane (by normal dot product) so materials and UV projections
//           survive vertex edits without duplicating persistent side IDs.
//           Planes that cannot be matched receive a fresh source identity
//           and the default attribute slot.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_OPS_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_OPS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Policy.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Result from a vertex manipulation operation. Carries the new boundary
// vertex count so the caller can inspect what happened without
// reconstructing again.
struct brush_vertex_op_result_t {
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };

    // Post-operation counts (zero on failure).
    common::u32 cVertices{ 0u };
    common::u32 cEdges{ 0u };
    common::u32 cFaces{ 0u };
};

// ---------------------------------------------------------------------------
// Vertex move — the core TrenchBroom vertex drag operation
// ---------------------------------------------------------------------------

// Moves a single boundary vertex to a new position and rebuilds the
// brush from the modified vertex set via convex hull. If the resulting
// brush is degenerate (fewer than 4 non-coplanar points), the operation
// is rejected and the brush is left unchanged.
//
// iVertex:     index into the current boundary vertex array.
// newPosition: the target world-space position for that vertex.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TryMoveVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::usize iVertex,
    math::vec3d_t newPosition ) noexcept;

// ---------------------------------------------------------------------------
// Edge move — translates both endpoints of an edge
// ---------------------------------------------------------------------------

// Moves both endpoints of a boundary edge by a delta vector and rebuilds.
// Equivalent to selecting an edge in the vertex tool and dragging it.
//
// iEdge: index into the boundary edge array.
// delta: world-space translation to apply to both endpoints.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TryMoveEdge(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::usize iEdge,
    math::vec3d_t delta ) noexcept;

// ---------------------------------------------------------------------------
// Face move — translates all vertices of a face
// ---------------------------------------------------------------------------

// Moves all vertices of a boundary face by a delta vector and rebuilds.
// Equivalent to selecting a face in the vertex tool and dragging it.
//
// iFace: index into the boundary face array.
// delta: world-space translation to apply to all face vertices.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TryMoveFace(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::usize iFace,
    math::vec3d_t delta ) noexcept;

// ---------------------------------------------------------------------------
// Add vertex — splits an edge by inserting a point
// ---------------------------------------------------------------------------

// Inserts a new vertex at the specified position and rebuilds. The point
// must be outside the current brush boundary (or on it) for the hull to
// grow. If the point is strictly interior, the hull is unchanged and the
// operation returns OK with the same vertex count.
//
// position: world-space position of the new vertex to add.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TryAddVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t position ) noexcept;

// ---------------------------------------------------------------------------
// Remove vertex — dissolves a vertex by rebuilding without it
// ---------------------------------------------------------------------------

// Removes a vertex from the boundary and rebuilds from the remaining
// points. If fewer than 4 non-coplanar points remain, returns DEGENERATE
// and leaves the brush unchanged.
//
// iVertex: index into the boundary vertex array.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TryRemoveVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::usize iVertex ) noexcept;

// ---------------------------------------------------------------------------
// Snap all vertices to grid — rounds every vertex then rebuilds
// ---------------------------------------------------------------------------

// Snaps all boundary vertices to the given grid spacing and rebuilds.
// Coincident snapped points collapse naturally during convex-hull
// construction. Returns DEGENERATE if fewer than four non-coplanar unique
// points remain. An already aligned brush is an exact no-op: storage,
// identities, and the allocator sequence are retained.
//
// gridSpacing: distance between grid lines in each axis.
CYPHER_NODISCARD brush_vertex_op_result_t BrushVertexOps_TrySnapToGrid(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::f64 gridSpacing ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_OPS_H
