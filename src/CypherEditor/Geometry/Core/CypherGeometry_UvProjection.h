//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvProjection.h
//  Purpose: Declares building a planar UV projection from any affine map of
//           a plane onto texture space, a shear included.
//  Details: A projection maps p to R(theta) * diag(1/su, 1/sv) * [u; v] *
//           (p - origin) + offset with u, v orthonormal. That looks too
//           narrow for a sheared or skew-scaled texture, but it is not: any
//           2x2 map K factors as R(phi) * diag(sx, sy) * R(beta) (Blinn,
//           "Consider the Lowly 2x2 Matrix"), and R(beta) * [e1; e2] is just
//           e1/e2 turned within their own plane, still orthonormal. So the
//           UV editor's shear and an affine texture lock (shearing a brush
//           with its textures stuck on) are exact.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_UV_PROJECTION_H
#define CYPHER_EDITOR_GEOMETRY_UV_PROJECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

#include "CypherMath_UV.h"

namespace cypher::editor::geometry
{

// Row-major [k00 k01; k10 k11]: UV per unit along e1 (first column) and e2.
struct uv_plane_map_t {
    common::f64 k00{ 1.0 }, k01{ 0.0 }, k10{ 0.0 }, k11{ 1.0 };
};

// Sets *pMapping so origin + a*e1 + b*e2 projects to offset + K * (a, b).
// e1 and e2 must be orthonormal and K non-singular (DEGENERATE otherwise;
// NUMERIC_FAILURE for non-finite input). Of the equivalent factorings the
// smallest turn of e1/e2 is used, so a K that is a turn and a scale keeps
// e1/e2 as the axes exactly. A negative determinant (a mirror) becomes a
// negative scale. The normal is cross(u, v). *pMapping is unchanged on failure.
CYPHER_NODISCARD geometry_status_t UvProjection_TryFromPlaneMap( math::planar_uv_mappingd_t *pMapping, math::vec3d_t origin,
                                                                 math::vec3d_t e1, math::vec3d_t e2, const uv_plane_map_t &k,
                                                                 math::vec2d_t offset ) noexcept;

// The projection's own map in the same terms: UV per unit along uAxis and
// vAxis, rotation and scale folded in.
CYPHER_NODISCARD uv_plane_map_t UvProjection_PlaneMap( const math::planar_uv_mappingd_t &mapping ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_UV_PROJECTION_H
