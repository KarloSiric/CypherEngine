//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushClip.cpp
//  Purpose: Implements failure-atomic plane clipping and slicing for brushes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushClip.h"
#include "CypherGeometry_Kernel_Classification.h"

namespace cypher::editor::geometry
{

namespace
{

bool BrushIsCanonicalEmpty( const brush_solid_t &brush ) noexcept
{
    return brush.sides.pData == nullptr &&
           brush.sides.nCount == 0u &&
           brush.sides.nCapacity == 0u &&
           brush.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( brush.sourceId );
}

geometry_status_t ValidateDestination(
    const brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( BrushIsCanonicalEmpty( *pBrush ) ) {
        return geometry_status_t::OK;
    }
    if ( pBrush->sides.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return geometry_status_t::CORRUPT_STATE;
}

geometry_status_t ValidateSourceBrush(
    const brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return BrushIsCanonicalEmpty( *pBrush )
            ? geometry_status_t::NOT_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }
    if ( !common::Allocator_IsValid( pBrush->sides.pAllocator ) ||
         !common::Vector_IsValid( &pBrush->sides ) ||
         !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( static_cast<common::u64>( pBrush->sides.nCount ) >
         limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    for ( common::usize iSide = 0u;
          iSide < pBrush->sides.nCount;
          ++iSide ) {
        const geometry_source_id_t id =
            pBrush->sides.pData[iSide].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( id.value == pBrush->sourceId.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        for ( common::usize iPrevious = 0u;
              iPrevious < iSide;
              ++iPrevious ) {
            if ( pBrush->sides.pData[iPrevious].sourceId.value == id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateClipPlane(
    math::planed_t clipPlane,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( !math::Planed_IsFinite( clipPlane ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !math::Planed_IsNormalized(
             clipPlane, policy.fUnitNormalTolerance ) ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( math::Scalar_Abs( clipPlane.d ) >
         policy.fCoordinateMagnitudeLimit ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

bool BoundaryIsReadable( const brush_boundary_t &boundary ) noexcept
{
    const common::allocator_t *pAllocator = boundary.vertices.pAllocator;
    return common::Allocator_IsValid( pAllocator ) &&
           boundary.edges.pAllocator == pAllocator &&
           boundary.faces.pAllocator == pAllocator &&
           boundary.faceVertexIndices.pAllocator == pAllocator &&
           common::Vector_IsValid( &boundary.vertices ) &&
           common::Vector_IsValid( &boundary.edges ) &&
           common::Vector_IsValid( &boundary.faces ) &&
           common::Vector_IsValid( &boundary.faceVertexIndices ) &&
           boundary.vertices.nCount >= 4u &&
           boundary.edges.nCount >= 6u &&
           boundary.faces.nCount >= 4u;
}

bool BrushContainsIdentity(
    const brush_solid_t &brush,
    geometry_source_id_t id ) noexcept
{
    if ( brush.sourceId.value == id.value ) {
        return true;
    }
    for ( common::usize iSide = 0u;
          iSide < brush.sides.nCount;
          ++iSide ) {
        if ( brush.sides.pData[iSide].sourceId.value == id.value ) {
            return true;
        }
    }
    return false;
}

geometry_status_t TryAllocateIdentityNotInBrush(
    geometry_source_id_allocator_t *pIds,
    const brush_solid_t &brush,
    geometry_source_id_t *pIdOut ) noexcept
{
    *pIdOut = GEOMETRY_SOURCE_ID_INVALID;
    const geometry_source_id_result_t result =
        GeometrySourceIdAllocator_Allocate( pIds );
    if ( result.status != geometry_status_t::OK ) {
        return result.status;
    }
    if ( BrushContainsIdentity( brush, result.id ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    *pIdOut = result.id;
    return geometry_status_t::OK;
}

geometry_status_t TryClonePreservingIdentities(
    brush_solid_t *pOut,
    const brush_solid_t &source,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits ) noexcept
{
    geometry_status_t status = BrushSolid_Init(
        pOut, pAllocator, source.sourceId );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    status = BrushSolid_TryReserve(
        pOut, limits, source.sides.nCount + 1u );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pOut );
        return status;
    }

    for ( common::usize iSide = 0u;
          iSide < source.sides.nCount;
          ++iSide ) {
        status = BrushSolid_TryAddSide(
            pOut, limits, source.sides.pData[iSide], nullptr );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pOut );
            return status;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t TryCloneWithFreshIdentities(
    brush_solid_t *pOut,
    const brush_solid_t &source,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_source_id_allocator_t *pIds ) noexcept
{
    const geometry_source_id_result_t brushId =
        GeometrySourceIdAllocator_Allocate( pIds );
    if ( brushId.status != geometry_status_t::OK ) {
        return brushId.status;
    }
    if ( BrushContainsIdentity( source, brushId.id ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    geometry_status_t status = BrushSolid_Init(
        pOut, pAllocator, brushId.id );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve(
        pOut, limits, source.sides.nCount + 1u );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pOut );
        return status;
    }

    for ( common::usize iSide = 0u;
          iSide < source.sides.nCount;
          ++iSide ) {
        brush_solid_side_t side = source.sides.pData[iSide];
        const geometry_source_id_result_t sideId =
            GeometrySourceIdAllocator_Allocate( pIds );
        if ( sideId.status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pOut );
            return sideId.status;
        }
        if ( BrushContainsIdentity( source, sideId.id ) ) {
            BrushSolid_Shutdown( pOut );
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        side.sourceId = sideId.id;
        status = BrushSolid_TryAddSide(
            pOut, limits, side, nullptr );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pOut );
            return status;
        }
    }
    return geometry_status_t::OK;
}

void PublishBrush(
    brush_solid_t *pDestination,
    brush_solid_t *pStaged,
    bool bReplaceLiveDestination ) noexcept
{
    if ( bReplaceLiveDestination ) {
        BrushSolid_Shutdown( pDestination );
    }
    pDestination->sourceId = pStaged->sourceId;
    common::Vector_Move( &pDestination->sides, &pStaged->sides );
    pStaged->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

geometry_status_t TryBuildStrictBoundary(
    brush_boundary_t *pBoundary,
    const brush_solid_t &brush,
    const geometry_policy_t &policy ) noexcept
{
    geometry_status_t status = BrushBoundary_Init(
        pBoundary, brush.sides.pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushBoundary_TryReconstruct(
        pBoundary, &brush, policy );
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( pBoundary );
    }
    return status;
}

} // namespace

brush_clip_classification_t BrushClip_Classify(
    const brush_boundary_t *pBoundary,
    math::planed_t clipPlane,
    const geometry_numerical_policy_t &numericalPolicy ) noexcept
{
    if ( pBoundary == nullptr ||
         !GeometryNumericalPolicy_IsValid( numericalPolicy ) ||
         !BoundaryIsReadable( *pBoundary ) ||
         ValidateClipPlane( clipPlane, numericalPolicy ) !=
             geometry_status_t::OK ) {
        return brush_clip_classification_t::INVALID;
    }

    bool bAnyFront = false;
    bool bAnyBack = false;
    for ( common::usize iVertex = 0u;
          iVertex < pBoundary->vertices.nCount;
          ++iVertex ) {
        const geometry_classify_result_t result = Kernel_ClassifyPoint(
            numericalPolicy,
            clipPlane,
            pBoundary->vertices.pData[iVertex] );
        if ( result.status != geometry_status_t::OK ) {
            return brush_clip_classification_t::INVALID;
        }
        if ( result.orientation == geometry_orientation_t::POSITIVE ) {
            bAnyFront = true;
        } else if ( result.orientation == geometry_orientation_t::NEGATIVE ) {
            bAnyBack = true;
        }
        if ( bAnyFront && bAnyBack ) {
            return brush_clip_classification_t::INTERSECTS;
        }
    }

    if ( bAnyFront ) {
        return brush_clip_classification_t::ALL_FRONT;
    }
    if ( bAnyBack ) {
        return brush_clip_classification_t::ALL_BACK;
    }
    return brush_clip_classification_t::ON_PLANE;
}

geometry_status_t BrushClip_TryClip(
    brush_solid_t *pBrush,
    math::planed_t clipPlane,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy ) noexcept
{
    if ( pIdAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t status = ValidateSourceBrush(
        pBrush, policy.limits );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateClipPlane( clipPlane, policy.numerical );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_boundary_t boundary{};
    status = TryBuildStrictBoundary( &boundary, *pBrush, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const brush_clip_classification_t classification =
        BrushClip_Classify( &boundary, clipPlane, policy.numerical );
    if ( classification == brush_clip_classification_t::ALL_BACK ) {
        BrushBoundary_Shutdown( &boundary );
        return geometry_status_t::OK;
    }
    if ( classification == brush_clip_classification_t::ALL_FRONT ||
         classification == brush_clip_classification_t::ON_PLANE ) {
        BrushBoundary_Shutdown( &boundary );
        return geometry_status_t::DEGENERATE;
    }
    if ( classification != brush_clip_classification_t::INTERSECTS ) {
        BrushBoundary_Shutdown( &boundary );
        return geometry_status_t::CORRUPT_STATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t stagedBrush{};
    status = TryClonePreservingIdentities(
        &stagedBrush,
        *pBrush,
        pBrush->sides.pAllocator,
        policy.limits );
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &boundary );
        return status;
    }

    geometry_source_id_t clipSideId{};
    status = TryAllocateIdentityNotInBrush(
        &stagedIds, stagedBrush, &clipSideId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryAddSide(
            &stagedBrush,
            policy.limits,
            brush_solid_side_t{ clipPlane, clipSideId, 0u },
            nullptr );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryCanonicalizeSides(
            &stagedBrush, policy );
    }
    if ( status == geometry_status_t::OK &&
         !BrushContainsIdentity( stagedBrush, clipSideId ) ) {
        status = geometry_status_t::CORRUPT_STATE;
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_TryReconstruct(
            &boundary, &stagedBrush, policy );
    }
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &stagedBrush );
        BrushBoundary_Shutdown( &boundary );
        return status;
    }

    PublishBrush( pBrush, &stagedBrush, true );
    *pIdAllocator = stagedIds;
    BrushBoundary_Shutdown( &boundary );
    return geometry_status_t::OK;
}

geometry_status_t BrushClip_TrySlice(
    const brush_solid_t *pSrc,
    const common::allocator_t *pAllocator,
    math::planed_t clipPlane,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pBackOut,
    brush_solid_t *pFrontOut ) noexcept
{
    if ( pSrc == nullptr || pAllocator == nullptr ||
         pIdAllocator == nullptr || pBackOut == nullptr ||
         pFrontOut == nullptr || pBackOut == pFrontOut ||
         pSrc == pBackOut || pSrc == pFrontOut ||
         !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    geometry_status_t status = ValidateDestination( pBackOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateDestination( pFrontOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateSourceBrush( pSrc, policy.limits );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateClipPlane( clipPlane, policy.numerical );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_boundary_t sourceBoundary{};
    status = BrushBoundary_Init( &sourceBoundary, pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushBoundary_TryReconstruct(
        &sourceBoundary, pSrc, policy );
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &sourceBoundary );
        return status;
    }
    const brush_clip_classification_t classification =
        BrushClip_Classify(
            &sourceBoundary, clipPlane, policy.numerical );
    BrushBoundary_Shutdown( &sourceBoundary );
    if ( classification == brush_clip_classification_t::ALL_FRONT ||
         classification == brush_clip_classification_t::ALL_BACK ||
         classification == brush_clip_classification_t::ON_PLANE ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( classification != brush_clip_classification_t::INTERSECTS ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t backStaged{};
    brush_solid_t frontStaged{};

    status = TryCloneWithFreshIdentities(
        &backStaged, *pSrc, pAllocator, policy.limits, &stagedIds );
    if ( status == geometry_status_t::OK ) {
        status = BrushClip_TryClip(
            &backStaged, clipPlane, &stagedIds, policy );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryCloneWithFreshIdentities(
            &frontStaged, *pSrc, pAllocator, policy.limits, &stagedIds );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushClip_TryClip(
            &frontStaged,
            math::Planed_Flip( clipPlane ),
            &stagedIds,
            policy );
    }
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &frontStaged );
        BrushSolid_Shutdown( &backStaged );
        return status;
    }

    PublishBrush( pBackOut, &backStaged, false );
    PublishBrush( pFrontOut, &frontStaged, false );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
