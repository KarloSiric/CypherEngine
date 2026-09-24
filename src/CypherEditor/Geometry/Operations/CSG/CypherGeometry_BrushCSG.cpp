//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCSG.cpp
//  Purpose: Implements failure-atomic convex brush CSG operations.
//  Details: Every operation stages identity allocation and owned output.
//           Plane-set results are canonicalized before publication so every
//           retained side contributes exactly one boundary face.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//  - Hardened allocation, identity, and canonicalization contracts on
//    2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCSG.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_Kernel_ConvexHull.h"

#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

namespace
{

enum class projection_relation_t : common::u8 {
    SKIP = 0u,
    OVERLAPPING,
    TOUCHING,
    SEPARATED
};

bool BrushDestinationIsCanonicalEmpty(
    const brush_solid_t &brush ) noexcept
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

bool SubtractResultIsCanonicalEmpty(
    const brush_csg_subtract_result_t &result ) noexcept
{
    return result.fragments == nullptr &&
           result.cFragments == 0u &&
           result.cCapacity == 0u &&
           result.sideProvenance.pData == nullptr &&
           result.sideProvenance.nCount == 0u &&
           result.sideProvenance.nCapacity == 0u &&
           result.sideProvenance.pAllocator == nullptr &&
           result.pAllocator == nullptr;
}

void ResetSubtractResult(
    brush_csg_subtract_result_t *pResult ) noexcept
{
    pResult->fragments = nullptr;
    pResult->cFragments = 0u;
    pResult->cCapacity = 0u;
    pResult->pAllocator = nullptr;
}

geometry_status_t ValidateSubtractDestination(
    const brush_csg_subtract_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( SubtractResultIsCanonicalEmpty( *pResult ) ) {
        return geometry_status_t::OK;
    }
    if ( pResult->fragments != nullptr &&
         pResult->pAllocator != nullptr &&
         pResult->cCapacity > 0u &&
         pResult->cFragments <= pResult->cCapacity &&
         common::Vector_IsValid( &pResult->sideProvenance ) &&
         pResult->sideProvenance.pAllocator == pResult->pAllocator ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return geometry_status_t::CORRUPT_STATE;
}

bool BrushStorageIsUsable( const brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ||
         !GeometrySourceId_IsValid( pBrush->sourceId ) ||
         pBrush->sides.pAllocator == nullptr ||
         !common::Allocator_IsValid( pBrush->sides.pAllocator ) ||
         !common::Vector_IsValid( &pBrush->sides ) ||
         pBrush->sides.nCount < 4u ) {
        return false;
    }
    for ( common::usize iSide = 0u;
          iSide < pBrush->sides.nCount;
          ++iSide ) {
        const brush_solid_side_t &side = pBrush->sides.pData[iSide];
        if ( !GeometrySourceId_IsValid( side.sourceId ) ) {
            return false;
        }
    }
    return true;
}

bool BrushPlanesAreFinite( const brush_solid_t *pBrush ) noexcept
{
    for ( common::usize iSide = 0u;
          iSide < pBrush->sides.nCount;
          ++iSide ) {
        if ( !math::Planed_IsFinite(
                 pBrush->sides.pData[iSide].plane ) ) {
            return false;
        }
    }
    return true;
}

bool BoundaryStorageIsUsable(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ) {
        return false;
    }
    const common::allocator_t *pAllocator =
        pBoundary->vertices.pAllocator;
    return common::Allocator_IsValid( pAllocator ) &&
           pBoundary->edges.pAllocator == pAllocator &&
           pBoundary->faces.pAllocator == pAllocator &&
           pBoundary->faceVertexIndices.pAllocator == pAllocator &&
           common::Vector_IsValid( &pBoundary->vertices ) &&
           common::Vector_IsValid( &pBoundary->edges ) &&
           common::Vector_IsValid( &pBoundary->faces ) &&
           common::Vector_IsValid( &pBoundary->faceVertexIndices ) &&
           pBoundary->vertices.nCount >= 4u &&
           pBoundary->edges.nCount >= 6u &&
           pBoundary->faces.nCount >= 4u;
}

bool BoundaryCorrespondsToBrush(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    common::f64 tolerance ) noexcept
{
    if ( !BoundaryStorageIsUsable( pBoundary ) ||
         !BrushStorageIsUsable( pBrush ) ||
         !math::Scalar_IsFinite( tolerance ) || tolerance < 0.0 ||
         pBoundary->faces.nCount != pBrush->sides.nCount ) {
        return false;
    }

    const common::usize cVertices = pBoundary->vertices.nCount;
    const common::usize cSides = pBrush->sides.nCount;
    for ( common::usize iVertex = 0u;
          iVertex < cVertices;
          ++iVertex ) {
        const math::vec3d_t vertex =
            pBoundary->vertices.pData[iVertex];
        if ( !math::Vec3d_IsFinite( vertex ) ) {
            return false;
        }

        for ( common::usize iSide = 0u;
              iSide < cSides;
              ++iSide ) {
            const common::f64 distance = math::Planed_SignedDistance(
                pBrush->sides.pData[iSide].plane, vertex );
            if ( !math::Scalar_IsFinite( distance ) ||
                 distance > tolerance ) {
                return false;
            }
        }
    }

    for ( common::usize iEdge = 0u;
          iEdge < pBoundary->edges.nCount;
          ++iEdge ) {
        const brush_boundary_edge_t &edge =
            pBoundary->edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices ||
             edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return false;
        }
    }

    common::usize iExpectedFirstIndex = 0u;
    const common::usize cFaceIndices =
        pBoundary->faceVertexIndices.nCount;
    for ( common::usize iFace = 0u;
          iFace < pBoundary->faces.nCount;
          ++iFace ) {
        const brush_boundary_face_t &face =
            pBoundary->faces.pData[iFace];
        const common::usize iFirstIndex = face.iFirstIndex;
        const common::usize cFaceVertices = face.cVertices;
        if ( static_cast<common::usize>( face.iSide ) != iFace ||
             cFaceVertices < 3u ||
             iFirstIndex != iExpectedFirstIndex ||
             iFirstIndex > cFaceIndices ||
             cFaceVertices > cFaceIndices - iFirstIndex ) {
            return false;
        }

        const math::planed_t sidePlane =
            pBrush->sides.pData[face.iSide].plane;
        for ( common::usize i = 0u; i < cFaceVertices; ++i ) {
            const common::u32 iVertex =
                pBoundary->faceVertexIndices.pData[iFirstIndex + i];
            if ( iVertex >= cVertices ) {
                return false;
            }
            const common::f64 distance = math::Planed_SignedDistance(
                sidePlane, pBoundary->vertices.pData[iVertex] );
            if ( !math::Scalar_IsFinite( distance ) ||
                 math::Scalar_Abs( distance ) > tolerance ) {
                return false;
            }
        }
        iExpectedFirstIndex += cFaceVertices;
    }
    return iExpectedFirstIndex == cFaceIndices;
}

