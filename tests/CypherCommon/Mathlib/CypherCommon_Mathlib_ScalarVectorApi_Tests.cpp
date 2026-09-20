//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_ScalarVectorApi_Tests.cpp
//  Purpose: Verifies the complete scalar, angle, and vector public API.
//  Details: These contract tests cover construction, component access, arithmetic,
//           rounding, interpolation, geometric helpers, and checked operations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cmath>
#include <limits>
#include <type_traits>

using namespace cypher::math;
using Catch::Approx;

namespace
{

void RequireVec2( vec2_t value, f32 x, f32 y, f32 margin = 0.00001f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

void RequireVec2d( vec2d_t value, f64 x, f64 y, f64 margin = 1e-9 )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
}

void RequireVec3d( vec3d_t value, f64 x, f64 y, f64 z, f64 margin = 1e-9 )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

void RequireVec3(
    vec3_t value, f32 x, f32 y, f32 z, f32 margin = 0.00001f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

void RequireVec4(
    vec4_t value, f32 x, f32 y, f32 z, f32 w,
    f32 margin = 0.00001f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
    REQUIRE( value.w == Approx( w ).margin( margin ) );
}

} // namespace

TEST_CASE( "scalar arithmetic and interpolation expose stable semantics",
           "[CypherCommon][Mathlib][Scalar][API]" )
{
    REQUIRE( Scalar_Square( 3.0f ) == 9.0f );
    REQUIRE( Scalar_Square( 4.0 ) == 16.0 );
    REQUIRE( Scalar_Lerp( 10.0f, 20.0f, 0.25f ) == Approx( 12.5f ) );
    REQUIRE( Scalar_Lerp( 10.0, 20.0, 0.75 ) == Approx( 17.5 ) );
    REQUIRE( Scalar_IsNearZero( 0.0001f, 0.001f ) );
    REQUIRE( Scalar_IsNearZero( 0.0001, 0.001 ) );

    REQUIRE( Scalar_Abs( -3.0f ) == 3.0f );
    REQUIRE( Scalar_Abs( -3.0 ) == 3.0 );
    REQUIRE( Scalar_Min( 2.0f, -1.0f ) == -1.0f );
    REQUIRE( Scalar_Min( 2.0, -1.0 ) == -1.0 );
    REQUIRE( Scalar_Max( 2.0f, -1.0f ) == 2.0f );
    REQUIRE( Scalar_Max( 2.0, -1.0 ) == 2.0 );
    REQUIRE( Scalar_Clamp( 7.0f, 1.0f, 4.0f ) == 4.0f );
    REQUIRE( Scalar_Clamp( -2.0, -1.0, 4.0 ) == -1.0 );
    REQUIRE( Scalar_Saturate( -1.0f ) == 0.0f );
    REQUIRE( Scalar_Saturate( 2.0 ) == 1.0 );
    REQUIRE( Scalar_Sign( -8.0f ) == -1.0f );
    REQUIRE( Scalar_Sign( 0.0 ) == 0.0 );
}

TEST_CASE( "scalar transcendental and rounding wrappers match their contracts",
           "[CypherCommon][Mathlib][Scalar][API]" )
{
    REQUIRE( Scalar_Sqrt( 25.0f ) == Approx( 5.0f ) );
    REQUIRE( Scalar_Sqrt( 25.0 ) == Approx( 5.0 ) );
    REQUIRE( Scalar_InvSqrt( 4.0f ) == Approx( 0.5f ) );
    REQUIRE( Scalar_InvSqrt( 4.0 ) == Approx( 0.5 ) );
    REQUIRE( Scalar_Sin( CY_HALF_PI_F ) == Approx( 1.0f ) );
    REQUIRE( Scalar_Cos( CY_PI_F ) == Approx( -1.0f ) );
    REQUIRE( Scalar_Tan( CY_QUARTER_PI_F ) == Approx( 1.0f ) );

    f32 sine = 0.0f;
    f32 cosine = 0.0f;
    Scalar_SinCos( CY_HALF_PI_F, &sine, &cosine );
    REQUIRE( sine == Approx( 1.0f ) );
    REQUIRE( cosine == Approx( 0.0f ).margin( 0.000001f ) );
    REQUIRE( Scalar_AsinClamped( 2.0f ) == Approx( CY_HALF_PI_F ) );
    REQUIRE( Scalar_AcosClamped( -2.0f ) == Approx( CY_PI_F ) );
    REQUIRE( Scalar_Atan2( 1.0f, 0.0f ) == Approx( CY_HALF_PI_F ) );

    REQUIRE( Scalar_Floor( -1.25f ) == -2.0f );
    REQUIRE( Scalar_Ceil( -1.25f ) == -1.0f );
    REQUIRE( Scalar_Round( 1.6f ) == 2.0f );
    REQUIRE( Scalar_Truncate( -1.75f ) == -1.0f );
    REQUIRE( Scalar_Fmod( 7.5f, 2.0f ) == Approx( 1.5f ) );
}

TEST_CASE( "angle values preserve radians and support arithmetic and trigonometry",
           "[CypherCommon][Mathlib][Angle][API]" )
{
    const angle_t quarter = Angle_FromRadians( CY_HALF_PI_F );
    REQUIRE( Angle_Radians( quarter ) == Approx( CY_HALF_PI_F ) );
    REQUIRE( Angle_IsFinite( quarter ) );
    REQUIRE_FALSE( Angle_IsFinite( Angle_FromRadians(
        std::numeric_limits<f32>::infinity() ) ) );

    REQUIRE( Angle_Degrees( Angle_Add( quarter, quarter ) ) ==
             Approx( 180.0f ) );
    REQUIRE( Angle_Degrees( Angle_Subtract( quarter, CY_ANGLE_HALF_TURN ) ) ==
             Approx( -90.0f ) );
    REQUIRE( Angle_Degrees( Angle_Scale( quarter, 0.5f ) ) ==
             Approx( 45.0f ) );
    REQUIRE( Angle_Degrees( Angle_Negate( quarter ) ) == Approx( -90.0f ) );
    REQUIRE( Angle_Sin( quarter ) == Approx( 1.0f ) );
    REQUIRE( Angle_Cos( quarter ) == Approx( 0.0f ).margin( 0.000001f ) );
    REQUIRE( Angle_Tan( Angle_FromDegrees( 45.0f ) ) == Approx( 1.0f ) );

    f32 sine = 0.0f;
    f32 cosine = 0.0f;
    Angle_SinCos( quarter, &sine, &cosine );
    REQUIRE( sine == Approx( 1.0f ) );
    REQUIRE( cosine == Approx( 0.0f ).margin( 0.000001f ) );
}

TEST_CASE( "Vector2 construction component access and arithmetic are complete",
           "[CypherCommon][Mathlib][Vector2][API]" )
{
    const f32 source[]{ 2.0f, -3.0f };
    vec2_t value = Vec2_FromArray( source );
    RequireVec2( value, 2.0f, -3.0f );
    REQUIRE( Vec2_Component( value, 0u ) == 2.0f );
    Vec2_SetComponent( &value, 1u, 5.0f );
    REQUIRE( Vec2_Component( value, 1u ) == 5.0f );

    f32 stored[2]{};
    Vec2_Store( value, stored );
    REQUIRE( stored[0] == 2.0f );
    REQUIRE( stored[1] == 5.0f );
    REQUIRE( Vec2_IsFinite( value ) );
    REQUIRE( Vec2_IsNearZero( Vec2_Make( 0.001f, -0.001f ), 0.01f ) );
    REQUIRE( Vec2_IsUnitLength( CY_VEC2_X, 0.00001f ) );
    REQUIRE( Vec2_NearlyEquals(
        value, Vec2_Make( 2.00001f, 5.00001f ), 0.00002f, 0.0f ) );

    RequireVec2( Vec2_Splat( 3.0f ), 3.0f, 3.0f );
    RequireVec2( Vec2_Add( value, CY_VEC2_ONE ), 3.0f, 6.0f );
    RequireVec2( Vec2_Subtract( value, CY_VEC2_ONE ), 1.0f, 4.0f );
    RequireVec2( Vec2_MultiplyComponents( value, Vec2_Make( 2.0f, 3.0f ) ),
                 4.0f, 15.0f );
    RequireVec2( Vec2_DivideComponents( value, Vec2_Make( 2.0f, 5.0f ) ),
                 1.0f, 1.0f );
    RequireVec2( Vec2_Scale( value, 2.0f ), 4.0f, 10.0f );
    RequireVec2( Vec2_DivideScalar( value, 2.0f ), 1.0f, 2.5f );
    RequireVec2( Vec2_Negate( value ), -2.0f, -5.0f );
    RequireVec2( Vec2_MulAdd( value, CY_VEC2_ONE, 3.0f ), 5.0f, 8.0f );
}

TEST_CASE( "Vector2 component math rounding and metrics are deterministic",
           "[CypherCommon][Mathlib][Vector2][API]" )
{
    const vec2_t value = Vec2_Make( -1.25f, 3.75f );
    RequireVec2( Vec2_Abs( value ), 1.25f, 3.75f );
    RequireVec2( Vec2_Min( value, Vec2_Make( -2.0f, 5.0f ) ), -2.0f, 3.75f );
    RequireVec2( Vec2_Max( value, Vec2_Make( -2.0f, 5.0f ) ), -1.25f, 5.0f );
    RequireVec2( Vec2_Clamp( value, Vec2_Make( -1.0f, 0.0f ),
                            Vec2_Make( 1.0f, 3.0f ) ),
                 -1.0f, 3.0f );
    RequireVec2( Vec2_Floor( value ), -2.0f, 3.0f );
    RequireVec2( Vec2_Ceil( value ), -1.0f, 4.0f );
    RequireVec2( Vec2_Round( value ), -1.0f, 4.0f );
    RequireVec2( Vec2_Truncate( value ), -1.0f, 3.0f );

    const vec2_t a = Vec2_Make( 3.0f, 4.0f );
    REQUIRE( Vec2_Dot( a, Vec2_Make( 2.0f, -1.0f ) ) == 2.0f );
    REQUIRE( Vec2_LengthSquared( a ) == 25.0f );
    REQUIRE( Vec2_Length( a ) == Approx( 5.0f ) );
    REQUIRE( Vec2_DistanceSquared( a, CY_VEC2_ZERO ) == 25.0f );
    REQUIRE( Vec2_Distance( a, CY_VEC2_ZERO ) == Approx( 5.0f ) );
}

TEST_CASE( "Vector2 interpolation projection and movement preserve geometry",
           "[CypherCommon][Mathlib][Vector2][API]" )
{
    RequireVec2( Vec2_NormalizeUnchecked( Vec2_Make( 3.0f, 4.0f ) ),
                 0.6f, 0.8f );
    RequireVec2( Vec2_Lerp( CY_VEC2_ZERO, Vec2_Make( 4.0f, 8.0f ), 0.25f ),
                 1.0f, 2.0f );
    RequireVec2( Vec2_LerpClamped(
        CY_VEC2_ZERO, Vec2_Make( 4.0f, 8.0f ), 2.0f ), 4.0f, 8.0f );
    RequireVec2( Vec2_MoveTowards(
        CY_VEC2_ZERO, Vec2_Make( 3.0f, 4.0f ), 2.0f ), 1.2f, 1.6f );
    RequireVec2( Vec2_ClampLength( Vec2_Make( 6.0f, 8.0f ), 2.0f, 5.0f ),
                 3.0f, 4.0f );

    const vec2_t value = Vec2_Make( 3.0f, 4.0f );
    RequireVec2( Vec2_ProjectOntoUnit( value, CY_VEC2_X ), 3.0f, 0.0f );
    RequireVec2( Vec2_RejectFromUnit( value, CY_VEC2_X ), 0.0f, 4.0f );
    RequireVec2( Vec2_ReflectUnitNormal(
        Vec2_Make( 1.0f, -2.0f ), CY_VEC2_Y ), 1.0f, 2.0f );

    vec2_t projected{};
    REQUIRE( Vec2_TryProjectOnto(
        value, Vec2_Make( 2.0f, 0.0f ), 0.000001f, &projected ) );
    RequireVec2( projected, 3.0f, 0.0f );
    f32 angle = 0.0f;
    REQUIRE( Vec2_TryAngleBetween(
        CY_VEC2_X, CY_VEC2_Y, 0.000001f, &angle ) );
    REQUIRE( angle == Approx( CY_HALF_PI_F ) );
}

TEST_CASE( "Vector3 construction component and arithmetic APIs preserve values",
           "[CypherCommon][Mathlib][Vector3][API]" )
{
    const f32 source[]{ -2.0f, 3.0f, -4.0f };
    vec3_t value = Vec3_FromArray( source );
    REQUIRE( Vec3_IsFinite( value ) );
    REQUIRE( Vec3_Component( value, 1u ) == 3.0f );
    Vec3_SetComponent( &value, 2u, 8.0f );
    REQUIRE( Vec3_Component( value, 2u ) == 8.0f );
    f32 stored[3]{};
    Vec3_Store( value, stored );
    REQUIRE( stored[0] == -2.0f );
    REQUIRE( stored[1] == 3.0f );
    REQUIRE( stored[2] == 8.0f );
    REQUIRE( Vec3_IsNearZero( Vec3_Splat( 0.001f ), 0.01f ) );

    RequireVec3( Vec3_Subtract( value, CY_VEC3_ONE ), -3.0f, 2.0f, 7.0f );
    RequireVec3( Vec3_MultiplyComponents(
        value, Vec3_Make( 2.0f, 3.0f, 4.0f ) ), -4.0f, 9.0f, 32.0f );
    RequireVec3( Vec3_DivideComponents(
        value, Vec3_Make( -2.0f, 3.0f, 4.0f ) ), 1.0f, 1.0f, 2.0f );
    RequireVec3( Vec3_DivideScalar( value, 2.0f ), -1.0f, 1.5f, 4.0f );
    RequireVec3( Vec3_Abs( value ), 2.0f, 3.0f, 8.0f );
    RequireVec3( Vec3_Min( value, Vec3_Make( -4.0f, 5.0f, 7.0f ) ),
                 -4.0f, 3.0f, 7.0f );
    RequireVec3( Vec3_Max( value, Vec3_Make( -4.0f, 5.0f, 7.0f ) ),
                 -2.0f, 5.0f, 8.0f );
    RequireVec3( Vec3_Clamp( value, Vec3_Splat( -1.0f ), Vec3_Splat( 4.0f ) ),
                 -1.0f, 3.0f, 4.0f );
}

TEST_CASE( "Vector3 rounding metrics and component reductions are complete",
           "[CypherCommon][Mathlib][Vector3][API]" )
{
    const vec3_t value = Vec3_Make( -1.25f, 2.5f, 3.75f );
    RequireVec3( Vec3_Floor( value ), -2.0f, 2.0f, 3.0f );
    RequireVec3( Vec3_Ceil( value ), -1.0f, 3.0f, 4.0f );
    RequireVec3( Vec3_Round( value ), -1.0f, 3.0f, 4.0f );
    RequireVec3( Vec3_Truncate( value ), -1.0f, 2.0f, 3.0f );
    REQUIRE( Vec3_SumComponents( Vec3_Make( 1.0f, 2.0f, 3.0f ) ) == 6.0f );
    REQUIRE( Vec3_ProductComponents( Vec3_Make( 2.0f, 3.0f, 4.0f ) ) == 24.0f );
    REQUIRE( Vec3_MinComponent( value ) == -1.25f );
    REQUIRE( Vec3_MaxComponent( value ) == 3.75f );
    REQUIRE( Vec3_MaxAbsComponent( value ) == 3.75f );

    const vec3_t metric = Vec3_Make( 2.0f, 3.0f, 6.0f );
    REQUIRE( Vec3_LengthSquared( metric ) == 49.0f );
    REQUIRE( Vec3_LengthXYSquared( metric ) == 13.0f );
    REQUIRE( Vec3_LengthXY( metric ) == Approx( std::sqrt( 13.0f ) ) );
    REQUIRE( Vec3_DistanceSquared( metric, CY_VEC3_ZERO ) == 49.0f );
}

TEST_CASE( "Vector3 interpolation and geometric helpers cover checked paths",
           "[CypherCommon][Mathlib][Vector3][API]" )
{
    RequireVec3( Vec3_Lerp(
        CY_VEC3_ZERO, Vec3_Make( 4.0f, 8.0f, 12.0f ), 0.25f ),
        1.0f, 2.0f, 3.0f );
    RequireVec3( Vec3_LerpClamped(
        CY_VEC3_ZERO, Vec3_Make( 4.0f, 8.0f, 12.0f ), 2.0f ),
        4.0f, 8.0f, 12.0f );
    RequireVec3( Vec3_MoveTowards(
        CY_VEC3_ZERO, Vec3_Make( 0.0f, 0.0f, 10.0f ), 3.0f ),
        0.0f, 0.0f, 3.0f );
    RequireVec3( Vec3_ClampLength(
        Vec3_Make( 0.0f, 0.0f, 10.0f ), 2.0f, 4.0f ),
        0.0f, 0.0f, 4.0f );
    RequireVec3( Vec3_ProjectOntoPlaneUnitNormal(
        Vec3_Make( 2.0f, 3.0f, 4.0f ), CY_VEC3_UP ),
        2.0f, 3.0f, 0.0f );
    REQUIRE( Vec3_AngleBetweenUnit( CY_VEC3_FORWARD, CY_VEC3_LEFT ) ==
             Approx( CY_HALF_PI_F ) );

    f32 angle = 0.0f;
    REQUIRE( Vec3_TryAngleBetween(
        CY_VEC3_FORWARD, CY_VEC3_LEFT, 0.000001f, &angle ) );
    REQUIRE( angle == Approx( CY_HALF_PI_F ) );
    vec3_t perpendicular{};
    REQUIRE( Vec3_TryBuildUnitPerpendicular(
        CY_VEC3_FORWARD, 0.000001f, &perpendicular ) );
    REQUIRE( Vec3_IsUnitLength( perpendicular, 0.00001f ) );
    REQUIRE( Vec3_Dot( perpendicular, CY_VEC3_FORWARD ) ==
             Approx( 0.0f ).margin( 0.000001f ) );

    vec3_t refracted{};
    REQUIRE( Vec3_TryRefractUnitNormal(
        Vec3_Make( 0.0f, 0.0f, -1.0f ), CY_VEC3_UP,
        1.0f, &refracted ) );
    RequireVec3( refracted, 0.0f, 0.0f, -1.0f );
}

TEST_CASE( "Binary64 authoring vectors expose a focused constexpr API",
           "[CypherCommon][Mathlib][Vector][Binary64][API]" )
{
    STATIC_REQUIRE( sizeof( vec2d_t ) == 2u * sizeof( f64 ) );
    STATIC_REQUIRE( sizeof( vec3d_t ) == 3u * sizeof( f64 ) );
    STATIC_REQUIRE( alignof( vec2d_t ) == alignof( f64 ) );
    STATIC_REQUIRE( alignof( vec3d_t ) == alignof( f64 ) );
    STATIC_REQUIRE( std::is_standard_layout_v<vec2d_t> );
    STATIC_REQUIRE( std::is_standard_layout_v<vec3d_t> );
    STATIC_REQUIRE( std::is_trivial_v<vec2d_t> );
    STATIC_REQUIRE( std::is_trivial_v<vec3d_t> );
    STATIC_REQUIRE( std::is_trivially_copyable_v<vec2d_t> );
    STATIC_REQUIRE( std::is_trivially_copyable_v<vec3d_t> );
    STATIC_REQUIRE( offsetof( vec2d_t, x ) == 0u );
    STATIC_REQUIRE( offsetof( vec2d_t, y ) == sizeof( f64 ) );
    STATIC_REQUIRE( offsetof( vec3d_t, x ) == 0u );
    STATIC_REQUIRE( offsetof( vec3d_t, y ) == sizeof( f64 ) );
    STATIC_REQUIRE( offsetof( vec3d_t, z ) == 2u * sizeof( f64 ) );

    constexpr vec2d_t a2 = Vec2d_Make( 2.0, -3.0 );
    constexpr vec2d_t b2 = Vec2d_Splat( 4.0 );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_Add( a2, b2 ), Vec2d_Make( 6.0, 1.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_Subtract( a2, b2 ), Vec2d_Make( -2.0, -7.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_DivideScalar( Vec2d_Scale( a2, 6.0 ), 3.0 ),
        Vec2d_Make( 4.0, -6.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_Negate( a2 ), Vec2d_Make( -2.0, 3.0 ) ) );
    STATIC_REQUIRE( Vec2d_Dot( a2, b2 ) == -4.0 );
    STATIC_REQUIRE( Vec2d_Cross( CY_VEC2D_X, CY_VEC2D_Y ) == 1.0 );
    STATIC_REQUIRE( Vec2d_LengthSquared( Vec2d_Make( 3.0, 4.0 ) ) == 25.0 );

    constexpr vec3d_t a3 = Vec3d_Make( 2.0, 3.0, 6.0 );
    constexpr vec3d_t b3 = Vec3d_Splat( 1.0 );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Add( a3, b3 ), Vec3d_Make( 3.0, 4.0, 7.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Subtract( a3, b3 ), Vec3d_Make( 1.0, 2.0, 5.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_DivideScalar( Vec3d_Scale( a3, 6.0 ), 3.0 ),
        Vec3d_Make( 4.0, 6.0, 12.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Negate( a3 ), Vec3d_Make( -2.0, -3.0, -6.0 ) ) );
    STATIC_REQUIRE( Vec3d_Dot( a3, b3 ) == 11.0 );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Cross( CY_VEC3D_FORWARD, CY_VEC3D_LEFT ), CY_VEC3D_UP ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Cross( Vec3d_Make( 1.0, 2.0, 3.0 ),
                      Vec3d_Make( 4.0, 5.0, 6.0 ) ),
        Vec3d_Make( -3.0, 6.0, -3.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Cross( Vec3d_Make( 4.0, 5.0, 6.0 ),
                      Vec3d_Make( 1.0, 2.0, 3.0 ) ),
        Vec3d_Make( 3.0, -6.0, 3.0 ) ) );
    STATIC_REQUIRE( Vec3d_LengthSquared( a3 ) == 49.0 );

    const vec2d_t precise = Vec2d_Make( 1'000'000.0 + 1.0e-8, 0.0 );
    REQUIRE( precise.x - 1'000'000.0 > 0.0 );
}

TEST_CASE( "Scalar binary64 transcendental and rounding wrappers match their f32 contracts",
           "[CypherCommon][Mathlib][Scalar][Binary64][API]" )
{
    REQUIRE( Scalar_Sin( CY_HALF_PI_D ) == Approx( 1.0 ) );
    REQUIRE( Scalar_Cos( CY_PI_D ) == Approx( -1.0 ) );
    REQUIRE( Scalar_Tan( CY_PI_D / 4.0 ) == Approx( 1.0 ) );

    f64 sine = 0.0;
    f64 cosine = 0.0;
    Scalar_SinCos( CY_HALF_PI_D, &sine, &cosine );
    REQUIRE( sine == Approx( 1.0 ) );
    REQUIRE( cosine == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( Scalar_AsinClamped( 2.0 ) == Approx( CY_HALF_PI_D ) );
    REQUIRE( Scalar_AcosClamped( -2.0 ) == Approx( CY_PI_D ) );
    REQUIRE( Scalar_Atan2( 1.0, 0.0 ) == Approx( CY_HALF_PI_D ) );

    REQUIRE( Scalar_Floor( -1.25 ) == -2.0 );
    REQUIRE( Scalar_Ceil( -1.25 ) == -1.0 );
    REQUIRE( Scalar_Round( 1.6 ) == 2.0 );
    REQUIRE( Scalar_Truncate( -1.75 ) == -1.0 );
    REQUIRE( Scalar_Fmod( 7.5, 2.0 ) == Approx( 1.5 ) );

    REQUIRE( Scalar_Repeat( -1.0, 4.0 ) == Approx( 3.0 ) );
    REQUIRE( Scalar_WrapRadiansPositive( CY_TAU_D + 0.5 ) == Approx( 0.5 ) );
    REQUIRE( Scalar_WrapRadiansSigned( CY_PI_D + 1.0 ) == Approx( 1.0 - CY_PI_D ) );
}

TEST_CASE( "Scalar binary64 mapping and smooth-motion helpers match their f32 contracts",
           "[CypherCommon][Mathlib][Scalar][Binary64][API]" )
{
    REQUIRE( Scalar_InverseLerp( 10.0, 20.0, 15.0 ) == Approx( 0.5 ) );
    REQUIRE( Scalar_Remap( 5.0, 0.0, 10.0, 100.0, 200.0 ) == Approx( 150.0 ) );
    REQUIRE( Scalar_SmoothStep( 0.0, 1.0, 0.5 ) == Approx( 0.5 ) );
    REQUIRE( Scalar_SmootherStep( 0.0, 1.0, 0.5 ) == Approx( 0.5 ) );
    REQUIRE( Scalar_MoveTowards( 0.0, 10.0, 4.0 ) == Approx( 4.0 ) );
    REQUIRE( Scalar_MoveTowards( 0.0, 3.0, 4.0 ) == Approx( 3.0 ) );
}

TEST_CASE( "Vector2 binary64 construction access and precision conversion are complete",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    const f64 source[]{ 2.0, -3.0 };
    vec2d_t value = Vec2d_FromArray( source );
    RequireVec2d( value, 2.0, -3.0 );
    REQUIRE( Vec2d_Component( value, 0u ) == 2.0 );
    Vec2d_SetComponent( &value, 1u, 5.0 );
    REQUIRE( Vec2d_Component( value, 1u ) == 5.0 );

    f64 stored[2]{};
    Vec2d_Store( value, stored );
    REQUIRE( stored[0] == 2.0 );
    REQUIRE( stored[1] == 5.0 );

    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_FromVec2( Vec2_Make( 2.0f, -3.0f ) ), Vec2d_Make( 2.0, -3.0 ) ) );

    vec2_t narrowed{};
    REQUIRE( Vec2d_TryToVec2( Vec2d_Make( 1.5, -2.5 ), &narrowed ) );
    RequireVec2( narrowed, 1.5f, -2.5f );

    // Regression: a component outside f32's representable range must fail and
    // reset the output rather than silently overflowing to infinity.
    vec2_t failedNarrow{ 1.0f, 1.0f };
    REQUIRE_FALSE( Vec2d_TryToVec2( Vec2d_Make( 1.0e300, 0.0 ), &failedNarrow ) );
    RequireVec2( failedNarrow, 0.0f, 0.0f );
}

TEST_CASE( "Vector2 binary64 validation predicates are correct",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    REQUIRE( Vec2d_NearlyEquals(
        Vec2d_Make( 2.0, 5.0 ), Vec2d_Make( 2.0000000001, 5.0000000001 ),
        1e-6, 0.0 ) );
    REQUIRE_FALSE( Vec2d_NearlyEquals(
        Vec2d_Make( 2.0, 5.0 ), Vec2d_Make( 2.1, 5.0 ), 1e-6, 0.0 ) );

    REQUIRE( Vec2d_IsNearZero( Vec2d_Make( 1e-9, -1e-9 ), 1e-6 ) );
    REQUIRE_FALSE( Vec2d_IsNearZero( Vec2d_Make( 1.0, 0.0 ), 1e-6 ) );

    REQUIRE( Vec2d_IsUnitLength( CY_VEC2D_X, 1e-9 ) );
    REQUIRE_FALSE( Vec2d_IsUnitLength( Vec2d_Make( 2.0, 0.0 ), 1e-9 ) );
}

TEST_CASE( "Vector2 binary64 TryLength and TryNormalize handle degenerate inputs",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    f64 length = -1.0;
    REQUIRE( Vec2d_TryLength( Vec2d_Make( 3.0, 4.0 ), &length ) );
    REQUIRE( length == Approx( 5.0 ) );

    // The zero vector is a valid input: length is exactly zero, not a failure.
    length = -1.0;
    REQUIRE( Vec2d_TryLength( CY_VEC2D_ZERO, &length ) );
    REQUIRE( length == 0.0 );

    // Non-finite components must fail rather than propagate NaN/Inf outward.
    length = -1.0;
    REQUIRE_FALSE( Vec2d_TryLength(
        Vec2d_Make( std::numeric_limits<f64>::infinity(), 0.0 ), &length ) );
    REQUIRE( length == 0.0 );

    vec2d_t normalized{};
    f64 originalLength = -1.0;
    REQUIRE( Vec2d_TryNormalize( Vec2d_Make( 3.0, 4.0 ), 0.0, &normalized, &originalLength ) );
    RequireVec2d( normalized, 0.6, 0.8 );
    REQUIRE( originalLength == Approx( 5.0 ) );

    // A vector at or below minimumLength must fail, not silently return a
    // degenerate near-unit result.
    vec2d_t failedNormalize{ 1.0, 1.0 };
    REQUIRE_FALSE( Vec2d_TryNormalize(
        Vec2d_Make( 0.5, 0.0 ), 1.0, &failedNormalize, nullptr ) );
    RequireVec2d( failedNormalize, 0.0, 0.0 );

    vec2d_t failedZero{ 1.0, 1.0 };
    REQUIRE_FALSE( Vec2d_TryNormalize( CY_VEC2D_ZERO, 0.0, &failedZero, nullptr ) );
    RequireVec2d( failedZero, 0.0, 0.0 );
}

TEST_CASE( "Vector2 binary64 component-wise arithmetic and utilities are complete",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_MultiplyComponents( Vec2d_Make( 2.0, 3.0 ), Vec2d_Make( 4.0, 5.0 ) ),
        Vec2d_Make( 8.0, 15.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_DivideComponents( Vec2d_Make( 8.0, 15.0 ), Vec2d_Make( 4.0, 5.0 ) ),
        Vec2d_Make( 2.0, 3.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_MulAdd( Vec2d_Make( 1.0, 1.0 ), Vec2d_Make( 2.0, 3.0 ), 2.0 ),
        Vec2d_Make( 5.0, 7.0 ) ) );

    const vec2d_t value = Vec2d_Make( -1.25, 3.75 );
    RequireVec2d( Vec2d_Abs( value ), 1.25, 3.75 );
    RequireVec2d( Vec2d_Min( value, Vec2d_Make( -2.0, 5.0 ) ), -2.0, 3.75 );
    RequireVec2d( Vec2d_Max( value, Vec2d_Make( -2.0, 5.0 ) ), -1.25, 5.0 );

    // Regression: Vec2d_Clamp previously returned the value completely
    // unclamped whenever the bounds were valid (the common case), silently
    // doing nothing. This exercises both the violated and in-range paths.
    RequireVec2d(
        Vec2d_Clamp(
            Vec2d_Make( 15.0, -3.0 ), Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 10.0, 10.0 ) ),
        10.0, 0.0 );
    RequireVec2d(
        Vec2d_Clamp(
            Vec2d_Make( 5.0, 5.0 ), Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 10.0, 10.0 ) ),
        5.0, 5.0 );
}

