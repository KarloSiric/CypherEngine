//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_CharacterSet_Tests.cpp
//  Purpose: Tests Tier1 CharacterSet behavior.
//  Details: These tests protect the complete byte mapping, range boundaries,
//           bounded-view handling, set algebra, aliasing, and invalid-input
//           fallback contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_CharacterSet.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_characterSetAssertCount = 0u;

assert_action_t CaptureCharacterSetAssert( const assert_info_t & ) noexcept
{
    ++g_characterSetAssertCount;
    return assert_action_t::Continue;
}

char ByteValue( u32 nValue ) noexcept
{
    return static_cast<char>( static_cast<u8>( nValue ) );
}

void RequireOnlyRange(
    const character_set_t &set,
    u32 nFirst,
    u32 nLast )
{
    for ( u32 nValue = 0u; nValue < CY_CHARACTER_SET_VALUE_COUNT; ++nValue ) {
        CAPTURE( nValue, nFirst, nLast );
        const bool_t bExpected = nValue >= nFirst && nValue <= nLast;
        REQUIRE( CharacterSet_Contains( &set, ByteValue( nValue ) ) == bExpected );
    }
}

} // namespace

TEST_CASE( "CharacterSet has compact value semantics",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    STATIC_REQUIRE( CY_CHARACTER_SET_VALUE_COUNT == 256u );
    STATIC_REQUIRE( CY_CHARACTER_SET_WORD_BITS == 64u );
    STATIC_REQUIRE( CY_CHARACTER_SET_WORD_COUNT == 4u );
    STATIC_REQUIRE( sizeof( character_set_t ) == 32u );
    STATIC_REQUIRE( is_trivially_copyable_v<character_set_t> );
    STATIC_REQUIRE( is_standard_layout_v<character_set_t> );
}

TEST_CASE( "Default CharacterSet is empty",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    const character_set_t set{};

    REQUIRE( CharacterSet_IsEmpty( &set ) );
    REQUIRE_FALSE( CharacterSet_IsFull( &set ) );
    REQUIRE( CharacterSet_Count( &set ) == 0u );

    for ( u32 nValue = 0u; nValue < CY_CHARACTER_SET_VALUE_COUNT; ++nValue ) {
        CAPTURE( nValue );
        REQUIRE_FALSE( CharacterSet_Contains( &set, ByteValue( nValue ) ) );
    }
}

TEST_CASE( "CharacterSet fill and clear cover all byte values",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t set{};

    CharacterSet_Fill( &set );
    REQUIRE( CharacterSet_IsFull( &set ) );
    REQUIRE_FALSE( CharacterSet_IsEmpty( &set ) );
    REQUIRE( CharacterSet_Count( &set ) == CY_CHARACTER_SET_VALUE_COUNT );

    for ( u32 nValue = 0u; nValue < CY_CHARACTER_SET_VALUE_COUNT; ++nValue ) {
        CAPTURE( nValue );
        REQUIRE( CharacterSet_Contains( &set, ByteValue( nValue ) ) );
    }

    CharacterSet_Clear( &set );
    REQUIRE( CharacterSet_IsEmpty( &set ) );
    REQUIRE( CharacterSet_Count( &set ) == 0u );
}

TEST_CASE( "CharacterSet maps byte boundaries to independent bits",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    constexpr u32 kBoundaryValues[] = {
        0u, 1u, 63u, 64u, 127u, 128u, 191u, 192u, 254u, 255u
    };

    character_set_t set{};
    for ( u32 nValue : kBoundaryValues ) {
        CharacterSet_Add( &set, ByteValue( nValue ) );
        CharacterSet_Add( &set, ByteValue( nValue ) );
    }

    REQUIRE( CharacterSet_Count( &set ) ==
             sizeof( kBoundaryValues ) / sizeof( kBoundaryValues[0] ) );

    for ( u32 nValue : kBoundaryValues ) {
        CAPTURE( nValue );
        REQUIRE( CharacterSet_Contains( &set, ByteValue( nValue ) ) );
    }

    CharacterSet_Remove( &set, ByteValue( 128u ) );
    CharacterSet_Remove( &set, ByteValue( 128u ) );
    REQUIRE_FALSE( CharacterSet_Contains( &set, ByteValue( 128u ) ) );
    REQUIRE( CharacterSet_Count( &set ) ==
             sizeof( kBoundaryValues ) / sizeof( kBoundaryValues[0] ) - 1u );
}

