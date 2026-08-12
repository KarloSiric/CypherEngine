//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_HashTable_Tests.cpp
//  Purpose: Tests allocator-backed Robin Hood hash containers.
//  Details: Covers collisions, growth, deletion-chain repair, duplicate handling,
//           allocation rollback, explicit object lifetime, iteration, maps, and sets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HashMap.h"
#include "CypherCommon_HashSet.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct constant_u32_hasher_t {
    hash64_t operator()( u32 ) const noexcept
    {
        return 7u;
    }
};

struct identity_u32_hasher_t {
    hash64_t operator()( u32 nValue ) const noexcept
    {
        return nValue;
    }
};

struct tracked_hash_value_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;

    i32 nValue;

    tracked_hash_value_t() = delete;

    explicit tracked_hash_value_t( i32 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
        ++cConstructed;
    }

    tracked_hash_value_t( const tracked_hash_value_t &other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
    }

    tracked_hash_value_t( tracked_hash_value_t &&other ) noexcept
        : nValue( other.nValue )
    {
        ++cConstructed;
        other.nValue = -1;
    }

    tracked_hash_value_t &operator=(
        tracked_hash_value_t &&other ) noexcept
    {
        nValue = other.nValue;
        other.nValue = -1;
        return *this;
    }

    ~tracked_hash_value_t() noexcept
    {
        ++cDestroyed;
    }

    static void Reset() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
    }
};

void *FailHashTableAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

} // namespace

TEST_CASE( "HashTable resolves full collision chains and rejects duplicates",
           "[CypherCommon][Tier1][HashTable]" )
{
    hash_table_t<u32, u32, constant_u32_hasher_t> table{};
    REQUIRE( HashTable_Init( &table, Allocator_GetSystem() ) );

    for ( u32 nKey = 0u; nKey < 128u; ++nKey ) {
        const hash_table_insert_result_t<u32> result =
            HashTable_Insert( &table, nKey, nKey * 3u );
        REQUIRE( result.bInserted );
        REQUIRE( result.pValue != nullptr );
        REQUIRE( *result.pValue == nKey * 3u );
    }

    REQUIRE( HashTable_Count( &table ) == 128u );
    for ( u32 nKey = 0u; nKey < 128u; ++nKey ) {
        const u32 *pValue = HashTable_Find( &table, nKey );
        REQUIRE( pValue != nullptr );
        REQUIRE( *pValue == nKey * 3u );
    }

    const hash_table_insert_result_t<u32> duplicate =
        HashTable_Insert( &table, 63u, 999u );
    REQUIRE_FALSE( duplicate.bInserted );
    REQUIRE( duplicate.pValue != nullptr );
    REQUIRE( *duplicate.pValue == 189u );
    REQUIRE( HashTable_Count( &table ) == 128u );
}

TEST_CASE( "HashTable erase repairs wrapped Robin Hood clusters",
           "[CypherCommon][Tier1][HashTable]" )
{
    hash_table_t<u32, u32, identity_u32_hasher_t> table{};
    REQUIRE( HashTable_Init( &table, Allocator_GetSystem(), 6u ) );

    for ( u32 nKey : { 7u, 15u, 23u, 31u, 39u, 47u } ) {
        REQUIRE( HashTable_Insert( &table, nKey, nKey + 1u ).bInserted );
    }

    REQUIRE( HashTable_Erase( &table, 23u ) );
    REQUIRE_FALSE( HashTable_Contains( &table, 23u ) );
    for ( u32 nKey : { 7u, 15u, 31u, 39u, 47u } ) {
        REQUIRE( HashTable_Contains( &table, nKey ) );
    }

    REQUIRE( HashTable_Erase( &table, 7u ) );
    REQUIRE( HashTable_Erase( &table, 47u ) );
    REQUIRE_FALSE( HashTable_Erase( &table, 999u ) );
    REQUIRE( HashTable_Count( &table ) == 3u );
}

TEST_CASE( "HashTable manages non-default-constructible value lifetimes",
           "[CypherCommon][Tier1][HashTable]" )
{
    tracked_hash_value_t::Reset();
    {
        hash_table_t<u32, tracked_hash_value_t, constant_u32_hasher_t> table{};
        REQUIRE( HashTable_Init( &table, Allocator_GetSystem(), 2u ) );

        for ( u32 nKey = 0u; nKey < 48u; ++nKey ) {
            const tracked_hash_value_t value( static_cast<i32>( nKey ) );
            REQUIRE( HashTable_Insert( &table, nKey, value ).bInserted );
        }

        REQUIRE( HashTable_Erase( &table, 11u ) );
        REQUIRE( HashTable_Erase( &table, 12u ) );
        HashTable_Clear( &table );
        REQUIRE( HashTable_IsEmpty( &table ) );
    }

    REQUIRE(
        tracked_hash_value_t::cConstructed ==
        tracked_hash_value_t::cDestroyed );
}

