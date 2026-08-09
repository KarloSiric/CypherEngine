//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Symbol.cpp
//  Purpose: Implements collision-safe interned symbols and symbol tables.
//  Details: Stable pooled text is indexed by compact generation-tagged symbols so
//           table clears invalidate stale identifiers while lookup remains constant.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Symbol.h"

#include "CypherCommon_HashMap.h"
#include "CypherCommon_StringPool.h"
#include "CypherCommon_Vector.h"

#include <new>

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_SYMBOL_TABLE_VALID_FLAGS =
    SYMBOL_TABLE_FLAG_CASE_INSENSITIVE_ASCII;

using symbol_lookup_t = hash_map_t<const char *, symbol_t>;

u32 Symbol_NextGeneration( u32 nGeneration ) noexcept
{
    nGeneration = ( nGeneration + 1u ) & CY_SYMBOL_GENERATION_MASK;
    return nGeneration != 0u ? nGeneration : 1u;
}

symbol_t Symbol_Make( u32 nGeneration, usize iIndex ) noexcept
{
    const u32 nStoredIndex = static_cast<u32>( iIndex + 1u );
    return {
        ( nGeneration << CY_SYMBOL_INDEX_BITS ) | nStoredIndex
    };
}

u32 Symbol_Generation( symbol_t symbol ) noexcept
{
    return symbol.value >> CY_SYMBOL_INDEX_BITS;
}

usize Symbol_Index( symbol_t symbol ) noexcept
{
    return static_cast<usize>(
        ( symbol.value & CY_SYMBOL_INDEX_MASK ) - 1u );
}

} // namespace

struct symbol_table_t {
    string_pool_t *pStringPool{ nullptr };
    vector_t<string_view_t> symbols{};
    symbol_lookup_t lookup{};
    const allocator_t *pAllocator{ nullptr };
    u32 nGeneration{ 1u };
    flags32_t flags{ SYMBOL_TABLE_FLAG_NONE };
};

namespace
{

bool_t SymbolTable_IsUsable( const symbol_table_t *pTable ) noexcept
{
    return pTable != nullptr &&
           Allocator_IsValid( pTable->pAllocator ) &&
           pTable->pStringPool != nullptr &&
           pTable->nGeneration > 0u &&
           pTable->nGeneration <= CY_SYMBOL_GENERATION_MASK &&
           ( pTable->flags & ~CY_SYMBOL_TABLE_VALID_FLAGS ) == 0u &&
           Vector_IsValid( &pTable->symbols ) &&
           HashMap_IsValid( &pTable->lookup );
}

} // namespace

