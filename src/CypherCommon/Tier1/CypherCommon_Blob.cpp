//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Blob.cpp
//  Purpose: Implements allocator-backed owning byte storage.
//  Details: Growth is transactional, overlapping internal sources are rebased,
//           and release transfers complete allocator provenance to the caller.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Blob.h"

#include <limits>

namespace cypher::common
{

namespace
{

bool_t BlobIsCanonicalEmpty( const blob_t &blob ) noexcept
{
    return blob.pData == nullptr &&
           blob.cbSize == 0u &&
           blob.cbCapacity == 0u &&
           blob.pAllocator == nullptr;
}

void ResetBlob( blob_t &blob ) noexcept
{
    blob.pData = nullptr;
    blob.cbSize = 0u;
    blob.cbCapacity = 0u;
    blob.pAllocator = nullptr;
}

bool_t CalculateBlobGrowth(
    usize cbCurrentCapacity,
    usize cbRequiredCapacity,
    usize &cbCapacityOut ) noexcept
{
    constexpr usize cbMinimumCapacity = 64u;
    usize cbCandidate = cbCurrentCapacity < cbMinimumCapacity
        ? cbMinimumCapacity
        : cbCurrentCapacity;
    if ( cbCandidate < cbRequiredCapacity ) {
        const usize cbHalf = cbCandidate / 2u;
        cbCandidate = cbCandidate > CY_USIZE_MAX - cbHalf
            ? CY_USIZE_MAX
            : cbCandidate + cbHalf;
    }
    if ( cbCandidate < cbRequiredCapacity ) {
        cbCandidate = cbRequiredCapacity;
    }

    cbCapacityOut = cbCandidate;
    return cbCandidate >= cbRequiredCapacity;
}

bool_t EnsureBlobCapacity(
    blob_t *pBlob,
    usize cbRequiredCapacity ) noexcept
{
    if ( cbRequiredCapacity <= pBlob->cbCapacity ) {
        return CY_TRUE;
    }

    usize cbGrowthCapacity = 0u;
    const bool_t bValidGrowth = CalculateBlobGrowth(
        pBlob->cbCapacity,
        cbRequiredCapacity,
        cbGrowthCapacity );
    CY_ASSERT_MSG( bValidGrowth, "Blob growth exceeds addressable storage." );
    return bValidGrowth && Blob_Reserve( pBlob, cbGrowthCapacity );
}

bool_t RebaseBlobSource(
    const blob_t &blob,
    binary_block_t source,
    bool_t &bInternalOut,
    usize &iSourceOffsetOut ) noexcept
{
    bInternalOut = CY_FALSE;
    iSourceOffsetOut = 0u;
    if ( source.cbSize == 0u || blob.pData == nullptr ) {
        return CY_TRUE;
    }

    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nStorageBegin = reinterpret_cast<uintptr>( blob.pData );
    const uintptr nSourceBegin = reinterpret_cast<uintptr>( source.pData );
    if ( nStorageBegin > nMaximumAddress - blob.cbCapacity ||
         nSourceBegin > nMaximumAddress - source.cbSize ) {
        return CY_FALSE;
    }
    const uintptr nStorageEnd = nStorageBegin + blob.cbCapacity;
    const uintptr nSourceEnd = nSourceBegin + source.cbSize;
    const bool_t bOverlapsStorage =
        nSourceBegin < nStorageEnd && nStorageBegin < nSourceEnd;
    if ( !bOverlapsStorage ) {
        return CY_TRUE;
    }

    const bool_t bInsideLogicalBytes =
        nSourceBegin >= nStorageBegin &&
        nSourceEnd <= nStorageBegin + blob.cbSize;
    CY_ASSERT_MSG(
        bInsideLogicalBytes,
        "Blob source may only overlap logical bytes owned by the blob." );
    if ( !bInsideLogicalBytes ) {
        return CY_FALSE;
    }

    bInternalOut = CY_TRUE;
    iSourceOffsetOut = nSourceBegin - nStorageBegin;
    return CY_TRUE;
}

} // namespace

blob_t::~blob_t() noexcept
{
    Blob_Shutdown( this );
}

bool_t Blob_Init(
    blob_t *pBlob,
    const allocator_t *pAllocator,
    usize cbInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pBlob != nullptr && BlobIsCanonicalEmpty( *pBlob );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "Blob_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "Blob_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    if ( cbInitialCapacity == 0u ) {
        pBlob->pAllocator = pAllocator;
        return CY_TRUE;
    }

    byte *pData = static_cast<byte *>( Allocator_Allocate(
        pAllocator,
        cbInitialCapacity,
        alignof( byte ) ) );
    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    pBlob->pData = pData;
    pBlob->cbCapacity = cbInitialCapacity;
    pBlob->pAllocator = pAllocator;
    return CY_TRUE;
}

void Blob_Shutdown( blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Shutdown requires a valid blob." );
    if ( !bValidBlob ) {
        return;
    }

    if ( pBlob->pData != nullptr ) {
        Allocator_Free(
            pBlob->pAllocator,
            pBlob->pData,
            pBlob->cbCapacity,
            alignof( byte ) );
    }
    ResetBlob( *pBlob );
}

void Blob_Clear( blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Clear requires a valid blob." );
    if ( bValidBlob ) {
        pBlob->cbSize = 0u;
    }
}

bool_t Blob_IsValid( const blob_t *pBlob ) noexcept
{
    if ( pBlob == nullptr ) {
        return CY_FALSE;
    }
    if ( pBlob->pData == nullptr ) {
        return pBlob->cbSize == 0u &&
               pBlob->cbCapacity == 0u &&
               ( pBlob->pAllocator == nullptr ||
                 Allocator_IsValid( pBlob->pAllocator ) );
    }

    return pBlob->cbCapacity > 0u &&
           pBlob->cbSize <= pBlob->cbCapacity &&
           Allocator_IsValid( pBlob->pAllocator );
}

bool_t Blob_IsEmpty( const blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_IsEmpty requires a valid blob." );
    return bValidBlob ? pBlob->cbSize == 0u : CY_TRUE;
}

byte *Blob_Data( blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Data requires a valid blob." );
    return bValidBlob ? pBlob->pData : nullptr;
}

const byte *Blob_Data( const blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Data requires a valid blob." );
    return bValidBlob ? pBlob->pData : nullptr;
}

usize Blob_Size( const blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Size requires a valid blob." );
    return bValidBlob ? pBlob->cbSize : 0u;
}

usize Blob_Capacity( const blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Capacity requires a valid blob." );
    return bValidBlob ? pBlob->cbCapacity : 0u;
}

bool_t Blob_Reserve( blob_t *pBlob, usize cbCapacity ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    const bool_t bAllocatorBound =
        bValidBlob && Allocator_IsValid( pBlob->pAllocator );
    CY_ASSERT_MSG( bValidBlob, "Blob_Reserve requires a valid blob." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Blob_Reserve requires an initialized allocator binding." );
    if ( !bValidBlob || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( cbCapacity <= pBlob->cbCapacity ) {
        return CY_TRUE;
    }

    void *pNewData = Allocator_Reallocate(
        pBlob->pAllocator,
        pBlob->pData,
        pBlob->cbCapacity,
        cbCapacity,
        alignof( byte ) );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    pBlob->pData = static_cast<byte *>( pNewData );
    pBlob->cbCapacity = cbCapacity;
    return CY_TRUE;
}

bool_t Blob_Resize(
    blob_t *pBlob,
    usize cbSize,
    byte fill ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Resize requires a valid blob." );
    if ( !bValidBlob || !Allocator_IsValid( pBlob->pAllocator ) ) {
        return CY_FALSE;
    }

    const usize cbOldSize = pBlob->cbSize;
    if ( cbSize > cbOldSize ) {
        if ( !EnsureBlobCapacity( pBlob, cbSize ) ) {
            return CY_FALSE;
        }
        Cy_MemSet( pBlob->pData + cbOldSize, fill, cbSize - cbOldSize );
    }
    pBlob->cbSize = cbSize;
    return CY_TRUE;
}

bool_t Blob_Assign( blob_t *pBlob, binary_block_t source ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    CY_ASSERT_MSG( bValidBlob, "Blob_Assign requires a valid blob." );
    CY_ASSERT_MSG( bValidSource, "Blob_Assign requires a valid source block." );
    if ( !bValidBlob || !bValidSource ||
         !Allocator_IsValid( pBlob->pAllocator ) ) {
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !RebaseBlobSource(
             *pBlob,
             source,
             bInternalSource,
             iSourceOffset ) ) {
        return CY_FALSE;
    }
    if ( !EnsureBlobCapacity( pBlob, source.cbSize ) ) {
        return CY_FALSE;
    }

    const byte *pSource = bInternalSource
        ? pBlob->pData + iSourceOffset
        : source.pData;
    if ( source.cbSize > 0u ) {
        Cy_MemMove( pBlob->pData, pSource, source.cbSize );
    }
    pBlob->cbSize = source.cbSize;
    return CY_TRUE;
}

bool_t Blob_Append( blob_t *pBlob, binary_block_t source ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    CY_ASSERT_MSG( bValidBlob, "Blob_Append requires a valid blob." );
    CY_ASSERT_MSG( bValidSource, "Blob_Append requires a valid source block." );
    if ( !bValidBlob || !bValidSource ||
         !Allocator_IsValid( pBlob->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( source.cbSize == 0u ) {
        return CY_TRUE;
    }
    if ( source.cbSize > CY_USIZE_MAX - pBlob->cbSize ) {
        CY_ASSERT_MSG( CY_FALSE, "Blob append size overflowed." );
        return CY_FALSE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSourceOffset = 0u;
    if ( !RebaseBlobSource(
             *pBlob,
             source,
             bInternalSource,
             iSourceOffset ) ) {
        return CY_FALSE;
    }
    const usize cbNewSize = pBlob->cbSize + source.cbSize;
    if ( !EnsureBlobCapacity( pBlob, cbNewSize ) ) {
        return CY_FALSE;
    }

    const byte *pSource = bInternalSource
        ? pBlob->pData + iSourceOffset
        : source.pData;
    Cy_MemMove( pBlob->pData + pBlob->cbSize, pSource, source.cbSize );
    pBlob->cbSize = cbNewSize;
    return CY_TRUE;
}

binary_block_t Blob_Block( const blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Block requires a valid blob." );
    return bValidBlob
        ? binary_block_t{ pBlob->pData, pBlob->cbSize }
        : binary_block_t{};
}

byte_span_t Blob_WritableSpan( blob_t *pBlob ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_WritableSpan requires a valid blob." );
    return bValidBlob
        ? byte_span_t{ pBlob->pData, pBlob->cbSize }
        : byte_span_t{};
}

void Blob_Move( blob_t *pDest, blob_t *pSource ) noexcept
{
    const bool_t bDistinctBlobs =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctBlobs && BlobIsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctBlobs && Blob_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctBlobs && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "Blob_Move requires distinct blobs and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    pDest->pData = pSource->pData;
    pDest->cbSize = pSource->cbSize;
    pDest->cbCapacity = pSource->cbCapacity;
    pDest->pAllocator = pSource->pAllocator;
    ResetBlob( *pSource );
}

owned_allocation_t Blob_Release(
    blob_t *pBlob,
    usize *pcbLogicalSizeOut ) noexcept
{
    const bool_t bValidBlob = Blob_IsValid( pBlob );
    CY_ASSERT_MSG( bValidBlob, "Blob_Release requires a valid blob." );
    if ( !bValidBlob ) {
        if ( pcbLogicalSizeOut != nullptr ) {
            *pcbLogicalSizeOut = 0u;
        }
        return {};
    }

    if ( pcbLogicalSizeOut != nullptr ) {
        *pcbLogicalSizeOut = pBlob->cbSize;
    }
    owned_allocation_t allocation{};
    if ( pBlob->pData != nullptr ) {
        const bool_t bAdopted = Allocator_AdoptOwned(
            &allocation,
            pBlob->pAllocator,
            pBlob->pData,
            pBlob->cbCapacity,
            alignof( byte ) );
        if ( !bAdopted ) {
            return {};
        }
    }
    ResetBlob( *pBlob );
    return allocation;
}

} // namespace cypher::common
