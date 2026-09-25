//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushReplacement.cpp
//  Purpose: Implements atomic exact-identity replacement of document brushes.
//  Details: The function builds a complete replacement pointer vector and a
//           complete replacement source-ID registry in private storage. The
//           document is changed only by allocation-free ownership swaps after
//           every fallible operation has succeeded.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_BrushValidation.h"

#include <algorithm>
#include <new>

namespace cypher::editor::geometry
{

namespace
{

// The replacement brushes: bare solids (legacy entry point) or complete
// authored brushes. Solids and their optional record tables are read through
// one view so every stage below serves both entry points.
struct output_set_t {
    const brush_solid_t *pSolids{ nullptr };
    const brush_source_t *pSources{ nullptr };
    common::usize nCount{ 0u };
    const brush_solid_t *At( common::usize i ) const noexcept { return pSources != nullptr ? &pSources[i].solid : &pSolids[i]; }
    const geometry_brush_side_attribute_store_t *StoreAt( common::usize i ) const noexcept
    {
        return pSources != nullptr ? &pSources[i].attributes : nullptr;
    }
};

bool SourceIdLess(
    geometry_source_id_t left,
    geometry_source_id_t right ) noexcept
{
    return left.value < right.value;
}

bool SourceIdEqual(
    geometry_source_id_t left,
    geometry_source_id_t right ) noexcept
{
    return left.value == right.value;
}

brush_solid_t *AllocateBrush(
    const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator, sizeof( brush_solid_t ), alignof( brush_solid_t ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }
    return new ( pMemory ) brush_solid_t{};
}

void FreeBrush(
    const common::allocator_t *pAllocator,
    brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return;
    }
    BrushSolid_Shutdown( pBrush );
    pBrush->~brush_solid_t();
    common::Allocator_Free(
        pAllocator,
        pBrush,
        sizeof( brush_solid_t ),
        alignof( brush_solid_t ) );
}

common::usize FindBrushIndex(
    const geometry_document_t &document,
    geometry_source_id_t brushId ) noexcept
{
    for ( common::usize iBrush = 0u;
          iBrush < document.brushes.nCount;
          ++iBrush ) {
        const brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( pBrush != nullptr &&
             pBrush->sourceId.value == brushId.value ) {
            return iBrush;
        }
    }
    return document.brushes.nCount;
}

bool SortedIdsContain(
    const common::vector_t<geometry_source_id_t> &ids,
    geometry_source_id_t id ) noexcept
{
    if ( ids.nCount == 0u ) {
        return false;
    }
    return std::binary_search(
        ids.pData,
        ids.pData + ids.nCount,
        id,
        SourceIdLess );
}

void SortIds(
    common::vector_t<geometry_source_id_t> *pIds ) noexcept
{
    if ( pIds->nCount < 2u ) {
        return;
    }
    std::sort(
        pIds->pData,
        pIds->pData + pIds->nCount,
        SourceIdLess );
}

geometry_status_t CheckDocument(
    const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_IsValid( &pDocument->brushes ) ||
         !common::Vector_IsValid( &pDocument->meshes ) ||
         !GeometrySourceIdRegistry_IsValid( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_IsInitialized( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_ValidateDeep( &pDocument->sourceIds ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateStoredBrushes(
    const geometry_document_t &document,
    common::u64 *pSideCountOut ) noexcept
{
    common::u64 cSidesTotal = 0u;
    for ( common::usize iBrush = 0u;
          iBrush < document.brushes.nCount;
          ++iBrush ) {
        const brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( pBrush == nullptr ||
             !common::Vector_IsValid( &pBrush->sides ) ||
             pBrush->sides.pAllocator == nullptr ||
             !GeometrySourceId_IsValid( pBrush->sourceId ) ||
             !GeometrySourceIdRegistry_Contains(
                 &document.sourceIds, pBrush->sourceId ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        const common::usize cSides = BrushSolid_SideCount( pBrush );
        if ( static_cast<common::u64>( cSides ) >
                 document.policy.limits.cBrushSidesPerBrushMax ||
             static_cast<common::u64>( cSides ) >
                 ~static_cast<common::u64>( 0u ) - cSidesTotal ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        for ( common::usize iSide = 0u;
              iSide < cSides;
              ++iSide ) {
            const geometry_source_id_t sideId =
                pBrush->sides.pData[iSide].sourceId;
            if ( !GeometrySourceId_IsValid( sideId ) ||
                 sideId.value == pBrush->sourceId.value ||
                 !GeometrySourceIdRegistry_Contains(
                     &document.sourceIds, sideId ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            for ( common::usize iPrior = 0u;
                  iPrior < iSide;
                  ++iPrior ) {
                if ( pBrush->sides.pData[iPrior].sourceId.value ==
                     sideId.value ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
            }
        }
        cSidesTotal += static_cast<common::u64>( cSides );
    }

    *pSideCountOut = cSidesTotal;
    return geometry_status_t::OK;
}

geometry_status_t ValidateRemovalRoots(
    const geometry_document_t &document,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    common::u64 *pRemovedSideCountOut,
    common::usize *pRemovedSourceCountOut ) noexcept
{
    common::u64 cRemovedSides = 0u;
    common::usize cRemovedSources = 0u;

    for ( common::usize iRemoval = 0u;
          iRemoval < removeBrushIds.nCount;
          ++iRemoval ) {
        const geometry_source_id_t brushId =
            removeBrushIds.pData[iRemoval];
        if ( !GeometrySourceId_IsValid( brushId ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( common::usize iPrior = 0u;
              iPrior < iRemoval;
              ++iPrior ) {
            if ( removeBrushIds.pData[iPrior].value == brushId.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }

        const common::usize iBrush = FindBrushIndex( document, brushId );
        if ( iBrush >= document.brushes.nCount ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }

        const common::usize cSides =
            BrushSolid_SideCount( document.brushes.pData[iBrush] );
        if ( static_cast<common::u64>( cSides ) >
             ~static_cast<common::u64>( 0u ) - cRemovedSides ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( cSides == common::CY_USIZE_MAX ||
             cRemovedSources > common::CY_USIZE_MAX - ( cSides + 1u ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cRemovedSides += static_cast<common::u64>( cSides );
        cRemovedSources += cSides + 1u;
    }

    *pRemovedSideCountOut = cRemovedSides;
    *pRemovedSourceCountOut = cRemovedSources;
    return geometry_status_t::OK;
}

geometry_status_t NormalizeOutputValidationStatus(
    geometry_status_t status ) noexcept
{
    if ( status == geometry_status_t::NOT_INITIALIZED ||
         status == geometry_status_t::CORRUPT_STATE ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return status;
}

geometry_status_t ValidateOutputs(
    const geometry_document_t &document,
    const output_set_t &replacementBrushes,
    common::u64 *pOutputSideCountOut,
    common::usize *pOutputSourceCountOut ) noexcept
{
    common::u64 cOutputSides = 0u;
    common::usize cOutputSources = 0u;

    for ( common::usize iOutput = 0u;
          iOutput < replacementBrushes.nCount;
          ++iOutput ) {
        const brush_validation_result_t validation = BrushValidation_Deep(
            replacementBrushes.At( iOutput ),
            document.policy,
            document.pAllocator );
        if ( validation.status != geometry_status_t::OK ) {
            return NormalizeOutputValidationStatus( validation.status );
        }

        const common::usize cSides =
            BrushSolid_SideCount( replacementBrushes.At( iOutput ) );
        if ( static_cast<common::u64>( cSides ) >
             ~static_cast<common::u64>( 0u ) - cOutputSides ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        if ( cSides == common::CY_USIZE_MAX ||
             cOutputSources > common::CY_USIZE_MAX - ( cSides + 1u ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cOutputSides += static_cast<common::u64>( cSides );
        cOutputSources += cSides + 1u;
    }

    *pOutputSideCountOut = cOutputSides;
    *pOutputSourceCountOut = cOutputSources;
    return geometry_status_t::OK;
}

geometry_status_t CheckDocumentLimits(
    const geometry_document_t &document,
    common::usize cRemovals,
    common::usize cOutputs,
    common::u64 cSidesBefore,
    common::u64 cRemovedSides,
    common::u64 cOutputSides,
    common::usize *pFinalBrushCountOut ) noexcept
{
    if ( cRemovals > document.brushes.nCount ||
         cRemovedSides > cSidesBefore ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cRetained =
        document.brushes.nCount - cRemovals;
    if ( cOutputs > common::CY_USIZE_MAX - cRetained ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cFinalBrushes = cRetained + cOutputs;
    if ( static_cast<common::u64>( cFinalBrushes ) >
         document.policy.limits.cBrushesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const common::u64 cRetainedSides = cSidesBefore - cRemovedSides;
    if ( cOutputSides >
         ~static_cast<common::u64>( 0u ) - cRetainedSides ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( cRetainedSides + cOutputSides >
         document.policy.limits.cBrushSidesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    *pFinalBrushCountOut = cFinalBrushes;
    return geometry_status_t::OK;
}

geometry_status_t InitAndFillRemovalRoots(
    common::vector_t<geometry_source_id_t> *pRoots,
    const common::allocator_t *pAllocator,
    common::span_t<const geometry_source_id_t> source ) noexcept
{
    if ( !common::Vector_Init( pRoots, pAllocator, source.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < source.nCount; ++i ) {
        if ( !common::Vector_PushBack( pRoots, source.pData[i] ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    SortIds( pRoots );
    return geometry_status_t::OK;
}

geometry_status_t InitAndFillRemovedSourceIds(
    common::vector_t<geometry_source_id_t> *pIds,
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removalRoots,
    common::usize cRemovedSources ) noexcept
{
    if ( !common::Vector_Init(
             pIds, document.pAllocator, cRemovedSources ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iRoot = 0u;
          iRoot < removalRoots.nCount;
          ++iRoot ) {
        const common::usize iBrush =
            FindBrushIndex( document, removalRoots.pData[iRoot] );
        if ( iBrush >= document.brushes.nCount ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        const brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( !common::Vector_PushBack( pIds, pBrush->sourceId ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( common::usize iSide = 0u;
              iSide < pBrush->sides.nCount;
              ++iSide ) {
            if ( !common::Vector_PushBack(
                     pIds, pBrush->sides.pData[iSide].sourceId ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    SortIds( pIds );
    for ( common::usize i = 1u; i < pIds->nCount; ++i ) {
        if ( SourceIdEqual( pIds->pData[i - 1u], pIds->pData[i] ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t InitAndFillOutputSourceIds(
    common::vector_t<geometry_source_id_t> *pIds,
    const geometry_document_t &document,
    const output_set_t &outputs,
    common::usize cOutputSources ) noexcept
{
    if ( !common::Vector_Init(
             pIds, document.pAllocator, cOutputSources ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iOutput = 0u;
          iOutput < outputs.nCount;
          ++iOutput ) {
        const brush_solid_t &brush = *outputs.At( iOutput );
        if ( !common::Vector_PushBack( pIds, brush.sourceId ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( common::usize iSide = 0u;
              iSide < brush.sides.nCount;
              ++iSide ) {
            if ( !common::Vector_PushBack(
                     pIds, brush.sides.pData[iSide].sourceId ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    SortIds( pIds );
    for ( common::usize i = 1u; i < pIds->nCount; ++i ) {
        if ( SourceIdEqual( pIds->pData[i - 1u], pIds->pData[i] ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t PreflightOutputIdentities(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removedIds,
    const common::vector_t<geometry_source_id_t> &outputIds,
    common::usize *pFreshIdCountOut ) noexcept
{
    common::usize cFreshIds = 0u;
    const bool bAllocatorExhausted =
        !GeometrySourceId_IsValid( document.sourceIds.allocator.next );

    for ( common::usize i = 0u; i < outputIds.nCount; ++i ) {
        const geometry_source_id_t id = outputIds.pData[i];
        if ( GeometrySourceIdRegistry_Contains(
                 &document.sourceIds, id ) &&
             !SortedIdsContain( removedIds, id ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }

        if ( common::HashSet_Contains(
                 &document.sourceIds.claimedIds, id ) ) {
            continue;
        }
        if ( bAllocatorExhausted ) {
            return geometry_status_t::INSUFFICIENT_CAPACITY;
        }
        if ( id.value < document.sourceIds.allocator.next.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        ++cFreshIds;
    }

    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &document.sourceIds );
    if ( cClaimed > document.sourceIds.cEntriesMax ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( cFreshIds > document.sourceIds.cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    *pFreshIdCountOut = cFreshIds;
    return geometry_status_t::OK;
}

geometry_status_t PrepareRegistry(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removedIds,
    const common::vector_t<geometry_source_id_t> &outputIds,
    common::usize cFreshIds,
    geometry_source_id_registry_t *pPreparedOut ) noexcept
{
    geometry_status_t status = GeometrySourceIdRegistry_TryClone(
        &document.sourceIds, pPreparedOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( pPreparedOut );
    status = GeometrySourceIdRegistry_Reserve(
        pPreparedOut, cClaimed + cFreshIds );
    if ( status != geometry_status_t::OK ) {
        GeometrySourceIdRegistry_Shutdown( pPreparedOut );
        return status;
    }

    const bool bLoadRegistrationOpenBefore =
        pPreparedOut->bLoadRegistrationOpen;
    for ( common::usize i = 0u; i < removedIds.nCount; ++i ) {
        status = GeometrySourceIdRegistry_Release(
            pPreparedOut, removedIds.pData[i] );
        if ( status != geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pPreparedOut );
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    for ( common::usize i = 0u; i < outputIds.nCount; ++i ) {
        const geometry_source_id_t id = outputIds.pData[i];
        if ( common::HashSet_Contains( &pPreparedOut->liveIds, id ) ) {
            GeometrySourceIdRegistry_Shutdown( pPreparedOut );
            return geometry_status_t::IDENTITY_CONFLICT;
        }

        if ( common::HashSet_Contains( &pPreparedOut->claimedIds, id ) ) {
            status = GeometrySourceIdRegistry_RestoreRetired(
                pPreparedOut, id );
        }
        else {
            pPreparedOut->bLoadRegistrationOpen = true;
            status = GeometrySourceIdRegistry_Register(
                pPreparedOut, id );
        }
        if ( status != geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pPreparedOut );
            return status;
        }
    }

    pPreparedOut->bLoadRegistrationOpen =
        bLoadRegistrationOpenBefore;
    if ( !GeometrySourceIdRegistry_ValidateDeep( pPreparedOut ) ) {
        GeometrySourceIdRegistry_Shutdown( pPreparedOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

void DestroyPreparedBrushes(
    common::vector_t<brush_solid_t *> *pPrepared,
    common::usize iFirstOwned,
    const common::allocator_t *pAllocator ) noexcept
{
    for ( common::usize i = iFirstOwned;
          i < pPrepared->nCount;
          ++i ) {
        FreeBrush( pAllocator, pPrepared->pData[i] );
    }
    common::Vector_Shutdown( pPrepared );
}

void DestroyPreparedStores(
    common::vector_t<geometry_brush_side_attribute_store_t *> *pPrepared,
    common::usize iFirstOwned,
    const common::allocator_t *pAllocator ) noexcept
{
    for ( common::usize i = iFirstOwned; i < pPrepared->nCount; ++i ) {
        BrushAttributes_Free( pAllocator, pPrepared->pData[i] );
    }
    common::Vector_Shutdown( pPrepared );
}

// Builds the final brush and record-table vectors in parallel: retained
// entries by pointer (they keep their addresses), then deep copies of the
// outputs, each with its records extended to cover its sides.
geometry_status_t PrepareBrushVector(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removalRoots,
    const output_set_t &outputs,
    common::usize cFinalBrushes,
    common::vector_t<brush_solid_t *> *pPreparedOut,
    common::vector_t<geometry_brush_side_attribute_store_t *> *pPreparedStoresOut ) noexcept
{
    if ( !common::Vector_Init(
             pPreparedOut, document.pAllocator, cFinalBrushes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Init( pPreparedStoresOut, document.pAllocator, cFinalBrushes ) ) {
        common::Vector_Shutdown( pPreparedOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( document.brushAttributes.nCount != document.brushes.nCount ) {
        common::Vector_Shutdown( pPreparedStoresOut );
        common::Vector_Shutdown( pPreparedOut );
        return geometry_status_t::CORRUPT_STATE;
    }

    for ( common::usize iBrush = 0u;
          iBrush < document.brushes.nCount;
          ++iBrush ) {
        brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( SortedIdsContain( removalRoots, pBrush->sourceId ) ) {
            continue;
        }
        if ( !common::Vector_PushBack( pPreparedOut, pBrush ) ||
             !common::Vector_PushBack( pPreparedStoresOut, document.brushAttributes.pData[iBrush] ) ) {
            common::Vector_Shutdown( pPreparedStoresOut );
            common::Vector_Shutdown( pPreparedOut );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    const common::usize cRetained = pPreparedOut->nCount;
    auto fail = [&]( geometry_status_t st ) noexcept {
        DestroyPreparedStores( pPreparedStoresOut, cRetained, document.pAllocator );
        DestroyPreparedBrushes( pPreparedOut, cRetained, document.pAllocator );
        return st;
    };

    for ( common::usize iOutput = 0u;
          iOutput < outputs.nCount;
          ++iOutput ) {
        brush_solid_t *pCopy = AllocateBrush( document.pAllocator );
        if ( pCopy == nullptr ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }

        const geometry_status_t copyStatus = BrushSolid_DeepCopy(
            pCopy,
            outputs.At( iOutput ),
            document.pAllocator,
            document.policy.limits );
        if ( copyStatus != geometry_status_t::OK ) {
            FreeBrush( document.pAllocator, pCopy );
            return fail( copyStatus );
        }
        if ( !common::Vector_PushBack( pPreparedOut, pCopy ) ) {
            FreeBrush( document.pAllocator, pCopy );
            return fail( geometry_status_t::ALLOCATION_FAILED );
        }
        geometry_brush_side_attribute_store_t *pStore = BrushAttributes_Allocate( document.pAllocator );
        if ( pStore == nullptr ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        const geometry_status_t storeStatus = BrushAttributes_TryBuildCovering(
            outputs.At( iOutput ), outputs.StoreAt( iOutput ), document.pAllocator, document.policy, pStore );
        if ( storeStatus != geometry_status_t::OK || !common::Vector_PushBack( pPreparedStoresOut, pStore ) ) {
            BrushAttributes_Free( document.pAllocator, pStore );
            return fail( storeStatus != geometry_status_t::OK ? storeStatus : geometry_status_t::ALLOCATION_FAILED );
        }
    }

    if ( pPreparedOut->nCount != cFinalBrushes || pPreparedStoresOut->nCount != cFinalBrushes ) {
        return fail( geometry_status_t::CORRUPT_STATE );
    }
    return geometry_status_t::OK;
}

void SwapBrushVectors(
    common::vector_t<brush_solid_t *> &left,
    common::vector_t<brush_solid_t *> &right ) noexcept
{
    brush_solid_t **pData = left.pData;
    left.pData = right.pData;
    right.pData = pData;

    common::usize value = left.nCount;
    left.nCount = right.nCount;
    right.nCount = value;

    value = left.nCapacity;
    left.nCapacity = right.nCapacity;
    right.nCapacity = value;

    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;
}

void SwapStoreVectors(
    common::vector_t<geometry_brush_side_attribute_store_t *> &left,
    common::vector_t<geometry_brush_side_attribute_store_t *> &right ) noexcept
{
    geometry_brush_side_attribute_store_t **pData = left.pData;
    left.pData = right.pData;
    right.pData = pData;
    common::usize value = left.nCount;
    left.nCount = right.nCount;
    right.nCount = value;
    value = left.nCapacity;
    left.nCapacity = right.nCapacity;
    right.nCapacity = value;
    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;
}

void SwapSourceIdSetStorage(
    geometry_source_id_set_t &left,
    geometry_source_id_set_t &right ) noexcept
{
    auto *pSlots = left.pSlots;
    left.pSlots = right.pSlots;
    right.pSlots = pSlots;

    common::usize value = left.nCount;
    left.nCount = right.nCount;
    right.nCount = value;

    value = left.nCapacity;
    left.nCapacity = right.nCapacity;
    right.nCapacity = value;

    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;
}

void SwapRegistries(
    geometry_source_id_registry_t &left,
    geometry_source_id_registry_t &right ) noexcept
{
    SwapSourceIdSetStorage( left.claimedIds, right.claimedIds );
    SwapSourceIdSetStorage( left.liveIds, right.liveIds );

    const geometry_source_id_allocator_t allocator = left.allocator;
    left.allocator = right.allocator;
    right.allocator = allocator;

    common::usize cEntriesMax = left.cEntriesMax;
    left.cEntriesMax = right.cEntriesMax;
    right.cEntriesMax = cEntriesMax;

    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;

    const bool bLoadRegistrationOpen = left.bLoadRegistrationOpen;
    left.bLoadRegistrationOpen = right.bLoadRegistrationOpen;
    right.bLoadRegistrationOpen = bLoadRegistrationOpen;
}

void ShutdownIdVectors(
    common::vector_t<geometry_source_id_t> *pRemovalRoots,
    common::vector_t<geometry_source_id_t> *pRemovedIds,
    common::vector_t<geometry_source_id_t> *pOutputIds ) noexcept
{
    common::Vector_Shutdown( pOutputIds );
    common::Vector_Shutdown( pRemovedIds );
    common::Vector_Shutdown( pRemovalRoots );
}

} // namespace

namespace
{

geometry_status_t ReplaceBrushes(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    const output_set_t &replacementBrushes ) noexcept
{
    const geometry_status_t documentStatus = CheckDocument( pDocument );
    if ( documentStatus != geometry_status_t::OK ) {
        return documentStatus;
    }
    if ( !common::Span_IsValid( removeBrushIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( removeBrushIds.nCount == 0u &&
         replacementBrushes.nCount == 0u ) {
        return geometry_status_t::OK;
    }

    common::u64 cSidesBefore = 0u;
    geometry_status_t status = ValidateStoredBrushes(
        *pDocument, &cSidesBefore );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::u64 cRemovedSides = 0u;
    common::usize cRemovedSources = 0u;
    status = ValidateRemovalRoots(
        *pDocument,
        removeBrushIds,
        &cRemovedSides,
        &cRemovedSources );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::u64 cOutputSides = 0u;
    common::usize cOutputSources = 0u;
    status = ValidateOutputs(
        *pDocument,
        replacementBrushes,
        &cOutputSides,
        &cOutputSources );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::usize cFinalBrushes = 0u;
    status = CheckDocumentLimits(
        *pDocument,
        removeBrushIds.nCount,
        replacementBrushes.nCount,
        cSidesBefore,
        cRemovedSides,
        cOutputSides,
        &cFinalBrushes );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::vector_t<geometry_source_id_t> removalRoots{};
    common::vector_t<geometry_source_id_t> removedIds{};
    common::vector_t<geometry_source_id_t> outputIds{};

    status = InitAndFillRemovalRoots(
        &removalRoots, pDocument->pAllocator, removeBrushIds );
    if ( status == geometry_status_t::OK ) {
        status = InitAndFillRemovedSourceIds(
            &removedIds,
            *pDocument,
            removalRoots,
            cRemovedSources );
    }
    if ( status == geometry_status_t::OK ) {
        status = InitAndFillOutputSourceIds(
            &outputIds,
            *pDocument,
            replacementBrushes,
            cOutputSources );
    }

    common::usize cFreshIds = 0u;
    if ( status == geometry_status_t::OK ) {
        status = PreflightOutputIdentities(
            *pDocument,
            removedIds,
            outputIds,
            &cFreshIds );
    }
    if ( status != geometry_status_t::OK ) {
        ShutdownIdVectors( &removalRoots, &removedIds, &outputIds );
        return status;
    }

    geometry_source_id_registry_t preparedRegistry{};
    status = PrepareRegistry(
        *pDocument,
        removedIds,
        outputIds,
        cFreshIds,
        &preparedRegistry );
    if ( status != geometry_status_t::OK ) {
        ShutdownIdVectors( &removalRoots, &removedIds, &outputIds );
        return status;
    }

    common::vector_t<brush_solid_t *> preparedBrushes{};
    common::vector_t<geometry_brush_side_attribute_store_t *> preparedStores{};
    status = PrepareBrushVector(
        *pDocument,
        removalRoots,
        replacementBrushes,
        cFinalBrushes,
        &preparedBrushes,
        &preparedStores );
    if ( status != geometry_status_t::OK ) {
        GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
        ShutdownIdVectors( &removalRoots, &removedIds, &outputIds );
        return status;
    }

    // No fallible work remains. These swaps publish the vector and registry as
    // one logical document step; revision belongs to the owning transaction.
    SwapBrushVectors( pDocument->brushes, preparedBrushes );
    SwapStoreVectors( pDocument->brushAttributes, preparedStores );
    SwapRegistries( pDocument->sourceIds, preparedRegistry );

    // preparedBrushes / preparedStores now own the old, still parallel,
    // pointer vectors. Only removed brushes and their record tables are
    // freed; retained objects are shared by pointer with the newly published
    // vectors and keep their storage addresses.
    for ( common::usize iBrush = 0u;
          iBrush < preparedBrushes.nCount;
          ++iBrush ) {
        brush_solid_t *pOldBrush = preparedBrushes.pData[iBrush];
        if ( SortedIdsContain( removalRoots, pOldBrush->sourceId ) ) {
            FreeBrush( pDocument->pAllocator, pOldBrush );
            if ( iBrush < preparedStores.nCount ) {
                BrushAttributes_Free( pDocument->pAllocator, preparedStores.pData[iBrush] );
            }
        }
    }
    common::Vector_Shutdown( &preparedStores );
    common::Vector_Shutdown( &preparedBrushes );
    GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
    ShutdownIdVectors( &removalRoots, &removedIds, &outputIds );

    return geometry_status_t::OK;
}

} // namespace

geometry_status_t GeometryDocument_TryReplaceBrushesExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    common::span_t<const brush_solid_t> replacementBrushes ) noexcept
{
    const geometry_status_t documentStatus = CheckDocument( pDocument );
    if ( documentStatus != geometry_status_t::OK ) { return documentStatus; }
    if ( !common::Span_IsValid( replacementBrushes ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    output_set_t outputs{};
    outputs.pSolids = replacementBrushes.pData;
    outputs.nCount = replacementBrushes.nCount;
    return ReplaceBrushes( pDocument, removeBrushIds, outputs );
}

geometry_status_t GeometryDocument_TryReplaceBrushSourcesExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    common::span_t<const brush_source_t> replacementBrushes ) noexcept
{
    const geometry_status_t documentStatus = CheckDocument( pDocument );
    if ( documentStatus != geometry_status_t::OK ) { return documentStatus; }
    if ( !common::Span_IsValid( replacementBrushes ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( common::usize i = 0u; i < replacementBrushes.nCount; ++i ) {
        if ( !BrushSource_IsInitialized( &replacementBrushes.pData[i] ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    output_set_t outputs{};
    outputs.pSources = replacementBrushes.pData;
    outputs.nCount = replacementBrushes.nCount;
    return ReplaceBrushes( pDocument, removeBrushIds, outputs );
}

} // namespace cypher::editor::geometry
