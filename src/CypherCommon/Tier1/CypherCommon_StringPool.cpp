//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPool.cpp
//  Purpose: Implements allocator-backed stable string interning.
//  Details: Geometric storage blocks preserve returned addresses while a Robin Hood
//           hash map supplies bounded lookup and complete collision-safe comparison.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringPool.h"

#include "CypherCommon_Hash.h"
#include "CypherCommon_HashMap.h"

#include <new>

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_STRING_POOL_VALID_FLAGS =
    STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII;

struct string_pool_hasher_t {
    bool_t bCaseInsensitiveAscii{ CY_FALSE }; // Must match equality policy exactly.

    hash64_t operator()( string_view_t text ) const noexcept
    {
        return bCaseInsensitiveAscii
            ? Hash64_StringInsensitiveAscii( text )
            : Hash64_String( text );
    }
};

struct string_pool_equal_t {
    bool_t bCaseInsensitiveAscii{ CY_FALSE }; // Locale-independent ASCII folding mode.
    usize *pCollisionCount{ nullptr };        // Optional unequal-key comparison counter.

    bool_t operator()(
        string_view_t left,
        string_view_t right ) const noexcept
    {
        const bool_t bEqual = bCaseInsensitiveAscii
            ? StringView_EqualsInsensitiveAscii( left, right )
            : StringView_Equals( left, right );
        if ( !bEqual && pCollisionCount != nullptr &&
             *pCollisionCount != CY_USIZE_MAX ) {
            ++*pCollisionCount;
        }
        return bEqual;
    }
};

using string_pool_map_t = hash_map_t<
    string_view_t,
    const char *,
    string_pool_hasher_t,
    string_pool_equal_t>;

struct string_pool_block_t {
    string_pool_block_t *pNext{ nullptr }; // Older stable-storage block.
    usize cbCapacity{ 0u };                // Bytes immediately following this header.
    usize cbUsed{ 0u };                    // Prefix occupied by terminated interned strings.
};

char *StringPool_BlockData( string_pool_block_t *pBlock ) noexcept
{
    return reinterpret_cast<char *>( pBlock + 1u );
}

bool_t StringPool_BlockAllocationSize(
    usize cbCapacity,
    usize &cbAllocationOut ) noexcept
{
    if ( cbCapacity == 0u ||
         cbCapacity > CY_USIZE_MAX - sizeof( string_pool_block_t ) ) {
        cbAllocationOut = 0u;
        return CY_FALSE;
    }
    cbAllocationOut = sizeof( string_pool_block_t ) + cbCapacity;
    return CY_TRUE;
}

} // namespace

struct string_pool_t {
    string_pool_map_t strings{};              // Text key to stable canonical address.
    string_pool_block_t *pBlocks{ nullptr };   // Newest geometric storage block.
    const allocator_t *pAllocator{ nullptr };  // Owns map, blocks, and pool object.
    usize cbInitialBlock{ 0u };                // Growth baseline restored by Clear.
    usize cbNextBlock{ 0u };                   // Target capacity for the next block.
    string_pool_stats_t stats{};               // Validated accounting exposed to callers.
    flags32_t flags{ STRING_POOL_FLAG_NONE };  // Interning/equality policy.
};

