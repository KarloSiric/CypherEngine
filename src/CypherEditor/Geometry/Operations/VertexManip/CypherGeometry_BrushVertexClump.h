//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexClump.h
//  Purpose: Declares the multi-brush vertex move ("vertex clumping"): when
//           several selected brushes have a vertex at the same place, a drag
//           of that vertex moves it in all of them, so seams between brushes
//           (terrain made of brushes, joined walls) stay closed.
//  Details: Builds on BrushVertexOps_TryMoveVertex, applied to a private copy
//           of every affected brush; the brushes are replaced only when every
//           move succeeded, so a move that would break one brush moves none.
//           Fusing is inherent to the vertex move: a vertex dragged onto a
//           neighbour becomes a coincident point and the convex rebuild
//           merges them.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_CLUMP_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_CLUMP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushVertexOps.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Moves the vertex at `position` (within the policy's weld distance) to
// newPosition in every brush that has one. *pMovedOut (optional) receives
// how many brushes changed. INVALID_ARGUMENT when no brush has a vertex
// there; any brush's failure fails the whole move with nothing changed.
CYPHER_NODISCARD geometry_status_t BrushVertexOps_TryMoveVertexClump(
    common::span_t<brush_solid_t *const> brushes,
    math::vec3d_t position,
    math::vec3d_t newPosition,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    common::u32 *pMovedOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_CLUMP_H