TEST_CASE( "HashTable growth failure preserves its allocation and values",
           "[CypherCommon][Tier1][HashTable]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    hash_table_t<u32, u32> table{};
    REQUIRE( HashTable_Init( &table, &allocator, 4u ) );

    const usize nInitialCapacity = HashTable_Capacity( &table );
    for ( u32 nKey = 0u; nKey < 6u; ++nKey ) {
        REQUIRE( HashTable_Insert( &table, nKey, nKey + 100u ).bInserted );
    }
    REQUIRE( HashTable_Count( &table ) == 6u );

    allocator.pfnAllocate = FailHashTableAllocation;
    const hash_table_insert_result_t<u32> failed =
        HashTable_Insert( &table, 99u, 199u );
    REQUIRE( failed.pValue == nullptr );
    REQUIRE_FALSE( failed.bInserted );
    REQUIRE( HashTable_Capacity( &table ) == nInitialCapacity );
    REQUIRE( HashTable_Count( &table ) == 6u );
    for ( u32 nKey = 0u; nKey < 6u; ++nKey ) {
        REQUIRE( *HashTable_Find( &table, nKey ) == nKey + 100u );
    }

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "HashTable iteration exposes each occupied key and value once",
           "[CypherCommon][Tier1][HashTable]" )
{
    hash_table_t<u32, u32> table{};
    REQUIRE( HashTable_Init( &table, Allocator_GetSystem() ) );
    for ( u32 nKey = 1u; nKey <= 32u; ++nKey ) {
        REQUIRE( HashTable_Insert( &table, nKey, nKey * nKey ).bInserted );
    }

    usize iSlot = 0u;
    usize nVisited = 0u;
    u64 nKeySum = 0u;
    while ( hash_table_slot_t<u32, u32> *pSlot =
                HashTable_NextOccupied( &table, &iSlot ) ) {
        const u32 *pKey = HashTable_SlotKey( pSlot );
        const u32 *pValue = HashTable_SlotValue( pSlot );
        REQUIRE( pKey != nullptr );
        REQUIRE( pValue != nullptr );
        REQUIRE( *pValue == *pKey * *pKey );
        nKeySum += *pKey;
        ++nVisited;
    }

    REQUIRE( nVisited == 32u );
    REQUIRE( nKeySum == 528u );
}

TEST_CASE( "HashMap and HashSet facades preserve table semantics",
           "[CypherCommon][Tier1][HashMap][HashSet]" )
{
    hash_map_t<u32, u32> map{};
    REQUIRE( HashMap_Init( &map, Allocator_GetSystem() ) );
    REQUIRE( HashMap_IsValid( &map ) );
    REQUIRE( HashMap_IsEmpty( &map ) );
    REQUIRE( HashMap_Reserve( &map, 32u ) );
    REQUIRE( HashMap_Capacity( &map ) >= 32u );
    REQUIRE( HashMap_Insert( &map, 4u, 40u ).bInserted );
    REQUIRE_FALSE( HashMap_Insert( &map, 4u, 99u ).bInserted );
    REQUIRE( HashMap_Contains( &map, 4u ) );
    REQUIRE( *HashMap_Find( &map, 4u ) == 40u );
    REQUIRE( HashMap_Erase( &map, 4u ) );
    REQUIRE_FALSE( HashMap_Contains( &map, 4u ) );

    hash_set_t<u32> set{};
    REQUIRE( HashSet_Init( &set, Allocator_GetSystem() ) );
    REQUIRE( HashSet_IsValid( &set ) );
    REQUIRE( HashSet_IsEmpty( &set ) );
    REQUIRE( HashSet_Reserve( &set, 32u ) );
    REQUIRE( HashSet_Capacity( &set ) >= 32u );
    REQUIRE( HashSet_Insert( &set, 8u ) );
    REQUIRE_FALSE( HashSet_Insert( &set, 8u ) );
    REQUIRE( HashSet_Contains( &set, 8u ) );
    REQUIRE( HashSet_Count( &set ) == 1u );
    REQUIRE( HashSet_Erase( &set, 8u ) );
    REQUIRE( HashSet_Count( &set ) == 0u );
}

TEST_CASE( "Hash containers expose reserve clear count and shutdown contracts",
           "[CypherCommon][Tier1][HashTable][HashMap][HashSet]" )
{
    hash_table_t<u32, u32> table{};
    REQUIRE( HashTable_IsValid( &table ) );
    REQUIRE( HashTable_Init( &table, Allocator_GetSystem() ) );
    REQUIRE( HashTable_Reserve( &table, 64u ) );
    REQUIRE( HashTable_Insert( &table, 1u, 11u ).bInserted );
    HashTable_Shutdown( &table );
    REQUIRE( HashTable_IsValid( &table ) );
    REQUIRE( HashTable_IsEmpty( &table ) );

    hash_map_t<u32, u32> map{};
    REQUIRE( HashMap_Init( &map, Allocator_GetSystem() ) );
    REQUIRE( HashMap_Insert( &map, 2u, 22u ).bInserted );
    REQUIRE( HashMap_Count( &map ) == 1u );
    HashMap_Clear( &map );
    REQUIRE( HashMap_Count( &map ) == 0u );
    HashMap_Shutdown( &map );
    REQUIRE( HashMap_IsValid( &map ) );

    hash_set_t<u32> set{};
    REQUIRE( HashSet_Init( &set, Allocator_GetSystem() ) );
    REQUIRE( HashSet_Insert( &set, 3u ) );
    HashSet_Clear( &set );
    REQUIRE( HashSet_IsEmpty( &set ) );
    HashSet_Shutdown( &set );
    REQUIRE( HashSet_IsValid( &set ) );
}
