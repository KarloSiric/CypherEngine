//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Triangle.cpp
//  Purpose: Implements checked triangle geometry operations.
//  Details: Closest-point regions follow the Voronoi-region method, avoiding an
//           unnecessary projection when the nearest feature is an edge or vertex.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Triangle.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

//==========================================================================
// Triangle measurements and coordinates
//==========================================================================

bool_t Triangle3_IsFinite( triangle3_t triangle ) noexcept
{
    return Vec3_IsFinite( triangle.a ) &&
           Vec3_IsFinite( triangle.b ) &&
           Vec3_IsFinite( triangle.c );
}

f32 Triangle3_TwiceArea( triangle3_t triangle ) noexcept
{
    return Vec3_Length( Triangle3_NormalUnnormalized( triangle ) );
}

f32 Triangle3_Area( triangle3_t triangle ) noexcept
{
    return Triangle3_TwiceArea( triangle ) * 0.5f;
}

bool_t Triangle3_TryNormal(
    triangle3_t triangle,
    f32 minimumTwiceArea,
    vec3_t *pUnitNormal ) noexcept
{
    const bool_t bValidOutput = pUnitNormal != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Triangle3_TryNormal requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    return Vec3_TryNormalize(
        Triangle3_NormalUnnormalized( triangle ),
        minimumTwiceArea, pUnitNormal, nullptr );
}

bool_t Triangle3_TryPlane(
    triangle3_t triangle,
    f32 minimumTwiceArea,
    plane_t *pPlane ) noexcept
{
    return Plane_TryFromTriangle(
        triangle.a, triangle.b, triangle.c,
        minimumTwiceArea, pPlane );
}

bool_t Triangle3_TryBarycentric(
    triangle3_t triangle,
    vec3_t point,
    f32 minimumAbsDenominator,
    vec3_t *pBarycentric ) noexcept
{
    const bool_t bValidOutput = pBarycentric != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsDenominator ) &&
                                   minimumAbsDenominator >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Triangle3_TryBarycentric requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Triangle3_TryBarycentric requires a finite nonnegative threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pBarycentric = CY_VEC3_ZERO;
    if ( !bValidThreshold || !Triangle3_IsFinite( triangle ) ||
         !Vec3_IsFinite( point ) ) {
        return false;
    }

    const vec3_t edge0 = Vec3_Subtract( triangle.b, triangle.a );
    const vec3_t edge1 = Vec3_Subtract( triangle.c, triangle.a );
    const vec3_t offset = Vec3_Subtract( point, triangle.a );
    const f32 dot00 = Vec3_Dot( edge0, edge0 );
    const f32 dot01 = Vec3_Dot( edge0, edge1 );
    const f32 dot11 = Vec3_Dot( edge1, edge1 );
    const f32 dot20 = Vec3_Dot( offset, edge0 );
    const f32 dot21 = Vec3_Dot( offset, edge1 );
    const f32 denominator = dot00 * dot11 - dot01 * dot01;
    // The Gram determinant approaches zero as the triangle becomes degenerate.
    if ( !Scalar_IsFinite( denominator ) ||
         Scalar_Abs( denominator ) <= minimumAbsDenominator ) {
        return false;
    }

    const f32 inverseDenominator = 1.0f / denominator;
    const f32 weightB = ( dot11 * dot20 - dot01 * dot21 ) * inverseDenominator;
    const f32 weightC = ( dot00 * dot21 - dot01 * dot20 ) * inverseDenominator;
    *pBarycentric = Vec3_Make( 1.0f - weightB - weightC, weightB, weightC );
    return Vec3_IsFinite( *pBarycentric );
}

