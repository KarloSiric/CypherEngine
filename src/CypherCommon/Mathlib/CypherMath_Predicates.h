//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Predicates.h
//  Purpose: Declares exact-sign geometric predicates for geometry authoring.
//  Details: Orientation and circumcircle/circumsphere tests answer topological
//           questions that must never flip sign due to floating-point rounding.
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

InCircle(a, b, c, d) returns the sign of the determinant whose rows are
[x, y, x*x + y*y, 1] for a, b, c, d in that order. InSphere(a, b, c, d, e)
does the same for rows [x, y, z, x*x + y*y + z*z, 1]. Therefore a positive
result means the query point d/e is strictly inside the circumcircle/sphere only
when Orient2D(a, b, c)/Orient3D(a, b, c, d) is positive; for a negatively
oriented defining simplex the inside and outside signs reverse. Swapping any two
rows, including two defining vertices, negates the result. Zero means the lifted
determinant is exactly degenerate (cocircular/cospherical for a nondegenerate
defining simplex). If that simplex is itself degenerate, the determinant sign is
still exact but has no geometric inside/outside meaning.

Every predicate takes a fast filtered path first: a plain double computation plus a
conservative error bound that can only be wrong in the direction of under-confidence
(an oversized bound just means more escalations, never a wrong sign). An uncertain
filter falls back to exact fixed-storage expansion arithmetic: nonoverlapping
binary64 components when their exponent range is sufficient, or fixed radix-2
components for wider finite inputs. This translation unit is compiled with
-ffp-contract=off so the binary64 path sees the unfused arithmetic its error-free
transformations require.
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

// Exact sign of the lifted 4x4 determinant for a, b, c, d. For a positively
// oriented (a, b, c), +1 means d is inside its circumcircle, -1 means outside,
// and 0 means exactly cocircular. The inside/outside meanings reverse when the
// triangle orientation reverses. A degenerate defining triangle has no circle,
// but the returned algebraic determinant sign remains exact.
//
// Returns 0 for non-finite input, with the same invalid/degenerate ambiguity as
// Orient2D and Orient3D.
CYPHER_NODISCARD CYPHER_MATH_API i32 InCircle(
    vec2d_t a, vec2d_t b, vec2d_t c, vec2d_t d ) noexcept;

// Exact sign of the lifted 5x5 determinant for a, b, c, d, e. For a positively
// oriented (a, b, c, d), +1 means e is inside its circumsphere, -1 means outside,
// and 0 means exactly cospherical. The inside/outside meanings reverse when the
// tetrahedron orientation reverses. A degenerate defining tetrahedron has no
// unique sphere, but the returned algebraic determinant sign remains exact.
//
// Returns 0 for non-finite input, with the same invalid/degenerate ambiguity as
// Orient2D and Orient3D.
CYPHER_NODISCARD CYPHER_MATH_API i32 InSphere(
    vec3d_t a, vec3d_t b, vec3d_t c, vec3d_t d, vec3d_t e ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_PREDICATES_H