TEST_CASE( "CharacterSet equality compares every storage word",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t setA{};
    character_set_t setB{};

    REQUIRE( CharacterSet_Equals( &setA, &setA ) );
    REQUIRE( CharacterSet_Equals( &setA, &setB ) );

    CharacterSet_Add( &setA, ByteValue( 255u ) );
    REQUIRE_FALSE( CharacterSet_Equals( &setA, &setB ) );

    CharacterSet_Add( &setB, ByteValue( 255u ) );
    REQUIRE( CharacterSet_Equals( &setA, &setB ) );
}

TEST_CASE( "CharacterSet ranges are inclusive and cross storage words",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t set{};

    CharacterSet_AddRange( &set, ByteValue( 62u ), ByteValue( 66u ) );
    REQUIRE( CharacterSet_Count( &set ) == 5u );
    RequireOnlyRange( set, 62u, 66u );

    CharacterSet_Clear( &set );
    CharacterSet_AddRange( &set, ByteValue( 250u ), ByteValue( 255u ) );
    REQUIRE( CharacterSet_Count( &set ) == 6u );
    RequireOnlyRange( set, 250u, 255u );

    CharacterSet_Fill( &set );
    CharacterSet_RemoveRange( &set, ByteValue( 63u ), ByteValue( 64u ) );
    REQUIRE( CharacterSet_Count( &set ) == 254u );
    REQUIRE_FALSE( CharacterSet_Contains( &set, ByteValue( 63u ) ) );
    REQUIRE_FALSE( CharacterSet_Contains( &set, ByteValue( 64u ) ) );
    REQUIRE( CharacterSet_Contains( &set, ByteValue( 62u ) ) );
    REQUIRE( CharacterSet_Contains( &set, ByteValue( 65u ) ) );
}

TEST_CASE( "CharacterSet ranges handle complete words and the full domain",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    for ( usize iExpectedWord = 0u;
          iExpectedWord < CY_CHARACTER_SET_WORD_COUNT;
          ++iExpectedWord ) {
        const u32 nFirst =
            static_cast<u32>( iExpectedWord * CY_CHARACTER_SET_WORD_BITS );
        const u32 nLast =
            nFirst + static_cast<u32>( CY_CHARACTER_SET_WORD_BITS ) - 1u;
        character_set_t set{};

        CharacterSet_AddRange(
            &set,
            ByteValue( nFirst ),
            ByteValue( nLast ) );

        CAPTURE( iExpectedWord, nFirst, nLast );
        REQUIRE( CharacterSet_Count( &set ) == CY_CHARACTER_SET_WORD_BITS );
        for ( usize iWord = 0u;
              iWord < CY_CHARACTER_SET_WORD_COUNT;
              ++iWord ) {
            const u64 nExpected =
                iWord == iExpectedWord ? CY_U64_MAX : 0u;
            REQUIRE( set.bitWords[iWord] == nExpected );
        }
    }

    character_set_t set{};
    CharacterSet_AddRange( &set, ByteValue( 0u ), ByteValue( 255u ) );
    REQUIRE( CharacterSet_IsFull( &set ) );

    CharacterSet_RemoveRange( &set, ByteValue( 0u ), ByteValue( 255u ) );
    REQUIRE( CharacterSet_IsEmpty( &set ) );
}

TEST_CASE( "CharacterSet rejects reversed ranges without modification",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t set{};
    CharacterSet_Add( &set, 'x' );
    const character_set_t original = set;

    g_characterSetAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCharacterSetAssert );

    CharacterSet_AddRange( &set, ByteValue( 20u ), ByteValue( 10u ) );
    CharacterSet_RemoveRange( &set, ByteValue( 20u ), ByteValue( 10u ) );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( g_characterSetAssertCount ==
             2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( CharacterSet_Equals( &set, &original ) );
}

