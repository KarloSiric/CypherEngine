//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Variant_Tests.cpp
//  Purpose: Tests exact non-owning primitive variant behavior.
//  Details: Covers every stored type, borrowed range validity, exact retrieval,
//           equality semantics, unchanged failure outputs, and invalid input handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Variant.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_variantAssertCount = 0u;

assert_action_t CaptureVariantAssert( const assert_info_t & ) noexcept
{
    ++g_variantAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Variant stores and retrieves every primitive type exactly",
           "[CypherCommon][Tier1][Variant]" )
{
    bool_t bValue = CY_FALSE;
    i64 iValue = 0;
    u64 uValue = 0u;
    f64 flValue = 0.0;
    string_view_t stringValue{};
    const_byte_span_t byteValue{};
    void *pValue = nullptr;
    u32 pointedValue = 77u;
    const byte bytes[]{ 1u, 2u, 3u, 4u };

    REQUIRE( Variant_GetBool(
        Variant_FromBool( CY_TRUE ),
        &bValue ) );
    REQUIRE( bValue );
    REQUIRE( Variant_GetI64( Variant_FromI64( -42 ), &iValue ) );
    REQUIRE( iValue == -42 );
    REQUIRE( Variant_GetU64( Variant_FromU64( 99u ), &uValue ) );
    REQUIRE( uValue == 99u );
    REQUIRE( Variant_GetF64( Variant_FromF64( 1.25 ), &flValue ) );
    REQUIRE( flValue == 1.25 );
    REQUIRE( Variant_GetString(
        Variant_FromString( StringView_FromCString( "material" ) ),
        &stringValue ) );
    REQUIRE( StringView_Equals(
        stringValue,
        StringView_FromCString( "material" ) ) );
    REQUIRE( Variant_GetBytes(
        Variant_FromBytes( Span_FromArray( bytes ) ),
        &byteValue ) );
    REQUIRE( byteValue.pData == bytes );
    REQUIRE( byteValue.nCount == 4u );
    REQUIRE( Variant_GetPointer(
        Variant_FromPointer( &pointedValue ),
        &pValue ) );
    REQUIRE( pValue == &pointedValue );
}

TEST_CASE( "Variant failed getters preserve caller output",
           "[CypherCommon][Tier1][Variant]" )
{
    u64 output = 1234u;
    REQUIRE_FALSE( Variant_GetU64( Variant_FromI64( 12 ), &output ) );
    REQUIRE( output == 1234u );

    string_view_t stringOutput = StringView_FromCString( "unchanged" );
    REQUIRE_FALSE( Variant_GetString(
        Variant_FromBool( CY_TRUE ),
        &stringOutput ) );
    REQUIRE( StringView_Equals(
        stringOutput,
        StringView_FromCString( "unchanged" ) ) );
}

TEST_CASE( "Variant equality compares borrowed contents rather than addresses",
           "[CypherCommon][Tier1][Variant]" )
{
    const char firstText[]{ 'c', 'y', 'p', 'h', 'e', 'r' };
    const char secondText[]{ 'c', 'y', 'p', 'h', 'e', 'r' };
    const byte firstBytes[]{ 9u, 8u, 7u };
    const byte secondBytes[]{ 9u, 8u, 7u };

    REQUIRE( Variant_Equals( Variant_Empty(), Variant_Empty() ) );
    REQUIRE( Variant_Equals(
        Variant_FromString( StringView_FromRange( firstText, 6u ) ),
        Variant_FromString( StringView_FromRange( secondText, 6u ) ) ) );
    REQUIRE( Variant_Equals(
        Variant_FromBytes( Span_FromArray( firstBytes ) ),
        Variant_FromBytes( Span_FromArray( secondBytes ) ) ) );
    REQUIRE_FALSE( Variant_Equals(
        Variant_FromI64( 1 ),
        Variant_FromU64( 1u ) ) );
    REQUIRE_FALSE( Variant_Equals(
        Variant_FromString( StringView_FromCString( "one" ) ),
        Variant_FromString( StringView_FromCString( "two" ) ) ) );
}

TEST_CASE( "Variant reset returns a value to its canonical empty state",
           "[CypherCommon][Tier1][Variant]" )
{
    variant_t value = Variant_FromU64( 88u );
    REQUIRE( Variant_Type( value ) == variant_type_t::U64 );
    Variant_Reset( &value );
    REQUIRE( Variant_IsValid( value ) );
    REQUIRE( Variant_IsEmpty( value ) );
}

TEST_CASE( "Variant rejects malformed ranges and invalid calls",
           "[CypherCommon][Tier1][Variant]" )
{
    g_variantAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureVariantAssert );

    const variant_t badString = Variant_FromString( { nullptr, 1u } );
    const variant_t badBytes = Variant_FromBytes( { nullptr, 1u } );
    REQUIRE( Variant_IsEmpty( badString ) );
    REQUIRE( Variant_IsEmpty( badBytes ) );

    variant_t corrupt{};
    corrupt.type = static_cast<variant_type_t>( 255u );
    REQUIRE_FALSE( Variant_IsValid( corrupt ) );
    REQUIRE( Variant_Type( corrupt ) == variant_type_t::EMPTY );
    Variant_Reset( nullptr );
    REQUIRE_FALSE( Variant_GetU64( Variant_FromU64( 1u ), nullptr ) );
    REQUIRE_FALSE( Variant_Equals( corrupt, Variant_Empty() ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_variantAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
