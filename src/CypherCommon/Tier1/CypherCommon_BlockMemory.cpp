//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BlockMemory.cpp
//  Purpose: Implements fixed-size free-list allocation over caller memory.
//  Details: An intrusive free list provides constant-time allocation while a
//           compact occupancy bitmap detects invalid and duplicate frees in O(1).
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BlockMemory.h"

#include <new>

namespace cypher::common
{

namespace
{

struct free_block_t {
    free_block_t *pNext; // Intrusive link stored inside a currently free payload.
};

usize OccupancyByteCount( usize nBlockCount ) noexcept
{
    return ( nBlockCount / 8u ) + ( ( nBlockCount % 8u ) != 0u ? 1u : 0u );
}

usize EffectiveBlockAlignment( usize nAlignment ) noexcept
{
    return nAlignment < alignof( free_block_t )
        ? alignof( free_block_t )
        : nAlignment;
}

bool_t CalculateBlockLayout(
    usize cbPayload,
    usize nAlignment,
    usize nBlockCount,
    usize &cbStrideOut,
    usize &cbOccupancyOut,
    usize &cbBlocksOut,
    usize &cbRequiredOut ) noexcept
{
    cbStrideOut = 0u;
    cbOccupancyOut = 0u;
    cbBlocksOut = 0u;
    cbRequiredOut = 0u;

    if ( cbPayload == 0u ||
         nBlockCount == 0u ||
         !Cy_AlignIsPowerOfTwo( nAlignment ) ) {
        return CY_FALSE;
    }

    const usize nEffectiveAlignment = EffectiveBlockAlignment( nAlignment );
    const usize cbNodePayload = cbPayload < sizeof( free_block_t )
        ? sizeof( free_block_t )
        : cbPayload;
    if ( !Cy_AlignUpChecked(
             cbNodePayload,
             nEffectiveAlignment,
             cbStrideOut ) ) {
        return CY_FALSE;
    }

    if ( nBlockCount > CY_USIZE_MAX / cbStrideOut ) {
        return CY_FALSE;
    }
    // Bitmap bytes lead the allocation; aligned payload strides follow them.
    cbBlocksOut = nBlockCount * cbStrideOut;
    cbOccupancyOut = OccupancyByteCount( nBlockCount );

    const usize cbAlignmentHeadroom = nEffectiveAlignment - 1u;
    if ( cbOccupancyOut > CY_USIZE_MAX - cbAlignmentHeadroom ) {
        return CY_FALSE;
    }
    const usize cbPrefix = cbOccupancyOut + cbAlignmentHeadroom;
    if ( cbBlocksOut > CY_USIZE_MAX - cbPrefix ) {
        return CY_FALSE;
    }

    cbRequiredOut = cbPrefix + cbBlocksOut;
    return CY_TRUE;
}

bool_t IsCanonicalEmpty( const block_memory_t &memory ) noexcept
{
    return memory.memory.pData == nullptr &&
           memory.memory.cbSize == 0u &&
           memory.pFreeHead == nullptr &&
           memory.pOccupancyBits == nullptr &&
           memory.cbOccupancyBits == 0u &&
           memory.cbBlockStride == 0u &&
           memory.cbPayload == 0u &&
           memory.nAlignment == 0u &&
           memory.nBlockCount == 0u &&
           memory.nFreeCount == 0u &&
           memory.nHighWaterCount == 0u;
}

bool_t IsInitialized( const block_memory_t *pMemory ) noexcept
{
    return pMemory != nullptr &&
           pMemory->memory.pData != nullptr &&
           pMemory->cbBlockStride != 0u &&
           pMemory->nBlockCount != 0u;
}

usize BlockIndexUnchecked(
    const block_memory_t &memory,
    const void *pBlock ) noexcept
{
    const usize iOffset = static_cast<usize>(
        reinterpret_cast<uintptr>( pBlock ) -
        reinterpret_cast<uintptr>( memory.memory.pData ) );
    return iOffset / memory.cbBlockStride;
}

bool_t OwnsUnchecked(
    const block_memory_t &memory,
    const void *pBlock ) noexcept
{
    if ( pBlock == nullptr ) {
        return CY_FALSE;
    }

    const uintptr nBase = reinterpret_cast<uintptr>( memory.memory.pData );
    const uintptr nAddress = reinterpret_cast<uintptr>( pBlock );
    if ( nAddress < nBase ) {
        return CY_FALSE;
    }

    const uintptr nOffset = nAddress - nBase;
    return nOffset < memory.memory.cbSize &&
           ( nOffset % memory.cbBlockStride ) == 0u;
}

bool_t OccupancyBitIsSet(
    const block_memory_t &memory,
    usize iBlock ) noexcept
{
    const usize iByte = iBlock / 8u;
    const u32 iBit = static_cast<u32>( iBlock % 8u );
    const byte nMask = static_cast<byte>( 1u << iBit );
    return ( memory.pOccupancyBits[iByte] & nMask ) != 0u;
}

void SetOccupancyBit(
    block_memory_t &memory,
    usize iBlock ) noexcept
{
    const usize iByte = iBlock / 8u;
    const u32 iBit = static_cast<u32>( iBlock % 8u );
    const byte nMask = static_cast<byte>( 1u << iBit );
    memory.pOccupancyBits[iByte] = static_cast<byte>(
        memory.pOccupancyBits[iByte] | nMask );
}

void ClearOccupancyBit(
    block_memory_t &memory,
    usize iBlock ) noexcept
{
    const usize iByte = iBlock / 8u;
    const u32 iBit = static_cast<u32>( iBlock % 8u );
    const byte nMask = static_cast<byte>( 1u << iBit );
    memory.pOccupancyBits[iByte] = static_cast<byte>(
        memory.pOccupancyBits[iByte] & static_cast<byte>( ~nMask ) );
}

void RebuildFreeList( block_memory_t &memory ) noexcept
{
    Cy_MemZero( memory.pOccupancyBits, memory.cbOccupancyBits );
    memory.pFreeHead = nullptr;

    // Build backwards so allocation returns blocks in increasing address order.
    for ( usize iBlock = memory.nBlockCount; iBlock > 0u; --iBlock ) {
        byte *pBlock = memory.memory.pData +
            ( iBlock - 1u ) * memory.cbBlockStride;
        auto *pNode = ::new ( static_cast<void *>( pBlock ) ) free_block_t{
            static_cast<free_block_t *>( memory.pFreeHead )
        };
        memory.pFreeHead = pNode;
    }

    memory.nFreeCount = memory.nBlockCount;
    memory.nHighWaterCount = 0u;
}

} // namespace

usize BlockMemory_RequiredBytes(
    usize cbPayload,
    usize nAlignment,
    usize nBlockCount ) noexcept
{
    usize cbStride = 0u;
    usize cbOccupancy = 0u;
    usize cbBlocks = 0u;
    usize cbRequired = 0u;
    return CalculateBlockLayout(
        cbPayload,
        nAlignment,
        nBlockCount,
        cbStride,
        cbOccupancy,
        cbBlocks,
        cbRequired )
        ? cbRequired
        : 0u;
}

bool_t BlockMemory_Init(
    block_memory_t *pMemory,
    byte_span_t storage,
    usize cbPayload,
    usize nAlignment,
    usize nBlockCount ) noexcept
{
    const bool_t bValidDestination =
        pMemory != nullptr && IsCanonicalEmpty( *pMemory );
    CY_ASSERT_MSG(
        bValidDestination,
        "BlockMemory_Init requires an empty destination." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    const bool_t bValidStorage = Span_IsValid( storage );
    CY_ASSERT_MSG(
        bValidStorage,
        "BlockMemory_Init requires a valid storage span." );
    if ( !bValidStorage ) {
        return CY_FALSE;
    }

    usize cbStride = 0u;
    usize cbOccupancy = 0u;
    usize cbBlocks = 0u;
    usize cbRequired = 0u;
    const bool_t bValidLayout = CalculateBlockLayout(
        cbPayload,
        nAlignment,
        nBlockCount,
        cbStride,
        cbOccupancy,
        cbBlocks,
        cbRequired );
    CY_ASSERT_MSG(
        bValidLayout,
        "BlockMemory_Init received invalid or overflowing layout values." );
    if ( !bValidLayout || storage.pData == nullptr || storage.nCount < cbRequired ) {
        return CY_FALSE;
    }

    const usize nEffectiveAlignment = EffectiveBlockAlignment( nAlignment );
    const uintptr nUnalignedBlocks = reinterpret_cast<uintptr>( storage.pData ) +
        cbOccupancy;
    usize nAlignedBlocks = 0u;
    if ( !Cy_AlignUpChecked(
             static_cast<usize>( nUnalignedBlocks ),
             nEffectiveAlignment,
             nAlignedBlocks ) ) {
        return CY_FALSE;
    }

    const usize cbPrefix = nAlignedBlocks -
        reinterpret_cast<uintptr>( storage.pData );
    if ( cbPrefix > storage.nCount ||
         cbBlocks > storage.nCount - cbPrefix ) {
        return CY_FALSE;
    }

    pMemory->memory = {
        reinterpret_cast<byte *>( nAlignedBlocks ),
        cbBlocks
    };
    pMemory->pOccupancyBits = storage.pData;
    pMemory->cbOccupancyBits = cbOccupancy;
    pMemory->cbBlockStride = cbStride;
    pMemory->cbPayload = cbPayload;
    pMemory->nAlignment = nEffectiveAlignment;
    pMemory->nBlockCount = nBlockCount;
    RebuildFreeList( *pMemory );
    return CY_TRUE;
}

void BlockMemory_Reset( block_memory_t *pMemory ) noexcept
{
    const bool_t bValidMemory = BlockMemory_IsValid( pMemory );
    CY_ASSERT_MSG( bValidMemory, "BlockMemory_Reset requires initialized memory." );
    if ( !bValidMemory ) {
        return;
    }

    RebuildFreeList( *pMemory );
}

void BlockMemory_Shutdown( block_memory_t *pMemory ) noexcept
{
    const bool_t bValidMemory = pMemory != nullptr;
    CY_ASSERT_MSG( bValidMemory, "BlockMemory_Shutdown requires an object." );
    if ( !bValidMemory ) {
        return;
    }

    *pMemory = {};
}

bool_t BlockMemory_IsValid( const block_memory_t *pMemory ) noexcept
{
    if ( pMemory == nullptr || IsCanonicalEmpty( *pMemory ) ) {
        return CY_FALSE;
    }

    if ( !FixedMemory_IsValid( pMemory->memory ) ||
         pMemory->memory.pData == nullptr ||
         pMemory->pOccupancyBits == nullptr ||
         pMemory->cbOccupancyBits != OccupancyByteCount( pMemory->nBlockCount ) ||
         pMemory->cbBlockStride == 0u ||
         pMemory->cbPayload == 0u ||
         pMemory->nBlockCount == 0u ||
         pMemory->nFreeCount > pMemory->nBlockCount ||
         pMemory->nHighWaterCount > pMemory->nBlockCount ||
         !Cy_AlignIsPowerOfTwo( pMemory->nAlignment ) ||
         !Cy_AlignIsPointerAligned( pMemory->memory.pData, pMemory->nAlignment ) ||
         !Cy_AlignIsAligned( pMemory->cbBlockStride, pMemory->nAlignment ) ||
         pMemory->nBlockCount > CY_USIZE_MAX / pMemory->cbBlockStride ) {
        return CY_FALSE;
    }

    return pMemory->memory.cbSize ==
        pMemory->nBlockCount * pMemory->cbBlockStride;
}

void *BlockMemory_Allocate( block_memory_t *pMemory ) noexcept
{
    const bool_t bInitialized = IsInitialized( pMemory );
    CY_ASSERT_MSG(
        bInitialized && BlockMemory_IsValid( pMemory ),
        "BlockMemory_Allocate received corrupted memory." );
    if ( !bInitialized || pMemory->pFreeHead == nullptr ) {
        return nullptr;
    }

    void *pBlock = pMemory->pFreeHead;
#if CYPHER_ASSERTS_ENABLED
    if ( !OwnsUnchecked( *pMemory, pBlock ) ) {
        CY_CRASH( "BlockMemory free list contains an invalid block." );
    }
#endif

    auto *pNode = static_cast<free_block_t *>( pBlock );
    pMemory->pFreeHead = pNode->pNext;

    const usize iBlock = BlockIndexUnchecked( *pMemory, pBlock );
#if CYPHER_ASSERTS_ENABLED
    if ( OccupancyBitIsSet( *pMemory, iBlock ) ) {
        CY_CRASH( "BlockMemory free list contains an allocated block." );
    }
#endif

    SetOccupancyBit( *pMemory, iBlock );
    --pMemory->nFreeCount;
    const usize nAllocatedCount = pMemory->nBlockCount - pMemory->nFreeCount;
    if ( nAllocatedCount > pMemory->nHighWaterCount ) {
        pMemory->nHighWaterCount = nAllocatedCount;
    }
    return pBlock;
}

bool_t BlockMemory_Free(
    block_memory_t *pMemory,
    void *pBlock ) noexcept
{
    const bool_t bInitialized = IsInitialized( pMemory );
    CY_ASSERT_MSG(
        bInitialized && BlockMemory_IsValid( pMemory ),
        "BlockMemory_Free received corrupted memory." );
    if ( !bInitialized || pBlock == nullptr ) {
        return CY_FALSE;
    }

    // Reject foreign/interior pointers before they can enter the free list.
    const bool_t bOwnedBlock = OwnsUnchecked( *pMemory, pBlock );
    CY_ASSERT_MSG( bOwnedBlock, "BlockMemory_Free received a foreign or interior pointer." );
    if ( !bOwnedBlock ) {
        return CY_FALSE;
    }

    const usize iBlock = BlockIndexUnchecked( *pMemory, pBlock );
    // One occupancy bit makes duplicate-free detection constant time.
    const bool_t bAllocated = OccupancyBitIsSet( *pMemory, iBlock );
    CY_ASSERT_MSG( bAllocated, "BlockMemory_Free detected a duplicate free." );
    if ( !bAllocated ) {
        return CY_FALSE;
    }

    ClearOccupancyBit( *pMemory, iBlock );
    auto *pNode = ::new ( pBlock ) free_block_t{
        static_cast<free_block_t *>( pMemory->pFreeHead )
    };
    pMemory->pFreeHead = pNode;
    ++pMemory->nFreeCount;
    return CY_TRUE;
}

bool_t BlockMemory_Owns(
    const block_memory_t *pMemory,
    const void *pBlock ) noexcept
{
    if ( !IsInitialized( pMemory ) ) {
        return CY_FALSE;
    }

    CY_ASSERT_MSG(
        BlockMemory_IsValid( pMemory ),
        "BlockMemory_Owns received corrupted memory." );
    return OwnsUnchecked( *pMemory, pBlock );
}

bool_t BlockMemory_IsAllocated(
    const block_memory_t *pMemory,
    const void *pBlock ) noexcept
{
    if ( !IsInitialized( pMemory ) || !OwnsUnchecked( *pMemory, pBlock ) ) {
        return CY_FALSE;
    }

    CY_ASSERT_MSG(
        BlockMemory_IsValid( pMemory ),
        "BlockMemory_IsAllocated received corrupted memory." );
    return OccupancyBitIsSet(
        *pMemory,
        BlockIndexUnchecked( *pMemory, pBlock ) );
}

usize BlockMemory_Capacity( const block_memory_t *pMemory ) noexcept
{
    CY_ASSERT_MSG(
        !IsInitialized( pMemory ) || BlockMemory_IsValid( pMemory ),
        "BlockMemory_Capacity received corrupted memory." );
    return IsInitialized( pMemory ) ? pMemory->nBlockCount : 0u;
}

usize BlockMemory_FreeCount( const block_memory_t *pMemory ) noexcept
{
    CY_ASSERT_MSG(
        !IsInitialized( pMemory ) || BlockMemory_IsValid( pMemory ),
        "BlockMemory_FreeCount received corrupted memory." );
    return IsInitialized( pMemory ) ? pMemory->nFreeCount : 0u;
}

usize BlockMemory_AllocatedCount( const block_memory_t *pMemory ) noexcept
{
    CY_ASSERT_MSG(
        !IsInitialized( pMemory ) || BlockMemory_IsValid( pMemory ),
        "BlockMemory_AllocatedCount received corrupted memory." );
    return IsInitialized( pMemory )
        ? pMemory->nBlockCount - pMemory->nFreeCount
        : 0u;
}

usize BlockMemory_HighWaterCount( const block_memory_t *pMemory ) noexcept
{
    CY_ASSERT_MSG(
        !IsInitialized( pMemory ) || BlockMemory_IsValid( pMemory ),
        "BlockMemory_HighWaterCount received corrupted memory." );
    return IsInitialized( pMemory ) ? pMemory->nHighWaterCount : 0u;
}

} // namespace cypher::common