TEST_CASE( "Vector2 binary64 rounding matches the scalar contract",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    const vec2d_t value = Vec2d_Make( -1.25, 3.75 );
    RequireVec2d( Vec2d_Floor( value ), -2.0, 3.0 );
    RequireVec2d( Vec2d_Ceil( value ), -1.0, 4.0 );
    RequireVec2d( Vec2d_Round( value ), -1.0, 4.0 );
    RequireVec2d( Vec2d_Truncate( value ), -1.0, 3.0 );
}

TEST_CASE( "Vector2 binary64 distance and perpendicular helpers are correct",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    STATIC_REQUIRE( Vec2d_DistanceSquared( Vec2d_Make( 3.0, 4.0 ), CY_VEC2D_ZERO ) == 25.0 );

    f64 distance = 0.0;
    REQUIRE( Vec2d_TryDistance( Vec2d_Make( 3.0, 4.0 ), CY_VEC2D_ZERO, &distance ) );
    REQUIRE( distance == Approx( 5.0 ) );

    // Regression: these must be usable in a constant expression, which requires
    // their definitions to live in the .inl rather than the .cpp.
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_PerpendicularCCW( CY_VEC2D_X ), CY_VEC2D_Y ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_PerpendicularCW( CY_VEC2D_X ), Vec2d_Negate( CY_VEC2D_Y ) ) );
}

