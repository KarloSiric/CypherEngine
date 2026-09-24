//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCSG.h
//  Purpose: Declares brush-level constructive solid geometry operations.
//  Details: All CSG here operates on convex brush solids. The intersection
//           of two convex sets is always convex, so intersection produces
//           a single brush. Subtraction of a convex brush from another
//           convex brush produces a set of convex fragments whose union
//           equals the subtraction result.
//
//           Classification determines the spatial relationship between
//           two brushes before the heavy operation runs, allowing the
//           caller to early-out on disjoint or contained cases.
//
//           Provenance: every output side retains the source ID of the
//           operand side it originated from. Newly introduced sides (clip
//           planes from subtraction) receive fresh IDs from the allocator.
//
//           Failure safety: if any CSG operation fails, both operands are
//           left unchanged and any partially built output is cleaned up.
//
//           Identity publication is deterministic for identical operands,
//           operand side order, policy, and initial allocator state. Identity
//           allocation is staged with the output and committed only on
//           success. A successful operation may therefore leave intentional
//           gaps for temporary or pruned records; source IDs are monotonic
//           and those unpublished identities are never recycled.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

// Spatial relationship between two convex brush boundaries. Determined
// by testing all vertices of each brush against all planes of the other.
enum class brush_csg_classification_t : common::u8 {
    // The two brushes share interior volume — CSG is productive.
    INTERSECTS = 0u,

    // No shared volume — the brushes are completely separate.
    DISJOINT,

    // Brush A is entirely contained within brush B.
    A_INSIDE_B,

    // Brush B is entirely contained within brush A.
    B_INSIDE_A,

    // The brushes touch at a point, edge, or face but share no interior
    // volume. CSG intersection would produce a degenerate result.
    TOUCHING,

    // One or both inputs are invalid or have no boundary.
    INVALID
};

// Classifies the spatial relationship between two brush boundaries.
// Both boundaries must be reconstructed from their corresponding brush
// arguments before calling. A structurally valid boundary belonging to a
// different brush is rejected. The tolerance controls the thickness of the
// "on-plane" band for correspondence and vertex classification.
CYPHER_NODISCARD brush_csg_classification_t BrushCSG_Classify(
    const brush_solid_t *pBrushA,
    const brush_boundary_t *pBoundaryA,
    const brush_solid_t *pBrushB,
    const brush_boundary_t *pBoundaryB,
    common::f64 tolerance ) noexcept;

// ---------------------------------------------------------------------------
// Intersection
// ---------------------------------------------------------------------------

// Computes A ∩ B by merging all half-space planes from both brushes into
// a single output brush. Since both operands are convex, the intersection
// is guaranteed convex. Boundary reconstruction on the output reveals the
// actual geometry.
//
// After merging, redundant planes (sides that contribute no face to the
// boundary) are pruned from the output to keep the side array clean.
//
// Returns DEGENERATE if the intersection has no interior volume (the
// brushes are disjoint or only touch). Returns OK on success.
//
// The output brush must be default-initialized. On failure it is left
// in a clean shutdown state.
CYPHER_NODISCARD geometry_status_t BrushCSG_TryIntersect(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pResultOut ) noexcept;

// ---------------------------------------------------------------------------
// Subtraction
// ---------------------------------------------------------------------------

enum class brush_csg_operand_t : common::u8 {
    MINUEND_A = 0u,
    SUBTRAHEND_B
};

enum class brush_csg_side_origin_t : common::u8 {
    INHERITED_A = 0u,
    CUT_FROM_B_EXTERIOR,
    PARTITION_FROM_B_INTERIOR
};

// Exact ancestry captured while the raw subtraction is constructed. This is
// deliberately separate from persistent destination identity: one inherited
// A side may occur in several fragments, while every adopted occurrence must
// later receive its own document-unique ID.
struct brush_csg_raw_side_provenance_t {
    geometry_source_id_t fragmentId{};
    geometry_source_id_t sideId{};
    brush_csg_operand_t operand{ brush_csg_operand_t::MINUEND_A };
    brush_csg_side_origin_t origin{ brush_csg_side_origin_t::INHERITED_A };
    geometry_source_id_t sourceBrushId{};
    geometry_source_id_t sourceSideId{};
    common::u32 iSourceAttribute{ 0u };
};

// Result of a subtraction operation. Contains an array of convex fragments
// whose union equals A \ B. The caller owns all fragments and must shut
// the result down with BrushCSGSubtractResult_Shutdown.
//
// Storage is allocated to the operation-specific upper bound (the number of
// sides on B) and is constrained by geometry_policy_t. Keeping this dynamic
// avoids a second, unrelated compile-time fragment limit. A zero-initialized
// record is the only valid output destination.
struct brush_csg_subtract_result_t {
    brush_csg_subtract_result_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( brush_csg_subtract_result_t );

