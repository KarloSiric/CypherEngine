//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Constraints.h
//  Purpose: Declares deterministic snapping and constraint resolution in
//           binary64.
//  Details: Constraints change a proposed coordinate or direction. They
//           never weld, merge, or otherwise change topology.
//
//           Grid rounding is round-half-toward-positive-infinity relative
//           to the grid origin, so -0.5 cells snaps to 0 and +0.5 to +1;
//           that rule is the same for every axis and input order, and
//           snapping an already snapped value is the identity.
//
//           Candidate resolution ranks by (distance, -priority, kind,
//           source ID, element): the nearest candidate wins, higher
//           priority breaks exact distance ties, and the stable key breaks
//           the rest. Snapping the result again selects the same candidate,
//           because it is now at distance zero and any other zero-distance
//           candidate would already have tied with it.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CONSTRAINTS_H
#define CYPHER_EDITOR_GEOMETRY_CONSTRAINTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushValue.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Grid, angle, axis
// ---------------------------------------------------------------------------

// Snaps a scalar to origin + k * spacing. INVALID_ARGUMENT for non-finite
// input or non-positive spacing; LIMIT_EXCEEDED when the cell index is not
// exactly representable (|value - origin| / spacing >= 2^53).
CYPHER_NODISCARD geometry_status_t Constraint_TrySnapScalar(
    math::f64 value, math::f64 spacing, math::f64 origin, math::f64 *pSnappedOut ) noexcept;

// Snaps the axes selected by axisMask (bit 0 = x, 1 = y, 2 = z) of a
// point; unselected axes pass through.
CYPHER_NODISCARD geometry_status_t Constraint_TrySnapPoint(
    math::vec3d_t point, math::vec3d_t spacing, math::vec3d_t origin,
    common::u32 axisMask, math::vec3d_t *pSnappedOut ) noexcept;

// Snaps an angle in radians to multiples of `increment`.
CYPHER_NODISCARD geometry_status_t Constraint_TrySnapAngle(
    math::f64 radians, math::f64 increment, math::f64 *pSnappedOut ) noexcept;

// Index of the axis with the largest absolute component (ties to the lower
// axis). Hosts use it to lock a drag to its dominant axis.
CYPHER_NODISCARD common::u32 Constraint_DominantAxis( math::vec3d_t delta ) noexcept;

// Keeps only the axes in axisMask of a displacement.
CYPHER_NODISCARD math::vec3d_t Constraint_MaskAxes(
    math::vec3d_t delta, common::u32 axisMask ) noexcept;

// Projects a point onto a plane along the plane normal (unit normal
// required; DEGENERATE otherwise).
CYPHER_NODISCARD geometry_status_t Constraint_TryProjectToPlane(
    const geometry_numerical_policy_t &policy, math::vec3d_t point, math::planed_t plane,
    math::vec3d_t *pProjectedOut ) noexcept;

// ---------------------------------------------------------------------------
// Candidates
// ---------------------------------------------------------------------------

enum class geometry_snap_kind_t : common::u8 {
    VERTEX = 0u,
    EDGE_MIDPOINT,
    FACE_CENTER,
    BRUSH_CENTER,
    GRID,
    USER,
    COUNT
};

struct geometry_snap_candidate_t {
    math::vec3d_t position{};
    geometry_snap_kind_t kind{ geometry_snap_kind_t::USER };
    common::u8 priority{ 0u };
    geometry_source_id_t sourceId{};   // owning brush, or zero
    common::u32 element{ 0u };         // vertex/edge/face index or user key
};

struct geometry_snap_result_t {
    common::bool_t bSnapped{ false };
    common::usize iCandidate{ 0u };
    math::vec3d_t position{};
    math::f64 distance{ 0.0 };
};

// Picks the best candidate within maxDistance of the query (inclusive).
// Returns OK with bSnapped false when none qualifies; the result position
// then echoes the query.
CYPHER_NODISCARD geometry_status_t Constraint_TryResolve(
    math::vec3d_t query,
    common::span_t<const geometry_snap_candidate_t> candidates,
    math::f64 maxDistance,
    geometry_snap_result_t *pResultOut ) noexcept;

// Bit mask of geometry_snap_kind_t values.
inline constexpr common::u32 SNAP_KIND_MASK_VERTEX = 1u << 0u;
inline constexpr common::u32 SNAP_KIND_MASK_EDGE_MIDPOINT = 1u << 1u;
inline constexpr common::u32 SNAP_KIND_MASK_FACE_CENTER = 1u << 2u;
inline constexpr common::u32 SNAP_KIND_MASK_BRUSH_CENTER = 1u << 3u;

// Appends the brush's snap candidates of the selected kinds to pOut
// (initialized). Priorities: vertex 3, edge midpoint 2, face center 1,
// brush center 0.
CYPHER_NODISCARD geometry_status_t Constraint_TryGatherBrushCandidates(
    const geometry_brush_value_t *pValue,
    common::u32 kindMask,
    common::vector_t<geometry_snap_candidate_t> *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CONSTRAINTS_H