TEST_CASE( "Vector2 binary64 interpolation and movement preserve geometry",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_Lerp( CY_VEC2D_ZERO, Vec2d_Make( 4.0, 8.0 ), 0.25 ),
        Vec2d_Make( 1.0, 2.0 ) ) );

    RequireVec2d(
        Vec2d_LerpClamped( CY_VEC2D_ZERO, Vec2d_Make( 4.0, 8.0 ), 2.0 ), 4.0, 8.0 );
    RequireVec2d(
        Vec2d_MoveTowards( CY_VEC2D_ZERO, Vec2d_Make( 3.0, 4.0 ), 2.0 ), 1.2, 1.6 );
    RequireVec2d(
        Vec2d_ClampLength( Vec2d_Make( 6.0, 8.0 ), 2.0, 5.0 ), 3.0, 4.0 );
}

TEST_CASE( "Vector2 binary64 projection rejection angle and ordering are correct",
           "[CypherCommon][Mathlib][Vector2][Binary64][API]" )
{
    constexpr vec2d_t value = Vec2d_Make( 3.0, 4.0 );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_ProjectOntoUnit( value, CY_VEC2D_X ), Vec2d_Make( 3.0, 0.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_RejectFromUnit( value, CY_VEC2D_X ), Vec2d_Make( 0.0, 4.0 ) ) );
    STATIC_REQUIRE( Vec2d_EqualsExact(
        Vec2d_ReflectUnitNormal( Vec2d_Make( 1.0, -2.0 ), CY_VEC2D_Y ),
        Vec2d_Make( 1.0, 2.0 ) ) );

    vec2d_t projected{};
    REQUIRE( Vec2d_TryProjectOnto( value, Vec2d_Make( 2.0, 0.0 ), 1e-9, &projected ) );
    RequireVec2d( projected, 3.0, 0.0 );

    f64 angle = 0.0;
    REQUIRE( Vec2d_TryAngleBetween( CY_VEC2D_X, CY_VEC2D_Y, 1e-9, &angle ) );
    REQUIRE( angle == Approx( CY_HALF_PI_D ) );

    STATIC_REQUIRE( Vec2d_LexicographicLess( Vec2d_Make( 1.0, 5.0 ), Vec2d_Make( 2.0, -3.0 ) ) );
    STATIC_REQUIRE( Vec2d_LexicographicLess( Vec2d_Make( 1.0, -5.0 ), Vec2d_Make( 1.0, 2.0 ) ) );
    STATIC_REQUIRE_FALSE(
        Vec2d_LexicographicLess( Vec2d_Make( 1.0, 2.0 ), Vec2d_Make( 1.0, 2.0 ) ) );
}

