//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Scratch.cpp
//  Purpose: Implements bounded temporary storage for geometry operations.
//  Details: Common ScratchBuffer owns local-or-fallback acquisition while
//           MemoryStack supplies aligned transactional allocation and rewind.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Scratch.h"

#include "CypherCommon_Align.h"

namespace cypher::editor::geometry
{

namespace
{

bool LocalStorageCanSatisfy(
    byte_span_t storage,
    usize cbSize,
    usize nAlignment ) noexcept
{
    if ( storage.pData == nullptr || cbSize == 0u ) {
        return false;
    }

    common::uintptr nAlignedAddress = 0u;
    if ( !common::Cy_AlignPointerUpChecked(
             storage.pData,
             nAlignment,
             nAlignedAddress ) ) {
        return false;
    }

    const usize cbPadding = static_cast<usize>(
        nAlignedAddress - reinterpret_cast<common::uintptr>( storage.pData ) );
    return cbPadding <= storage.nCount &&
           cbSize <= storage.nCount - cbPadding;
}

bool IsReleasedState( const geometry_scratch_t &scratch ) noexcept
{
    return common::ScratchBuffer_IsValid( &scratch.buffer ) &&
           scratch.buffer.pData == nullptr &&
           scratch.buffer.cbSize == 0u &&
           scratch.buffer.nAlignment == 0u &&
           scratch.buffer.localStorage.pData == nullptr &&
           scratch.buffer.localStorage.nCount == 0u &&
           scratch.stack.memory.pData == nullptr &&
           scratch.stack.memory.cbSize == 0u &&
           scratch.stack.iOffset == 0u &&
           scratch.stack.cbHighWater == 0u &&
           scratch.cbBudget == 0u &&
           scratch.nBaseAlignment == 0u &&
           scratch.nMarkerSerialCounter == 0u &&
           scratch.nCurrentMarkerSerial == 0u &&
           !scratch.bInitialized;
}

} // namespace

geometry_status_t GeometryScratch_Acquire(
    geometry_scratch_t *pScratch,
    const geometry_scratch_desc_t &desc ) noexcept
{
    if ( pScratch == nullptr ||
         !common::Span_IsValid( desc.localStorage ) ||
         desc.cbCapacity == 0u ||
         desc.cbBudget == 0u ||
         !common::Cy_AlignIsPowerOfTwo( desc.nBaseAlignment ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pScratch->bInitialized ) {
        return GeometryScratch_IsValid( pScratch )
            ? geometry_status_t::ALREADY_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }
    if ( !IsReleasedState( *pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( pScratch->nAcquisitionSerial == common::CY_U64_MAX ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( desc.cbCapacity > desc.cbBudget ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const bool bLocalFits = LocalStorageCanSatisfy(
        desc.localStorage,
        desc.cbCapacity,
        desc.nBaseAlignment );
    if ( !bLocalFits &&
         !common::Allocator_IsValid( desc.pFallbackAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( !common::ScratchBuffer_Acquire(
             &pScratch->buffer,
             desc.localStorage,
             desc.pFallbackAllocator,
             desc.cbCapacity,
             desc.nBaseAlignment ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    if ( !common::MemoryStack_Init(
             &pScratch->stack,
             common::ScratchBuffer_Span( &pScratch->buffer ) ) ) {
        common::ScratchBuffer_Release( &pScratch->buffer );
        pScratch->stack = {};
        return geometry_status_t::CORRUPT_STATE;
    }

    pScratch->cbBudget = desc.cbBudget;
    pScratch->nBaseAlignment = desc.nBaseAlignment;
    ++pScratch->nAcquisitionSerial;
    pScratch->bInitialized = common::CY_TRUE;
    return geometry_status_t::OK;
}

geometry_status_t GeometryScratch_Release(
    geometry_scratch_t *pScratch ) noexcept
{
    if ( pScratch == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::OK;
    }

    common::ScratchBuffer_Release( &pScratch->buffer );
    pScratch->stack = {};
    pScratch->cbBudget = 0u;
    pScratch->nBaseAlignment = 0u;
    pScratch->nMarkerSerialCounter = 0u;
    pScratch->nCurrentMarkerSerial = 0u;
    pScratch->bInitialized = common::CY_FALSE;
    return geometry_status_t::OK;
}

bool GeometryScratch_IsValid(
    const geometry_scratch_t *pScratch ) noexcept
{
    if ( pScratch == nullptr ) {
        return false;
    }
    if ( !pScratch->bInitialized ) {
        return IsReleasedState( *pScratch );
    }
    if ( !common::ScratchBuffer_IsValid( &pScratch->buffer ) ||
         !common::MemoryStack_IsValid( &pScratch->stack ) ||
         pScratch->cbBudget < pScratch->buffer.cbSize ||
         pScratch->buffer.cbSize == 0u ||
         pScratch->nAcquisitionSerial == 0u ||
         pScratch->nBaseAlignment != pScratch->buffer.nAlignment ||
         pScratch->nCurrentMarkerSerial >
             pScratch->nMarkerSerialCounter ||
         !common::Cy_AlignIsPowerOfTwo( pScratch->nBaseAlignment ) ) {
        return false;
    }

    return pScratch->stack.memory.pData == pScratch->buffer.pData &&
           pScratch->stack.memory.cbSize == pScratch->buffer.cbSize &&
           common::Cy_AlignIsPointerAligned(
               pScratch->buffer.pData,
               pScratch->nBaseAlignment );
}

bool GeometryScratch_IsInitialized(
    const geometry_scratch_t *pScratch ) noexcept
{
    return pScratch != nullptr &&
           pScratch->bInitialized &&
           GeometryScratch_IsValid( pScratch );
}

geometry_status_t GeometryScratch_Allocate(
    geometry_scratch_t *pScratch,
    usize cbSize,
    usize nAlignment,
    void **ppStorage ) noexcept
{
    if ( ppStorage == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppStorage = nullptr;

    if ( pScratch == nullptr || cbSize == 0u ||
         !common::Cy_AlignIsPowerOfTwo( nAlignment ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cbUsed = common::MemoryStack_Used( &pScratch->stack );
    if ( cbUsed > pScratch->cbBudget ||
         cbSize > pScratch->cbBudget - cbUsed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    void *pStorage = common::MemoryStack_Allocate(
        &pScratch->stack,
        cbSize,
        nAlignment );
    if ( pStorage == nullptr ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }

    *ppStorage = pStorage;
    return geometry_status_t::OK;
}

geometry_status_t GeometryScratch_Mark(
    geometry_scratch_t *pScratch,
    geometry_scratch_mark_t *pMark ) noexcept
{
    if ( pMark == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pMark = {};

    if ( pScratch == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( pScratch->nMarkerSerialCounter == common::CY_U64_MAX ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    pMark->pOwner = pScratch;
    pMark->nAcquisitionSerial = pScratch->nAcquisitionSerial;
    pMark->nMarkerSerial = ++pScratch->nMarkerSerialCounter;
    pMark->nParentMarkerSerial = pScratch->nCurrentMarkerSerial;
    pMark->iOffset = common::MemoryStack_Mark( &pScratch->stack );
    pScratch->nCurrentMarkerSerial = pMark->nMarkerSerial;
    return geometry_status_t::OK;
}

geometry_status_t GeometryScratch_Rewind(
    geometry_scratch_t *pScratch,
    geometry_scratch_mark_t mark ) noexcept
{
    if ( pScratch == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( mark.pOwner != pScratch ||
         mark.nAcquisitionSerial == 0u ||
         mark.nAcquisitionSerial != pScratch->nAcquisitionSerial ||
         mark.nMarkerSerial == 0u ||
         mark.nMarkerSerial != pScratch->nCurrentMarkerSerial ||
         mark.nParentMarkerSerial >= mark.nMarkerSerial ||
         mark.iOffset > common::MemoryStack_Used( &pScratch->stack ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( !common::MemoryStack_Restore( &pScratch->stack, mark.iOffset ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    pScratch->nCurrentMarkerSerial = mark.nParentMarkerSerial;
    return geometry_status_t::OK;
}

geometry_status_t GeometryScratch_QueryStats(
    const geometry_scratch_t *pScratch,
    geometry_scratch_stats_t *pStats ) noexcept
{
    if ( pStats == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pStats = {};

    if ( pScratch == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    pStats->cbBudget = pScratch->cbBudget;
    pStats->cbCapacity = common::MemoryStack_Capacity( &pScratch->stack );
    pStats->cbUsed = common::MemoryStack_Used( &pScratch->stack );
    pStats->cbRemaining = common::MemoryStack_Remaining( &pScratch->stack );
    pStats->cbHighWater = common::MemoryStack_HighWater( &pScratch->stack );
    pStats->bUsesLocalStorage =
        common::ScratchBuffer_UsesLocalStorage( &pScratch->buffer );
    pStats->bUsesFallbackAllocation =
        common::ScratchBuffer_UsesFallbackAllocation( &pScratch->buffer );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
