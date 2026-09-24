//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushGenerator.cpp
//  Purpose: Implements failure-atomic primitive brush generation.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"

#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::vec3d_t;

constexpr vec3d_t cBoxNormals[6] = {
    {  1.0,  0.0,  0.0 },
    { -1.0,  0.0,  0.0 },
    {  0.0,  1.0,  0.0 },
    {  0.0, -1.0,  0.0 },
    {  0.0,  0.0,  1.0 },
    {  0.0,  0.0, -1.0 }
};

struct triangle_indices_t {
    common::u32 a{ 0u };
    common::u32 b{ 0u };
    common::u32 c{ 0u };
};

struct midpoint_record_t {
    common::u32 a{ 0u };
    common::u32 b{ 0u };
    common::u32 midpoint{ 0u };
};

f64 Component( vec3d_t value, common::u32 axis ) noexcept
{
    return axis == 0u ? value.x : axis == 1u ? value.y : value.z;
}

void SetComponent( vec3d_t *pValue, common::u32 axis, f64 value ) noexcept
{
    if ( axis == 0u ) {
        pValue->x = value;
    } else if ( axis == 1u ) {
        pValue->y = value;
    } else {
        pValue->z = value;
    }
}

vec3d_t AxisVector( common::u32 axis, f64 value ) noexcept
{
    vec3d_t result{};
    SetComponent( &result, axis, value );
    return result;
}

bool BrushDestinationIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