TEST_CASE( "Vector3 binary64 construction access and precision conversion are complete",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    const f64 source[]{ 2.0, -3.0, 4.0 };
    vec3d_t value = Vec3d_FromArray( source );
    RequireVec3d( value, 2.0, -3.0, 4.0 );
    REQUIRE( Vec3d_Component( value, 0u ) == 2.0 );
    Vec3d_SetComponent( &value, 2u, 8.0 );
    REQUIRE( Vec3d_Component( value, 2u ) == 8.0 );

    f64 stored[3]{};
    Vec3d_Store( value, stored );
    REQUIRE( stored[0] == 2.0 );
    REQUIRE( stored[1] == -3.0 );
    REQUIRE( stored[2] == 8.0 );

    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_FromVec3( Vec3_Make( 2.0f, -3.0f, 4.0f ) ), Vec3d_Make( 2.0, -3.0, 4.0 ) ) );

    vec3_t narrowed{};
    REQUIRE( Vec3d_TryToVec3( Vec3d_Make( 1.5, -2.5, 3.5 ), &narrowed ) );
    RequireVec3( narrowed, 1.5f, -2.5f, 3.5f );

    vec3_t failedNarrow{ 1.0f, 1.0f, 1.0f };
    REQUIRE_FALSE( Vec3d_TryToVec3( Vec3d_Make( 1.0e300, 0.0, 0.0 ), &failedNarrow ) );
    RequireVec3( failedNarrow, 0.0f, 0.0f, 0.0f );
}

