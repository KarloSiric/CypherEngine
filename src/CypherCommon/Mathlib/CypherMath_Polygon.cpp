//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Polygon.cpp
//  Purpose: Implements planar three-dimensional polygon operations.
//  Details: Newell's method supplies a stable polygon normal and local 2D basis;
//           projected predicates then share one orientation policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Polygon.h"

#include "CypherCommon_Assert.h"

#include <cmath>

namespace cypher::math
{

namespace
{

bool_t Polygon3ArgumentsValid( const vec3_t *pVertices, usize cVertices ) noexcept
{
    return pVertices != nullptr && cVertices >= 3u;
}

vec2_t Polygon3_ProjectPoint( vec3_t point, polygon3_basis_t basis ) noexcept
{
    const vec3_t relative = Vec3_Subtract( point, basis.origin );
    return Vec2_Make(
        Vec3_Dot( relative, basis.tangent ),
        Vec3_Dot( relative, basis.bitangent ) );
}

} // namespace

bool_t Polygon3_TryBasis(
    const vec3_t *pVertices,
    usize cVertices,
    f32 minimumNormalLength,
    polygon3_basis_t *pBasis ) noexcept
{
    const bool_t bValidOutput = pBasis != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Polygon3_TryBasis requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pBasis = {};
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) ||
         minimumNormalLength < 0.0f ) {
        return false;
    }

    // Newell's method accumulates a stable area-weighted normal from the whole
    // boundary instead of trusting one possibly skinny triangle.
    vec3_t newell = CY_VEC3_ZERO;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const vec3_t current = pVertices[i];
        const vec3_t next = pVertices[( i + 1u ) % cVertices];
        if ( !Vec3_IsFinite( current ) ) {
            return false;
        }
        newell.x += ( current.y - next.y ) * ( current.z + next.z );
        newell.y += ( current.z - next.z ) * ( current.x + next.x );
        newell.z += ( current.x - next.x ) * ( current.y + next.y );
    }

    vec3_t normal{};
    if ( !Vec3_TryNormalize( newell, minimumNormalLength, &normal, nullptr ) ) {
        return false;
    }

    polygon3_basis_t basis{};
    basis.origin = pVertices[0];
    basis.normal = normal;
    Vec3_BuildOrthonormalBasis( normal, &basis.tangent, &basis.bitangent );
    if ( !Vec3_IsFinite( basis.tangent ) || !Vec3_IsFinite( basis.bitangent ) ) {
        return false;
    }
    *pBasis = basis;
    return true;
}

bool_t Polygon3_IsPlanar(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f32 distanceTolerance ) noexcept
{
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) ||
         distanceTolerance < 0.0f || !Vec3_IsFinite( basis.origin ) ||
         !Vec3_IsFinite( basis.normal ) ) {
        return false;
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        const f32 distance = Vec3_Dot(
            basis.normal,
            Vec3_Subtract( pVertices[i], basis.origin ) );
        if ( !Scalar_IsFinite( distance ) || std::abs( distance ) > distanceTolerance ) {
            return false;
        }
    }
    return true;
}

void Polygon3_ProjectToBasis(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    vec2_t *pProjected ) noexcept
{
    const bool_t bValidPointers = cVertices == 0u ||
        ( pVertices != nullptr && pProjected != nullptr );
    CY_ASSERT_MSG( bValidPointers, "Polygon3_ProjectToBasis received invalid storage." );
    if ( !bValidPointers ) {
        return;
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        pProjected[i] = Polygon3_ProjectPoint( pVertices[i], basis );
    }
}

bool_t Polygon3_TryPlane(
    const vec3_t *pVertices,
    usize cVertices,
    f32 minimumNormalLength,
    plane_t *pPlane ) noexcept
{
    const bool_t bValidOutput = pPlane != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Polygon3_TryPlane requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPlane = CY_PLANE_Z;

    polygon3_basis_t basis{};
    if ( !Polygon3_TryBasis( pVertices, cVertices, minimumNormalLength, &basis ) ) {
        return false;
    }
    *pPlane = Plane_Make(
        basis.normal,
        -Vec3_Dot( basis.normal, basis.origin ) );
    return true;
}