symbol_table_t *SymbolTable_Create(
    const symbol_table_desc_t &desc ) noexcept
{
    const bool_t bValidAllocator = Allocator_IsValid( desc.pAllocator );
    const bool_t bValidCapacity =
        desc.nInitialCapacity <= CY_SYMBOL_MAX_COUNT;
    const bool_t bValidFlags =
        ( desc.flags & ~CY_SYMBOL_TABLE_VALID_FLAGS ) == 0u;
    CY_ASSERT_MSG(
        bValidAllocator,
        "SymbolTable_Create requires a valid allocator." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "SymbolTable_Create initial capacity exceeds symbol index space." );
    CY_ASSERT_MSG(
        bValidFlags,
        "SymbolTable_Create received unknown flags." );
    if ( !bValidAllocator || !bValidCapacity || !bValidFlags ) {
        return nullptr;
    }

    void *pMemory = Allocator_Allocate(
        desc.pAllocator,
        sizeof( symbol_table_t ),
        alignof( symbol_table_t ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }

    symbol_table_t *pTable = ::new ( pMemory ) symbol_table_t{};
    pTable->pAllocator = desc.pAllocator;
    pTable->flags = desc.flags;

    string_pool_desc_t poolDesc{};
    poolDesc.pAllocator = desc.pAllocator;
    poolDesc.nInitialBuckets = desc.nInitialCapacity;
    poolDesc.flags =
        ( desc.flags & SYMBOL_TABLE_FLAG_CASE_INSENSITIVE_ASCII ) != 0u
        ? STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII
        : STRING_POOL_FLAG_NONE;
    pTable->pStringPool = StringPool_Create( poolDesc );
    if ( pTable->pStringPool == nullptr ) {
        pTable->~symbol_table_t();
        Allocator_Free(
            desc.pAllocator,
            pTable,
            sizeof( symbol_table_t ),
            alignof( symbol_table_t ) );
        return nullptr;
    }

    if ( !Vector_Init(
             &pTable->symbols,
             desc.pAllocator,
             desc.nInitialCapacity ) ) {
        StringPool_Destroy( pTable->pStringPool );
        pTable->pStringPool = nullptr;
        pTable->~symbol_table_t();
        Allocator_Free(
            desc.pAllocator,
            pTable,
            sizeof( symbol_table_t ),
            alignof( symbol_table_t ) );
        return nullptr;
    }
    if ( !HashMap_Init(
             &pTable->lookup,
             desc.pAllocator,
             desc.nInitialCapacity ) ) {
        Vector_Shutdown( &pTable->symbols );
        StringPool_Destroy( pTable->pStringPool );
        pTable->pStringPool = nullptr;
        pTable->~symbol_table_t();
        Allocator_Free(
            desc.pAllocator,
            pTable,
            sizeof( symbol_table_t ),
            alignof( symbol_table_t ) );
        return nullptr;
    }
    return pTable;
}

void SymbolTable_Destroy( symbol_table_t *pTable ) noexcept
{
    if ( pTable == nullptr ) {
        return;
    }
    const bool_t bValidTable = SymbolTable_IsValid( pTable );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Destroy requires a valid table." );
    if ( !bValidTable ) {
        return;
    }

    const allocator_t *pAllocator = pTable->pAllocator;
    HashMap_Shutdown( &pTable->lookup );
    Vector_Shutdown( &pTable->symbols );
    StringPool_Destroy( pTable->pStringPool );
    pTable->pStringPool = nullptr;
    pTable->~symbol_table_t();
    Allocator_Free(
        pAllocator,
        pTable,
        sizeof( symbol_table_t ),
        alignof( symbol_table_t ) );
}

void SymbolTable_Clear( symbol_table_t *pTable ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsValid( pTable );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Clear requires a valid table." );
    if ( !bValidTable ) {
        return;
    }

    HashMap_Clear( &pTable->lookup );
    Vector_Clear( &pTable->symbols );
    StringPool_Clear( pTable->pStringPool );
    pTable->nGeneration = Symbol_NextGeneration( pTable->nGeneration );
}

bool_t SymbolTable_IsValid( const symbol_table_t *pTable ) noexcept
{
    if ( !SymbolTable_IsUsable( pTable ) ||
         !StringPool_IsValid( pTable->pStringPool ) ) {
        return CY_FALSE;
    }
    const usize nSymbols = Vector_Count( &pTable->symbols );
    return nSymbols <= CY_SYMBOL_MAX_COUNT &&
           nSymbols == HashMap_Count( &pTable->lookup ) &&
           nSymbols == StringPool_Stats( pTable->pStringPool ).nStrings;
}

symbol_t SymbolTable_Intern(
    symbol_table_t *pTable,
    string_view_t text ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsUsable( pTable );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Intern requires a valid table." );
    CY_ASSERT_MSG(
        bValidText,
        "SymbolTable_Intern requires a valid string view." );
    if ( !bValidTable || !bValidText ) {
        return CY_SYMBOL_INVALID;
    }

    if ( const char *pExisting =
             StringPool_Find( pTable->pStringPool, text ) ) {
        const symbol_t *pSymbol = HashMap_Find(
            &pTable->lookup,
            pExisting );
        CY_ASSERT_MSG(
            pSymbol != nullptr,
            "SymbolTable lookup is missing an interned string." );
        return pSymbol != nullptr ? *pSymbol : CY_SYMBOL_INVALID;
    }

    const usize nCount = Vector_Count( &pTable->symbols );
    if ( nCount >= CY_SYMBOL_MAX_COUNT ) {
        CY_ASSERT_MSG( CY_FALSE, "SymbolTable exhausted its symbol index space." );
        return CY_SYMBOL_INVALID;
    }
    if ( !Vector_Reserve( &pTable->symbols, nCount + 1u ) ||
         !HashMap_Reserve( &pTable->lookup, nCount + 1u ) ) {
        return CY_SYMBOL_INVALID;
    }

    const char *pStored = StringPool_Intern( pTable->pStringPool, text );
    if ( pStored == nullptr ) {
        return CY_SYMBOL_INVALID;
    }

    const symbol_t symbol = Symbol_Make( pTable->nGeneration, nCount );
    const string_view_t storedView{ pStored, text.cchLength };
    const bool_t bPushed = Vector_PushBack(
        &pTable->symbols,
        storedView );
    CY_ASSERT_MSG(
        bPushed,
        "SymbolTable vector append failed after capacity was secured." );
    if ( !bPushed ) {
        return CY_SYMBOL_INVALID;
    }

    const hash_table_insert_result_t<symbol_t> inserted =
        HashMap_Insert( &pTable->lookup, pStored, symbol );
    CY_ASSERT_MSG(
        inserted.pValue != nullptr,
        "SymbolTable lookup insertion failed after capacity was secured." );
    if ( inserted.pValue == nullptr ) {
        Vector_PopBack( &pTable->symbols );
        return CY_SYMBOL_INVALID;
    }
    if ( !inserted.bInserted ) {
        Vector_PopBack( &pTable->symbols );
        return *inserted.pValue;
    }
    return symbol;
}

symbol_t SymbolTable_Find(
    const symbol_table_t *pTable,
    string_view_t text ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsUsable( pTable );
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Find requires a valid table." );
    CY_ASSERT_MSG(
        bValidText,
        "SymbolTable_Find requires a valid string view." );
    if ( !bValidTable || !bValidText ) {
        return CY_SYMBOL_INVALID;
    }

    const char *pStored = StringPool_Find( pTable->pStringPool, text );
    if ( pStored == nullptr ) {
        return CY_SYMBOL_INVALID;
    }
    const symbol_t *pSymbol = HashMap_Find( &pTable->lookup, pStored );
    CY_ASSERT_MSG(
        pSymbol != nullptr,
        "SymbolTable lookup is missing an interned string." );
    return pSymbol != nullptr ? *pSymbol : CY_SYMBOL_INVALID;
}

string_view_t SymbolTable_Resolve(
    const symbol_table_t *pTable,
    symbol_t symbol ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsUsable( pTable );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Resolve requires a valid table." );
    if ( !bValidTable || !SymbolTable_Contains( pTable, symbol ) ) {
        return {};
    }
    const string_view_t *pText = Vector_At(
        &pTable->symbols,
        Symbol_Index( symbol ) );
    return pText != nullptr ? *pText : string_view_t{};
}

bool_t Symbol_IsValid( symbol_t symbol ) noexcept
{
    const u32 nIndex = symbol.value & CY_SYMBOL_INDEX_MASK;
    const u32 nGeneration = Symbol_Generation( symbol );
    return nIndex != 0u && nGeneration != 0u;
}

bool_t SymbolTable_Contains(
    const symbol_table_t *pTable,
    symbol_t symbol ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsUsable( pTable );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Contains requires a valid table." );
    return bValidTable && Symbol_IsValid( symbol ) &&
           Symbol_Generation( symbol ) == pTable->nGeneration &&
           Symbol_Index( symbol ) < Vector_Count( &pTable->symbols );
}

symbol_table_stats_t SymbolTable_Stats(
    const symbol_table_t *pTable ) noexcept
{
    const bool_t bValidTable = SymbolTable_IsValid( pTable );
    CY_ASSERT_MSG(
        bValidTable,
        "SymbolTable_Stats requires a valid table." );
    if ( !bValidTable ) {
        return {};
    }

    const string_pool_stats_t poolStats =
        StringPool_Stats( pTable->pStringPool );
    usize cbReserved = poolStats.cbReserved;
    const usize cbSymbolCapacity =
        Vector_Capacity( &pTable->symbols ) * sizeof( string_view_t );
    const usize cbLookupCapacity =
        HashMap_Capacity( &pTable->lookup ) *
        sizeof( hash_table_slot_t<const char *, symbol_t> );
    cbReserved = cbReserved <= CY_USIZE_MAX - cbSymbolCapacity
        ? cbReserved + cbSymbolCapacity
        : CY_USIZE_MAX;
    cbReserved = cbReserved <= CY_USIZE_MAX - cbLookupCapacity
        ? cbReserved + cbLookupCapacity
        : CY_USIZE_MAX;
    return {
        Vector_Count( &pTable->symbols ),
        poolStats.cbStringData,
        cbReserved
    };
}

} // namespace cypher::common
