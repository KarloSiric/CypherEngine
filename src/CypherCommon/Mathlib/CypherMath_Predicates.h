//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Predicates.h
//  Purpose: Declares exact-sign orientation predicates for geometry authoring.
//  Details: Orient2D/Orient3D answer "which side" questions that must never
//           flip sign due to floating-point rounding -- BrushSolid face
//           classification and Polygon3d triangulation both depend on that
//           guarantee near-degenerate (nearly collinear/coplanar) inputs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Predicates Contract

Orient2D(a, b, c) returns the sign of twice the signed area of triangle (a, b, c):
positive when c is left of the directed line a->b, negative when c is to the right,
zero when the three points are exactly collinear. Orient3D(a, b, c, d) returns the
sign of six times the signed volume of tetrahedron (a, b, c, d): positive when d is
below the plane through a, b, c in CypherMath's right-handed winding, negative when
above, zero when the four points are exactly coplanar.

Both take a fast filtered path first: a plain double computation plus a conservative
error bound that is cheap to evaluate and can only be wrong in the direction of
under-confidence (an oversized bound just means more escalations, never a wrong
sign). Only when the filter cannot certify the sign does either predicate fall back
to exact expansion arithmetic (CypherMath_Expansion.h), which is why this
translation unit is also compiled with -ffp-contract=off -- the filtered fast path
must see the same unfused arithmetic the error bound was derived for.
================
*/

#ifndef CYPHER_COMMON_MATH_PREDICATES_H
#define CYPHER_COMMON_MATH_PREDICATES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Vector2.h"
#include "CypherMath_Vector3.h"

namespace cypher::math
{

using common::i32;

// Exact sign of twice the signed area of triangle (a, b, c). +1 when c is left of
// the directed line a->b, -1 when right, 0 when exactly collinear.
//
// Returns 0 for non-finite input as well. The i32 result has no room for a
// distinct error value, so a caller that must tell "degenerate" apart from
// "invalid" has to validate the inputs itself first -- Cypher::EditorGeometry's
// Kernel layer is where that validation belongs for authored geometry.
CYPHER_NODISCARD CYPHER_MATH_API i32 Orient2D(
    vec2d_t a, vec2d_t b, vec2d_t c ) noexcept;

// Exact sign of six times the signed volume of tetrahedron (a, b, c, d). +1 when d
// is below the plane through a, b, c under CypherMath's right-handed winding, -1
// when above, 0 when exactly coplanar.
CYPHER_NODISCARD CYPHER_MATH_API i32 Orient3D(
    vec3d_t a, vec3d_t b, vec3d_t c, vec3d_t d ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_PREDICATES_H