bool_t Polygon3_TryAreaCentroid(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 minimumAbsArea,
    f32 *pArea,
    vec3_t *pCentroid ) noexcept
{
    const bool_t bValidOutput = pArea != nullptr && pCentroid != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Polygon3_TryAreaCentroid requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pArea = 0.0f;
    *pCentroid = CY_VEC3_ZERO;
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) || minimumAbsArea < 0.0 ) {
        return false;
    }

    // Form a triangle fan around vertex zero. Signed areas preserve winding so
    // weighted centroids remain correct for either orientation.
    const vec3_t origin = pVertices[0];
    f64 signedAreaSum = 0.0;
    f64 weightedX = 0.0;
    f64 weightedY = 0.0;
    f64 weightedZ = 0.0;
    for ( usize i = 1u; i + 1u < cVertices; ++i ) {
        const vec3_t edgeA = Vec3_Subtract( pVertices[i], origin );
        const vec3_t edgeB = Vec3_Subtract( pVertices[i + 1u], origin );
        const f64 signedTriangleArea = 0.5 * static_cast<f64>(
            Vec3_Dot( Vec3_Cross( edgeA, edgeB ), basis.normal ) );
        const vec3_t triangleCentroid = Vec3_Scale(
            Vec3_Add( Vec3_Add( origin, pVertices[i] ), pVertices[i + 1u] ),
            1.0f / 3.0f );
        signedAreaSum += signedTriangleArea;
        weightedX += static_cast<f64>( triangleCentroid.x ) * signedTriangleArea;
        weightedY += static_cast<f64>( triangleCentroid.y ) * signedTriangleArea;
        weightedZ += static_cast<f64>( triangleCentroid.z ) * signedTriangleArea;
    }
    if ( std::abs( signedAreaSum ) <= minimumAbsArea ) {
        return false;
    }

    *pArea = static_cast<f32>( std::abs( signedAreaSum ) );
    *pCentroid = Vec3_Make(
        static_cast<f32>( weightedX / signedAreaSum ),
        static_cast<f32>( weightedY / signedAreaSum ),
        static_cast<f32>( weightedZ / signedAreaSum ) );
    return Vec3_IsFinite( *pCentroid );
}

bool_t Polygon3_IsConvex(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 orientationTolerance,
    vec2_t *pProjectedScratch,
    usize cProjectedScratch ) noexcept
{
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) ||
         pProjectedScratch == nullptr || cProjectedScratch < cVertices ) {
        return false;
    }
    // Once projected into the polygon's local plane, the shared 2D winding
    // predicate handles edges and boundary tolerance consistently.
    Polygon3_ProjectToBasis( pVertices, cVertices, basis, pProjectedScratch );
    return Polygon2_IsConvex( pProjectedScratch, cVertices, orientationTolerance );
}

bool_t Polygon3_ContainsPoint(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    vec3_t point,
    f32 planeTolerance,
    f32 boundaryTolerance,
    bool_t bIncludeBoundary,
    vec2_t *pProjectedScratch,
    usize cProjectedScratch ) noexcept
{
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) ||
         pProjectedScratch == nullptr || cProjectedScratch < cVertices ||
         planeTolerance < 0.0f ) {
        return false;
    }
    const f32 planeDistance = Vec3_Dot(
        basis.normal,
        Vec3_Subtract( point, basis.origin ) );
    if ( std::abs( planeDistance ) > planeTolerance ) {
        return false;
    }

    Polygon3_ProjectToBasis( pVertices, cVertices, basis, pProjectedScratch );
    return Polygon2_ContainsPoint(
        pProjectedScratch,
        cVertices,
        Polygon3_ProjectPoint( point, basis ),
        boundaryTolerance,
        bIncludeBoundary );
}

polygon_triangulation_result_t Polygon3_Triangulate(
    const vec3_t *pVertices,
    usize cVertices,
    polygon3_basis_t basis,
    f64 orientationTolerance,
    vec2_t *pProjectedScratch,
    usize cProjectedScratch,
    u32 *pIndexScratch,
    usize cIndexScratch,
    u32 *pOutputIndices,
    usize cOutputIndices ) noexcept
{
    if ( !Polygon3ArgumentsValid( pVertices, cVertices ) ||
         pProjectedScratch == nullptr || cProjectedScratch < cVertices ) {
        return { polygon_triangulation_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    Polygon3_ProjectToBasis( pVertices, cVertices, basis, pProjectedScratch );
    return Polygon2_Triangulate(
        pProjectedScratch,
        cVertices,
        orientationTolerance,
        pIndexScratch,
        cIndexScratch,
        pOutputIndices,
        cOutputIndices );
}

} // namespace cypher::math
