//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation.h
//  Purpose: Declares fan-triangulated output from a brush boundary.
//  Details: Each boundary face (a convex polygon with CCW winding) is
//           decomposed into a triangle fan rooted at vertex 0. Every
//           emitted triangle records the source side index it came from,
//           so downstream consumers (pick, render, UV) can trace any
//           triangle back to its authoring side. For a valid axis-aligned
//           box this produces exactly 12 triangles from 6 quad faces.
//
//           Vertex positions are borrowed references into the boundary's
//           vertex array — the tessellation does not duplicate positions.
//           The tessellation must be rebuilt whenever the boundary is
//           reconstructed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
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

// One triangle in the tessellated brush mesh. Vertex indices reference
// the boundary's vertex array. Winding matches the source face: CCW
// when viewed from the outward normal direction.
struct brush_tessellation_triangle_t {
    common::u32 iVertex0;
    common::u32 iVertex1;
    common::u32 iVertex2;

    // Which brush side (face) produced this triangle. Used by pick,
    // selection, and attribute lookup to map triangles back to authored
    // sides.
    common::u32 iSourceSide;
};

// Triangulated representation of a brush boundary. Owns its triangle
// storage; vertex positions are read from the boundary that was used
// to build this tessellation.
struct brush_tessellation_t {
    common::vector_t<brush_tessellation_triangle_t> triangles{};

    // Cached vertex count from the boundary at build time, so queries
    // can validate indices without needing the boundary pointer.
    common::usize cVertices{ 0u };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Binds an allocator. Must be called before TryBuild. Does not allocate
// until TryBuild runs.
CYPHER_NODISCARD geometry_status_t BrushTessellation_Init(
    brush_tessellation_t *pTessellation,
    const common::allocator_t *pAllocator ) noexcept;

// Releases triangle storage. Safe on a zero-initialized tessellation.
void BrushTessellation_Shutdown(
    brush_tessellation_t *pTessellation ) noexcept;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Fan-triangulates every face in the boundary. Clears any previous
// triangles before starting, so this is safe to call after boundary
// reconstruction. Each face with N vertices produces N-2 triangles.
//
// Preconditions:
//   - pTessellation has been initialized (Init called)
//   - pBoundary has been successfully reconstructed (non-empty)
//
// On failure the tessellation is left empty.
CYPHER_NODISCARD geometry_status_t BrushTessellation_TryBuild(
    brush_tessellation_t *pTessellation,
    const brush_boundary_t *pBoundary ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize BrushTessellation_TriangleCount(
    const brush_tessellation_t *pTessellation ) noexcept;

CYPHER_NODISCARD common::usize BrushTessellation_VertexCount(
    const brush_tessellation_t *pTessellation ) noexcept;

// Copies one triangle record out by index.
CYPHER_NODISCARD geometry_status_t BrushTessellation_TryGetTriangle(
    const brush_tessellation_t *pTessellation,
    common::usize iTriangle,
    brush_tessellation_triangle_t *pTriangleOut ) noexcept;

static_assert( std::is_trivially_copyable_v<brush_tessellation_triangle_t> );
static_assert( std::is_standard_layout_v<brush_tessellation_triangle_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_TESSELLATION_H