geometry_status_t ValidateBrushDestination(
    const brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( BrushDestinationIsCanonicalEmpty( *pBrush ) ) {
        return geometry_status_t::OK;
    }
    if ( pBrush->sides.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return geometry_status_t::CORRUPT_STATE;
}

geometry_status_t ValidateCommonArguments(
    const brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    if ( pAllocator == nullptr || pIdAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return ValidateBrushDestination( pBrush );
}

geometry_status_t ValidateBrushArrayDestinations(
    const brush_solid_t *pBrushes,
    common::usize nCount ) noexcept
{
    if ( pBrushes == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( common::usize i = 0u; i < nCount; ++i ) {
        const geometry_status_t status = ValidateBrushDestination( &pBrushes[i] );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
    }
    return geometry_status_t::OK;
}

bool FitsCoordinateLimit(
    vec3d_t center,
    vec3d_t halfExtents,
    f64 limit ) noexcept
{
    const f64 x = math::Scalar_Abs( center.x ) + halfExtents.x;
    const f64 y = math::Scalar_Abs( center.y ) + halfExtents.y;
    const f64 z = math::Scalar_Abs( center.z ) + halfExtents.z;
    return math::Scalar_IsFinite( x ) && x <= limit &&
           math::Scalar_IsFinite( y ) && y <= limit &&
           math::Scalar_IsFinite( z ) && z <= limit;
}

bool ExtentsMeetMinimumEdge(
    vec3d_t halfExtents,
    f64 minimumEdge ) noexcept
{
    return halfExtents.x >= minimumEdge * 0.5 &&
           halfExtents.y >= minimumEdge * 0.5 &&
           halfExtents.z >= minimumEdge * 0.5;
}

bool AxialPrimitiveFits(
    vec3d_t center,
    f64 radius,
    f64 halfHeight,
    common::u32 axis,
    f64 limit ) noexcept
{
    vec3d_t halfExtents{ radius, radius, radius };
    SetComponent( &halfExtents, axis, halfHeight );
    return FitsCoordinateLimit( center, halfExtents, limit );
}

f64 BoxPlaneDistance(
    vec3d_t center,
    vec3d_t halfExtents,
    common::usize iFace ) noexcept
{
    const common::usize iAxis = iFace / 2u;
    const f64 c = Component( center, static_cast<common::u32>( iAxis ) );
    const f64 h = Component( halfExtents, static_cast<common::u32>( iAxis ) );
    return ( iFace % 2u == 0u ) ? -( c + h ) : c - h;
}

geometry_status_t TryBeginBrush(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_source_id_allocator_t *pIdAllocator,
    common::usize cSides ) noexcept
{
    if ( static_cast<common::u64>( cSides ) >
         limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_source_id_result_t brushId =
        GeometrySourceIdAllocator_Allocate( pIdAllocator );
    if ( brushId.status != geometry_status_t::OK ) {
        return brushId.status;
    }
    geometry_status_t status = BrushSolid_Init( pBrush, pAllocator, brushId.id );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve( pBrush, limits, cSides );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pBrush );
    }
    return status;
}

geometry_status_t TryAppendPlane(
    brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits,
    geometry_source_id_allocator_t *pIdAllocator,
    math::planed_t plane,
    common::u32 iAttributeIndex ) noexcept
{
    const geometry_source_id_result_t sideId =
        GeometrySourceIdAllocator_Allocate( pIdAllocator );
    if ( sideId.status != geometry_status_t::OK ) {
        return sideId.status;
    }
    brush_solid_side_t side{};
    side.plane = plane;
    side.sourceId = sideId.id;
    side.iAttributeIndex = iAttributeIndex;
    return BrushSolid_TryAddSide( pBrush, limits, side, nullptr );
}

void PublishBrush(
    brush_solid_t *pDestination,
    brush_solid_t *pSource ) noexcept
{
    pDestination->sourceId = pSource->sourceId;
    common::Vector_Move( &pDestination->sides, &pSource->sides );
    pSource->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

geometry_status_t AllocateTemporaryBrushes(
    brush_solid_t **ppBrushes,
    common::usize nCount,
    const common::allocator_t *pAllocator,
    common::u64 cbScratchMax ) noexcept
{
    common::usize cbStorage = 0u;
    if ( !common::Cy_TryArrayByteCount<brush_solid_t>( nCount, cbStorage ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( static_cast<common::u64>( cbStorage ) > cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    brush_solid_t *pBrushes =
        common::Allocator_AllocateArrayStorage<brush_solid_t>( pAllocator, nCount );
    if ( pBrushes == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < nCount; ++i ) {
        ::new ( static_cast<void *>( &pBrushes[i] ) ) brush_solid_t{};
    }
    *ppBrushes = pBrushes;
    return geometry_status_t::OK;
}

void DestroyTemporaryBrushes(
    brush_solid_t *pBrushes,
    common::usize nCount,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pBrushes == nullptr ) {
        return;
    }
    for ( common::usize i = 0u; i < nCount; ++i ) {
        pBrushes[i].~brush_solid_t();
    }
    common::Allocator_FreeArrayStorage( pAllocator, pBrushes, nCount );
}

geometry_status_t TryBuildBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    vec3d_t halfExtents ) noexcept
{
    geometry_status_t status = TryBeginBrush(
        pBrush, pAllocator, policy.limits, pIdAllocator, 6u );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( common::u32 i = 0u; i < 6u; ++i ) {
        status = TryAppendPlane(
            pBrush,
            policy.limits,
            pIdAllocator,
            math::Planed_Make(
                cBoxNormals[i],
                BoxPlaneDistance( center, halfExtents, i ) ),
            i );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pBrush );
            return status;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateBoxGeometry(
    const geometry_policy_t &policy,
    vec3d_t center,
    vec3d_t halfExtents ) noexcept
{
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Vec3d_IsFinite( halfExtents ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( halfExtents.x <= 0.0 || halfExtents.y <= 0.0 ||
         halfExtents.z <= 0.0 ||
         !ExtentsMeetMinimumEdge(
             halfExtents, policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !FitsCoordinateLimit(
             center,
             halfExtents,
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateAxialPrimitive(
    const geometry_policy_t &policy,
    vec3d_t center,
    f64 radius,
    f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept
{
    if ( axis > 2u || nSides < 3u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Scalar_IsFinite( radius ) ||
         !math::Scalar_IsFinite( halfHeight ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( radius <= 0.0 || halfHeight <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    const common::u64 cTotalSides = static_cast<common::u64>( nSides ) + 2u;
    if ( cTotalSides > policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const f64 polygonEdge = 2.0 * radius * math::Scalar_Sin(
        math::CY_PI_D / static_cast<f64>( nSides ) );
    if ( !math::Scalar_IsFinite( polygonEdge ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( polygonEdge < policy.numerical.fMinimumEdgeLength ||
         2.0 * halfHeight < policy.numerical.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !AxialPrimitiveFits(
             center,
             radius,
             halfHeight,
             axis,
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

geometry_status_t TryBuildPrism(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    f64 radius,
    f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept
{
    const common::usize cTotalSides = static_cast<common::usize>( nSides ) + 2u;
    geometry_status_t status = TryBeginBrush(
        pBrush, pAllocator, policy.limits, pIdAllocator, cTotalSides );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const common::u32 uAxis = axis == 0u ? 1u : 0u;
    const common::u32 vAxis = axis <= 1u ? 2u : 1u;
    const f64 angleStep = 2.0 * math::CY_PI_D / static_cast<f64>( nSides );
    const f64 apothem = radius * math::Scalar_Cos(
        math::CY_PI_D / static_cast<f64>( nSides ) );

    for ( common::u32 i = 0u; i < nSides; ++i ) {
        const f64 angle = angleStep * ( static_cast<f64>( i ) + 0.5 );
        vec3d_t normal{};
        SetComponent( &normal, uAxis, math::Scalar_Cos( angle ) );
        SetComponent( &normal, vAxis, math::Scalar_Sin( angle ) );
        const f64 d = -( math::Vec3d_Dot( normal, center ) + apothem );
        status = TryAppendPlane(
            pBrush, policy.limits, pIdAllocator,
            math::Planed_Make( normal, d ), i );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pBrush );
            return status;
        }
    }
    for ( common::u32 cap = 0u; cap < 2u; ++cap ) {
        const f64 sign = cap == 0u ? 1.0 : -1.0;
        const f64 centerComponent = Component( center, axis );
        const f64 d = cap == 0u
            ? -( centerComponent + halfHeight )
            : centerComponent - halfHeight;
        status = TryAppendPlane(
            pBrush, policy.limits, pIdAllocator,
            math::Planed_Make( AxisVector( axis, sign ), d ),
            nSides + cap );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pBrush );
            return status;
        }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t BrushGenerator_TryMakeBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    vec3d_t halfExtents ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateBoxGeometry( policy, center, halfExtents );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( policy.limits.cBrushSidesPerBrushMax < 6u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBuildBox(
        &temporary, pAllocator, policy, &stagedIds, center, halfExtents );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeWedge(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    vec3d_t halfExtents,
    common::u32 cutAxis,
    common::u32 slopeAxis ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( cutAxis > 2u || slopeAxis > 2u || cutAxis == slopeAxis ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    status = ValidateBoxGeometry( policy, center, halfExtents );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( policy.limits.cBrushSidesPerBrushMax < 5u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBeginBrush(
        &temporary, pAllocator, policy.limits, &stagedIds, 5u );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const common::u32 extrusionAxis = 3u - cutAxis - slopeAxis;
    common::u32 iAttribute = 0u;
    auto append = [&]( vec3d_t normal, f64 d ) noexcept -> geometry_status_t {
        return TryAppendPlane(
            &temporary, policy.limits, &stagedIds,
            math::Planed_Make( normal, d ), iAttribute++ );
    };

    status = append(
        AxisVector( slopeAxis, -1.0 ),
        Component( center, slopeAxis ) - Component( halfExtents, slopeAxis ) );
    if ( status == geometry_status_t::OK ) {
        status = append(
            AxisVector( extrusionAxis, 1.0 ),
            -( Component( center, extrusionAxis ) +
               Component( halfExtents, extrusionAxis ) ) );
    }
    if ( status == geometry_status_t::OK ) {
        status = append(
            AxisVector( extrusionAxis, -1.0 ),
            Component( center, extrusionAxis ) -
                Component( halfExtents, extrusionAxis ) );
    }
    if ( status == geometry_status_t::OK ) {
        status = append(
            AxisVector( cutAxis, -1.0 ),
            Component( center, cutAxis ) - Component( halfExtents, cutAxis ) );
    }
    if ( status == geometry_status_t::OK ) {
        const f64 hCut = Component( halfExtents, cutAxis );
        const f64 hSlope = Component( halfExtents, slopeAxis );
        vec3d_t normal{};
        SetComponent( &normal, cutAxis, hSlope );
        SetComponent( &normal, slopeAxis, hCut );
        normal = math::Vec3d_Scale(
            normal,
            1.0 / math::Scalar_Sqrt( math::Vec3d_LengthSquared( normal ) ) );

        vec3d_t point = center;
        SetComponent(
            &point, cutAxis, Component( center, cutAxis ) + hCut );
        SetComponent(
            &point, slopeAxis, Component( center, slopeAxis ) - hSlope );
        status = append( normal, -math::Vec3d_Dot( normal, point ) );
    }

    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &temporary );
        return status;
    }
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakePrism(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 radius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateAxialPrimitive(
        policy, center, radius, halfHeight, nSides, axis );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBuildPrism(
        &temporary, pAllocator, policy, &stagedIds,
        center, radius, halfHeight, nSides, axis );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeStaircase(
    brush_solid_t *pBrushes,
    common::usize nSteps,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t origin,
    common::f64 stepWidth,
    common::f64 stepHeight,
    common::f64 stepDepth ) noexcept
{
    if ( pBrushes == nullptr || pAllocator == nullptr ||
         pIdAllocator == nullptr || !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( nSteps == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( static_cast<common::u64>( nSteps ) > policy.limits.cBrushesMax ||
         static_cast<common::u64>( nSteps ) >
             policy.limits.cBrushSidesMax / 6u ||
         policy.limits.cBrushSidesPerBrushMax < 6u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_status_t outputStatus =
        ValidateBrushArrayDestinations( pBrushes, nSteps );
    if ( outputStatus != geometry_status_t::OK ) {
        return outputStatus;
    }
    if ( !math::Vec3d_IsFinite( origin ) ||
         !math::Scalar_IsFinite( stepWidth ) ||
         !math::Scalar_IsFinite( stepHeight ) ||
         !math::Scalar_IsFinite( stepDepth ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( stepWidth < policy.numerical.fMinimumEdgeLength ||
         stepHeight < policy.numerical.fMinimumEdgeLength ||
         stepDepth < policy.numerical.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }

    const f64 stepCount = static_cast<f64>( nSteps );
    const vec3d_t farCorner = math::Vec3d_Make(
        std::fma( stepCount, stepWidth, origin.x ),
        origin.y + stepDepth,
        std::fma( stepCount, stepHeight, origin.z ) );
    const f64 limit = policy.numerical.fCoordinateMagnitudeLimit;
    if ( !math::Vec3d_IsFinite( farCorner ) ||
         math::Scalar_Abs( origin.x ) > limit ||
         math::Scalar_Abs( origin.y ) > limit ||
         math::Scalar_Abs( origin.z ) > limit ||
         math::Scalar_Abs( farCorner.x ) > limit ||
         math::Scalar_Abs( farCorner.y ) > limit ||
         math::Scalar_Abs( farCorner.z ) > limit ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    brush_solid_t *pTemporary = nullptr;
    geometry_status_t status = AllocateTemporaryBrushes(
        &pTemporary, nSteps, pAllocator, policy.limits.cbScratchMax );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    for ( common::usize i = 0u; i < nSteps; ++i ) {
        const f64 height = static_cast<f64>( i + 1u ) * stepHeight;
        const vec3d_t halfExtents = math::Vec3d_Make(
            stepWidth * 0.5, stepDepth * 0.5, height * 0.5 );
        const vec3d_t center = math::Vec3d_Make(
            std::fma( static_cast<f64>( i ) + 0.5, stepWidth, origin.x ),
            origin.y + halfExtents.y,
            origin.z + halfExtents.z );
        status = TryBuildBox(
            &pTemporary[i], pAllocator, policy, &stagedIds,
            center, halfExtents );
        if ( status != geometry_status_t::OK ) {
            DestroyTemporaryBrushes( pTemporary, nSteps, pAllocator );
            return status;
        }
    }
    for ( common::usize i = 0u; i < nSteps; ++i ) {
        PublishBrush( &pBrushes[i], &pTemporary[i] );
    }
    *pIdAllocator = stagedIds;
    DestroyTemporaryBrushes( pTemporary, nSteps, pAllocator );
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeArch(
    brush_solid_t *pBrushes,
    common::usize nSegments,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 outerRadius,
    common::f64 innerRadius,
    common::f64 thickness,
    common::f64 arcAngleRadians ) noexcept
{
    if ( pBrushes == nullptr || pAllocator == nullptr ||
         pIdAllocator == nullptr || !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( nSegments == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( static_cast<common::u64>( nSegments ) > policy.limits.cBrushesMax ||
         static_cast<common::u64>( nSegments ) >
             policy.limits.cBrushSidesMax / 6u ||
         policy.limits.cBrushSidesPerBrushMax < 6u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_status_t outputStatus =
        ValidateBrushArrayDestinations( pBrushes, nSegments );
    if ( outputStatus != geometry_status_t::OK ) {
        return outputStatus;
    }
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Scalar_IsFinite( outerRadius ) ||
         !math::Scalar_IsFinite( innerRadius ) ||
         !math::Scalar_IsFinite( thickness ) ||
         !math::Scalar_IsFinite( arcAngleRadians ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( outerRadius <= 0.0 || innerRadius <= 0.0 ||
         innerRadius >= outerRadius || thickness <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( arcAngleRadians <= 0.0 ||
         arcAngleRadians > 2.0 * math::CY_PI_D ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const f64 segmentAngle =
        arcAngleRadians / static_cast<f64>( nSegments );
    if ( segmentAngle >=
         math::CY_PI_D - policy.numerical.fAngularToleranceRadians ) {
        return geometry_status_t::DEGENERATE;
    }
    const f64 halfThickness = thickness * 0.5;
    const f64 radialThickness = outerRadius - innerRadius;
    const f64 innerEdge = 2.0 * innerRadius *
        math::Scalar_Sin( segmentAngle * 0.5 );
    if ( thickness < policy.numerical.fMinimumEdgeLength ||
         radialThickness < policy.numerical.fMinimumEdgeLength ||
         innerEdge < policy.numerical.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !FitsCoordinateLimit(
             center,
             math::Vec3d_Make( outerRadius, halfThickness, outerRadius ),
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    brush_solid_t *pTemporary = nullptr;
    geometry_status_t status = AllocateTemporaryBrushes(
        &pTemporary, nSegments, pAllocator, policy.limits.cbScratchMax );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    const f64 radialProjection = math::Scalar_Cos( segmentAngle * 0.5 );
    for ( common::usize i = 0u; i < nSegments; ++i ) {
        status = TryBeginBrush(
            &pTemporary[i], pAllocator, policy.limits, &stagedIds, 6u );
        if ( status != geometry_status_t::OK ) {
            DestroyTemporaryBrushes( pTemporary, nSegments, pAllocator );
            return status;
        }

        const f64 angle0 = segmentAngle * static_cast<f64>( i );
        const f64 angle1 = angle0 + segmentAngle;
        const f64 midAngle = ( angle0 + angle1 ) * 0.5;
        const vec3d_t radial = math::Vec3d_Make(
            math::Scalar_Cos( midAngle ), 0.0,
            math::Scalar_Sin( midAngle ) );
        const vec3d_t inward = math::Vec3d_Negate( radial );
        const f64 outerApothem = outerRadius * radialProjection;
        const f64 innerApothem = innerRadius * radialProjection;

        math::planed_t planes[6]{};
        planes[0] = math::Planed_Make(
            radial,
            -( math::Vec3d_Dot( radial, center ) + outerApothem ) );
        planes[1] = math::Planed_Make(
            inward,
            -( math::Vec3d_Dot( inward, center ) - innerApothem ) );
        const vec3d_t left = math::Vec3d_Make(
            math::Scalar_Sin( angle0 ), 0.0,
            -math::Scalar_Cos( angle0 ) );
        const vec3d_t right = math::Vec3d_Make(
            -math::Scalar_Sin( angle1 ), 0.0,
            math::Scalar_Cos( angle1 ) );
        planes[2] = math::Planed_Make(
            left, -math::Vec3d_Dot( left, center ) );
        planes[3] = math::Planed_Make(
            right, -math::Vec3d_Dot( right, center ) );
        planes[4] = math::Planed_Make(
            math::Vec3d_Make( 0.0, 1.0, 0.0 ),
            -( center.y + halfThickness ) );
        planes[5] = math::Planed_Make(
            math::Vec3d_Make( 0.0, -1.0, 0.0 ),
            center.y - halfThickness );

        for ( common::u32 iSide = 0u; iSide < 6u; ++iSide ) {
            status = TryAppendPlane(
                &pTemporary[i], policy.limits, &stagedIds,
                planes[iSide], iSide );
            if ( status != geometry_status_t::OK ) {
                DestroyTemporaryBrushes( pTemporary, nSegments, pAllocator );
                return status;
            }
        }
    }
    for ( common::usize i = 0u; i < nSegments; ++i ) {
        PublishBrush( &pBrushes[i], &pTemporary[i] );
    }
    *pIdAllocator = stagedIds;
    DestroyTemporaryBrushes( pTemporary, nSegments, pAllocator );
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeCylinder(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 radius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept
{
    return BrushGenerator_TryMakePrism(
        pBrush, pAllocator, policy, pIdAllocator,
        center, radius, halfHeight, nSides, axis );
}

geometry_status_t BrushGenerator_TryMakeCone(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 bottomRadius,
    common::f64 topRadius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( axis > 2u || nSides < 3u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Scalar_IsFinite( bottomRadius ) ||
         !math::Scalar_IsFinite( topRadius ) ||
         !math::Scalar_IsFinite( halfHeight ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( bottomRadius <= 0.0 || topRadius < 0.0 || halfHeight <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( bottomRadius == topRadius ) {
        return BrushGenerator_TryMakePrism(
            pBrush, pAllocator, policy, pIdAllocator,
            center, bottomRadius, halfHeight, nSides, axis );
    }

    const bool bHasTopFace = topRadius > 0.0;
    const common::u64 cTotalSides = static_cast<common::u64>( nSides ) +
        ( bHasTopFace ? 2u : 1u );
    if ( cTotalSides > policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const f64 maxRadius = bottomRadius > topRadius
        ? bottomRadius
        : topRadius;
    const f64 sine = math::Scalar_Sin(
        math::CY_PI_D / static_cast<f64>( nSides ) );
    const f64 bottomEdge = 2.0 * bottomRadius * sine;
    const f64 topEdge = 2.0 * topRadius * sine;
    if ( !math::Scalar_IsFinite( bottomEdge ) ||
         !math::Scalar_IsFinite( topEdge ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( bottomEdge < policy.numerical.fMinimumEdgeLength ||
         ( topRadius > 0.0 &&
           topEdge < policy.numerical.fMinimumEdgeLength ) ||
         2.0 * halfHeight < policy.numerical.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !AxialPrimitiveFits(
             center, maxRadius, halfHeight, axis,
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBeginBrush(
        &temporary, pAllocator, policy.limits, &stagedIds,
        static_cast<common::usize>( cTotalSides ) );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const common::u32 uAxis = axis == 0u ? 1u : 0u;
    const common::u32 vAxis = axis <= 1u ? 2u : 1u;
    const f64 angleStep = 2.0 * math::CY_PI_D / static_cast<f64>( nSides );
    const f64 halfAngle = math::CY_PI_D / static_cast<f64>( nSides );
    const f64 bottomApothem = bottomRadius * math::Scalar_Cos( halfAngle );
    const f64 topApothem = topRadius * math::Scalar_Cos( halfAngle );
    const f64 fullHeight = 2.0 * halfHeight;
    const f64 apothemDifference = bottomApothem - topApothem;

    for ( common::u32 i = 0u; i < nSides; ++i ) {
        const f64 angle = angleStep * ( static_cast<f64>( i ) + 0.5 );
        const f64 cosAngle = math::Scalar_Cos( angle );
        const f64 sinAngle = math::Scalar_Sin( angle );
        vec3d_t normal{};
        SetComponent( &normal, uAxis, fullHeight * cosAngle );
        SetComponent( &normal, vAxis, fullHeight * sinAngle );
        SetComponent( &normal, axis, apothemDifference );
        const f64 lengthSquared = math::Vec3d_LengthSquared( normal );
        if ( !math::Scalar_IsFinite( lengthSquared ) || lengthSquared <= 0.0 ) {
            BrushSolid_Shutdown( &temporary );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        normal = math::Vec3d_Scale(
            normal, 1.0 / math::Scalar_Sqrt( lengthSquared ) );

        vec3d_t bottomMidpoint = center;
        SetComponent(
            &bottomMidpoint, uAxis,
            Component( center, uAxis ) + bottomApothem * cosAngle );
        SetComponent(
            &bottomMidpoint, vAxis,
            Component( center, vAxis ) + bottomApothem * sinAngle );
        SetComponent(
            &bottomMidpoint, axis,
            Component( center, axis ) - halfHeight );
        status = TryAppendPlane(
            &temporary, policy.limits, &stagedIds,
            math::Planed_Make(
                normal, -math::Vec3d_Dot( normal, bottomMidpoint ) ),
            i );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &temporary );
            return status;
        }
    }
    if ( bHasTopFace ) {
        status = TryAppendPlane(
            &temporary, policy.limits, &stagedIds,
            math::Planed_Make(
                AxisVector( axis, 1.0 ),
                -( Component( center, axis ) + halfHeight ) ),
            nSides );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &temporary );
            return status;
        }
    }
    status = TryAppendPlane(
        &temporary, policy.limits, &stagedIds,
        math::Planed_Make(
            AxisVector( axis, -1.0 ),
            Component( center, axis ) - halfHeight ),
        nSides + ( bHasTopFace ? 1u : 0u ) );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &temporary );
        return status;
    }
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeSphere(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 radius,
    common::u32 nSubdivisions ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Scalar_IsFinite( radius ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( radius <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !FitsCoordinateLimit(
             center,
             math::Vec3d_Make( radius, radius, radius ),
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    common::u64 subdivisionFactor = 1u;
    for ( common::u32 i = 0u; i < nSubdivisions; ++i ) {
        if ( subdivisionFactor > common::CY_U64_MAX / 4u ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        subdivisionFactor *= 4u;
        if ( subdivisionFactor >
             policy.limits.cBrushSidesPerBrushMax / 20u ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }
    const common::u64 cFaces64 = 20u * subdivisionFactor;
    const common::u64 cVertices64 = 10u * subdivisionFactor + 2u;
    if ( cFaces64 > policy.limits.cBrushSidesPerBrushMax ||
         cVertices64 > policy.limits.cVerticesMax ||
         cFaces64 > std::numeric_limits<common::u32>::max() ||
         cVertices64 > std::numeric_limits<common::u32>::max() ||
         cFaces64 > common::CY_USIZE_MAX ||
         cVertices64 > common::CY_USIZE_MAX ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cFaceCapacity =
        static_cast<common::usize>( cFaces64 );
    const common::usize cVertexCapacity =
        static_cast<common::usize>( cVertices64 );

    common::usize cEdgeCapacity = 0u;
    if ( nSubdivisions > 0u ) {
        if ( cFaceCapacity > common::CY_USIZE_MAX / 3u ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cEdgeCapacity = cFaceCapacity * 3u / 4u;
    }

    common::usize cbVertices = 0u;
    common::usize cbTriangles = 0u;
    common::usize cbTriangleSwap = 0u;
    common::usize cbMidpoints = 0u;
    if ( !common::Cy_TryArrayByteCount<vec3d_t>(
             cVertexCapacity, cbVertices ) ||
         !common::Cy_TryArrayByteCount<triangle_indices_t>(
             cFaceCapacity, cbTriangles ) ||
         ( nSubdivisions > 0u &&
           ( !common::Cy_TryArrayByteCount<triangle_indices_t>(
                 cFaceCapacity, cbTriangleSwap ) ||
             !common::Cy_TryArrayByteCount<midpoint_record_t>(
                 cEdgeCapacity, cbMidpoints ) ) ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    common::u64 cbScratch = static_cast<common::u64>( cbVertices );
    if ( cbScratch > policy.limits.cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::u64 scratchParts[] = {
        static_cast<common::u64>( cbTriangles ),
        static_cast<common::u64>( cbTriangleSwap ),
        static_cast<common::u64>( cbMidpoints )
    };
    for ( common::u64 cbPart : scratchParts ) {
        if ( cbPart > policy.limits.cbScratchMax - cbScratch ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cbScratch += cbPart;
    }

    vec3d_t *pVertices =
        common::Allocator_AllocateArrayStorage<vec3d_t>(
            pAllocator, cVertexCapacity );
    triangle_indices_t *pTriangles =
        common::Allocator_AllocateArrayStorage<triangle_indices_t>(
            pAllocator, cFaceCapacity );
    triangle_indices_t *pTriangleSwap = nSubdivisions > 0u
        ? common::Allocator_AllocateArrayStorage<triangle_indices_t>(
              pAllocator, cFaceCapacity )
        : nullptr;
    midpoint_record_t *pMidpoints = nSubdivisions > 0u
        ? common::Allocator_AllocateArrayStorage<midpoint_record_t>(
              pAllocator, cEdgeCapacity )
        : nullptr;

    auto cleanupWorkspace = [&]() noexcept {
        if ( pMidpoints != nullptr ) {
            common::Allocator_FreeArrayStorage(
                pAllocator, pMidpoints, cEdgeCapacity );
        }
        if ( pTriangleSwap != nullptr ) {
            common::Allocator_FreeArrayStorage(
                pAllocator, pTriangleSwap, cFaceCapacity );
        }
        if ( pTriangles != nullptr ) {
            common::Allocator_FreeArrayStorage(
                pAllocator, pTriangles, cFaceCapacity );
        }
        if ( pVertices != nullptr ) {
            common::Allocator_FreeArrayStorage(
                pAllocator, pVertices, cVertexCapacity );
        }
    };
    if ( pVertices == nullptr || pTriangles == nullptr ||
         ( nSubdivisions > 0u &&
           ( pTriangleSwap == nullptr || pMidpoints == nullptr ) ) ) {
        cleanupWorkspace();
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const f64 phi = ( 1.0 + math::Scalar_Sqrt( 5.0 ) ) * 0.5;
    const f64 inverseLength = 1.0 /
        math::Scalar_Sqrt( 1.0 + phi * phi );
    const vec3d_t initialVertices[12] = {
        math::Vec3d_Scale( math::Vec3d_Make( -1.0,  phi,  0.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  1.0,  phi,  0.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make( -1.0, -phi,  0.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  1.0, -phi,  0.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  0.0, -1.0,  phi ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  0.0,  1.0,  phi ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  0.0, -1.0, -phi ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  0.0,  1.0, -phi ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  phi,  0.0, -1.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make(  phi,  0.0,  1.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make( -phi,  0.0, -1.0 ), inverseLength ),
        math::Vec3d_Scale( math::Vec3d_Make( -phi,  0.0,  1.0 ), inverseLength )
    };
    const triangle_indices_t initialFaces[20] = {
        { 0u, 11u, 5u }, { 0u, 5u, 1u }, { 0u, 1u, 7u },
        { 0u, 7u, 10u }, { 0u, 10u, 11u }, { 1u, 5u, 9u },
        { 5u, 11u, 4u }, { 11u, 10u, 2u }, { 10u, 7u, 6u },
        { 7u, 1u, 8u }, { 3u, 9u, 4u }, { 3u, 4u, 2u },
        { 3u, 2u, 6u }, { 3u, 6u, 8u }, { 3u, 8u, 9u },
        { 4u, 9u, 5u }, { 2u, 4u, 11u }, { 6u, 2u, 10u },
        { 8u, 6u, 7u }, { 9u, 8u, 1u }
    };
    for ( common::usize i = 0u; i < 12u; ++i ) {
        pVertices[i] = initialVertices[i];
    }
    for ( common::usize i = 0u; i < 20u; ++i ) {
        pTriangles[i] = initialFaces[i];
    }
    common::usize cVertices = 12u;
    common::usize cTriangles = 20u;

    for ( common::u32 subdivision = 0u;
          subdivision < nSubdivisions;
          ++subdivision ) {
        common::usize cMidpoints = 0u;
        common::usize cNewTriangles = 0u;
        auto tryMidpoint = [&]( common::u32 a,
                                common::u32 b,
                                common::u32 *pIndexOut ) noexcept
            -> geometry_status_t {
            const common::u32 low = a < b ? a : b;
            const common::u32 high = a < b ? b : a;
            for ( common::usize i = 0u; i < cMidpoints; ++i ) {
                if ( pMidpoints[i].a == low && pMidpoints[i].b == high ) {
                    *pIndexOut = pMidpoints[i].midpoint;
                    return geometry_status_t::OK;
                }
            }
            if ( cMidpoints >= cEdgeCapacity ||
                 cVertices >= cVertexCapacity ) {
                return geometry_status_t::LIMIT_EXCEEDED;
            }
            const vec3d_t midpoint = math::Vec3d_Scale(
                math::Vec3d_Add( pVertices[a], pVertices[b] ), 0.5 );
            const f64 lengthSquared = math::Vec3d_LengthSquared( midpoint );
            if ( !math::Scalar_IsFinite( lengthSquared ) ||
                 lengthSquared <= 0.0 ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            const common::u32 index = static_cast<common::u32>( cVertices );
            pVertices[cVertices++] = math::Vec3d_Scale(
                midpoint, 1.0 / math::Scalar_Sqrt( lengthSquared ) );
            pMidpoints[cMidpoints++] = { low, high, index };
            *pIndexOut = index;
            return geometry_status_t::OK;
        };

        for ( common::usize i = 0u; i < cTriangles; ++i ) {
            const triangle_indices_t triangle = pTriangles[i];
            common::u32 ab = 0u;
            common::u32 bc = 0u;
            common::u32 ca = 0u;
            status = tryMidpoint( triangle.a, triangle.b, &ab );
            if ( status == geometry_status_t::OK ) {
                status = tryMidpoint( triangle.b, triangle.c, &bc );
            }
            if ( status == geometry_status_t::OK ) {
                status = tryMidpoint( triangle.c, triangle.a, &ca );
            }
            if ( status != geometry_status_t::OK ) {
                cleanupWorkspace();
                return status;
            }
            if ( cNewTriangles > cFaceCapacity - 4u ) {
                cleanupWorkspace();
                return geometry_status_t::LIMIT_EXCEEDED;
            }
            pTriangleSwap[cNewTriangles++] = { triangle.a, ab, ca };
            pTriangleSwap[cNewTriangles++] = { triangle.b, bc, ab };
            pTriangleSwap[cNewTriangles++] = { triangle.c, ca, bc };
            pTriangleSwap[cNewTriangles++] = { ab, bc, ca };
        }
        std::swap( pTriangles, pTriangleSwap );
        cTriangles = cNewTriangles;
    }

    if ( cVertices != cVertexCapacity || cTriangles != cFaceCapacity ) {
        cleanupWorkspace();
        return geometry_status_t::CORRUPT_STATE;
    }
    for ( common::usize i = 0u; i < cVertices; ++i ) {
        pVertices[i] = math::Vec3d_Add(
            math::Vec3d_Scale( pVertices[i], radius ), center );
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBeginBrush(
        &temporary, pAllocator, policy.limits, &stagedIds, cTriangles );
    if ( status != geometry_status_t::OK ) {
        cleanupWorkspace();
        return status;
    }
    for ( common::usize i = 0u; i < cTriangles; ++i ) {
        const triangle_indices_t triangle = pTriangles[i];
        const vec3d_t a = pVertices[triangle.a];
        const vec3d_t b = pVertices[triangle.b];
        const vec3d_t c = pVertices[triangle.c];
        vec3d_t normal = math::Vec3d_Cross(
            math::Vec3d_Subtract( b, a ),
            math::Vec3d_Subtract( c, a ) );
        const f64 lengthSquared = math::Vec3d_LengthSquared( normal );
        const f64 minimumArea = policy.numerical.fMinimumFaceArea;
        if ( !math::Scalar_IsFinite( lengthSquared ) ||
             lengthSquared * 0.25 < minimumArea * minimumArea ) {
            BrushSolid_Shutdown( &temporary );
            cleanupWorkspace();
            return geometry_status_t::DEGENERATE;
        }
        normal = math::Vec3d_Scale(
            normal, 1.0 / math::Scalar_Sqrt( lengthSquared ) );
        if ( math::Vec3d_Dot(
                 normal, math::Vec3d_Subtract( a, center ) ) < 0.0 ) {
            normal = math::Vec3d_Negate( normal );
        }
        status = TryAppendPlane(
            &temporary, policy.limits, &stagedIds,
            math::Planed_Make( normal, -math::Vec3d_Dot( normal, a ) ),
            static_cast<common::u32>( i ) );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &temporary );
            cleanupWorkspace();
            return status;
        }
    }
    cleanupWorkspace();
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushGenerator_TryMakeTetrahedron(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    common::f64 radius ) noexcept
{
    geometry_status_t status = ValidateCommonArguments(
        pBrush, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Scalar_IsFinite( radius ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const f64 edgeLength = radius * math::Scalar_Sqrt( 8.0 / 3.0 );
    if ( radius <= 0.0 ||
         edgeLength < policy.numerical.fMinimumEdgeLength ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( !FitsCoordinateLimit(
             center,
             math::Vec3d_Make( radius, radius, radius ),
             policy.numerical.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( policy.limits.cBrushSidesPerBrushMax < 4u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const f64 sqrt2 = math::Scalar_Sqrt( 2.0 );
    const f64 sqrt6 = math::Scalar_Sqrt( 6.0 );
    const vec3d_t vertices[4] = {
        math::Vec3d_Add(
            center,
            math::Vec3d_Scale(
                math::Vec3d_Make( 0.0, 0.0, 1.0 ), radius ) ),
        math::Vec3d_Add(
            center,
            math::Vec3d_Scale(
                math::Vec3d_Make(
                    2.0 * sqrt2 / 3.0, 0.0, -1.0 / 3.0 ), radius ) ),
        math::Vec3d_Add(
            center,
            math::Vec3d_Scale(
                math::Vec3d_Make(
                    -sqrt2 / 3.0, sqrt6 / 3.0, -1.0 / 3.0 ), radius ) ),
        math::Vec3d_Add(
            center,
            math::Vec3d_Scale(
                math::Vec3d_Make(
                    -sqrt2 / 3.0, -sqrt6 / 3.0, -1.0 / 3.0 ), radius ) )
    };
    const triangle_indices_t faces[4] = {
        { 0u, 1u, 2u }, { 0u, 2u, 3u },
        { 0u, 3u, 1u }, { 1u, 3u, 2u }
    };

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t temporary{};
    status = TryBeginBrush(
        &temporary, pAllocator, policy.limits, &stagedIds, 4u );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( common::u32 i = 0u; i < 4u; ++i ) {
        const vec3d_t a = vertices[faces[i].a];
        const vec3d_t b = vertices[faces[i].b];
        const vec3d_t c = vertices[faces[i].c];
        vec3d_t normal = math::Vec3d_Cross(
            math::Vec3d_Subtract( b, a ),
            math::Vec3d_Subtract( c, a ) );
        const f64 lengthSquared = math::Vec3d_LengthSquared( normal );
        if ( !math::Scalar_IsFinite( lengthSquared ) || lengthSquared <= 0.0 ) {
            BrushSolid_Shutdown( &temporary );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        normal = math::Vec3d_Scale(
            normal, 1.0 / math::Scalar_Sqrt( lengthSquared ) );
        if ( math::Vec3d_Dot(
                 normal, math::Vec3d_Subtract( center, a ) ) > 0.0 ) {
            normal = math::Vec3d_Negate( normal );
        }
        status = TryAppendPlane(
            &temporary, policy.limits, &stagedIds,
            math::Planed_Make( normal, -math::Vec3d_Dot( normal, a ) ), i );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &temporary );
            return status;
        }
    }
    PublishBrush( pBrush, &temporary );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
