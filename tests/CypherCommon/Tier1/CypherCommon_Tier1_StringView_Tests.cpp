//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringView_Tests.cpp
//  Purpose: Tests Tier1 StringView behavior.
//  Details: These tests protect pointer preservation, bounded range semantics,
//           structural validity, empty-state rules, and invalid-input fallback.
//
//  History:
//  - Created by Karlo Siric on 2026-07-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringView.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_stringViewAssertCount = 0u;

assert_action_t CaptureStringViewAssert( const assert_info_t & ) noexcept
{
    ++g_stringViewAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "StringView remains a simple non-owning value", "[CypherCommon][Tier1][StringView]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<string_view_t> );
    STATIC_REQUIRE( is_standard_layout_v<string_view_t> );
}

TEST_CASE( "Default StringView is valid and empty", "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view{};

    REQUIRE( view.pData == nullptr );
    REQUIRE( StringView_Length( view ) == 0u );
    REQUIRE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView_FromCString maps null to the canonical empty view",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromCString( nullptr );

    REQUIRE( view.pData == nullptr );
    REQUIRE( StringView_Length( view ) == 0u );
    REQUIRE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView_FromCString preserves the source pointer and discovered length",
           "[CypherCommon][Tier1][StringView]" )
{
    const char szEmpty[] = "";
    const char szText[] = "textures/world/wall.dds";

    const string_view_t emptyView = StringView_FromCString( szEmpty );
    REQUIRE( emptyView.pData == szEmpty );
    REQUIRE( StringView_Length( emptyView ) == 0u );
    REQUIRE( StringView_IsEmpty( emptyView ) );
    REQUIRE( StringView_IsValid( emptyView ) );

    const string_view_t textView = StringView_FromCString( szText );
    REQUIRE( textView.pData == szText );
    REQUIRE( StringView_Length( textView ) == sizeof( szText ) - 1u );
    REQUIRE_FALSE( StringView_IsEmpty( textView ) );
    REQUIRE( StringView_IsValid( textView ) );
}

TEST_CASE( "StringView_FromCString stops at the first null terminator",
           "[CypherCommon][Tier1][StringView]" )
{
    const char szText[] = { 'a', 'b', '\0', 'c', 'd', '\0' };
    const string_view_t view = StringView_FromCString( szText );

    REQUIRE( view.pData == szText );
    REQUIRE( StringView_Length( view ) == 2u );
}

TEST_CASE( "StringView_FromRange preserves exact bounded storage",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = { 'a', 'b', '\0', 'c', 'd' };
    const string_view_t view = StringView_FromRange( data, sizeof( data ) );

    REQUIRE( view.pData == data );
    REQUIRE( StringView_Length( view ) == sizeof( data ) );
    REQUIRE_FALSE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView_FromRange preserves non-null empty positions",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "cypher";
    const char *pEnd = data + sizeof( data ) - 1u;
    const string_view_t view = StringView_FromRange( pEnd, 0u );

    REQUIRE( view.pData == pEnd );
    REQUIRE( StringView_Length( view ) == 0u );
    REQUIRE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView_FromRange accepts a null empty range",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromRange( nullptr, 0u );

    REQUIRE( view.pData == nullptr );
    REQUIRE( StringView_Length( view ) == 0u );
    REQUIRE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView validity rejects only null non-empty state",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "x";
    const string_view_t invalidView{ nullptr, 4u };
    const string_view_t emptyView{ nullptr, 0u };
    const string_view_t positionedEmptyView{ data, 0u };
    const string_view_t populatedView{ data, 1u };

    REQUIRE_FALSE( StringView_IsValid( invalidView ) );
    REQUIRE( StringView_IsValid( emptyView ) );
    REQUIRE( StringView_IsValid( positionedEmptyView ) );
    REQUIRE( StringView_IsValid( populatedView ) );

    REQUIRE_FALSE( StringView_IsEmpty( invalidView ) );
    REQUIRE( StringView_Length( invalidView ) == 4u );
}

TEST_CASE( "StringView_FromRange reports invalid null non-empty input and returns empty",
           "[CypherCommon][Tier1][StringView]" )
{
    g_stringViewAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringViewAssert );

    const string_view_t view = StringView_FromRange( nullptr, 4u );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( g_stringViewAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( view.pData == nullptr );
    REQUIRE( StringView_Length( view ) == 0u );
    REQUIRE( StringView_IsEmpty( view ) );
    REQUIRE( StringView_IsValid( view ) );
}

TEST_CASE( "StringView begin and end preserve bounded range positions",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = { 'a', '\0', 'b', 'c' };
    const string_view_t view = StringView_FromRange( data, sizeof( data ) );

    REQUIRE( StringView_Begin( view ) == data );
    REQUIRE( StringView_End( view ) == data + sizeof( data ) );

    const string_view_t canonicalEmpty{};
    REQUIRE( StringView_Begin( canonicalEmpty ) == nullptr );
    REQUIRE( StringView_End( canonicalEmpty ) == nullptr );

    const string_view_t positionedEmpty = StringView_FromRange( data + 2u, 0u );
    REQUIRE( StringView_Begin( positionedEmpty ) == data + 2u );
    REQUIRE( StringView_End( positionedEmpty ) == data + 2u );
}

TEST_CASE( "StringView bounded access returns represented characters",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = { 'a', '\0', 'b', 'c' };
    const string_view_t view = StringView_FromRange( data, sizeof( data ) );

    REQUIRE( StringView_At( view, 0u ) == 'a' );
    REQUIRE( StringView_At( view, 1u ) == '\0' );
    REQUIRE( StringView_At( view, 3u ) == 'c' );
    REQUIRE( StringView_Front( view ) == 'a' );
    REQUIRE( StringView_Back( view ) == 'c' );
}

TEST_CASE( "StringView bounded access reports contract violations and remains safe",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "abc";
    const string_view_t invalidView{ nullptr, 3u };
    const string_view_t view = StringView_FromRange( data, 3u );
    const string_view_t canonicalEmpty{};
    const string_view_t positionedEmpty = StringView_FromRange( data + 3u, 0u );

    g_stringViewAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringViewAssert );

    const char *pInvalidBegin = StringView_Begin( invalidView );
    const char *pInvalidEnd = StringView_End( invalidView );
    const char chInvalidView = StringView_At( invalidView, 0u );
    const char chInvalidIndex = StringView_At( view, 3u );
    const char chEmptyFront = StringView_Front( canonicalEmpty );
    const char chEmptyBack = StringView_Back( positionedEmpty );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( pInvalidBegin == nullptr );
    REQUIRE( pInvalidEnd == nullptr );
    REQUIRE( chInvalidView == '\0' );
    REQUIRE( chInvalidIndex == '\0' );
    REQUIRE( chEmptyFront == '\0' );
    REQUIRE( chEmptyBack == '\0' );
    REQUIRE(
        g_stringViewAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