bool_t Triangle3_ContainsPoint(
    triangle3_t triangle,
    vec3_t point,
    f32 minimumTwiceArea,
    f32 planeTolerance,
    f32 barycentricTolerance ) noexcept
{
    const bool_t bValidTolerances =
        Scalar_IsFinite( planeTolerance ) && planeTolerance >= 0.0f &&
        Scalar_IsFinite( barycentricTolerance ) && barycentricTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerances,
        "Triangle3_ContainsPoint requires finite nonnegative tolerances." );
    if ( !bValidTolerances ) {
        return false;
    }

    plane_t plane{};
    // Barycentric coordinates alone describe the projected point, so first make
    // sure the original point lies inside the plane tolerance band.
    if ( !Triangle3_TryPlane( triangle, minimumTwiceArea, &plane ) ||
         Scalar_Abs( Plane_SignedDistance( plane, point ) ) > planeTolerance ) {
        return false;
    }
    vec3_t barycentric{};
    const f32 denominatorThreshold = minimumTwiceArea * minimumTwiceArea;
    if ( !Triangle3_TryBarycentric(
             triangle, point, denominatorThreshold, &barycentric ) ) {
        return false;
    }
    return barycentric.x >= -barycentricTolerance &&
           barycentric.y >= -barycentricTolerance &&
           barycentric.z >= -barycentricTolerance;
}

vec3_t Triangle3_ClosestPoint( triangle3_t triangle, vec3_t point ) noexcept
{
    // Test the vertex and edge Voronoi regions in order. Only points left after
    // all six boundary tests belong to the triangle's interior face region.
    const vec3_t edgeAB = Vec3_Subtract( triangle.b, triangle.a );
    const vec3_t edgeAC = Vec3_Subtract( triangle.c, triangle.a );
    const vec3_t fromA = Vec3_Subtract( point, triangle.a );
    const f32 d1 = Vec3_Dot( edgeAB, fromA );
    const f32 d2 = Vec3_Dot( edgeAC, fromA );
    if ( d1 <= 0.0f && d2 <= 0.0f ) {
        return triangle.a;
    }

    const vec3_t fromB = Vec3_Subtract( point, triangle.b );
    const f32 d3 = Vec3_Dot( edgeAB, fromB );
    const f32 d4 = Vec3_Dot( edgeAC, fromB );
    if ( d3 >= 0.0f && d4 <= d3 ) {
        return triangle.b;
    }

    const f32 vertexCRegion = d1 * d4 - d3 * d2;
    if ( vertexCRegion <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) {
        const f32 t = d1 / ( d1 - d3 );
        return Vec3_MulAdd( triangle.a, edgeAB, t );
    }

    const vec3_t fromC = Vec3_Subtract( point, triangle.c );
    const f32 d5 = Vec3_Dot( edgeAB, fromC );
    const f32 d6 = Vec3_Dot( edgeAC, fromC );
    if ( d6 >= 0.0f && d5 <= d6 ) {
        return triangle.c;
    }

    const f32 vertexBRegion = d5 * d2 - d1 * d6;
    if ( vertexBRegion <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) {
        const f32 t = d2 / ( d2 - d6 );
        return Vec3_MulAdd( triangle.a, edgeAC, t );
    }

    const f32 vertexARegion = d3 * d6 - d5 * d4;
    if ( vertexARegion <= 0.0f &&
         ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) {
        const vec3_t edgeBC = Vec3_Subtract( triangle.c, triangle.b );
        const f32 t = ( d4 - d3 ) /
                      ( ( d4 - d3 ) + ( d5 - d6 ) );
        return Vec3_MulAdd( triangle.b, edgeBC, t );
    }

    const f32 inverseSum =
        1.0f / ( vertexARegion + vertexBRegion + vertexCRegion );
    // The remaining signed sub-areas form barycentric weights on the face.
    const f32 weightB = vertexBRegion * inverseSum;
    const f32 weightC = vertexCRegion * inverseSum;
    return Vec3_Add(
        triangle.a,
        Vec3_Add(
            Vec3_Scale( edgeAB, weightB ),
            Vec3_Scale( edgeAC, weightC ) ) );
}

} // namespace cypher::math
