//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_FixedString_Tests.cpp
//  Purpose: Tests inline fixed-capacity null-terminated strings.
//  Details: Protects exact-fit and truncation reporting, overlap, embedded bytes,
//           zero capacity, comparison, and invalid-state failure behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_FixedString.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_fixedStringAssertCount = 0u;

assert_action_t CaptureFixedStringAssert( const assert_info_t & ) noexcept
{
    ++g_fixedStringAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "FixedString defaults to a valid empty string",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<8u> string{};
    REQUIRE( FixedString_IsValid( string ) );
    REQUIRE( FixedString_IsEmpty( string ) );
    REQUIRE( FixedString_Length( string ) == 0u );
    REQUIRE( FixedString_Capacity( string ) == 8u );
    REQUIRE( FixedString_CStr( string )[0] == '\0' );
    REQUIRE( FixedString_Data( &string ) == string.data );

    const string_view_t view = FixedString_View( string );
    REQUIRE( view.pData == string.data );
    REQUIRE( view.cchLength == 0u );
}

TEST_CASE( "FixedString assignment reports exact fit and truncation",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<5u> string{};

    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "hello" ) ) == 5u );
    REQUIRE( string.cchLength == 5u );
    REQUIRE( string.data[5] == '\0' );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "hello" ) ) );

    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "toolchain" ) ) == 9u );
    REQUIRE( string.cchLength == 5u );
    REQUIRE( string.data[5] == '\0' );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "toolc" ) ) );
}

TEST_CASE( "FixedString append reports full required length",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<8u> string{};
    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "cy" ) ) == 2u );
    REQUIRE( FixedString_Append(
                 &string,
                 StringView_FromCString( "pher" ) ) == 6u );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "cypher" ) ) );

    REQUIRE( FixedString_Append(
                 &string,
                 StringView_FromCString( "engine" ) ) == 12u );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "cypheren" ) ) );
    REQUIRE( string.data[8] == '\0' );

    REQUIRE_FALSE( FixedString_AppendChar( &string, '!' ) );
    REQUIRE( string.cchLength == 8u );
}

TEST_CASE( "FixedString supports overlapping source views",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<16u> string{};
    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "abcdef" ) ) == 6u );

    const string_view_t suffix =
        StringView_FromRange( string.data + 2u, 4u );
    REQUIRE( FixedString_Assign( &string, suffix ) == 4u );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "cdef" ) ) );

    const string_view_t self = FixedString_View( string );
    REQUIRE( FixedString_Append( &string, self ) == 8u );
    REQUIRE( FixedString_Equals(
                 string,
                 StringView_FromCString( "cdefcdef" ) ) );
}

TEST_CASE( "FixedString remains length-aware with embedded null bytes",
           "[CypherCommon][Tier1][FixedString]" )
{
    const char source[] = { 'a', '\0', 'b' };
    fixed_string_t<4u> string{};

    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromRange( source, 3u ) ) == 3u );
    REQUIRE( FixedString_IsValid( string ) );
    REQUIRE( string.cchLength == 3u );
    REQUIRE( string.data[0] == 'a' );
    REQUIRE( string.data[1] == '\0' );
    REQUIRE( string.data[2] == 'b' );
    REQUIRE( string.data[3] == '\0' );
}

TEST_CASE( "FixedString zero capacity preserves its sole terminator",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<0u> string{};
    REQUIRE( FixedString_IsValid( string ) );
    REQUIRE( FixedString_Capacity( string ) == 0u );
    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "x" ) ) == 1u );
    REQUIRE( string.cchLength == 0u );
    REQUIRE( string.data[0] == '\0' );
    REQUIRE_FALSE( FixedString_AppendChar( &string, 'x' ) );
}

TEST_CASE( "FixedString clear and comparison preserve invariants",
           "[CypherCommon][Tier1][FixedString]" )
{
    fixed_string_t<8u> string{};
    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "alpha" ) ) == 5u );
    REQUIRE( FixedString_Compare(
                 string,
                 StringView_FromCString( "alpha" ) ) == 0 );
    REQUIRE( FixedString_Compare(
                 string,
                 StringView_FromCString( "beta" ) ) < 0 );

    FixedString_Clear( &string );
    REQUIRE( FixedString_IsValid( string ) );
    REQUIRE( FixedString_IsEmpty( string ) );
}

TEST_CASE( "FixedString invalid calls assert and fail without mutation",
           "[CypherCommon][Tier1][FixedString]" )
{
    g_fixedStringAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureFixedStringAssert );

    fixed_string_t<4u> string{};
    REQUIRE( FixedString_Assign(
                 &string,
                 StringView_FromCString( "abc" ) ) == 3u );

    REQUIRE( FixedString_Assign(
                 static_cast<fixed_string_t<4u> *>( nullptr ),
                 StringView_FromCString( "x" ) ) == CY_USIZE_MAX );
    REQUIRE( FixedString_Append(
                 &string,
                 string_view_t{ nullptr, 1u } ) == CY_USIZE_MAX );

    string.cchLength = 5u;
    REQUIRE_FALSE( FixedString_IsValid( string ) );
    REQUIRE( FixedString_Append(
                 &string,
                 StringView_FromCString( "x" ) ) == CY_USIZE_MAX );

    string.cchLength = 3u;
    string.data[3] = '\0';
    REQUIRE( FixedString_Append(
                 &string,
                 string_view_t{ string.data, CY_USIZE_MAX } ) == CY_USIZE_MAX );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_fixedStringAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
