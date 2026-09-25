//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snap.h
//  Purpose: Declares deterministic snapping functions for geometry editing.
//  Details: Snapping constrains a candidate position or angle to discrete
//           values. All snap functions are pure — they take a value and
//           return a snapped value with no side effects, no implicit weld,
//           and documented tie-breaking behavior.
//
//           Grid snap: rounds each axis independently to the nearest
//           multiple of the grid spacing. Exact midpoints round toward
//           positive infinity (consistent with IEEE 754 round-half-up
//           applied per-axis after division).
//
//           Angle snap: rounds an angle in radians to the nearest
//           multiple of the snap increment. Same tie-breaking rule.
//
//           Component snap: given a candidate point and an array of
//           target positions, returns the index of the nearest target
//           within a distance threshold, or reports no match. When two
//           targets are equidistant, the lower index wins.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SNAP_H
#define CYPHER_EDITOR_GEOMETRY_SNAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Grid snap
// ---------------------------------------------------------------------------

// Snaps each axis of the candidate point to the nearest multiple of
// fGridSpacing. If the candidate or spacing is non-finite, or spacing is
// zero/negative, the affected input is returned unchanged (snapping is
// effectively disabled and no NaN is manufactured from valid input).
//
// Idempotent: snapping an already-snapped value produces the same value.
CYPHER_NODISCARD math::vec3d_t Snap_GridPoint(
    math::vec3d_t candidate,
    common::f64 fGridSpacing ) noexcept;

// Snaps a single scalar value to the nearest multiple of fGridSpacing.
CYPHER_NODISCARD common::f64 Snap_GridScalar(
    common::f64 value,
    common::f64 fGridSpacing ) noexcept;

// ---------------------------------------------------------------------------
// Angle snap
// ---------------------------------------------------------------------------

// Snaps an angle in radians to the nearest multiple of fAngleIncrement.
// If the angle/increment is non-finite or the increment is zero/negative,
// the angle is returned unchanged.
//
// Idempotent: snapping an already-snapped angle produces the same angle.
CYPHER_NODISCARD common::f64 Snap_Angle(
    common::f64 fAngleRadians,
    common::f64 fAngleIncrement ) noexcept;

// ---------------------------------------------------------------------------
// Component snap
// ---------------------------------------------------------------------------

// Result of a component snap query.
struct snap_component_result_t {
    // True if a target was found within the distance threshold.
    bool bFound{ false };

    // Index of the nearest target in the input array. Valid only
    // when bFound is true.
    common::usize iTarget{ 0u };

    // Squared distance to the nearest target. Valid only when bFound
    // is true.
    common::f64 fDistanceSquared{ 0.0 };
};

// Finds the nearest target position to the candidate within
// fMaxDistance. If multiple targets are equidistant, the lowest
// index wins. The distance threshold is inclusive. Non-finite candidates,
// thresholds, and targets never produce a match. If no target is within
// range, bFound is false.
//
// Does not perform an implicit weld — the result is advisory. The
// caller decides whether to use the snapped position.
CYPHER_NODISCARD snap_component_result_t Snap_NearestComponent(
    math::vec3d_t candidate,
    const math::vec3d_t *pTargets,
    common::usize cTargets,
    common::f64 fMaxDistance ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SNAP_H