    // Only [0, cFragments) contains initialized brush solids. The remaining
    // slots are canonical empty brush records reserved for construction.
    brush_solid_t *fragments{ nullptr };

    // Number of valid fragments in the array. Zero means the subtraction
    // was empty (B contains A).
    common::usize cFragments{ 0u };

    // Number of constructed brush_solid_t slots in fragments.
    common::usize cCapacity{ 0u };

    // One record per side occurrence, ordered by fragment and then side.
    // It is captured from construction-time identity rather than inferred
    // from planes after the Boolean, so coincident planes remain unambiguous.
    common::vector_t<brush_csg_raw_side_provenance_t> sideProvenance{};

    // Allocator that owns fragments. Null only for the canonical empty state.
    const common::allocator_t *pAllocator{ nullptr };
};

// Shuts down all fragments in a subtract result and resets the count.
void BrushCSGSubtractResult_Shutdown(
    brush_csg_subtract_result_t *pResult ) noexcept;

// Computes A \ B by iteratively clipping A with each flipped plane of B.
// The algorithm partitions A into at most |sides(B)| convex fragments,
// each fragment being the portion of A on the exterior side of one of B's
// planes while remaining inside all previously processed planes.
//
// Provenance: each fragment's sides retain their original source IDs and
// attribute indices. New clip sides derived from B's planes receive fresh
// IDs and inherit the corresponding B-side attribute index.
//
// Returns OK if the subtraction produced at least one fragment.
// Returns DEGENERATE if A is entirely contained in B (empty result).
// Returns LIMIT_EXCEEDED when the policy cannot represent or budget the
// intermediate/output fragments.
// Returns INVALID_ARGUMENT for null or uninitialized inputs.
//
// On failure, pResultOut is left clean (all fragments shut down, count 0).
// Both operands are never modified.
CYPHER_NODISCARD geometry_status_t BrushCSG_TrySubtract(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_subtract_result_t *pResultOut ) noexcept;

// ---------------------------------------------------------------------------
// Hollow
// ---------------------------------------------------------------------------

// Hollows a brush by insetting each face by `wallThickness` and subtracting
// the inner solid from the outer. The result is a set of convex wall
// fragments whose union is the original brush minus its hollow interior.
//
// The inset brush is built by shifting each plane of the original brush
// inward by `wallThickness` units along its outward normal. This produces
// a smaller brush with the same shape, centered inside the original.
// Its operation-local sides preserve the original brush and side ancestry,
// so both inherited outer faces and generated inner faces can resolve their
// material/UV source against pBrush after the temporary inset is destroyed.
//
// Parameters:
//   pBrush         — the brush to hollow (not modified)
//   wallThickness  — distance to inset each face (strictly positive)
//   pAllocator     — allocator for fragment brushes
//   pIdAllocator   — source ID allocator for new clip planes
//   policy         — geometry policy for all intermediate operations
//   pResultOut     — receives the wall fragments (same layout as subtraction)
//
// Returns DEGENERATE if the wall thickness is too large for the brush
// (the inset brush collapses to zero volume). Returns OK on success.
CYPHER_NODISCARD geometry_status_t BrushCSG_TryHollow(
    const brush_solid_t *pBrush,
    common::f64 wallThickness,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_subtract_result_t *pResultOut ) noexcept;

// ---------------------------------------------------------------------------
// Merge (union)
// ---------------------------------------------------------------------------

// Merges two convex brushes into a single convex brush when their union
// is itself convex. Builds a convex hull from all boundary vertices of
// both brushes, then verifies that every hull plane is an existing operand
// plane. A newly introduced supporting plane spans a concavity between the
// operands, so the merge is rejected with DEGENERATE.
//
// Both boundaries must be reconstructed from their corresponding brush
// arguments before calling. Mismatched brush-boundary pairs are rejected
// with INVALID_ARGUMENT.
//
// On success, pResultOut is a fully formed brush_solid_t whose planes are
// the convex hull planes. Each side inherits the source ID and attribute
// index of the first equivalent operand plane, searching A before B and
// preserving operand side order as the deterministic tie-break.
//
// On failure, pResultOut is left in a clean shutdown state.
CYPHER_NODISCARD geometry_status_t BrushCSG_TryMerge(
    const brush_solid_t *pBrushA,
    const brush_boundary_t *pBoundaryA,
    const brush_solid_t *pBrushB,
    const brush_boundary_t *pBoundaryB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pResultOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_H
