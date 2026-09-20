//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector3.h
//  Purpose: Declares the engine's three-dimensional vector contract.
//  Details: This API provides construction, validation, arithmetic, geometric,
//           interpolation, and basis operations shared by runtime and tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_VECTOR3_H
#define CYPHER_COMMON_MATH_VECTOR3_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Annotations.h"
#include "CypherMath_API.h"
#include "CypherCommon_BaseTypes.h"

#include <type_traits>

namespace cypher::math
{

using common::bool_t;
using common::f32;
using common::f64;
using common::u32;

struct vec3_t {
    f32 x; // Forward-axis component under the Cypher coordinate convention.
    f32 y; // Left-axis component under the Cypher coordinate convention.
    f32 z; // Up-axis component under the Cypher coordinate convention.
};

// Binary64 authoring vector. Geometry authoring keeps this precision until an
// explicit checked cook/runtime conversion requests vec3_t.
struct vec3d_t {
    f64 x;
    f64 y;
    f64 z;
};

inline constexpr vec3_t CY_VEC3_ZERO{ 0.0f, 0.0f, 0.0f };       // Additive identity.
inline constexpr vec3_t CY_VEC3_ONE{ 1.0f, 1.0f, 1.0f };        // Unit value on every axis.
inline constexpr vec3_t CY_VEC3_FORWARD{ 1.0f, 0.0f, 0.0f };    // Positive X direction.
inline constexpr vec3_t CY_VEC3_BACKWARD{ -1.0f, 0.0f, 0.0f }; // Negative X direction.
inline constexpr vec3_t CY_VEC3_LEFT{ 0.0f, 1.0f, 0.0f };       // Positive Y direction.
inline constexpr vec3_t CY_VEC3_RIGHT{ 0.0f, -1.0f, 0.0f };     // Negative Y direction.
inline constexpr vec3_t CY_VEC3_UP{ 0.0f, 0.0f, 1.0f };         // Positive Z direction.
inline constexpr vec3_t CY_VEC3_DOWN{ 0.0f, 0.0f, -1.0f };      // Negative Z direction.

inline constexpr vec3d_t CY_VEC3D_ZERO{ 0.0, 0.0, 0.0 };
inline constexpr vec3d_t CY_VEC3D_ONE{ 1.0, 1.0, 1.0 };
inline constexpr vec3d_t CY_VEC3D_FORWARD{ 1.0, 0.0, 0.0 };
inline constexpr vec3d_t CY_VEC3D_BACKWARD{ -1.0, 0.0, 0.0 };
inline constexpr vec3d_t CY_VEC3D_LEFT{ 0.0, 1.0, 0.0 };
inline constexpr vec3d_t CY_VEC3D_RIGHT{ 0.0, -1.0, 0.0 };
inline constexpr vec3d_t CY_VEC3D_UP{ 0.0, 0.0, 1.0 };
inline constexpr vec3d_t CY_VEC3D_DOWN{ 0.0, 0.0, -1.0 };

// Some useful macros that I use for convenience mostly ---------------------------------------------
#define CY_VEC3_MAKE(x, y, z) \
    (::cypher::math::Vec3_Make((x), (y), (z)))

#define CY_VEC3_DOT(a, b) \
    (::cypher::math::Vec3_Dot((a), (b)))

#define CY_VEC3_CROSS(a, b) \
    (::cypher::math::Vec3_Cross((a), (b)))

#define CY_VEC3_LENGTH(value) \
    (::cypher::math::Vec3_Length((value)))

#define CY_VEC3_LENGTH_SQ(value) \
    (::cypher::math::Vec3_LengthSquared((value)))

#define CY_VEC3_DISTANCE(a, b) \
    (::cypher::math::Vec3_Distance((a), (b)))

#define CY_VEC3_LERP(a, b, t) \
    (::cypher::math::Vec3_Lerp((a), (b), (t)))

/* Construction and component access. */
CYPHER_NODISCARD constexpr vec3_t Vec3_Make( f32 x, f32 y, f32 z ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Splat( f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_FromArray( CY_IN_READS( 3 ) const f32 *pValues ) noexcept;
CYPHER_MATH_API void Vec3_Store( vec3_t value, CY_OUT_WRITES( 3 ) f32 *pValues ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_Component( vec3_t value, u32 iComponent ) noexcept;
CYPHER_MATH_API void Vec3_SetComponent( CY_INOUT vec3_t *pValue, u32 iComponent, f32 value ) noexcept;

/* NOTE: Adding for double precisions special functionalities **********************/
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_FromArray(
    CY_IN_READS( 3 ) const f64 *pValues ) noexcept;
CYPHER_MATH_API void Vec3d_Store(
    vec3d_t value, CY_OUT_WRITES( 3 ) f64 *pValues ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec3d_Component(
    vec3d_t value, u32 iComponent ) noexcept;
CYPHER_MATH_API void Vec3d_SetComponent(
    CY_INOUT vec3d_t *pValue, u32 iComponent, f64 value ) noexcept;

// Widening is exact and lossless; narrowing is checked for the same reason as Vec2d.
CYPHER_NODISCARD constexpr vec3d_t Vec3d_FromVec3( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryToVec3(
    vec3d_t value, CY_OUT vec3_t *pResult ) noexcept;

/* Validation and comparisons. */
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_IsFinite( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec3_EqualsExact( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_NearlyEquals(
    vec3_t a, vec3_t b, f32 absoluteTolerance, f32 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_IsNearZero( vec3_t value, f32 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_IsUnitLength( vec3_t value, f32 tolerance ) noexcept;

/* Component-wise arithmetic. Divisors must be nonzero. */
CYPHER_NODISCARD constexpr vec3_t Vec3_Add( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Subtract( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_MultiplyComponents( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_DivideComponents( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Scale( vec3_t value, f32 scale ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_DivideScalar( vec3_t value, f32 divisor ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Negate( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_MulAdd( vec3_t a, vec3_t b, f32 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Abs( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Min( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Max( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Clamp( vec3_t value, vec3_t minimum, vec3_t maximum ) noexcept;

/* Component rounding used by grid and spatial operations. */
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Floor( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Ceil( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Round( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_Truncate( vec3_t value ) noexcept;

/* Products, magnitude and distance. */
CYPHER_NODISCARD constexpr f32 Vec3_Dot( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Cross( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec3_SumComponents( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec3_ProductComponents( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_MinComponent( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_MaxComponent( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_MaxAbsComponent( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec3_LengthSquared( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_Length( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec3_LengthXYSquared( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_LengthXY( vec3_t value ) noexcept;
CYPHER_NODISCARD constexpr f32 Vec3_DistanceSquared( vec3_t a, vec3_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_Distance( vec3_t a, vec3_t b ) noexcept;

/* Normalization and interpolation. */
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_NormalizeUnchecked( vec3_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TryNormalize(
    vec3_t value, f32 minimumLength, CY_OUT vec3_t *pNormalized,
    CY_OUT_OPTIONAL f32 *pOriginalLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TrySetLength(
    vec3_t value,
    f32 requestedLength,
    f32 minimumInputLength,
    CY_OUT vec3_t *pResult ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_Lerp( vec3_t a, vec3_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_LerpClamped( vec3_t a, vec3_t b, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_MoveTowards(
    vec3_t current, vec3_t target, f32 maximumDistance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Vec3_ClampLength(
    vec3_t value, f32 minimumLength, f32 maximumLength ) noexcept;

/* Checked geometric operations accepting arbitrary nonzero vectors. */
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TryProjectOnto(
    vec3_t value,
    vec3_t onto,
    f32 minimumLength,
    CY_OUT vec3_t *pProjected ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TryAngleBetween(
    vec3_t a,
    vec3_t b,
    f32 minimumLength,
    CY_OUT f32 *pAngleRadians ) noexcept;

// Produces a unit vector perpendicular to the supplied nonzero vector.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TryBuildUnitPerpendicular(
    vec3_t value,
    f32 minimumLength,
    CY_OUT vec3_t *pPerpendicular ) noexcept;

/* Unit-vector geometric operations. */
CYPHER_NODISCARD constexpr vec3_t Vec3_ProjectOntoUnit( vec3_t value, vec3_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_RejectFromUnit( vec3_t value, vec3_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_ProjectOntoPlaneUnitNormal( vec3_t value, vec3_t unitNormal ) noexcept;
CYPHER_NODISCARD constexpr vec3_t Vec3_ReflectUnitNormal( vec3_t incident, vec3_t unitNormal ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3_TryRefractUnitNormal(
    vec3_t incident, vec3_t unitNormal, f32 eta, CY_OUT vec3_t *pRefracted ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Vec3_AngleBetweenUnit( vec3_t a, vec3_t b ) noexcept;
CYPHER_MATH_API void Vec3_BuildOrthonormalBasis(
    vec3_t unitNormal, CY_OUT vec3_t *pTangent, CY_OUT vec3_t *pBitangent ) noexcept;

// Binary64 authoring subset -----------------------------------------------------
// Raw constexpr arithmetic follows IEEE-754 and does not validate operands;
// LengthSquared may overflow. TryLength and TryNormalize scale before squaring,
// reject non-finite or unrepresentable results, and reset outputs on failure.
// TryLength accepts the zero vector. TryNormalize requires length > minimumLength.
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Make(
    f64 x, f64 y, f64 z ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Splat( f64 value ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_IsFinite( vec3d_t value ) noexcept;
CYPHER_NODISCARD constexpr bool_t Vec3d_EqualsExact(
    vec3d_t a, vec3d_t b ) noexcept;

// NOTE: DOUBLE PRECISIONS important!!!!!
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_NearlyEquals(
    vec3d_t a, vec3d_t b, f64 absoluteTolerance, f64 relativeTolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_IsNearZero(
    vec3d_t value, f64 tolerance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_IsUnitLength(
    vec3d_t value, f64 tolerance ) noexcept;

CYPHER_NODISCARD constexpr vec3d_t Vec3d_Add(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Subtract(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Scale(
    vec3d_t value, f64 scale ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_DivideScalar(
    vec3d_t value, f64 divisor ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Negate( vec3d_t value ) noexcept;

CYPHER_NODISCARD constexpr f64 Vec3d_Dot( vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Cross(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr f64 Vec3d_LengthSquared( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryLength(
    vec3d_t value, CY_OUT f64 *pLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryNormalize(
    vec3d_t value, f64 minimumLength, CY_OUT vec3d_t *pNormalized,
    CY_OUT_OPTIONAL f64 *pOriginalLength ) noexcept;

/* Component-wise arithmetic. */
CYPHER_NODISCARD constexpr vec3d_t Vec3d_MultiplyComponents(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_DivideComponents(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_MulAdd(
    vec3d_t a, vec3d_t b, f64 scale ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Abs( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Min( vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Max( vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Clamp(
    vec3d_t value, vec3d_t minimum, vec3d_t maximum ) noexcept;

/* Component rounding. */
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Floor( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Ceil( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Round( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_Truncate( vec3d_t value ) noexcept;

/* Reductions. */
CYPHER_NODISCARD constexpr f64 Vec3d_SumComponents( vec3d_t value ) noexcept;
CYPHER_NODISCARD constexpr f64 Vec3d_ProductComponents( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec3d_MinComponent( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec3d_MaxComponent( vec3d_t value ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec3d_MaxAbsComponent( vec3d_t value ) noexcept;

/* Distance. */
CYPHER_NODISCARD constexpr f64 Vec3d_DistanceSquared( vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryDistance(
    vec3d_t a, vec3d_t b, CY_OUT f64 *pDistance ) noexcept;

/* Interpolation and movement. */
CYPHER_NODISCARD constexpr vec3d_t Vec3d_Lerp( vec3d_t a, vec3d_t b, f64 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_LerpClamped(
    vec3d_t a, vec3d_t b, f64 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_MoveTowards(
    vec3d_t current, vec3d_t target, f64 maximumDistance ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3d_t Vec3d_ClampLength(
    vec3d_t value, f64 minimumLength, f64 maximumLength ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TrySetLength(
    vec3d_t value, f64 requestedLength, f64 minimumInputLength,
    CY_OUT vec3d_t *pResult ) noexcept;

/* Unit-vector geometric operations. */
CYPHER_NODISCARD constexpr vec3d_t Vec3d_ProjectOntoUnit(
    vec3d_t value, vec3d_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_RejectFromUnit(
    vec3d_t value, vec3d_t unitDirection ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_ProjectOntoPlaneUnitNormal(
    vec3d_t value, vec3d_t unitNormal ) noexcept;
CYPHER_NODISCARD constexpr vec3d_t Vec3d_ReflectUnitNormal(
    vec3d_t incident, vec3d_t unitNormal ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryProjectOnto(
    vec3d_t value, vec3d_t onto, f64 minimumLength,
    CY_OUT vec3d_t *pProjected ) noexcept;

/* Angle. */
CYPHER_NODISCARD CYPHER_MATH_API f64 Vec3d_AngleBetweenUnit(
    vec3d_t a, vec3d_t b ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryAngleBetween(
    vec3d_t a, vec3d_t b, f64 minimumLength,
    CY_OUT f64 *pAngleRadians ) noexcept;

/* Basis construction. */
CYPHER_NODISCARD CYPHER_MATH_API bool_t Vec3d_TryBuildUnitPerpendicular(
    vec3d_t value, f64 minimumLength,
    CY_OUT vec3d_t *pPerpendicular ) noexcept;
CYPHER_MATH_API void Vec3d_BuildOrthonormalBasis(
    vec3d_t unitNormal, CY_OUT vec3d_t *pTangent, CY_OUT vec3d_t *pBitangent ) noexcept;

/* Deterministic ordering. */
CYPHER_NODISCARD constexpr bool_t Vec3d_LexicographicLess(
    vec3d_t a, vec3d_t b ) noexcept;

static_assert( sizeof( vec3_t ) == 12u, "vec3_t must remain tightly packed." );
static_assert( alignof( vec3_t ) == alignof( f32 ), "vec3_t must remain scalar-aligned." );
static_assert( std::is_standard_layout_v<vec3_t> );
static_assert( std::is_trivial_v<vec3_t> );
static_assert( std::is_trivially_copyable_v<vec3_t> );

static_assert( sizeof( vec3d_t ) == 24u );
static_assert( alignof( vec3d_t ) == alignof( f64 ) );
static_assert( std::is_standard_layout_v<vec3d_t> );
static_assert( std::is_trivial_v<vec3d_t> );
static_assert( std::is_trivially_copyable_v<vec3d_t> );

}           // namespace cypher::math

#ifndef CYPHER_COMMON_MATH_VECTOR3_INL
    #include "CypherMath_Vector3.inl"
#endif

#endif      // CYPHER_COMMON_MATH_VECTOR3_H