TEST_CASE( "Vector3 binary64 TryLength and TryNormalize handle degenerate inputs",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    f64 length = -1.0;
    REQUIRE( Vec3d_TryLength( Vec3d_Make( 2.0, 3.0, 6.0 ), &length ) );
    REQUIRE( length == Approx( 7.0 ) );

    length = -1.0;
    REQUIRE( Vec3d_TryLength( CY_VEC3D_ZERO, &length ) );
    REQUIRE( length == 0.0 );

    length = -1.0;
    REQUIRE_FALSE( Vec3d_TryLength(
        Vec3d_Make( std::numeric_limits<f64>::infinity(), 0.0, 0.0 ), &length ) );
    REQUIRE( length == 0.0 );

    vec3d_t normalized{};
    f64 originalLength = -1.0;
    REQUIRE( Vec3d_TryNormalize(
        Vec3d_Make( 2.0, 3.0, 6.0 ), 0.0, &normalized, &originalLength ) );
    RequireVec3d( normalized, 2.0 / 7.0, 3.0 / 7.0, 6.0 / 7.0 );
    REQUIRE( originalLength == Approx( 7.0 ) );

    vec3d_t failedNormalize{ 1.0, 1.0, 1.0 };
    REQUIRE_FALSE( Vec3d_TryNormalize(
        Vec3d_Make( 0.5, 0.0, 0.0 ), 1.0, &failedNormalize, nullptr ) );
    RequireVec3d( failedNormalize, 0.0, 0.0, 0.0 );
}