geometry_status_t ValidateCommonArguments(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    const geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy ) noexcept
{
    if ( pAllocator == nullptr || pIdAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !BrushStorageIsUsable( pBrushA ) ||
         !BrushStorageIsUsable( pBrushB ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t statusA =
        BrushValidation_Quick( pBrushA, policy );
    if ( statusA != geometry_status_t::OK ) {
        return statusA;
    }
    return BrushValidation_Quick( pBrushB, policy );
}

void MoveBrush(
    brush_solid_t *pDestination,
    brush_solid_t *pSource ) noexcept
{
    pDestination->sourceId = pSource->sourceId;
    common::Vector_Move( &pDestination->sides, &pSource->sides );
    pSource->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

void PublishSubtractResult(
    brush_csg_subtract_result_t *pDestination,
    brush_csg_subtract_result_t *pSource ) noexcept
{
    pDestination->fragments = pSource->fragments;
    pDestination->cFragments = pSource->cFragments;
    pDestination->cCapacity = pSource->cCapacity;
    common::Vector_Move(
        &pDestination->sideProvenance,
        &pSource->sideProvenance );
    pDestination->pAllocator = pSource->pAllocator;
    pSource->fragments = nullptr;
    pSource->cFragments = 0u;
    pSource->cCapacity = 0u;
    pSource->pAllocator = nullptr;
}

geometry_status_t AllocateSubtractStorage(
    brush_csg_subtract_result_t *pResult,
    common::usize cCapacity,
    const common::allocator_t *pAllocator,
    common::u64 cbScratchMax ) noexcept
{
    if ( pResult == nullptr || !SubtractResultIsCanonicalEmpty( *pResult ) ||
         pAllocator == nullptr || !common::Allocator_IsValid( pAllocator ) ||
         cCapacity == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::usize cbStorage = 0u;
    if ( !common::Cy_TryArrayByteCount<brush_solid_t>(
             cCapacity, cbStorage ) ||
         static_cast<common::u64>( cbStorage ) > cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    brush_solid_t *pFragments =
        common::Allocator_AllocateArrayStorage<brush_solid_t>(
            pAllocator, cCapacity );
    if ( pFragments == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize i = 0u; i < cCapacity; ++i ) {
        ::new ( static_cast<void *>( &pFragments[i] ) ) brush_solid_t{};
    }

    if ( !common::Vector_Init(
             &pResult->sideProvenance, pAllocator, 0u ) ) {
        for ( common::usize i = 0u; i < cCapacity; ++i ) {
            pFragments[i].~brush_solid_t();
        }
        common::Allocator_FreeArrayStorage(
            pAllocator, pFragments, cCapacity );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    pResult->fragments = pFragments;
    pResult->cCapacity = cCapacity;
    pResult->pAllocator = pAllocator;
    return geometry_status_t::OK;
}

geometry_status_t RecordGeneratedSideOrigin(
    brush_csg_subtract_result_t *pResult,
    geometry_source_id_t generatedSideId,
    const brush_solid_t &brushB,
    const brush_solid_side_t &sourceSide,
    brush_csg_side_origin_t origin ) noexcept
{
    const brush_csg_raw_side_provenance_t record{
        GEOMETRY_SOURCE_ID_INVALID,
        generatedSideId,
        brush_csg_operand_t::SUBTRAHEND_B,
        origin,
        brushB.sourceId,
        sourceSide.sourceId,
        sourceSide.iAttributeIndex
    };
    return common::Vector_PushBack(
               &pResult->sideProvenance, record )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t FinalizeSubtractProvenance(
    brush_csg_subtract_result_t *pResult,
    const brush_solid_t &brushA ) noexcept
{
    common::usize cSidesTotal = 0u;
    for ( common::usize iFragment = 0u;
          iFragment < pResult->cFragments;
          ++iFragment ) {
        const common::usize cSides =
            pResult->fragments[iFragment].sides.nCount;
        if ( cSides > common::CY_USIZE_MAX - cSidesTotal ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cSidesTotal += cSides;
    }

    common::vector_t<brush_csg_raw_side_provenance_t> finalized{};
    if ( !common::Vector_Init(
             &finalized, pResult->pAllocator, cSidesTotal ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iFragment = 0u;
          iFragment < pResult->cFragments;
          ++iFragment ) {
        const brush_solid_t &fragment =
            pResult->fragments[iFragment];
        for ( common::usize iSide = 0u;
              iSide < fragment.sides.nCount;
              ++iSide ) {
            const brush_solid_side_t &side =
                fragment.sides.pData[iSide];
            brush_csg_raw_side_provenance_t record{};
            bool bFound = false;

            for ( common::usize iSource = 0u;
                  iSource < brushA.sides.nCount;
                  ++iSource ) {
                const brush_solid_side_t &sourceSide =
                    brushA.sides.pData[iSource];
                if ( sourceSide.sourceId.value != side.sourceId.value ) {
                    continue;
                }
                record = {
                    fragment.sourceId,
                    side.sourceId,
                    brush_csg_operand_t::MINUEND_A,
                    brush_csg_side_origin_t::INHERITED_A,
                    brushA.sourceId,
                    sourceSide.sourceId,
                    sourceSide.iAttributeIndex
                };
                bFound = true;
                break;
            }

            for ( common::usize iGenerated = 0u;
                  iGenerated < pResult->sideProvenance.nCount;
                  ++iGenerated ) {
                const brush_csg_raw_side_provenance_t &generated =
                    pResult->sideProvenance.pData[iGenerated];
                if ( generated.sideId.value != side.sourceId.value ) {
                    continue;
                }
                if ( bFound ) {
                    common::Vector_Shutdown( &finalized );
                    return geometry_status_t::IDENTITY_CONFLICT;
                }
                record = generated;
                record.fragmentId = fragment.sourceId;
                bFound = true;
            }

            if ( !bFound ||
                 !common::Vector_PushBack( &finalized, record ) ) {
                common::Vector_Shutdown( &finalized );
                return bFound
                    ? geometry_status_t::ALLOCATION_FAILED
                    : geometry_status_t::CORRUPT_STATE;
            }
        }
    }

    common::Vector_Shutdown( &pResult->sideProvenance );
    common::Vector_Move( &pResult->sideProvenance, &finalized );
    return geometry_status_t::OK;
}

geometry_status_t TryCopyBrushWithFreshIdentity(
    const brush_solid_t *pSource,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits,
    geometry_source_id_allocator_t *pIds,
    brush_solid_t *pDestination ) noexcept
{
    geometry_status_t status = BrushSolid_DeepCopy(
        pDestination, pSource, pAllocator, limits );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const geometry_source_id_result_t brushId =
        GeometrySourceIdAllocator_Allocate( pIds );
    if ( brushId.status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pDestination );
        return brushId.status;
    }
    pDestination->sourceId = brushId.id;
    return geometry_status_t::OK;
}

geometry_status_t TryPublishSingleFragmentCopy(
    const brush_solid_t *pSource,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIds,
    brush_csg_subtract_result_t *pResultOut ) noexcept
{
    brush_csg_subtract_result_t pending{};
    geometry_status_t status = AllocateSubtractStorage(
        &pending, 1u, pAllocator, policy.limits.cbScratchMax );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    status = TryCopyBrushWithFreshIdentity(
        pSource, pAllocator, policy.limits, pIds,
        &pending.fragments[0] );
    if ( status != geometry_status_t::OK ) {
        BrushCSGSubtractResult_Shutdown( &pending );
        return status;
    }
    pending.cFragments = 1u;
    status = FinalizeSubtractProvenance( &pending, *pSource );
    if ( status != geometry_status_t::OK ) {
        BrushCSGSubtractResult_Shutdown( &pending );
        return status;
    }
    PublishSubtractResult( pResultOut, &pending );
    return geometry_status_t::OK;
}

bool ArePlanesEquivalent(
    math::planed_t a,
    math::planed_t b,
    const geometry_numerical_policy_t &numerical ) noexcept
{
    const math::vec3d_t normalDelta =
        math::Vec3d_Subtract( a.normal, b.normal );
    const common::f64 normalTolerance =
        numerical.fAngularToleranceRadians;
    return math::Vec3d_LengthSquared( normalDelta ) <=
               normalTolerance * normalTolerance &&
           math::Scalar_Abs( a.d - b.d ) <=
               numerical.fCoplanarDistanceTolerance;
}

bool TryProjectBoundary(
    const brush_boundary_t *pBoundary,
    math::vec3d_t axis,
    common::f64 *pMinimumOut,
    common::f64 *pMaximumOut ) noexcept
{
    const common::usize cVertices = pBoundary->vertices.nCount;
    common::f64 minimum = math::Vec3d_Dot(
        pBoundary->vertices.pData[0], axis );
    common::f64 maximum = minimum;
    if ( !math::Scalar_IsFinite( minimum ) ) {
        return false;
    }

    for ( common::usize i = 1u; i < cVertices; ++i ) {
        const common::f64 projection = math::Vec3d_Dot(
            pBoundary->vertices.pData[i], axis );
        if ( !math::Scalar_IsFinite( projection ) ) {
            return false;
        }
        if ( projection < minimum ) { minimum = projection; }
        if ( projection > maximum ) { maximum = projection; }
    }

    *pMinimumOut = minimum;
    *pMaximumOut = maximum;
    return true;
}

bool TryProjectionRelation(
    const brush_boundary_t *pBoundaryA,
    const brush_boundary_t *pBoundaryB,
    math::vec3d_t axis,
    common::f64 tolerance,
    projection_relation_t *pRelationOut ) noexcept
{
    if ( pRelationOut == nullptr || !math::Vec3d_IsFinite( axis ) ) {
        return false;
    }

    const common::f64 lengthSquared = math::Vec3d_LengthSquared( axis );
    if ( !math::Scalar_IsFinite( lengthSquared ) ) {
        return false;
    }
    if ( lengthSquared <= tolerance * tolerance ) {
        *pRelationOut = projection_relation_t::SKIP;
        return true;
    }

    common::f64 minimumA = 0.0;
    common::f64 maximumA = 0.0;
    common::f64 minimumB = 0.0;
    common::f64 maximumB = 0.0;
    if ( !TryProjectBoundary(
             pBoundaryA, axis, &minimumA, &maximumA ) ||
         !TryProjectBoundary(
             pBoundaryB, axis, &minimumB, &maximumB ) ) {
        return false;
    }

    const common::f64 scaledTolerance =
        tolerance * std::sqrt( lengthSquared );
    if ( !math::Scalar_IsFinite( scaledTolerance ) ) {
        return false;
    }

    if ( maximumA < minimumB - scaledTolerance ||
         maximumB < minimumA - scaledTolerance ) {
        *pRelationOut = projection_relation_t::SEPARATED;
        return true;
    }

    const common::f64 overlap =
        ( maximumA < maximumB ? maximumA : maximumB ) -
        ( minimumA > minimumB ? minimumA : minimumB );
    *pRelationOut = overlap <= scaledTolerance
        ? projection_relation_t::TOUCHING
        : projection_relation_t::OVERLAPPING;
    return true;
}

bool TryTestSeparatingAxes(
    const brush_solid_t *pBrushA,
    const brush_boundary_t *pBoundaryA,
    const brush_solid_t *pBrushB,
    const brush_boundary_t *pBoundaryB,
    common::f64 tolerance,
    bool *pSeparatedOut,
    bool *pTouchingOut ) noexcept
{
    *pSeparatedOut = false;
    *pTouchingOut = false;

    const brush_solid_t *brushes[2]{ pBrushA, pBrushB };
    for ( common::usize iBrush = 0u; iBrush < 2u; ++iBrush ) {
        const common::usize cSides =
            BrushSolid_SideCount( brushes[iBrush] );
        for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
            projection_relation_t relation{};
            if ( !TryProjectionRelation(
                     pBoundaryA, pBoundaryB,
                     brushes[iBrush]->sides.pData[iSide].plane.normal,
                     tolerance, &relation ) ) {
                return false;
            }
            if ( relation == projection_relation_t::SEPARATED ) {
                *pSeparatedOut = true;
                return true;
            }
            *pTouchingOut = *pTouchingOut ||
                relation == projection_relation_t::TOUCHING;
        }
    }

    for ( common::usize iEdgeA = 0u;
          iEdgeA < pBoundaryA->edges.nCount;
          ++iEdgeA ) {
        const brush_boundary_edge_t &edgeA =
            pBoundaryA->edges.pData[iEdgeA];
        if ( edgeA.iVertex0 >= pBoundaryA->vertices.nCount ||
             edgeA.iVertex1 >= pBoundaryA->vertices.nCount ) {
            return false;
        }
        const math::vec3d_t directionA = math::Vec3d_Subtract(
            pBoundaryA->vertices.pData[edgeA.iVertex1],
            pBoundaryA->vertices.pData[edgeA.iVertex0] );

        for ( common::usize iEdgeB = 0u;
              iEdgeB < pBoundaryB->edges.nCount;
              ++iEdgeB ) {
            const brush_boundary_edge_t &edgeB =
                pBoundaryB->edges.pData[iEdgeB];
            if ( edgeB.iVertex0 >= pBoundaryB->vertices.nCount ||
                 edgeB.iVertex1 >= pBoundaryB->vertices.nCount ) {
                return false;
            }
            const math::vec3d_t directionB = math::Vec3d_Subtract(
                pBoundaryB->vertices.pData[edgeB.iVertex1],
                pBoundaryB->vertices.pData[edgeB.iVertex0] );
            const math::vec3d_t axis =
                math::Vec3d_Cross( directionA, directionB );

            projection_relation_t relation{};
            if ( !TryProjectionRelation(
                     pBoundaryA, pBoundaryB, axis,
                     tolerance, &relation ) ) {
                return false;
            }
            if ( relation == projection_relation_t::SEPARATED ) {
                *pSeparatedOut = true;
                return true;
            }
            *pTouchingOut = *pTouchingOut ||
                relation == projection_relation_t::TOUCHING;
        }
    }
    return true;
}

int ClassifyVertexAgainstBrush(
    math::vec3d_t vertex,
    const brush_solid_t *pBrush,
    common::f64 tolerance ) noexcept
{
    bool bOnAnyPlane = false;
    for ( common::usize i = 0u;
          i < pBrush->sides.nCount;
          ++i ) {
        const common::f64 distance = math::Planed_SignedDistance(
            pBrush->sides.pData[i].plane, vertex );
        if ( distance > tolerance ) {
            return -1;
        }
        if ( distance >= -tolerance ) {
            bOnAnyPlane = true;
        }
    }
    return bOnAnyPlane ? 0 : 1;
}

struct vertex_census_t {
    common::usize cInside{ 0u };
    common::usize cOutside{ 0u };
    common::usize cOnSurface{ 0u };
};

vertex_census_t CensusVerticesAgainstBrush(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    common::f64 tolerance ) noexcept
{
    vertex_census_t census{};
    for ( common::usize i = 0u;
          i < pBoundary->vertices.nCount;
          ++i ) {
        const int classification = ClassifyVertexAgainstBrush(
            pBoundary->vertices.pData[i], pBrush, tolerance );
        if ( classification > 0 ) { ++census.cInside; }
        else if ( classification < 0 ) { ++census.cOutside; }
        else { ++census.cOnSurface; }
    }
    return census;
}

geometry_status_t TryReconstructOperands(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    brush_boundary_t *pBoundaryA,
    brush_boundary_t *pBoundaryB ) noexcept
{
    geometry_status_t status = BrushBoundary_Init(
        pBoundaryA, pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushBoundary_Init( pBoundaryB, pAllocator );
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( pBoundaryA );
        return status;
    }
    status = BrushBoundary_TryReconstruct(
        pBoundaryA, pBrushA, policy );
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_TryReconstruct(
            pBoundaryB, pBrushB, policy );
    }
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( pBoundaryB );
        BrushBoundary_Shutdown( pBoundaryA );
    }
    return status;
}

geometry_status_t ValidateSubtractComplexity(
    common::usize cSidesA,
    common::usize cSidesB,
    const geometry_policy_t &policy ) noexcept
{
    if ( cSidesA > common::CY_USIZE_MAX - cSidesB ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cIntermediateSides = cSidesA + cSidesB;
    if ( static_cast<common::u64>( cIntermediateSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const common::u64 a = static_cast<common::u64>( cSidesA );
    const common::u64 b = static_cast<common::u64>( cSidesB );
    if ( b != 0u && a > common::CY_U64_MAX / b ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::u64 copiedSides = a * b;
    if ( b == common::CY_U64_MAX ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::u64 bPlusOne = b + 1u;
    const common::u64 triangular = ( b % 2u == 0u )
        ? ( b / 2u ) * bPlusOne
        : b * ( bPlusOne / 2u );
    if ( copiedSides > common::CY_U64_MAX - triangular ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::u64 outputSideUpperBound = copiedSides + triangular;
    if ( outputSideUpperBound > policy.limits.cBrushSidesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    common::usize cbFragments = 0u;
    if ( !common::Cy_TryArrayByteCount<brush_solid_t>(
             cSidesB, cbFragments ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( outputSideUpperBound >
         common::CY_U64_MAX / sizeof( brush_solid_side_t ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::u64 cbOutputSides =
        outputSideUpperBound * sizeof( brush_solid_side_t );
    if ( cbOutputSides > policy.limits.cbScratchMax ||
         static_cast<common::u64>( cbFragments ) >
             policy.limits.cbScratchMax - cbOutputSides ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

} // namespace

brush_csg_classification_t BrushCSG_Classify(
    const brush_solid_t *pBrushA,
    const brush_boundary_t *pBoundaryA,
    const brush_solid_t *pBrushB,
    const brush_boundary_t *pBoundaryB,
    common::f64 tolerance ) noexcept
{
    if ( !BrushStorageIsUsable( pBrushA ) ||
         !BrushStorageIsUsable( pBrushB ) ||
         !BrushPlanesAreFinite( pBrushA ) ||
         !BrushPlanesAreFinite( pBrushB ) ||
         !BoundaryStorageIsUsable( pBoundaryA ) ||
         !BoundaryStorageIsUsable( pBoundaryB ) ||
         !math::Scalar_IsFinite( tolerance ) || tolerance < 0.0 ) {
        return brush_csg_classification_t::INVALID;
    }
    if ( !BoundaryCorrespondsToBrush(
             pBoundaryA, pBrushA, tolerance ) ||
         !BoundaryCorrespondsToBrush(
             pBoundaryB, pBrushB, tolerance ) ) {
        return brush_csg_classification_t::INVALID;
    }

    bool bSeparated = false;
    bool bTouching = false;
    if ( !TryTestSeparatingAxes(
             pBrushA, pBoundaryA, pBrushB, pBoundaryB,
             tolerance, &bSeparated, &bTouching ) ) {
        return brush_csg_classification_t::INVALID;
    }
    if ( bSeparated ) {
        return brush_csg_classification_t::DISJOINT;
    }

    const vertex_census_t censusAvsB = CensusVerticesAgainstBrush(
        pBoundaryA, pBrushB, tolerance );
    const vertex_census_t censusBvsA = CensusVerticesAgainstBrush(
        pBoundaryB, pBrushA, tolerance );

    if ( censusAvsB.cOutside == 0u ) {
        return brush_csg_classification_t::A_INSIDE_B;
    }
    if ( censusBvsA.cOutside == 0u ) {
        return brush_csg_classification_t::B_INSIDE_A;
    }
    return bTouching
        ? brush_csg_classification_t::TOUCHING
        : brush_csg_classification_t::INTERSECTS;
}

geometry_status_t BrushCSG_TryIntersect(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pResultOut ) noexcept
{
    geometry_status_t status = ValidateBrushDestination( pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateCommonArguments(
        pBrushA, pBrushB, pAllocator, pIdAllocator, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_boundary_t boundaryA{};
    brush_boundary_t boundaryB{};
    status = TryReconstructOperands(
        pBrushA, pBrushB, pAllocator, policy,
        &boundaryA, &boundaryB );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const brush_csg_classification_t classification = BrushCSG_Classify(
        pBrushA, &boundaryA, pBrushB, &boundaryB,
        policy.numerical.fCoplanarDistanceTolerance );
    BrushBoundary_Shutdown( &boundaryB );
    BrushBoundary_Shutdown( &boundaryA );

    if ( classification == brush_csg_classification_t::INVALID ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( classification == brush_csg_classification_t::DISJOINT ||
         classification == brush_csg_classification_t::TOUCHING ) {
        return geometry_status_t::DEGENERATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t stagedBrush{};

    if ( classification == brush_csg_classification_t::A_INSIDE_B ||
         classification == brush_csg_classification_t::B_INSIDE_A ) {
        const brush_solid_t *pContained =
            classification == brush_csg_classification_t::A_INSIDE_B
            ? pBrushA : pBrushB;
        status = TryCopyBrushWithFreshIdentity(
            pContained, pAllocator, policy.limits,
            &stagedIds, &stagedBrush );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        MoveBrush( pResultOut, &stagedBrush );
        *pIdAllocator = stagedIds;
        return geometry_status_t::OK;
    }

    const common::usize cSidesA = BrushSolid_SideCount( pBrushA );
    const common::usize cSidesB = BrushSolid_SideCount( pBrushB );
    if ( cSidesA > common::CY_USIZE_MAX - cSidesB ||
         static_cast<common::u64>( cSidesA + cSidesB ) >
             policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const geometry_source_id_result_t brushId =
        GeometrySourceIdAllocator_Allocate( &stagedIds );
    if ( brushId.status != geometry_status_t::OK ) {
        return brushId.status;
    }
    status = BrushSolid_Init( &stagedBrush, pAllocator, brushId.id );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve(
        &stagedBrush, policy.limits, cSidesA + cSidesB );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &stagedBrush );
        return status;
    }

    const brush_solid_t *operands[2]{ pBrushA, pBrushB };
    for ( common::usize iOperand = 0u; iOperand < 2u; ++iOperand ) {
        for ( common::usize iSide = 0u;
              iSide < operands[iOperand]->sides.nCount;
              ++iSide ) {
            const brush_solid_side_t side =
                operands[iOperand]->sides.pData[iSide];
            bool bDuplicate = false;
            for ( common::usize iExisting = 0u;
                  iExisting < stagedBrush.sides.nCount;
                  ++iExisting ) {
                if ( ArePlanesEquivalent(
                         side.plane,
                         stagedBrush.sides.pData[iExisting].plane,
                         policy.numerical ) ) {
                    bDuplicate = true;
                    break;
                }
            }
            if ( bDuplicate ) {
                continue;
            }
            status = BrushSolid_TryAddSide(
                &stagedBrush, policy.limits, side, nullptr );
            if ( status != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &stagedBrush );
                return status;
            }
        }
    }

    status = BrushSolid_TryCanonicalizeSides( &stagedBrush, policy );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &stagedBrush );
        return status;
    }

    MoveBrush( pResultOut, &stagedBrush );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

void BrushCSGSubtractResult_Shutdown(
    brush_csg_subtract_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return;
    }

    if ( pResult->fragments != nullptr &&
         pResult->pAllocator != nullptr &&
         pResult->cCapacity > 0u &&
         common::Allocator_IsValid( pResult->pAllocator ) ) {
        for ( common::usize i = 0u;
              i < pResult->cCapacity;
              ++i ) {
            pResult->fragments[i].~brush_solid_t();
        }
        common::Allocator_FreeArrayStorage(
            pResult->pAllocator,
            pResult->fragments,
            pResult->cCapacity );
    }
    if ( pResult->sideProvenance.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pResult->sideProvenance );
    }
    ResetSubtractResult( pResult );
}

geometry_status_t BrushCSG_TrySubtract(
    const brush_solid_t *pBrushA,
    const brush_solid_t *pBrushB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_subtract_result_t *pResultOut ) noexcept
{
    geometry_status_t status = ValidateSubtractDestination( pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateCommonArguments(
        pBrushA, pBrushB, pAllocator, pIdAllocator, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_boundary_t boundaryA{};
    brush_boundary_t boundaryB{};
    status = TryReconstructOperands(
        pBrushA, pBrushB, pAllocator, policy,
        &boundaryA, &boundaryB );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const brush_csg_classification_t classification = BrushCSG_Classify(
        pBrushA, &boundaryA, pBrushB, &boundaryB,
        policy.numerical.fCoplanarDistanceTolerance );
    BrushBoundary_Shutdown( &boundaryB );
    BrushBoundary_Shutdown( &boundaryA );

    if ( classification == brush_csg_classification_t::INVALID ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    if ( classification == brush_csg_classification_t::DISJOINT ||
         classification == brush_csg_classification_t::TOUCHING ) {
        status = TryPublishSingleFragmentCopy(
            pBrushA, pAllocator, policy, &stagedIds, pResultOut );
        if ( status == geometry_status_t::OK ) {
            *pIdAllocator = stagedIds;
        }
        return status;
    }
    if ( classification == brush_csg_classification_t::A_INSIDE_B ) {
        return geometry_status_t::DEGENERATE;
    }

    const common::usize cSidesA = BrushSolid_SideCount( pBrushA );
    const common::usize cSidesB = BrushSolid_SideCount( pBrushB );
    status = ValidateSubtractComplexity( cSidesA, cSidesB, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_csg_subtract_result_t pending{};
    status = AllocateSubtractStorage(
        &pending, cSidesB, pAllocator,
        policy.limits.cbScratchMax );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_solid_t remainder{};
    status = BrushSolid_DeepCopy(
        &remainder, pBrushA, pAllocator, policy.limits );
    if ( status != geometry_status_t::OK ) {
        BrushCSGSubtractResult_Shutdown( &pending );
        return status;
    }

    bool bHasRemainder = true;
    for ( common::usize iSideB = 0u;
          iSideB < cSidesB && bHasRemainder;
          ++iSideB ) {
        const brush_solid_side_t &sideB =
            pBrushB->sides.pData[iSideB];

        brush_solid_t candidate{};
        status = BrushSolid_DeepCopy(
            &candidate, &remainder, pAllocator, policy.limits );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return status;
        }

        const geometry_source_id_result_t exteriorSideId =
            GeometrySourceIdAllocator_Allocate( &stagedIds );
        if ( exteriorSideId.status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &candidate );
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return exteriorSideId.status;
        }
        status = RecordGeneratedSideOrigin(
            &pending,
            exteriorSideId.id,
            *pBrushB,
            sideB,
            brush_csg_side_origin_t::CUT_FROM_B_EXTERIOR );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &candidate );
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return status;
        }
        const brush_solid_side_t exteriorSide{
            math::Planed_Flip( sideB.plane ),
            exteriorSideId.id,
            sideB.iAttributeIndex
        };
        status = BrushSolid_TryAddSide(
            &candidate, policy.limits, exteriorSide, nullptr );
        if ( status == geometry_status_t::OK ) {
            status = BrushSolid_TryCanonicalizeSides(
                &candidate, policy );
        }

        if ( status == geometry_status_t::OK ) {
            const geometry_source_id_result_t fragmentId =
                GeometrySourceIdAllocator_Allocate( &stagedIds );
            if ( fragmentId.status != geometry_status_t::OK ) {
                BrushSolid_Shutdown( &candidate );
                BrushSolid_Shutdown( &remainder );
                BrushCSGSubtractResult_Shutdown( &pending );
                return fragmentId.status;
            }
            candidate.sourceId = fragmentId.id;
            MoveBrush(
                &pending.fragments[pending.cFragments],
                &candidate );
            ++pending.cFragments;
        }
        else {
            BrushSolid_Shutdown( &candidate );
            if ( status != geometry_status_t::DEGENERATE ) {
                BrushSolid_Shutdown( &remainder );
                BrushCSGSubtractResult_Shutdown( &pending );
                return status;
            }
        }

        brush_solid_t nextRemainder{};
        status = BrushSolid_DeepCopy(
            &nextRemainder, &remainder, pAllocator, policy.limits );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return status;
        }

        const geometry_source_id_result_t interiorSideId =
            GeometrySourceIdAllocator_Allocate( &stagedIds );
        if ( interiorSideId.status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &nextRemainder );
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return interiorSideId.status;
        }
        status = RecordGeneratedSideOrigin(
            &pending,
            interiorSideId.id,
            *pBrushB,
            sideB,
            brush_csg_side_origin_t::PARTITION_FROM_B_INTERIOR );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &nextRemainder );
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return status;
        }
        const brush_solid_side_t interiorSide{
            sideB.plane,
            interiorSideId.id,
            sideB.iAttributeIndex
        };
        status = BrushSolid_TryAddSide(
            &nextRemainder, policy.limits, interiorSide, nullptr );
        if ( status == geometry_status_t::OK ) {
            status = BrushSolid_TryCanonicalizeSides(
                &nextRemainder, policy );
        }

        if ( status == geometry_status_t::DEGENERATE ) {
            BrushSolid_Shutdown( &nextRemainder );
            BrushSolid_Shutdown( &remainder );
            bHasRemainder = false;
            break;
        }
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &nextRemainder );
            BrushSolid_Shutdown( &remainder );
            BrushCSGSubtractResult_Shutdown( &pending );
            return status;
        }

        BrushSolid_Shutdown( &remainder );
        MoveBrush( &remainder, &nextRemainder );
    }

    if ( bHasRemainder ) {
        BrushSolid_Shutdown( &remainder );
    }
    if ( pending.cFragments == 0u ) {
        BrushCSGSubtractResult_Shutdown( &pending );
        return geometry_status_t::DEGENERATE;
    }

    status = FinalizeSubtractProvenance( &pending, *pBrushA );
    if ( status != geometry_status_t::OK ) {
        BrushCSGSubtractResult_Shutdown( &pending );
        return status;
    }

    PublishSubtractResult( pResultOut, &pending );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

geometry_status_t BrushCSG_TryHollow(
    const brush_solid_t *pBrush,
    common::f64 wallThickness,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_subtract_result_t *pResultOut ) noexcept
{
    geometry_status_t status = ValidateSubtractDestination( pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( pAllocator == nullptr || pIdAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ||
         !BrushStorageIsUsable( pBrush ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    status = BrushValidation_Quick( pBrush, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !math::Scalar_IsFinite( wallThickness ) ||
         wallThickness <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t innerBrush{};
    status = BrushSolid_Init(
        &innerBrush, pAllocator, pBrush->sourceId );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve(
        &innerBrush, policy.limits, pBrush->sides.nCount );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &innerBrush );
        return status;
    }

    for ( common::usize iSide = 0u;
          iSide < pBrush->sides.nCount;
          ++iSide ) {
        brush_solid_side_t side = pBrush->sides.pData[iSide];
        side.plane.d += wallThickness;
        if ( !math::Scalar_IsFinite( side.plane.d ) ) {
            BrushSolid_Shutdown( &innerBrush );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( math::Scalar_Abs( side.plane.d ) >
             policy.numerical.fCoordinateMagnitudeLimit ) {
            BrushSolid_Shutdown( &innerBrush );
            return geometry_status_t::LIMIT_EXCEEDED;
        }

        status = BrushSolid_TryAddSide(
            &innerBrush, policy.limits, side, nullptr );
        if ( status != geometry_status_t::OK ) {
            BrushSolid_Shutdown( &innerBrush );
            return status;
        }
    }

    status = BrushSolid_TryCanonicalizeSides( &innerBrush, policy );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &innerBrush );
        return status;
    }

    status = BrushCSG_TrySubtract(
        pBrush, &innerBrush, pAllocator,
        &stagedIds, policy, pResultOut );
    BrushSolid_Shutdown( &innerBrush );
    if ( status == geometry_status_t::OK ) {
        *pIdAllocator = stagedIds;
    }
    return status;
}

geometry_status_t BrushCSG_TryMerge(
    const brush_solid_t *pBrushA,
    const brush_boundary_t *pBoundaryA,
    const brush_solid_t *pBrushB,
    const brush_boundary_t *pBoundaryB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pResultOut ) noexcept
{
    geometry_status_t status = ValidateBrushDestination( pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateCommonArguments(
        pBrushA, pBrushB, pAllocator, pIdAllocator, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !BoundaryStorageIsUsable( pBoundaryA ) ||
         !BoundaryStorageIsUsable( pBoundaryB ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::f64 correspondenceTolerance =
        policy.numerical.fCoplanarDistanceTolerance;
    if ( !BoundaryCorrespondsToBrush(
             pBoundaryA, pBrushA, correspondenceTolerance ) ||
         !BoundaryCorrespondsToBrush(
             pBoundaryB, pBrushB, correspondenceTolerance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const brush_csg_classification_t classification = BrushCSG_Classify(
        pBrushA, pBoundaryA, pBrushB, pBoundaryB,
        correspondenceTolerance );
    if ( classification == brush_csg_classification_t::INVALID ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( classification == brush_csg_classification_t::DISJOINT ) {
        return geometry_status_t::DEGENERATE;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    brush_solid_t stagedBrush{};
    if ( classification == brush_csg_classification_t::A_INSIDE_B ||
         classification == brush_csg_classification_t::B_INSIDE_A ) {
        const brush_solid_t *pContaining =
            classification == brush_csg_classification_t::A_INSIDE_B
            ? pBrushB : pBrushA;
        status = TryCopyBrushWithFreshIdentity(
            pContaining, pAllocator, policy.limits,
            &stagedIds, &stagedBrush );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        MoveBrush( pResultOut, &stagedBrush );
        *pIdAllocator = stagedIds;
        return geometry_status_t::OK;
    }

    const common::usize cVerticesA = pBoundaryA->vertices.nCount;
    const common::usize cVerticesB = pBoundaryB->vertices.nCount;
    if ( cVerticesA > common::CY_USIZE_MAX - cVerticesB ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cPoints = cVerticesA + cVerticesB;
    if ( cPoints < 4u ||
         static_cast<common::u64>( cPoints ) >
             policy.limits.cVerticesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    common::usize cbPoints = 0u;
    if ( !common::Cy_TryArrayByteCount<math::vec3d_t>(
             cPoints, cbPoints ) ||
         static_cast<common::u64>( cbPoints ) >
             policy.limits.cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    math::vec3d_t *pPoints =
        common::Allocator_AllocateArrayStorage<math::vec3d_t>(
            pAllocator, cPoints );
    if ( pPoints == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cVerticesA; ++i ) {
        pPoints[i] = pBoundaryA->vertices.pData[i];
    }
    for ( common::usize i = 0u; i < cVerticesB; ++i ) {
        pPoints[cVerticesA + i] = pBoundaryB->vertices.pData[i];
    }

    status = ConvexHull_TryBuildBrush(
        &stagedBrush, pAllocator, policy,
        &stagedIds, pPoints, cPoints );
    common::Allocator_FreeArrayStorage(
        pAllocator, pPoints, cPoints );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_boundary_t hullBoundary{};
    status = BrushBoundary_Init( &hullBoundary, pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_TryReconstruct(
            &hullBoundary, &stagedBrush, policy );
    }
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &hullBoundary );
        BrushSolid_Shutdown( &stagedBrush );
        return status;
    }

    for ( common::usize iHull = 0u;
          iHull < stagedBrush.sides.nCount;
          ++iHull ) {
        const math::planed_t hullPlane =
            stagedBrush.sides.pData[iHull].plane;
        const brush_solid_side_t *pMatch = nullptr;

        for ( common::usize iSide = 0u;
              iSide < pBrushA->sides.nCount && pMatch == nullptr;
              ++iSide ) {
            if ( ArePlanesEquivalent(
                     hullPlane,
                     pBrushA->sides.pData[iSide].plane,
                     policy.numerical ) ) {
                pMatch = &pBrushA->sides.pData[iSide];
            }
        }
        for ( common::usize iSide = 0u;
              iSide < pBrushB->sides.nCount && pMatch == nullptr;
              ++iSide ) {
            if ( ArePlanesEquivalent(
                     hullPlane,
                     pBrushB->sides.pData[iSide].plane,
                     policy.numerical ) ) {
                pMatch = &pBrushB->sides.pData[iSide];
            }
        }

        if ( pMatch == nullptr ) {
            BrushBoundary_Shutdown( &hullBoundary );
            BrushSolid_Shutdown( &stagedBrush );
            return geometry_status_t::DEGENERATE;
        }
        stagedBrush.sides.pData[iHull].sourceId = pMatch->sourceId;
        stagedBrush.sides.pData[iHull].iAttributeIndex =
            pMatch->iAttributeIndex;
    }

    BrushBoundary_Shutdown( &hullBoundary );
    MoveBrush( pResultOut, &stagedBrush );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