namespace
{

bool_t StringPool_IsUsable( const string_pool_t *pPool ) noexcept
{
    return pPool != nullptr &&
           Allocator_IsValid( pPool->pAllocator ) &&
           pPool->cbInitialBlock > 0u &&
           pPool->cbNextBlock > 0u &&
           ( pPool->flags & ~CY_STRING_POOL_VALID_FLAGS ) == 0u &&
           HashMap_IsValid( &pPool->strings );
}

void StringPool_FreeBlock(
    string_pool_t *pPool,
    string_pool_block_t *pBlock ) noexcept
{
    usize cbAllocation = 0u;
    const bool_t bValidSize = StringPool_BlockAllocationSize(
        pBlock->cbCapacity,
        cbAllocation );
    CY_ASSERT_MSG(
        bValidSize,
        "StringPool block metadata is corrupt." );
    if ( !bValidSize ) {
        return;
    }
    pBlock->~string_pool_block_t();
    Allocator_Free(
        pPool->pAllocator,
        pBlock,
        cbAllocation,
        alignof( string_pool_block_t ) );
}

void StringPool_FreeBlocks( string_pool_t *pPool ) noexcept
{
    string_pool_block_t *pBlock = pPool->pBlocks;
    while ( pBlock != nullptr ) {
        string_pool_block_t *pNext = pBlock->pNext;
        StringPool_FreeBlock( pPool, pBlock );
        pBlock = pNext;
    }
    pPool->pBlocks = nullptr;
}

string_pool_block_t *StringPool_AllocateBlock(
    string_pool_t *pPool,
    usize cbRequired ) noexcept
{
    usize cbCapacity = pPool->cbNextBlock;
    if ( cbCapacity < cbRequired ) {
        cbCapacity = cbRequired;
    }

    usize cbAllocation = 0u;
    if ( !StringPool_BlockAllocationSize( cbCapacity, cbAllocation ) ||
         cbCapacity > CY_USIZE_MAX - pPool->stats.cbReserved ) {
        CY_ASSERT_MSG(
            CY_FALSE,
            "StringPool block capacity overflowed." );
        return nullptr;
    }

    void *pMemory = Allocator_Allocate(
        pPool->pAllocator,
        cbAllocation,
        alignof( string_pool_block_t ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }

    string_pool_block_t *pBlock =
        ::new ( pMemory ) string_pool_block_t{};
    pBlock->pNext = pPool->pBlocks;
    pBlock->cbCapacity = cbCapacity;
    pPool->pBlocks = pBlock;
    pPool->stats.cbReserved += cbCapacity;

    // Blocks never move, so all previously returned pointers remain stable.
    // Geometric growth reduces allocation frequency for large symbol sets.
    pPool->cbNextBlock = cbCapacity <= CY_USIZE_MAX / 2u
        ? cbCapacity * 2u
        : cbCapacity;
    return pBlock;
}

void StringPool_RollbackBlockWrite(
    string_pool_t *pPool,
    string_pool_block_t *pBlock,
    usize cbOldUsed,
    bool_t bNewBlock,
    usize cbOldNextBlock ) noexcept
{
    if ( !bNewBlock ) {
        pBlock->cbUsed = cbOldUsed;
        return;
    }

    CY_ASSERT_MSG(
        pPool->pBlocks == pBlock && cbOldUsed == 0u,
        "StringPool rollback requires the newest empty block." );
    pPool->pBlocks = pBlock->pNext;
    pPool->stats.cbReserved -= pBlock->cbCapacity;
    pPool->cbNextBlock = cbOldNextBlock;
    StringPool_FreeBlock( pPool, pBlock );
}

} // namespace

string_pool_t *StringPool_Create( const string_pool_desc_t &desc ) noexcept
{
    const bool_t bValidAllocator = Allocator_IsValid( desc.pAllocator );
    usize cbInitialAllocation = 0u;
    const bool_t bValidBlockSize = StringPool_BlockAllocationSize(
        desc.cbInitialBlock,
        cbInitialAllocation );
    const bool_t bValidFlags =
        ( desc.flags & ~CY_STRING_POOL_VALID_FLAGS ) == 0u;
    CY_ASSERT_MSG(
        bValidAllocator,
        "StringPool_Create requires a valid allocator." );
    CY_ASSERT_MSG(
        bValidBlockSize,
        "StringPool_Create requires a non-zero initial block size." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringPool_Create received unknown flags." );
    if ( !bValidAllocator || !bValidBlockSize || !bValidFlags ) {
        return nullptr;
    }

    void *pMemory = Allocator_Allocate(
        desc.pAllocator,
        sizeof( string_pool_t ),
        alignof( string_pool_t ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }

    string_pool_t *pPool = ::new ( pMemory ) string_pool_t{};
    pPool->pAllocator = desc.pAllocator;
    pPool->cbInitialBlock = desc.cbInitialBlock;
    pPool->cbNextBlock = desc.cbInitialBlock;
    pPool->flags = desc.flags;
    const bool_t bCaseInsensitive =
        ( desc.flags & STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII ) != 0u;
    const bool_t bMapInitialized = HashMap_Init(
        &pPool->strings,
        desc.pAllocator,
        desc.nInitialBuckets,
        string_pool_hasher_t{ bCaseInsensitive },
        string_pool_equal_t{
            bCaseInsensitive,
            &pPool->stats.nCollisions
        } );
    if ( !bMapInitialized ) {
        pPool->~string_pool_t();
        Allocator_Free(
            desc.pAllocator,
            pPool,
            sizeof( string_pool_t ),
            alignof( string_pool_t ) );
        return nullptr;
    }
    return pPool;
}

void StringPool_Destroy( string_pool_t *pPool ) noexcept
{
    if ( pPool == nullptr ) {
        return;
    }
    const bool_t bValidPool = StringPool_IsValid( pPool );
    CY_ASSERT_MSG(
        bValidPool,
        "StringPool_Destroy requires a valid pool." );
    if ( !bValidPool ) {
        return;
    }

    const allocator_t *pAllocator = pPool->pAllocator;
    HashMap_Shutdown( &pPool->strings );
    StringPool_FreeBlocks( pPool );
    pPool->~string_pool_t();
    Allocator_Free(
        pAllocator,
        pPool,
        sizeof( string_pool_t ),
        alignof( string_pool_t ) );
}

void StringPool_Clear( string_pool_t *pPool ) noexcept
{
    const bool_t bValidPool = StringPool_IsValid( pPool );
    CY_ASSERT_MSG(
        bValidPool,
        "StringPool_Clear requires a valid pool." );
    if ( !bValidPool ) {
        return;
    }

    HashMap_Clear( &pPool->strings );
    StringPool_FreeBlocks( pPool );
    pPool->cbNextBlock = pPool->cbInitialBlock;
    pPool->stats = {};
}

bool_t StringPool_IsValid( const string_pool_t *pPool ) noexcept
{
    if ( !StringPool_IsUsable( pPool ) ||
         pPool->stats.nStrings != HashMap_Count( &pPool->strings ) ||
         pPool->stats.cbStringData > pPool->stats.cbReserved ) {
        return CY_FALSE;
    }

    usize cbReserved = 0u;
    usize cbUsed = 0u;
    for ( const string_pool_block_t *pBlock = pPool->pBlocks;
          pBlock != nullptr;
          pBlock = pBlock->pNext ) {
        if ( pBlock->cbCapacity == 0u ||
             pBlock->cbUsed > pBlock->cbCapacity ||
             pBlock->cbCapacity > CY_USIZE_MAX - cbReserved ) {
            return CY_FALSE;
        }
        cbReserved += pBlock->cbCapacity;
        if ( pBlock->cbUsed > CY_USIZE_MAX - cbUsed ) {
            return CY_FALSE;
        }
        cbUsed += pBlock->cbUsed;
    }
    return cbReserved == pPool->stats.cbReserved &&
           cbUsed == pPool->stats.cbStringData;
}

const char *StringPool_Intern(
    string_pool_t *pPool,
    string_view_t text ) noexcept
{
    const bool_t bValidPool = StringPool_IsUsable( pPool );
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidSize =
        !bValidText || text.cchLength != CY_USIZE_MAX;
    CY_ASSERT_MSG(
        bValidPool,
        "StringPool_Intern requires a valid pool." );
    CY_ASSERT_MSG(
        bValidText,
        "StringPool_Intern requires a valid string view." );
    CY_ASSERT_MSG(
        bValidSize,
        "StringPool_Intern text length overflowed." );
    if ( !bValidPool || !bValidText || !bValidSize ) {
        return nullptr;
    }

    if ( const char *const *ppExisting =
             HashMap_Find( &pPool->strings, text ) ) {
        return *ppExisting;
    }
    if ( pPool->stats.nStrings == CY_USIZE_MAX ||
         !HashMap_Reserve(
             &pPool->strings,
             pPool->stats.nStrings + 1u ) ) {
        return nullptr;
    }

    const usize cbRequired = text.cchLength + 1u;
    string_pool_block_t *pBlock = pPool->pBlocks;
    bool_t bNewBlock = CY_FALSE;
    const usize cbOldNextBlock = pPool->cbNextBlock;
    if ( pBlock == nullptr ||
         cbRequired > pBlock->cbCapacity - pBlock->cbUsed ) {
        pBlock = StringPool_AllocateBlock( pPool, cbRequired );
        bNewBlock = CY_TRUE;
    }
    if ( pBlock == nullptr ) {
        return nullptr;
    }

    const usize cbOldUsed = pBlock->cbUsed;
    char *pStored = StringPool_BlockData( pBlock ) + cbOldUsed;
    if ( text.cchLength > 0u ) {
        Cy_MemMove( pStored, text.pData, text.cchLength );
    }
    pStored[text.cchLength] = '\0';
    pBlock->cbUsed += cbRequired;

    const string_view_t storedView{ pStored, text.cchLength };
    const char *pStoredValue = pStored;
    const hash_table_insert_result_t<const char *> inserted =
        HashMap_Insert( &pPool->strings, storedView, pStoredValue );
    // Storage and index insertion form one transaction. If map insertion fails,
    // restore the previous block cursor or free a block allocated for this string.
    if ( inserted.pValue == nullptr || !inserted.bInserted ) {
        StringPool_RollbackBlockWrite(
            pPool,
            pBlock,
            cbOldUsed,
            bNewBlock,
            cbOldNextBlock );
        return inserted.pValue != nullptr ? *inserted.pValue : nullptr;
    }

    ++pPool->stats.nStrings;
    pPool->stats.cbStringData += cbRequired;
    return pStored;
}

const char *StringPool_Find(
    const string_pool_t *pPool,
    string_view_t text ) noexcept
{
    const bool_t bValidPool = StringPool_IsUsable( pPool );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidPool,
        "StringPool_Find requires a valid pool." );
    CY_ASSERT_MSG(
        bValidText,
        "StringPool_Find requires a valid string view." );
    if ( !bValidPool || !bValidText ) {
        return nullptr;
    }

    const char *const *ppValue = HashMap_Find( &pPool->strings, text );
    return ppValue != nullptr ? *ppValue : nullptr;
}

bool_t StringPool_Contains(
    const string_pool_t *pPool,
    string_view_t text ) noexcept
{
    return StringPool_Find( pPool, text ) != nullptr;
}

string_pool_stats_t StringPool_Stats(
    const string_pool_t *pPool ) noexcept
{
    const bool_t bValidPool = StringPool_IsValid( pPool );
    CY_ASSERT_MSG(
        bValidPool,
        "StringPool_Stats requires a valid pool." );
    return bValidPool ? pPool->stats : string_pool_stats_t{};
}

} // namespace cypher::common