TEST_CASE( "Vector3 binary64 validation predicates are correct",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    REQUIRE( Vec3d_NearlyEquals(
        Vec3d_Make( 2.0, 5.0, -1.0 ), Vec3d_Make( 2.0000000001, 5.0000000001, -1.0 ),
        1e-6, 0.0 ) );
    REQUIRE_FALSE( Vec3d_NearlyEquals(
        Vec3d_Make( 2.0, 5.0, -1.0 ), Vec3d_Make( 2.1, 5.0, -1.0 ), 1e-6, 0.0 ) );

    REQUIRE( Vec3d_IsNearZero( Vec3d_Make( 1e-9, -1e-9, 1e-9 ), 1e-6 ) );
    REQUIRE_FALSE( Vec3d_IsNearZero( Vec3d_Make( 1.0, 0.0, 0.0 ), 1e-6 ) );

    REQUIRE( Vec3d_IsUnitLength( CY_VEC3D_UP, 1e-9 ) );
    REQUIRE_FALSE( Vec3d_IsUnitLength( Vec3d_Make( 2.0, 0.0, 0.0 ), 1e-9 ) );
}

TEST_CASE( "Vector3 binary64 component-wise arithmetic and utilities are complete",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_MultiplyComponents( Vec3d_Make( 2.0, 3.0, 4.0 ), Vec3d_Make( 5.0, 6.0, 7.0 ) ),
        Vec3d_Make( 10.0, 18.0, 28.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_DivideComponents( Vec3d_Make( 10.0, 18.0, 28.0 ), Vec3d_Make( 5.0, 6.0, 7.0 ) ),
        Vec3d_Make( 2.0, 3.0, 4.0 ) ) );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_MulAdd( Vec3d_Make( 1.0, 1.0, 1.0 ), Vec3d_Make( 2.0, 3.0, 4.0 ), 2.0 ),
        Vec3d_Make( 5.0, 7.0, 9.0 ) ) );

    const vec3d_t value = Vec3d_Make( -1.25, 3.75, -2.5 );
    RequireVec3d( Vec3d_Abs( value ), 1.25, 3.75, 2.5 );
    RequireVec3d( Vec3d_Min( value, Vec3d_Make( -2.0, 5.0, -3.0 ) ), -2.0, 3.75, -3.0 );
    RequireVec3d( Vec3d_Max( value, Vec3d_Make( -2.0, 5.0, -3.0 ) ), -1.25, 5.0, -2.5 );

    RequireVec3d(
        Vec3d_Clamp(
            Vec3d_Make( 15.0, -3.0, 4.0 ),
            Vec3d_Make( 0.0, 0.0, 0.0 ),
            Vec3d_Make( 10.0, 10.0, 10.0 ) ),
        10.0, 0.0, 4.0 );
}

