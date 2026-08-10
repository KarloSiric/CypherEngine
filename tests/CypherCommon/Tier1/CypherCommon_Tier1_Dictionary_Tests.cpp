//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Dictionary_Tests.cpp
//  Purpose: Tests string-keyed owning dictionaries.
//  Details: Covers key ownership, duplicate insertion, case policy, erase behavior,
//           clear reuse, const lookup, and explicit shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Dictionary.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Dictionary owns key bytes and preserves duplicate values",
           "[CypherCommon][Tier1][Dictionary]" )
{
    dictionary_t<u32> dictionary{};
    REQUIRE( Dictionary_Init( &dictionary, Allocator_GetSystem(), 8u ) );

    char mutableKey[]{ 'p', 'l', 'a', 'y', 'e', 'r', '\0' };
    const hash_table_insert_result_t<u32> inserted = Dictionary_Insert(
        &dictionary,
        StringView_FromCString( mutableKey ),
        17u );
    REQUIRE( inserted.bInserted );
    REQUIRE( *inserted.pValue == 17u );
    mutableKey[0] = 'x';

    REQUIRE( Dictionary_Contains(
        &dictionary,
        StringView_FromCString( "player" ) ) );
    REQUIRE( *Dictionary_Find(
        &dictionary,
        StringView_FromCString( "player" ) ) == 17u );

    const hash_table_insert_result_t<u32> duplicate = Dictionary_Insert(
        &dictionary,
        StringView_FromCString( "player" ),
        99u );
    REQUIRE_FALSE( duplicate.bInserted );
    REQUIRE( *duplicate.pValue == 17u );
    REQUIRE( Dictionary_Count( &dictionary ) == 1u );
}

TEST_CASE( "Dictionary applies one ASCII case policy to pool and hash map",
           "[CypherCommon][Tier1][Dictionary]" )
{
    dictionary_t<i32> dictionary{};
    REQUIRE( Dictionary_Init(
        &dictionary,
        Allocator_GetSystem(),
        0u,
        CY_TRUE ) );
    REQUIRE( Dictionary_Insert(
        &dictionary,
        StringView_FromCString( "Materials/Wall" ),
        42 ).bInserted );
    REQUIRE( *Dictionary_Find(
        &dictionary,
        StringView_FromCString( "materials/wall" ) ) == 42 );
    REQUIRE_FALSE( Dictionary_Insert(
        &dictionary,
        StringView_FromCString( "MATERIALS/WALL" ),
        77 ).bInserted );
    REQUIRE( Dictionary_Count( &dictionary ) == 1u );

    const dictionary_t<i32> &constDictionary = dictionary;
    STATIC_REQUIRE( is_same_v<
        decltype( Dictionary_Find(
            &constDictionary,
            StringView_FromCString( "materials/wall" ) ) ),
        const i32 *> );

    REQUIRE( Dictionary_Erase(
        &dictionary,
        StringView_FromCString( "MATERIALS/WALL" ) ) );
    REQUIRE( Dictionary_IsEmpty( &dictionary ) );
    REQUIRE( Dictionary_Insert(
        &dictionary,
        StringView_FromCString( "new_key" ),
        9 ).bInserted );
    Dictionary_Clear( &dictionary );
    REQUIRE( Dictionary_IsEmpty( &dictionary ) );

    Dictionary_Shutdown( &dictionary );
    REQUIRE( Dictionary_IsValid( &dictionary ) );
}
