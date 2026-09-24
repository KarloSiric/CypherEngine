//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushBoundary.h
//  Purpose: Declares derived boundary geometry for a plane-defined brush.
//  Details: The boundary is entirely derived from the brush's plane set —
//           it is never serialized and must be reconstructed whenever a
//           plane changes. It stores unique indexed vertices, unique edges,
//           and per-side face polygons with counter-clockwise winding.
//
//           For a valid axis-aligned box (6 planes), reconstruction
//           produces exactly 8 vertices, 12 edges, and 6 quad faces.
//           The Euler relation v - e + f = 2 holds for any valid convex
//           brush, and deep validation checks it.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_BOUNDARY_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_BOUNDARY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"

namespace cypher::editor::geometry
{

// One unique edge in the boundary mesh. Vertex indices are ordered so that
// iVertex0 < iVertex1, giving every edge a single canonical representation
// regardless of which face polygon discovered it.
struct brush_boundary_edge_t {
    common::u32 iVertex0;
    common::u32 iVertex1;
};

// One face polygon on the brush boundary, corresponding to a single brush
// side. Vertices are ordered counter-clockwise when viewed from the
// outward normal direction (the side plane's normal). The vertex ring is
// stored as indices into the boundary's packed faceVertexIndices array.
struct brush_boundary_face_t {
    // Which brush side produced this face. A side that contributes no
    // face (because all its potential vertices are clipped away by other
    // planes) gets no entry here — the face array can be shorter than
    // the side array for degenerate brushes that fail validation.
    common::u32 iSide;

    // Contiguous run [iFirstIndex, iFirstIndex + cVertices) in the
    // boundary's faceVertexIndices array. Each element is an index into
    // the boundary's vertices array.
    common::u32 iFirstIndex;
    common::u32 cVertices;
};

// The complete derived boundary of a convex brush. Populated by
// BrushBoundary_TryReconstruct and read by validation, tessellation,
// queries, and rendering. Never serialized — the brush's plane set is the
// only canonical data.
//
// All four arrays share the same allocator, bound by BrushBoundary_Init.
struct brush_boundary_t {
    // Unique boundary vertex positions. These are the points where exactly
    // three (or more, in degenerate configurations) side planes intersect
    // and that lie inside every other side's half-space. For a clean box:
    // 8 entries.
    common::vector_t<math::vec3d_t> vertices;

    // Unique edges between boundary vertices. For a clean box: 12 entries.
    common::vector_t<brush_boundary_edge_t> edges;

    // One face record per brush side that contributes a polygon. For a
    // clean box: 6 entries, one per side, each with cVertices == 4.
    common::vector_t<brush_boundary_face_t> faces;