TEST_CASE( "Vector3 binary64 rounding matches the scalar contract",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    const vec3d_t value = Vec3d_Make( -1.25, 3.75, 2.5 );
    RequireVec3d( Vec3d_Floor( value ), -2.0, 3.0, 2.0 );
    RequireVec3d( Vec3d_Ceil( value ), -1.0, 4.0, 3.0 );
    RequireVec3d( Vec3d_Round( value ), -1.0, 4.0, 3.0 );
    RequireVec3d( Vec3d_Truncate( value ), -1.0, 3.0, 2.0 );
}

TEST_CASE( "Vector3 binary64 reductions and distance are correct",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    STATIC_REQUIRE( Vec3d_SumComponents( Vec3d_Make( 1.0, 2.0, 3.0 ) ) == 6.0 );
    STATIC_REQUIRE( Vec3d_ProductComponents( Vec3d_Make( 2.0, 3.0, 4.0 ) ) == 24.0 );

    const vec3d_t value = Vec3d_Make( -1.25, 2.5, 3.75 );
    REQUIRE( Vec3d_MinComponent( value ) == -1.25 );
    REQUIRE( Vec3d_MaxComponent( value ) == 3.75 );
    REQUIRE( Vec3d_MaxAbsComponent( value ) == 3.75 );

    STATIC_REQUIRE( Vec3d_DistanceSquared( Vec3d_Make( 2.0, 3.0, 6.0 ), CY_VEC3D_ZERO ) == 49.0 );
    f64 distance = 0.0;
    REQUIRE( Vec3d_TryDistance( Vec3d_Make( 2.0, 3.0, 6.0 ), CY_VEC3D_ZERO, &distance ) );
    REQUIRE( distance == Approx( 7.0 ) );
}

TEST_CASE( "Vector3 binary64 interpolation and movement preserve geometry",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_Lerp( CY_VEC3D_ZERO, Vec3d_Make( 4.0, 8.0, 12.0 ), 0.25 ),
        Vec3d_Make( 1.0, 2.0, 3.0 ) ) );

    RequireVec3d(
        Vec3d_LerpClamped( CY_VEC3D_ZERO, Vec3d_Make( 4.0, 8.0, 12.0 ), 2.0 ), 4.0, 8.0, 12.0 );
    RequireVec3d(
        Vec3d_MoveTowards( CY_VEC3D_ZERO, Vec3d_Make( 0.0, 0.0, 10.0 ), 3.0 ), 0.0, 0.0, 3.0 );
    RequireVec3d(
        Vec3d_ClampLength( Vec3d_Make( 0.0, 0.0, 10.0 ), 2.0, 4.0 ), 0.0, 0.0, 4.0 );

    vec3d_t setLength{};
    REQUIRE( Vec3d_TrySetLength( Vec3d_Make( 3.0, 4.0, 0.0 ), 10.0, 0.0, &setLength ) );
    RequireVec3d( setLength, 6.0, 8.0, 0.0 );
}