TEST_CASE( "CharacterSet views preserve bounded bytes and embedded nulls",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    const char data[] = {
        'a', 'b', '\0', ByteValue( 255u ), 'a'
    };
    const string_view_t view{ data, sizeof( data ) };
    character_set_t set{};

    CharacterSet_AddView( &set, view );
    REQUIRE( CharacterSet_Count( &set ) == 4u );
    REQUIRE( CharacterSet_Contains( &set, 'a' ) );
    REQUIRE( CharacterSet_Contains( &set, 'b' ) );
    REQUIRE( CharacterSet_Contains( &set, '\0' ) );
    REQUIRE( CharacterSet_Contains( &set, ByteValue( 255u ) ) );
    REQUIRE( CharacterSet_ContainsAny( &set, view ) );
    REQUIRE( CharacterSet_ContainsAll( &set, view ) );

    const string_view_t removeView{ data + 1u, 2u };
    CharacterSet_RemoveView( &set, removeView );
    REQUIRE_FALSE( CharacterSet_Contains( &set, 'b' ) );
    REQUIRE_FALSE( CharacterSet_Contains( &set, '\0' ) );
    REQUIRE( CharacterSet_Count( &set ) == 2u );
}

TEST_CASE( "CharacterSet view queries define empty and missing behavior",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t set{};
    CharacterSet_AddRange( &set, 'a', 'f' );

    const char anyData[] = { 'x', 'c' };
    const char allData[] = { 'a', 'c', 'f', 'a' };
    const char missingData[] = { 'a', 'z' };

    REQUIRE( CharacterSet_ContainsAny( &set, { anyData, sizeof( anyData ) } ) );
    REQUIRE( CharacterSet_ContainsAll( &set, { allData, sizeof( allData ) } ) );
    REQUIRE_FALSE(
        CharacterSet_ContainsAll( &set, { missingData, sizeof( missingData ) } ) );

    const string_view_t emptyView{};
    REQUIRE_FALSE( CharacterSet_ContainsAny( &set, emptyView ) );
    REQUIRE( CharacterSet_ContainsAll( &set, emptyView ) );
}

TEST_CASE( "CharacterSet algebra produces exact mathematical sets",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t setA{};
    CharacterSet_Add( &setA, 'a' );
    CharacterSet_Add( &setA, 'b' );
    CharacterSet_Add( &setA, ByteValue( 200u ) );

    character_set_t setB{};
    CharacterSet_Add( &setB, 'b' );
    CharacterSet_Add( &setB, 'c' );
    CharacterSet_Add( &setB, ByteValue( 200u ) );
    CharacterSet_Add( &setB, ByteValue( 255u ) );

    character_set_t result{};
    CharacterSet_Union( &result, &setA, &setB );
    REQUIRE( CharacterSet_Count( &result ) == 5u );
    REQUIRE( CharacterSet_Contains( &result, 'a' ) );
    REQUIRE( CharacterSet_Contains( &result, 'b' ) );
    REQUIRE( CharacterSet_Contains( &result, 'c' ) );
    REQUIRE( CharacterSet_Contains( &result, ByteValue( 200u ) ) );
    REQUIRE( CharacterSet_Contains( &result, ByteValue( 255u ) ) );

    CharacterSet_Intersection( &result, &setA, &setB );
    REQUIRE( CharacterSet_Count( &result ) == 2u );
    REQUIRE( CharacterSet_Contains( &result, 'b' ) );
    REQUIRE( CharacterSet_Contains( &result, ByteValue( 200u ) ) );

    CharacterSet_Difference( &result, &setA, &setB );
    REQUIRE( CharacterSet_Count( &result ) == 1u );
    REQUIRE( CharacterSet_Contains( &result, 'a' ) );
}

