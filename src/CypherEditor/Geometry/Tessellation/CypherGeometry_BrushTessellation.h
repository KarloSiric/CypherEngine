//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation.h
//  Purpose: Declares deterministic triangulation of a reconstructed brush
//           boundary with per-triangle source provenance.
//  Details: Every brush face is convex, so a fan from the face's canonical
//           first vertex is a valid triangulation. Because reconstruction
//           already fixed the ring start and vertex order canonically, the
//           triangle list is a pure function of the plane set: plane-order
//           permutations yield identical triangles (modulo which side
//           index names them, which is why each triangle also carries the
//           persistent side source ID).
//
//           Triangles index into the boundary's own vertex array and wind
//           counter-clockwise seen from outside, so their geometric normal
//           points along the owning side's outward plane normal.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_TESSELLATION_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_TESSELLATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"

namespace cypher::editor::geometry
{

struct geometry_brush_triangle_t {
    // Indices into the source boundary's vertices array.
    common::u32 iVertex0;
    common::u32 iVertex1;
    common::u32 iVertex2;
    // Positional side index at tessellation time, and the side's persistent
    // identity. Consumers that outlive the brush value must use sideId.
    common::u32 iSide;
    geometry_source_id_t sideId;
};

struct geometry_brush_tessellation_t {
    common::vector_t<geometry_brush_triangle_t> triangles{};
};

CYPHER_NODISCARD geometry_status_t BrushTessellation_Init(
    geometry_brush_tessellation_t *pTessellation,
    const common::allocator_t *pAllocator ) noexcept;

void BrushTessellation_Shutdown( geometry_brush_tessellation_t *pTessellation ) noexcept;

// Rebuilds the triangle list from a boundary reconstructed from pBrush.
// Each face with n vertices contributes up to n - 2 triangles; fan
// triangles whose area is below policy.numerical.fMinimumFaceArea (collinear
// ring vertices) are omitted without leaving a hole. Failure-atomic: on
// any non-OK status the triangle list is empty.
//
//   NOT_INITIALIZED    tessellation, brush, or boundary not initialized
//   CORRUPT_STATE      the boundary references sides or vertices that do not
//                      exist (it was not built from this brush)
//   DEGENERATE         the boundary is empty
//   ALLOCATION_FAILED  growing the triangle list failed
CYPHER_NODISCARD geometry_status_t BrushTessellation_TryBuild(
    geometry_brush_tessellation_t *pTessellation,
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy ) noexcept;

CYPHER_NODISCARD common::usize BrushTessellation_TriangleCount(
    const geometry_brush_tessellation_t *pTessellation ) noexcept;

CYPHER_NODISCARD geometry_status_t BrushTessellation_TryGetTriangle(
    const geometry_brush_tessellation_t *pTessellation,
    common::usize iTriangle,
    geometry_brush_triangle_t *pTriangleOut ) noexcept;

static_assert( std::is_trivially_copyable_v<geometry_brush_triangle_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_TESSELLATION_H
