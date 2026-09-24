//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries.h
//  Purpose: Declares derived spatial and component queries on brushes.
//  Details: All queries derive from the boundary reconstruction — they
//           never access the raw plane set directly. Bounds are computed
//           in double precision from boundary vertices and converted to
//           f32 AABB for broad-phase consumers. Component enumeration
//           provides side/vertex/edge counts and individual lookups.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherMath_Bounds.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

// Computes the tight double-precision AABB enclosing all boundary vertices.
// Returns CY_AABBD_EMPTY if the boundary has no vertices.
CYPHER_NODISCARD math::aabbd_t BrushQueries_ComputeBoundsd(
    const brush_boundary_t *pBoundary ) noexcept;

// Computes the tight f32 AABB by converting the f64 bounds. Returns
// CY_AABB_EMPTY if the boundary has no vertices or the conversion
// overflows f32 range.
CYPHER_NODISCARD math::aabb_t BrushQueries_ComputeBounds(
    const brush_boundary_t *pBoundary ) noexcept;

// ---------------------------------------------------------------------------
// Component counts (convenience wrappers over BrushBoundary)
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize BrushQueries_VertexCount(
    const brush_boundary_t *pBoundary ) noexcept;

CYPHER_NODISCARD common::usize BrushQueries_EdgeCount(
    const brush_boundary_t *pBoundary ) noexcept;

CYPHER_NODISCARD common::usize BrushQueries_FaceCount(
    const brush_boundary_t *pBoundary ) noexcept;

// ---------------------------------------------------------------------------
// Vertex position lookup
// ---------------------------------------------------------------------------

// Retrieves one boundary vertex position by index. Returns INVALID_ARGUMENT
// if the index is out of range.
CYPHER_NODISCARD geometry_status_t BrushQueries_TryGetVertex(
    const brush_boundary_t *pBoundary,
    common::usize iVertex,
    math::vec3d_t *pPositionOut ) noexcept;

// ---------------------------------------------------------------------------
// Edge lookup
// ---------------------------------------------------------------------------

// Retrieves one boundary edge by index. Returns INVALID_ARGUMENT if the
// index is out of range.
CYPHER_NODISCARD geometry_status_t BrushQueries_TryGetEdge(
    const brush_boundary_t *pBoundary,
    common::usize iEdge,
    brush_boundary_edge_t *pEdgeOut ) noexcept;

// ---------------------------------------------------------------------------
// Center of mass (vertex centroid)
// ---------------------------------------------------------------------------

// Computes the arithmetic mean of all boundary vertex positions. Returns
// a zero vector if the boundary has no vertices.
CYPHER_NODISCARD math::vec3d_t BrushQueries_ComputeCentroid(
    const brush_boundary_t *pBoundary ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