TEST_CASE( "CharacterSet algebra supports aliased output",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t setA{};
    CharacterSet_AddRange( &setA, 'a', 'f' );

    character_set_t setB{};
    CharacterSet_AddRange( &setB, 'd', 'z' );

    character_set_t expected{};
    character_set_t aliased = setA;

    CharacterSet_Union( &expected, &setA, &setB );
    CharacterSet_Union( &aliased, &aliased, &setB );
    REQUIRE( CharacterSet_Equals( &aliased, &expected ) );

    CharacterSet_Intersection( &expected, &setA, &setB );
    aliased = setA;
    CharacterSet_Intersection( &aliased, &aliased, &setB );
    REQUIRE( CharacterSet_Equals( &aliased, &expected ) );

    CharacterSet_Difference( &expected, &setA, &setB );
    aliased = setA;
    CharacterSet_Difference( &aliased, &aliased, &setB );
    REQUIRE( CharacterSet_Equals( &aliased, &expected ) );

    CharacterSet_Invert( &expected, &setA );
    aliased = setA;
    CharacterSet_Invert( &aliased, &aliased );
    REQUIRE( CharacterSet_Equals( &aliased, &expected ) );
    REQUIRE( CharacterSet_Count( &aliased ) ==
             CY_CHARACTER_SET_VALUE_COUNT - CharacterSet_Count( &setA ) );

    CharacterSet_Invert( &aliased, &aliased );
    REQUIRE( CharacterSet_Equals( &aliased, &setA ) );
}

TEST_CASE( "CharacterSet intersection and subset queries handle edge cases",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t empty{};
    character_set_t setA{};
    character_set_t setB{};

    CharacterSet_Add( &setA, ByteValue( 1u ) );
    CharacterSet_Add( &setA, ByteValue( 200u ) );
    CharacterSet_Add( &setB, ByteValue( 2u ) );

    REQUIRE_FALSE( CharacterSet_Intersects( &empty, &empty ) );
    REQUIRE_FALSE( CharacterSet_Intersects( &setA, &setB ) );
    REQUIRE( CharacterSet_IsSubset( &empty, &setA ) );
    REQUIRE( CharacterSet_IsSubset( &setA, &setA ) );
    REQUIRE_FALSE( CharacterSet_IsSubset( &setA, &setB ) );

    CharacterSet_Add( &setB, ByteValue( 200u ) );
    REQUIRE( CharacterSet_Intersects( &setA, &setB ) );

    character_set_t superset{};
    CharacterSet_Union( &superset, &setA, &setB );
    REQUIRE( CharacterSet_IsSubset( &setA, &superset ) );
    REQUIRE( CharacterSet_IsSubset( &setB, &superset ) );
}

TEST_CASE( "CharacterSet invalid inputs assert and use safe fallbacks",
           "[CypherCommon][Tier1][CharacterSet]" )
{
    character_set_t set{};
    CharacterSet_Add( &set, 'x' );
    const character_set_t original = set;
    const string_view_t invalidView{ nullptr, 1u };

    g_characterSetAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCharacterSetAssert );

    CharacterSet_Clear( nullptr );
    CharacterSet_Fill( nullptr );
    REQUIRE_FALSE( CharacterSet_IsEmpty( nullptr ) );
    REQUIRE_FALSE( CharacterSet_IsFull( nullptr ) );
    REQUIRE( CharacterSet_Count( nullptr ) == 0u );
    REQUIRE_FALSE( CharacterSet_Equals( nullptr, &set ) );
    CharacterSet_Add( nullptr, 'a' );
    CharacterSet_Remove( nullptr, 'a' );
    REQUIRE_FALSE( CharacterSet_Contains( nullptr, 'a' ) );
    CharacterSet_AddRange( nullptr, 'a', 'z' );
    CharacterSet_RemoveRange( nullptr, 'a', 'z' );
    CharacterSet_AddView( &set, invalidView );
    CharacterSet_RemoveView( &set, invalidView );
    REQUIRE_FALSE( CharacterSet_ContainsAny( &set, invalidView ) );
    REQUIRE_FALSE( CharacterSet_ContainsAll( &set, invalidView ) );
    CharacterSet_Union( nullptr, &set, &set );
    CharacterSet_Intersection( nullptr, &set, &set );
    CharacterSet_Difference( nullptr, &set, &set );
    CharacterSet_Invert( nullptr, &set );
    REQUIRE_FALSE( CharacterSet_Intersects( nullptr, &set ) );
    REQUIRE_FALSE( CharacterSet_IsSubset( nullptr, &set ) );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( g_characterSetAssertCount ==
             21u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( CharacterSet_Equals( &set, &original ) );
}
