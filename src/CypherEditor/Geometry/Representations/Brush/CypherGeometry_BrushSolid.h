//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSolid.h
//  Purpose: Declares the canonical plane-defined convex solid and its
//           side management interface.
//  Details: A brush solid is defined entirely by its set of outward-facing
//           half-space planes. Vertices, edges, and face polygons are
//           derived from the plane set by boundary reconstruction and are
//           never serialized — the planes are the single source of truth.
//           Each side carries a persistent source ID that survives edits,
//           undo, and serialization, plus an index into the brush's
//           attribute store for material and UV projection bindings.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_SOLID_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_SOLID_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Policy.h"      // settings and geometrical policies!
#include "CypherGeometry_Types.h"       // types we will need!
#include "CypherCommon_Vector.h"

#include "CypherMath.h"         /* NOTE: including everything we will need for now! */

/* NOTE: Adding namespaces for easier navigation and writings */
/*
namespace com = cypher::common;
namespace math = cypher::math;
namespace editor = cypher::editor;
*/

// using namespace cypher;
using namespace cypher::common;
using namespace cypher::math;
using namespace cypher::editor;

namespace cypher::editor::geometry
{
    
/* using cypher::math::planed_t; */
    
// A single side of a plane-defined convex solid. The plane is the canonical
// authored data; vertices and edges are derived from the complete plane set
// and are never stored here. Separating identity from derived geometry means
// a side-plane drag changes one plane value without invalidating the side's
// identity or its attribute binding.

struct brush_solid_side_t {
    // Outward-facing half-space plane. The brush interior is the intersection
    // of every side's nonpositive half-space, matching map-authoring convention.
    math::planed_t plane{};
    
    // Persistent identity for this side. Survives undo, redo, serialization,
    // and re-ordering. Allocated by the document's source-ID allocator so it
    // is unique within its geometry document.
    geometry_source_id_t sourceId{};
    
    // Index into the per-brush attribute store (material + UV projection).
    // Established when the side enters the brush and remains stable unless
    // the brush is rebuilt from scratch (e.g. CSG produces a new brush).
    common::u32 iAttributeIndex{ 0u };
};

// A convex solid defined as the intersection of outward-facing half-space
// planes. This is the canonical authoring representation — everything else
// (boundary vertices, edges, face polygons, bounds) is derived from these
// planes and cached separately in brush_boundary_t.
//
// Ownership: the brush owns its side storage. Init binds an allocator;
// Shutdown releases it. The attribute store that iAttributeIndex points
// into is owned alongside the brush by whoever holds both, typically the
// geometry document or a transaction workspace.
struct brush_solid_t {
    // Bounded side array. Growth is checked against
    // geometry_limit_policy_t::cBrushSidesPerBrushMax before allocating.
    common::vector_t<brush_solid_side_t> sides{};

    // Persistent identity for the brush as a whole, distinct from its
    // individual side identities. Used by the document, transactions,
    // selection, and serialization to refer to this brush.
    geometry_source_id_t sourceId{};
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Binds an allocator and sets the brush's persistent identity. A brush must
// be initialized before any side can be added. Calling Init on an already-
// initialized brush returns ALREADY_INITIALIZED to prevent leaking the
// existing allocation.
CYPHER_NODISCARD geometry_status_t BrushSolid_Init(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    geometry_source_id_t brushId ) noexcept;

// Releases side storage. Safe to call on a zero-initialized or already-
// shut-down brush, so partial construction can unwind without tracking
// how far it got.
void BrushSolid_Shutdown( brush_solid_t *pBrush ) noexcept;

// ---------------------------------------------------------------------------
// Side queries
// ---------------------------------------------------------------------------

// Returns zero for an uninitialized brush rather than asserting, so a
// diagnostic pass can inspect a brush in any state.
CYPHER_NODISCARD common::usize BrushSolid_SideCount(
    const brush_solid_t *pBrush ) noexcept;

// Copies one side record out by index. The output is zeroed before the
// index check so a failed call never returns stale data.
CYPHER_NODISCARD geometry_status_t BrushSolid_TryGetSide(
    const brush_solid_t *pBrush,
    common::usize iIndex,
    brush_solid_side_t *pSideOut ) noexcept;

// ---------------------------------------------------------------------------
// Side mutation
// ---------------------------------------------------------------------------

// Appends a fully formed side record. Validates:
//   - brush is initialized
//   - side count has not reached cBrushSidesPerBrushMax
//   - side's source ID is valid (non-zero)
//   - side's source ID differs from the brush ID and from every existing
//     side's ID (IDENTITY_CONFLICT otherwise)
//   - side's plane normal is finite
// The attribute index is taken as-is; the caller is responsible for having
// a valid slot in the attribute store before adding the side.
// On success, writes the new side's index to *pIndexOut (if non-null).
CYPHER_NODISCARD geometry_status_t BrushSolid_TryAddSide(
    brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits,
    const brush_solid_side_t &side,
    common::usize *pIndexOut ) noexcept;

// Replaces the plane for an existing side without touching its identity or
// attribute binding. This is the operation a side-plane drag edit uses:
// the plane moves, everything else stays. Boundary reconstruction must be
// re-run after this call to see the geometric effect.
CYPHER_NODISCARD geometry_status_t BrushSolid_TrySetSidePlane(
    brush_solid_t *pBrush,
    common::usize iIndex,
    cypher::math::planed_t plane ) noexcept;

// Reserves storage for at least nCapacity sides without adding any. Used
// by generators and copy operations that know the final count up front,
// so the growth path allocates once instead of per-side.
CYPHER_NODISCARD geometry_status_t BrushSolid_TryReserve(
    brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits,
    common::usize nCapacity ) noexcept;

// Removes all sides without releasing the underlying allocation, so the
// brush can be repopulated without re-allocating. Identity is preserved.
void BrushSolid_Clear( brush_solid_t *pBrush ) noexcept;

// The struct is trivially copyable and its fields are at known offsets;
// tail padding for f64 alignment is expected and harmless.
static_assert( std::is_trivially_copyable_v<brush_solid_side_t> );
static_assert( std::is_standard_layout_v<brush_solid_side_t> );
    
}       // namespace cypher::editor::geometry!!!

#endif          // ENDIF CYPHER_EDITOR_GEOMETRY_BRUSH_SOLID_H !!!