TEST_CASE( "Vector3 binary64 projection rejection and angle helpers are correct",
           "[CypherCommon][Mathlib][Vector3][Binary64][API]" )
{
    constexpr vec3d_t value = Vec3d_Make( 2.0, 3.0, 4.0 );
    STATIC_REQUIRE( Vec3d_EqualsExact(
        Vec3d_ProjectOntoPlaneUnitNormal( value, CY_VEC3D_UP ), Vec3d_Make( 2.0, 3.0, 0.0 ) ) );

    f64 angle = 0.0;
    REQUIRE( Vec3d_TryAngleBetween( CY_VEC3D_FORWARD, CY_VEC3D_LEFT, 1e-9, &angle ) );
    REQUIRE( angle == Approx( CY_HALF_PI_D ) );
    REQUIRE( Vec3d_AngleBetweenUnit( CY_VEC3D_FORWARD, CY_VEC3D_LEFT ) == Approx( CY_HALF_PI_D ) );

    vec3d_t projected{};
    REQUIRE( Vec3d_TryProjectOnto( value, CY_VEC3D_FORWARD, 1e-9, &projected ) );
    RequireVec3d( projected, 2.0, 0.0, 0.0 );

    vec3d_t perpendicular{};
    REQUIRE( Vec3d_TryBuildUnitPerpendicular( CY_VEC3D_FORWARD, 1e-9, &perpendicular ) );
    REQUIRE( Vec3d_IsUnitLength( perpendicular, 1e-9 ) );
    REQUIRE( Vec3d_Dot( perpendicular, CY_VEC3D_FORWARD ) == Approx( 0.0 ).margin( 1e-9 ) );

    vec3d_t tangent{};
    vec3d_t bitangent{};
    Vec3d_BuildOrthonormalBasis( CY_VEC3D_UP, &tangent, &bitangent );
    REQUIRE( Vec3d_IsUnitLength( tangent, 1e-9 ) );
    REQUIRE( Vec3d_IsUnitLength( bitangent, 1e-9 ) );
    REQUIRE( Vec3d_Dot( tangent, bitangent ) == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( Vec3d_Dot( tangent, CY_VEC3D_UP ) == Approx( 0.0 ).margin( 1e-9 ) );

    STATIC_REQUIRE( Vec3d_LexicographicLess(
        Vec3d_Make( 1.0, 5.0, 0.0 ), Vec3d_Make( 2.0, -3.0, 0.0 ) ) );
    STATIC_REQUIRE( Vec3d_LexicographicLess(
        Vec3d_Make( 1.0, 1.0, -5.0 ), Vec3d_Make( 1.0, 1.0, 2.0 ) ) );
    STATIC_REQUIRE_FALSE( Vec3d_LexicographicLess(
        Vec3d_Make( 1.0, 2.0, 3.0 ), Vec3d_Make( 1.0, 2.0, 3.0 ) ) );
}

TEST_CASE( "Vector4 construction arithmetic and metrics preserve all components",
           "[CypherCommon][Mathlib][Vector4][API]" )
{
    const f32 source[]{ 1.0f, -2.0f, 3.0f, -4.0f };
    vec4_t value = Vec4_FromArray( source );
    RequireVec4( value, 1.0f, -2.0f, 3.0f, -4.0f );
    REQUIRE( Vec4_IsFinite( value ) );
    REQUIRE( Vec4_Component( value, 2u ) == 3.0f );
    Vec4_SetComponent( &value, 3u, 8.0f );
    f32 stored[4]{};
    Vec4_Store( value, stored );
    REQUIRE( stored[3] == 8.0f );
    RequireVec4( Vec4_Splat( 2.0f ), 2.0f, 2.0f, 2.0f, 2.0f );
    RequireVec4( Vec4_FromVec3( Vec3_Make( 2.0f, 3.0f, 4.0f ), 5.0f ),
                 2.0f, 3.0f, 4.0f, 5.0f );
    RequireVec3( Vec4_XYZ( value ), 1.0f, -2.0f, 3.0f );
    REQUIRE( Vec4_NearlyEquals(
        value, Vec4_Make( 1.00001f, -2.0f, 3.0f, 8.0f ),
        0.00002f, 0.0f ) );

    RequireVec4( Vec4_Add( value, CY_VEC4_ONE ), 2.0f, -1.0f, 4.0f, 9.0f );
    RequireVec4( Vec4_Subtract( value, CY_VEC4_ONE ), 0.0f, -3.0f, 2.0f, 7.0f );
    RequireVec4( Vec4_MultiplyComponents(
        value, Vec4_Make( 2.0f, 3.0f, 4.0f, 5.0f ) ),
        2.0f, -6.0f, 12.0f, 40.0f );
    RequireVec4( Vec4_DivideComponents(
        value, Vec4_Make( 1.0f, -2.0f, 3.0f, 4.0f ) ),
        1.0f, 1.0f, 1.0f, 2.0f );
    RequireVec4( Vec4_Scale( value, 2.0f ), 2.0f, -4.0f, 6.0f, 16.0f );
    RequireVec4( Vec4_DivideScalar( value, 2.0f ), 0.5f, -1.0f, 1.5f, 4.0f );
    RequireVec4( Vec4_Negate( value ), -1.0f, 2.0f, -3.0f, -8.0f );
    RequireVec4( Vec4_MulAdd( value, CY_VEC4_ONE, 2.0f ),
                 3.0f, 0.0f, 5.0f, 10.0f );
}

TEST_CASE( "Vector4 component helpers normalization and interpolation are complete",
           "[CypherCommon][Mathlib][Vector4][API]" )
{
    const vec4_t value = Vec4_Make( -1.0f, 2.0f, -3.0f, 4.0f );
    RequireVec4( Vec4_Abs( value ), 1.0f, 2.0f, 3.0f, 4.0f );
    RequireVec4( Vec4_Min( value, CY_VEC4_ZERO ), -1.0f, 0.0f, -3.0f, 0.0f );
    RequireVec4( Vec4_Max( value, CY_VEC4_ZERO ), 0.0f, 2.0f, 0.0f, 4.0f );
    RequireVec4( Vec4_Clamp( value, Vec4_Splat( -2.0f ), Vec4_Splat( 2.0f ) ),
                 -1.0f, 2.0f, -2.0f, 2.0f );
    REQUIRE( Vec4_Dot( value, CY_VEC4_ONE ) == 2.0f );
    REQUIRE( Vec4_LengthSquared( value ) == 30.0f );
    REQUIRE( Vec4_Length( value ) == Approx( std::sqrt( 30.0f ) ) );
    REQUIRE( Vec4_DistanceSquared( value, CY_VEC4_ZERO ) == 30.0f );
    REQUIRE( Vec4_Distance( value, CY_VEC4_ZERO ) == Approx( std::sqrt( 30.0f ) ) );

    const vec4_t normalized = Vec4_NormalizeUnchecked( Vec4_Make( 0.0f, 0.0f, 3.0f, 4.0f ) );
    RequireVec4( normalized, 0.0f, 0.0f, 0.6f, 0.8f );
    RequireVec4( Vec4_Lerp( CY_VEC4_ZERO, Vec4_Make( 4.0f, 8.0f, 12.0f, 16.0f ), 0.25f ),
                 1.0f, 2.0f, 3.0f, 4.0f );
    RequireVec4( Vec4_LerpClamped(
        CY_VEC4_ZERO, Vec4_Make( 4.0f, 8.0f, 12.0f, 16.0f ), 2.0f ),
        4.0f, 8.0f, 12.0f, 16.0f );
}
