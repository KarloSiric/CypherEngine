//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsgAdoption.cpp
//  Purpose: Implements document-ready brush subtraction result preparation.
//  Details: Construction is fully staged so identity allocation and owned
//           output publish together only after topology, ancestry, and
//           surface records have all validated.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCsgAdoption.h"

#include "CypherGeometry_BrushValidation.h"

#include <limits>
#include <new>

namespace cypher::editor::geometry
{

namespace
{

bool ResultIsCanonicalEmpty(
    const brush_csg_adoption_result_t &result ) noexcept
{
    return result.pFragments == nullptr &&
           result.pAttributeStores == nullptr &&
           result.cFragments == 0u &&
           result.cCapacity == 0u &&
           result.sideProvenance.pData == nullptr &&
           result.sideProvenance.nCount == 0u &&
           result.sideProvenance.nCapacity == 0u &&
           result.sideProvenance.pAllocator == nullptr &&
           result.pAllocator == nullptr;
}

geometry_status_t ValidateResultDestination(
    const brush_csg_adoption_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( ResultIsCanonicalEmpty( *pResult ) ) {
        return geometry_status_t::OK;
    }
    if ( pResult->pFragments != nullptr &&
         pResult->pAttributeStores != nullptr &&
         pResult->cCapacity > 0u &&
         pResult->cFragments <= pResult->cCapacity &&
         pResult->pAllocator != nullptr &&
         common::Allocator_IsValid( pResult->pAllocator ) &&
         common::Vector_IsValid( &pResult->sideProvenance ) &&
         pResult->sideProvenance.pAllocator == pResult->pAllocator ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return geometry_status_t::CORRUPT_STATE;
}

void ResetResult( brush_csg_adoption_result_t *pResult ) noexcept
{
    pResult->pFragments = nullptr;
    pResult->pAttributeStores = nullptr;
    pResult->cFragments = 0u;
    pResult->cCapacity = 0u;
    pResult->pAllocator = nullptr;
}

struct resolved_side_source_t {
    brush_csg_operand_t operand{ brush_csg_operand_t::MINUEND_A };
    brush_csg_side_origin_t origin{
        brush_csg_side_origin_t::INHERITED_A };
    const brush_solid_t *pBrush{ nullptr };
    const geometry_brush_side_attribute_store_t *pAttributes{ nullptr };
    const brush_solid_side_t *pSide{ nullptr };
};

geometry_status_t ResolveSideSource(
    const brush_csg_raw_side_provenance_t &provenance,
    const brush_solid_t &brushA,
    const geometry_brush_side_attribute_store_t &attributesA,
    const brush_solid_t &brushB,
    const geometry_brush_side_attribute_store_t &attributesB,
    resolved_side_source_t *pSourceOut ) noexcept
{
    if ( pSourceOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( provenance.operand != brush_csg_operand_t::MINUEND_A &&
         provenance.operand != brush_csg_operand_t::SUBTRAHEND_B ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const bool bFromA =
        provenance.operand == brush_csg_operand_t::MINUEND_A;
    const bool bValidOrigin = bFromA
        ? provenance.origin == brush_csg_side_origin_t::INHERITED_A
        : provenance.origin == brush_csg_side_origin_t::CUT_FROM_B_EXTERIOR ||
          provenance.origin ==
              brush_csg_side_origin_t::PARTITION_FROM_B_INTERIOR;
    if ( !bValidOrigin ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const brush_solid_t &sourceBrush = bFromA ? brushA : brushB;
    const geometry_brush_side_attribute_store_t &sourceAttributes =
        bFromA ? attributesA : attributesB;
    if ( provenance.sourceBrushId.value != sourceBrush.sourceId.value ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    for ( common::usize i = 0u; i < sourceBrush.sides.nCount; ++i ) {
        const brush_solid_side_t &sourceSide =
            sourceBrush.sides.pData[i];
        if ( sourceSide.sourceId.value !=
             provenance.sourceSideId.value ) {
            continue;
        }
        if ( sourceSide.iAttributeIndex !=
             provenance.iSourceAttribute ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        *pSourceOut = {
            provenance.operand,
            provenance.origin,
            &sourceBrush,
            &sourceAttributes,
            &sourceSide
        };
        return geometry_status_t::OK;
    }
    return geometry_status_t::CORRUPT_STATE;
}

geometry_status_t ValidateOperand(
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const geometry_policy_t &policy ) noexcept
{
    const geometry_status_t brushStatus =
        BrushValidation_Quick( pBrush, policy );
    if ( brushStatus != geometry_status_t::OK ) {
        return brushStatus;
    }
    const geometry_status_t attributeStatus =
        BrushSideAttributeStore_Validate( pAttributes, policy );
    if ( attributeStatus != geometry_status_t::OK ) {
        return attributeStatus;
    }

    for ( common::usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        geometry_brush_side_attributes_t ignored{};
        if ( BrushSideAttributeStore_TryGet(
                 pAttributes,
                 pBrush->sides.pData[i].iAttributeIndex,
                 &ignored ) != geometry_status_t::OK ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateRawResult(
    const brush_csg_subtract_result_t *pRawResult,
    const geometry_policy_t &policy,
    common::usize *pSideCountOut ) noexcept
{
    if ( pRawResult == nullptr || pSideCountOut == nullptr ||
         pRawResult->fragments == nullptr ||
         pRawResult->pAllocator == nullptr ||
         !common::Allocator_IsValid( pRawResult->pAllocator ) ||
         pRawResult->cFragments == 0u ||
         pRawResult->cFragments > pRawResult->cCapacity ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::Vector_IsValid( &pRawResult->sideProvenance ) ||
         pRawResult->sideProvenance.pAllocator !=
             pRawResult->pAllocator ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( static_cast<common::u64>( pRawResult->cFragments ) >
         policy.limits.cBrushesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    common::usize cSidesTotal = 0u;
    for ( common::usize i = 0u;
          i < pRawResult->cFragments;
          ++i ) {
        const brush_solid_t &fragment = pRawResult->fragments[i];
        const geometry_status_t status =
            BrushValidation_Quick( &fragment, policy );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        if ( fragment.sides.nCount >
             common::CY_USIZE_MAX - cSidesTotal ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cSidesTotal += fragment.sides.nCount;
    }
    if ( static_cast<common::u64>( cSidesTotal ) >
         policy.limits.cBrushSidesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( pRawResult->sideProvenance.nCount != cSidesTotal ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    *pSideCountOut = cSidesTotal;
    return geometry_status_t::OK;
}

geometry_status_t AllocateResultStorage(
    brush_csg_adoption_result_t *pResult,
    common::usize cFragments,
    common::usize cSides,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy ) noexcept
{
    common::usize cbFragments = 0u;
    common::usize cbAttributes = 0u;
    common::usize cbProvenance = 0u;
    if ( !common::Cy_TryArrayByteCount<brush_solid_t>(
             cFragments, cbFragments ) ||
         !common::Cy_TryArrayByteCount<
             geometry_brush_side_attribute_store_t>(
                 cFragments, cbAttributes ) ||
         !common::Cy_TryArrayByteCount<brush_csg_side_provenance_t>(
             cSides, cbProvenance ) ||
         cbFragments > common::CY_USIZE_MAX - cbAttributes ||
         cbFragments + cbAttributes >
             common::CY_USIZE_MAX - cbProvenance ||
         static_cast<common::u64>(
             cbFragments + cbAttributes + cbProvenance ) >
             policy.limits.cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    brush_solid_t *pFragments =
        common::Allocator_AllocateArrayStorage<brush_solid_t>(
            pAllocator, cFragments );
    if ( pFragments == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cFragments; ++i ) {
        ::new ( static_cast<void *>( &pFragments[i] ) ) brush_solid_t{};
    }

    geometry_brush_side_attribute_store_t *pAttributeStores =
        common::Allocator_AllocateArrayStorage<
            geometry_brush_side_attribute_store_t>(
                pAllocator, cFragments );
    if ( pAttributeStores == nullptr ) {
        for ( common::usize i = 0u; i < cFragments; ++i ) {
            pFragments[i].~brush_solid_t();
        }
        common::Allocator_FreeArrayStorage(
            pAllocator, pFragments, cFragments );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cFragments; ++i ) {
        ::new ( static_cast<void *>( &pAttributeStores[i] ) )
            geometry_brush_side_attribute_store_t{};
    }

    if ( !common::Vector_Init(
             &pResult->sideProvenance, pAllocator, cSides ) ) {
        for ( common::usize i = 0u; i < cFragments; ++i ) {
            pAttributeStores[i].~geometry_brush_side_attribute_store_t();
            pFragments[i].~brush_solid_t();
        }
        common::Allocator_FreeArrayStorage(
            pAllocator, pAttributeStores, cFragments );
        common::Allocator_FreeArrayStorage(
            pAllocator, pFragments, cFragments );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    pResult->pFragments = pFragments;
    pResult->pAttributeStores = pAttributeStores;
    pResult->cCapacity = cFragments;
    pResult->pAllocator = pAllocator;
    return geometry_status_t::OK;
}

void PublishResult(
    brush_csg_adoption_result_t *pDestination,
    brush_csg_adoption_result_t *pPending ) noexcept
{
    pDestination->pFragments = pPending->pFragments;
    pDestination->pAttributeStores = pPending->pAttributeStores;
    pDestination->cFragments = pPending->cFragments;
    pDestination->cCapacity = pPending->cCapacity;
    common::Vector_Move(
        &pDestination->sideProvenance,
        &pPending->sideProvenance );
    pDestination->pAllocator = pPending->pAllocator;
    pPending->pFragments = nullptr;
    pPending->pAttributeStores = nullptr;
    pPending->cFragments = 0u;
    pPending->cCapacity = 0u;
    pPending->pAllocator = nullptr;
}

} // namespace

void BrushCsgAdoptionResult_Shutdown(
    brush_csg_adoption_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return;
    }
    if ( pResult->pFragments != nullptr &&
         pResult->pAllocator != nullptr &&
        common::Allocator_IsValid( pResult->pAllocator ) ) {
        for ( common::usize i = 0u; i < pResult->cCapacity; ++i ) {
            BrushSideAttributeStore_Shutdown(
                &pResult->pAttributeStores[i] );
            BrushSolid_Shutdown( &pResult->pFragments[i] );
            pResult->pAttributeStores[i]
                .~geometry_brush_side_attribute_store_t();
            pResult->pFragments[i].~brush_solid_t();
        }
        common::Allocator_FreeArrayStorage(
            pResult->pAllocator,
            pResult->pAttributeStores,
            pResult->cCapacity );
        common::Allocator_FreeArrayStorage(
            pResult->pAllocator,
            pResult->pFragments,
            pResult->cCapacity );
    }
    if ( pResult->sideProvenance.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pResult->sideProvenance );
    }
    ResetResult( pResult );
}

geometry_status_t BrushCsgAdoption_TryPrepareSubtraction(
    const brush_csg_subtract_result_t *pRawResult,
    const brush_solid_t *pBrushA,
    const geometry_brush_side_attribute_store_t *pAttributesA,
    const brush_solid_t *pBrushB,
    const geometry_brush_side_attribute_store_t *pAttributesB,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_csg_adoption_result_t *pResultOut ) noexcept
{
    if ( pAllocator == nullptr || pIdAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_status_t destinationStatus =
        ValidateResultDestination( pResultOut );
    if ( destinationStatus != geometry_status_t::OK ) {
        return destinationStatus;
    }

    geometry_status_t status =
        ValidateOperand( pBrushA, pAttributesA, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateOperand( pBrushB, pAttributesB, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::usize cSidesTotal = 0u;
    status = ValidateRawResult(
        pRawResult, policy, &cSidesTotal );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    brush_csg_adoption_result_t pending{};
    status = AllocateResultStorage(
        &pending,
        pRawResult->cFragments,
        cSidesTotal,
        pAllocator,
        policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    common::usize iRawProvenance = 0u;
    for ( common::usize iFragment = 0u;
          iFragment < pRawResult->cFragments;
          ++iFragment ) {
        const brush_solid_t &raw =
            pRawResult->fragments[iFragment];
        brush_solid_t &destinationBrush =
            pending.pFragments[iFragment];
        geometry_brush_side_attribute_store_t &destinationAttributes =
            pending.pAttributeStores[iFragment];

        const geometry_source_id_result_t brushId =
            GeometrySourceIdAllocator_Allocate( &stagedIds );
        if ( brushId.status != geometry_status_t::OK ) {
            status = brushId.status;
            break;
        }
        status = BrushSolid_Init(
            &destinationBrush, pAllocator, brushId.id );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = BrushSideAttributeStore_Init(
            &destinationAttributes, pAllocator );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = BrushSolid_TryReserve(
            &destinationBrush,
            policy.limits,
            raw.sides.nCount );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = BrushSideAttributeStore_TryReserve(
            &destinationAttributes,
            policy.limits,
            raw.sides.nCount );
        if ( status != geometry_status_t::OK ) {
            break;
        }

        for ( common::usize iSide = 0u;
              iSide < raw.sides.nCount;
              ++iSide ) {
            const brush_solid_side_t &rawSide =
                raw.sides.pData[iSide];
            const brush_csg_raw_side_provenance_t &rawProvenance =
                pRawResult->sideProvenance.pData[iRawProvenance++];
            if ( rawProvenance.fragmentId.value != raw.sourceId.value ||
                 rawProvenance.sideId.value != rawSide.sourceId.value ) {
                status = geometry_status_t::CORRUPT_STATE;
                break;
            }
            resolved_side_source_t source{};
            status = ResolveSideSource(
                rawProvenance,
                *pBrushA,
                *pAttributesA,
                *pBrushB,
                *pAttributesB,
                &source );
            if ( status != geometry_status_t::OK ) {
                break;
            }

            geometry_brush_side_attributes_t attributes{};
            status = BrushSideAttributeStore_TryGet(
                source.pAttributes,
                source.pSide->iAttributeIndex,
                &attributes );
            if ( status != geometry_status_t::OK ) {
                status = geometry_status_t::CORRUPT_STATE;
                break;
            }

            common::usize iDestinationAttribute = 0u;
            status = BrushSideAttributeStore_TryAppend(
                &destinationAttributes,
                policy,
                attributes,
                &iDestinationAttribute );
            if ( status != geometry_status_t::OK ) {
                break;
            }
            if ( iDestinationAttribute >
                 std::numeric_limits<common::u32>::max() ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                break;
            }

            const geometry_source_id_result_t sideId =
                GeometrySourceIdAllocator_Allocate( &stagedIds );
            if ( sideId.status != geometry_status_t::OK ) {
                status = sideId.status;
                break;
            }
            const brush_solid_side_t destinationSide{
                rawSide.plane,
                sideId.id,
                static_cast<common::u32>( iDestinationAttribute )
            };
            status = BrushSolid_TryAddSide(
                &destinationBrush,
                policy.limits,
                destinationSide,
                nullptr );
            if ( status != geometry_status_t::OK ) {
                break;
            }

            const brush_csg_side_provenance_t provenance{
                brushId.id,
                sideId.id,
                static_cast<common::u32>( iDestinationAttribute ),
                source.operand,
                source.origin,
                source.pBrush->sourceId,
                source.pSide->sourceId,
                source.pSide->iAttributeIndex
            };
            if ( !common::Vector_PushBack(
                     &pending.sideProvenance, provenance ) ) {
                status = geometry_status_t::CORRUPT_STATE;
                break;
            }
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = BrushValidation_Quick(
            &destinationBrush, policy );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = BrushSideAttributeStore_Validate(
            &destinationAttributes, policy );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        ++pending.cFragments;
    }

    if ( status != geometry_status_t::OK ||
         pending.cFragments != pRawResult->cFragments ||
         pending.sideProvenance.nCount != cSidesTotal ) {
        if ( status == geometry_status_t::OK ) {
            status = geometry_status_t::CORRUPT_STATE;
        }
        BrushCsgAdoptionResult_Shutdown( &pending );
        return status;
    }

    PublishResult( pResultOut, &pending );
    *pIdAllocator = stagedIds;
    return geometry_status_t::OK;
}

const brush_csg_side_provenance_t *
BrushCsgAdoption_FindSideProvenance(
    const brush_csg_adoption_result_t *pResult,
    geometry_source_id_t destinationSideId ) noexcept
{
    if ( pResult == nullptr ||
         !GeometrySourceId_IsValid( destinationSideId ) ||
         !common::Vector_IsValid( &pResult->sideProvenance ) ) {
        return nullptr;
    }
    for ( common::usize i = 0u;
          i < pResult->sideProvenance.nCount;
          ++i ) {
        if ( pResult->sideProvenance.pData[i]
                 .destinationSideId.value == destinationSideId.value ) {
            return &pResult->sideProvenance.pData[i];
        }
    }
    return nullptr;
}

} // namespace cypher::editor::geometry
