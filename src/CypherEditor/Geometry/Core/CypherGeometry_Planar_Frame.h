//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Frame.h
//  Purpose: Declares the orthonormal plane frame used to move geometry
//           between 3D authoring space and a 2D planar working space.
//  Details: Every Planar algorithm (arrangement, overlay, offset,
//           triangulation) runs in 2D. A frame fixes the mapping so that
//           results can be lifted back into 3D exactly where they came from
//           and so that the same plane always yields the same 2D axes.
//
//           Frames are right-handed: cross(u, v) == normal. A polygon that
//           winds counter-clockwise around `normal` in 3D therefore winds
//           counter-clockwise in (u, v), which is what lets 2D orientation
//           predicates stand in for 3D winding tests.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_FRAME_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_FRAME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Orthonormal right-handed frame embedded in 3D.
//   world(p2) = origin + p2.x * u + p2.y * v
struct planar_frame_t {
    math::vec3d_t origin{};
    math::vec3d_t u{};
    math::vec3d_t v{};
    math::vec3d_t normal{};
};

// Result of building a frame from a point set: the frame plus the largest
// absolute out-of-plane distance of any input point, so callers can report
// how warped a rejected face was.
struct planar_frame_result_t {
    planar_frame_t frame{};
    common::f64 fMaxDeviation{ 0.0 };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// True when every vector is finite, u/v/normal are unit length within
// fUnitTolerance, mutually orthogonal within fUnitTolerance, and
// cross(u, v) agrees with normal. Frames built by this module always pass
// with the policy's fUnitNormalTolerance.
CYPHER_NODISCARD bool PlanarFrame_IsValid(
    const planar_frame_t &frame,
    common::f64 fUnitTolerance ) noexcept;

// Builds the canonical frame of a plane (dot(n, x) + d = 0).
//
// Determinism: the origin is the plane point closest to the world origin
// (-d * n), and u is derived from the world axis *least* aligned with the
// normal, ties broken X before Y before Z. Two calls with the same plane
// value always produce bit-identical frames, which matters because 2D
// results are keyed and cached by their frame.
//
// Returns INVALID_ARGUMENT for null output, NUMERIC_FAILURE for a
// non-finite or zero-length normal.
CYPHER_NODISCARD geometry_status_t PlanarFrame_TryFromPlane(
    math::planed_t plane,
    planar_frame_t *pFrameOut ) noexcept;

// Builds a frame for a 3D polygon loop. The normal is the Newell normal of
// the loop (so the loop is CCW in the resulting 2D space), the origin is
// the vertex centroid, and u follows the same axis rule as FromPlane.
//
// Fails with:
//   INVALID_ARGUMENT — null points or fewer than 3 points;
//   NUMERIC_FAILURE  — non-finite coordinates;
//   DEGENERATE       — the loop has (near) zero vector area;
//   NON_PLANAR       — some point is farther than fPlanarityTolerance from
//                      the fitted plane (fMaxDeviation still reported).
CYPHER_NODISCARD planar_frame_result_t PlanarFrame_TryFromLoop(
    const math::vec3d_t *pPoints,
    common::usize cPoints,
    common::f64 fPlanarityTolerance ) noexcept;

// Maps a world point into frame coordinates. Points off the plane are
// projected orthogonally; use SignedDistance to detect that.
CYPHER_NODISCARD math::vec2d_t PlanarFrame_Project(
    const planar_frame_t &frame,
    math::vec3d_t point ) noexcept;

// Maps frame coordinates back into world space (always on the plane).
CYPHER_NODISCARD math::vec3d_t PlanarFrame_Unproject(
    const planar_frame_t &frame,
    math::vec2d_t point ) noexcept;

// Signed distance of a world point from the frame plane along `normal`.
CYPHER_NODISCARD common::f64 PlanarFrame_SignedDistance(
    const planar_frame_t &frame,
    math::vec3d_t point ) noexcept;

// The frame's plane in dot(n, x) + d = 0 form.
CYPHER_NODISCARD math::planed_t PlanarFrame_Plane(
    const planar_frame_t &frame ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_FRAME_H