    // Packed vertex indices for all face polygons. Each face's
    // [iFirstIndex .. iFirstIndex + cVertices) references entries here,
    // and each entry is an index into the vertices array. For a clean
    // box: 24 entries (6 faces × 4 vertices per face).
    common::vector_t<common::u32> faceVertexIndices;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Binds an allocator to all four internal arrays. Must be called before
// reconstruction. Does not allocate until TryReconstruct runs.
CYPHER_NODISCARD geometry_status_t BrushBoundary_Init(
    brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator ) noexcept;

// Releases all boundary storage. Safe on a zero-initialized boundary.
void BrushBoundary_Shutdown( brush_boundary_t *pBoundary ) noexcept;

// ---------------------------------------------------------------------------
// Reconstruction
// ---------------------------------------------------------------------------

// Derives the complete boundary (vertices, edges, face polygons) from the
// brush's current plane set. Clears any previous boundary data before
// starting, so this is safe to call repeatedly after plane edits.
//
// The algorithm:
//   1. Extracts planes from the brush, validates finiteness, normalization,
//      and the per-brush side limit
//   2. Enumerates all plane triples → three-plane intersection → candidate
//      vertices, filtering those outside any other half-space and merging
//      coincident results (delegates to Brushd_BuildVertices)
//   3. Sorts the unique vertices into canonical order: ascending Kernel
//      coordinate key, ties broken by raw coordinates
//   4. For each side, projects the brush vertices onto the side plane and
//      orders them counter-clockwise (delegates to Brushd_BuildFacePolygon),
//      maps them to canonical vertex indices, and rotates the ring so it
//      starts at its smallest vertex index
//   5. Extracts unique edges and sorts them lexicographically
//
// Canonical traversal: vertex order, edge order, and each face's ring
// start depend only on the geometry, not on the order of the brush's
// sides. Faces are emitted in side order, so two brushes whose side arrays
// are permutations of each other produce identical vertex and edge arrays,
// and the face for a given side (matched by source ID) has an identical
// index ring.
//
// A convex polyhedron bounded by F planes has at most 2F - 4 vertices.
// Reconstruction reserves 2F vertex slots; a plane set that produces more
// is not a convex solid and fails as DEGENERATE.
//
// Returns OK only when the boundary is fully consistent: at least 4
// vertices (tetrahedron minimum) and at least 4 faces. A side whose plane
// is redundant contributes no face; deep validation reports that case.
// Vertices beyond policy.numerical.fCoordinateMagnitudeLimit fail with
// LIMIT_EXCEEDED because they cannot receive canonical coordinate keys.
//
// On any failure, the boundary is left empty (cleared) — never
// half-populated — and its allocator binding is preserved.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryReconstruct(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize BrushBoundary_VertexCount(
    const brush_boundary_t *pBoundary ) noexcept;

CYPHER_NODISCARD common::usize BrushBoundary_EdgeCount(
    const brush_boundary_t *pBoundary ) noexcept;

CYPHER_NODISCARD common::usize BrushBoundary_FaceCount(
    const brush_boundary_t *pBoundary ) noexcept;

// Retrieves the CCW vertex indices for one face. Writes at most
// nOutputCapacity indices and reports how many the face actually has.
// Returns INSUFFICIENT_CAPACITY if the output array is too small, with
// *pCountOut set to the required size so the caller can retry.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetFaceVertexIndices(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    CY_OUT_WRITES( nOutputCapacity ) common::u32 *pIndicesOut,
    common::usize nOutputCapacity,
    common::usize *pCountOut ) noexcept;

// Copies one canonical boundary vertex position.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetVertex(
    const brush_boundary_t *pBoundary,
    common::usize iVertex,
    math::vec3d_t *pVertexOut ) noexcept;

// Copies one canonical edge record (iVertex0 < iVertex1).
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetEdge(
    const brush_boundary_t *pBoundary,
    common::usize iEdge,
    brush_boundary_edge_t *pEdgeOut ) noexcept;

// Copies one face record. The face's iSide names the brush side that
// produced it; iFirstIndex/cVertices address faceVertexIndices.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetFace(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    brush_boundary_face_t *pFaceOut ) noexcept;

// Finds the face produced by brush side iSide. Returns INVALID_ARGUMENT
// when that side contributed no face (a redundant plane).
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryFindFaceForSide(
    const brush_boundary_t *pBoundary,
    common::usize iSide,
    common::usize *pFaceOut ) noexcept;

// Computes the axis-aligned bounds of every boundary vertex. Returns
// DEGENERATE for an empty (failed or never reconstructed) boundary.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetBounds(
    const brush_boundary_t *pBoundary,
    math::aabbd_t *pBoundsOut ) noexcept;

// Returns the outward-facing plane normal for a boundary face by looking
// up the corresponding brush side's plane.
CYPHER_NODISCARD geometry_status_t BrushBoundary_TryGetFaceNormal(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    common::usize iFace,
    math::vec3d_t *pNormalOut ) noexcept;

static_assert( std::is_trivially_copyable_v<brush_boundary_edge_t> );
static_assert( std::is_trivially_copyable_v<brush_boundary_face_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_BOUNDARY_H
