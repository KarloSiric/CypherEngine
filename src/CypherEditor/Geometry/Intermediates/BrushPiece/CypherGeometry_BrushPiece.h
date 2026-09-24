//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPiece.h
//  Purpose: Declares the identity-free convex working record used by brush
//           CSG, clipping, primitives, and hull construction.
//  Details: A piece is an ordered list of outward half-space planes, each
//           tagged with where it came from. It deliberately carries no
//           source IDs of its own: operations produce pieces, and only the
//           materialization step at the operation layer turns a piece into
//           a published brush with fresh identities and transferred
//           attributes. That split keeps every geometric algorithm pure and
//           lets one algorithm serve CSG, clipping, extrusion, primitives,
//           and imports alike.
//
//           Reduce is the single normalization step every producer ends
//           with: it reconstructs the boundary, drops planes that no longer
//           contribute a face, and classifies the result as a valid solid
//           or as empty. Treating "no volume" as an ordinary outcome rather
//           than an error is what lets subtraction probe fragments cheaply.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_PIECE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_PIECE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"

#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// How a piece plane relates to its origin.
enum class geometry_piece_plane_origin_t : common::u8 {
    NONE = 0u,       // synthesized (primitive generator, hull facet)
    SOURCE_SIDE,     // an authored side, same orientation
    FLIPPED_SIDE,    // an authored side, reversed (exposed CSG cut surface)
    CUT_PLANE,       // an operation-supplied plane (clip, split, hollow)
    COUNT
};

// Provenance travels with the plane through every CSG step so the
// materializer can transfer material and UV projection from the side a
// face actually came from.
struct geometry_piece_plane_t {
    math::planed_t plane{};
    geometry_piece_plane_origin_t origin{ geometry_piece_plane_origin_t::NONE };
    geometry_source_id_t sourceBrushId{};
    geometry_source_id_t sourceSideId{};
};

struct geometry_brush_piece_t {
    common::vector_t<geometry_piece_plane_t> planes{};
};

enum class geometry_piece_extent_t : common::u8 {
    EMPTY = 0u,  // the half-spaces enclose no volume above tolerance
    SOLID,       // a valid closed convex solid; every plane contributes a face
    COUNT
};

// ---------------------------------------------------------------------------
// Piece lifecycle and editing
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t BrushPiece_Init(
    geometry_brush_piece_t *pPiece, const common::allocator_t *pAllocator ) noexcept;

void BrushPiece_Shutdown( geometry_brush_piece_t *pPiece ) noexcept;

CYPHER_NODISCARD common::usize BrushPiece_PlaneCount(
    const geometry_brush_piece_t *pPiece ) noexcept;

void BrushPiece_Clear( geometry_brush_piece_t *pPiece ) noexcept;

// Appends one plane. The plane must be finite and unit length within
// policy (NUMERIC_FAILURE / DEGENERATE); the count is bounded by
// cBrushSidesPerBrushMax (LIMIT_EXCEEDED). Failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushPiece_TryAppendPlane(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    const geometry_piece_plane_t &plane ) noexcept;

// Replaces the destination with a copy of the source. Failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushPiece_TryCopy(
    geometry_brush_piece_t *pDestination,
    const geometry_brush_piece_t *pSource ) noexcept;

// Replaces the piece's planes with the brush's sides, tagged SOURCE_SIDE
// with the brush and side IDs. Failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushPiece_TryFromBrush(
    geometry_brush_piece_t *pPiece,
    const brush_solid_t *pBrush ) noexcept;

// Offsets every plane outward by `distance` (negative shrinks). Used by
// hollow and by expand/contract commands. Never allocates.
void BrushPiece_OffsetPlanes( geometry_brush_piece_t *pPiece, math::f64 distance ) noexcept;

// ---------------------------------------------------------------------------
// Normalization
// ---------------------------------------------------------------------------

// Reconstructs the piece, removes planes that contribute no face, and
// reports whether a solid remains. On SOLID the surviving planes keep their
// relative order and pass the full boundary checks. On EMPTY the piece is
// cleared. pBoundsOut (optional) receives the solid's bounds.
//
// Returns OK with the extent for both outcomes. Errors are reserved for
// real failures: NOT_INITIALIZED, NUMERIC_FAILURE or LIMIT_EXCEEDED from
// out-of-range planes or vertices, ALLOCATION_FAILED. On error the piece is
// unchanged.
//
// A plane set that reconstructs but fails the closed-solid checks after
// pruning (a sliver thinner than tolerance) is reported EMPTY: it has no
// usable volume, and every caller wants to discard it rather than fail.
CYPHER_NODISCARD geometry_status_t BrushPiece_TryReduce(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    geometry_piece_extent_t *pExtentOut,
    math::aabbd_t *pBoundsOut ) noexcept;

// Reconstructs the piece's boundary into a caller-initialized boundary
// without modifying the piece. Useful for reading a SOLID piece's
// vertices (hull merge, previews).
CYPHER_NODISCARD geometry_status_t BrushPiece_TryBuildBoundary(
    const geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    brush_boundary_t *pBoundaryOut ) noexcept;

// Maximum points accepted by the hull. Hull cost is O(n^4); 128 points is
// well under a second and far above what merging a handful of authored
// brushes or generating a primitive produces.
inline constexpr common::usize BRUSH_PIECE_HULL_POINTS_MAX = 128u;

// Replaces the piece with the convex hull of a point cloud. Points closer
// than the weld distance are merged first. DEGENERATE when the points are
// coplanar or fewer than four remain; LIMIT_EXCEEDED above
// BRUSH_PIECE_HULL_POINTS_MAX. Facets carry NONE provenance. On failure the
// piece is unchanged.
CYPHER_NODISCARD geometry_status_t BrushPiece_TryFromPoints(
    geometry_brush_piece_t *pOut,
    common::span_t<const math::vec3d_t> points,
    const geometry_policy_t &policy ) noexcept;

// ---------------------------------------------------------------------------
// Piece lists
// ---------------------------------------------------------------------------

struct geometry_piece_range_t {
    common::u32 iFirst;
    common::u32 cPlanes;
};

// Many pieces in two flat arrays, so an operation that emits a variable
// number of fragments grows two vectors instead of allocating per piece.
struct geometry_piece_list_t {
    common::vector_t<geometry_piece_plane_t> planes{};
    common::vector_t<geometry_piece_range_t> pieces{};
};

CYPHER_NODISCARD geometry_status_t PieceList_Init(
    geometry_piece_list_t *pList, const common::allocator_t *pAllocator ) noexcept;

void PieceList_Shutdown( geometry_piece_list_t *pList ) noexcept;

void PieceList_Clear( geometry_piece_list_t *pList ) noexcept;

CYPHER_NODISCARD common::usize PieceList_Count( const geometry_piece_list_t *pList ) noexcept;

// Appends a copy of the piece's planes as a new entry. Failure-atomic.
CYPHER_NODISCARD geometry_status_t PieceList_TryAppend(
    geometry_piece_list_t *pList, const geometry_brush_piece_t *pPiece ) noexcept;

// Copies entry iPiece into a caller-initialized piece. Failure-atomic.
CYPHER_NODISCARD geometry_status_t PieceList_TryGet(
    const geometry_piece_list_t *pList,
    common::usize iPiece,
    geometry_brush_piece_t *pPieceOut ) noexcept;

// Truncates the list to its first cPieces entries (rollback after a
// multi-fragment operation fails partway).
void PieceList_Truncate( geometry_piece_list_t *pList, common::usize cPieces ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_PIECE_H
