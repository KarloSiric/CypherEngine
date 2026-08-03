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

TEST_CASE( "StringView comparison uses bounded byte ordering",
           "[CypherCommon][Tier1][StringView]" )
{
    const char dataA[] = { 'a', 'b', '\0', 'c' };
    const char dataB[] = { 'a', 'b', '\0', 'c' };
    const char dataC[] = { 'a', 'b', '\0', 'd' };
    const string_view_t viewA = StringView_FromRange( dataA, sizeof( dataA ) );
    const string_view_t viewB = StringView_FromRange( dataB, sizeof( dataB ) );
    const string_view_t viewC = StringView_FromRange( dataC, sizeof( dataC ) );
    const string_view_t prefix = StringView_FromRange( dataA, 3u );

    REQUIRE( StringView_Compare( viewA, viewA ) == 0 );
    REQUIRE( StringView_Compare( viewA, viewB ) == 0 );
    REQUIRE( StringView_Compare( viewA, viewC ) < 0 );
    REQUIRE( StringView_Compare( viewC, viewA ) > 0 );
    REQUIRE( StringView_Compare( prefix, viewA ) < 0 );
    REQUIRE( StringView_Compare( viewA, prefix ) > 0 );
    REQUIRE( StringView_Equals( viewA, viewB ) );
    REQUIRE_FALSE( StringView_Equals( viewA, viewC ) );
    REQUIRE_FALSE( StringView_Equals( prefix, viewA ) );
}

TEST_CASE( "StringView ASCII-insensitive comparison folds only ASCII case",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t mixed = StringView_FromCString( "CyPhEr-123" );
    const string_view_t lower = StringView_FromCString( "cypher-123" );
    const string_view_t later = StringView_FromCString( "cypher-124" );
    const char highByteA[] = { static_cast<char>( 0xC0u ) };
    const char highByteB[] = { static_cast<char>( 0xE0u ) };

    REQUIRE( StringView_CompareInsensitiveAscii( mixed, lower ) == 0 );
    REQUIRE( StringView_CompareInsensitiveAscii( mixed, later ) < 0 );
    REQUIRE( StringView_EqualsInsensitiveAscii( mixed, lower ) );
    REQUIRE_FALSE( StringView_Equals( mixed, lower ) );
    REQUIRE_FALSE( StringView_EqualsInsensitiveAscii( mixed, later ) );
    REQUIRE(
        StringView_CompareInsensitiveAscii(
            StringView_FromRange( highByteA, 1u ),
            StringView_FromRange( highByteB, 1u ) ) != 0 );
}

TEST_CASE( "StringView comparison treats all valid empty views as equal",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "abc";
    const string_view_t canonicalEmpty{};
    const string_view_t positionedEmpty = StringView_FromRange( data + 2u, 0u );

    REQUIRE( StringView_Compare( canonicalEmpty, positionedEmpty ) == 0 );
    REQUIRE( StringView_CompareInsensitiveAscii( canonicalEmpty, positionedEmpty ) == 0 );
    REQUIRE( StringView_Equals( canonicalEmpty, positionedEmpty ) );
    REQUIRE( StringView_EqualsInsensitiveAscii( canonicalEmpty, positionedEmpty ) );
}

TEST_CASE( "StringView prefix and suffix checks honor exact bounds",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromCString( "textures/wall.DDS" );
    const string_view_t prefix = StringView_FromCString( "textures/" );
    const string_view_t wrongPrefix = StringView_FromCString( "texture/" );
    const string_view_t suffix = StringView_FromCString( ".DDS" );
    const string_view_t wrongSuffix = StringView_FromCString( ".png" );
    const string_view_t longValue = StringView_FromCString( "textures/wall.DDS.extra" );
    const string_view_t empty{};

    REQUIRE( StringView_StartsWith( view, prefix ) );
    REQUIRE_FALSE( StringView_StartsWith( view, wrongPrefix ) );
    REQUIRE_FALSE( StringView_StartsWith( view, longValue ) );
    REQUIRE( StringView_StartsWith( view, empty ) );

    REQUIRE( StringView_EndsWith( view, suffix ) );
    REQUIRE_FALSE( StringView_EndsWith( view, wrongSuffix ) );
    REQUIRE_FALSE( StringView_EndsWith( view, longValue ) );
    REQUIRE( StringView_EndsWith( view, empty ) );
}

TEST_CASE( "StringView prefix and suffix checks support ASCII-insensitive matching",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromCString( "Textures/Wall.DDS" );

    REQUIRE(
        StringView_StartsWithInsensitiveAscii(
            view,
            StringView_FromCString( "textures/" ) ) );
    REQUIRE_FALSE(
        StringView_StartsWithInsensitiveAscii(
            view,
            StringView_FromCString( "materials/" ) ) );
    REQUIRE(
        StringView_EndsWithInsensitiveAscii(
            view,
            StringView_FromCString( ".dds" ) ) );
    REQUIRE_FALSE(
        StringView_EndsWithInsensitiveAscii(
            view,
            StringView_FromCString( ".png" ) ) );
}

TEST_CASE( "StringView slicing preserves source positions and clamps lengths",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "0123456789";
    const string_view_t view = StringView_FromRange( data, 10u );

    const string_view_t middle = StringView_Subview( view, 3u, 4u );
    REQUIRE( middle.pData == data + 3u );
    REQUIRE( middle.cchLength == 4u );
    REQUIRE( StringView_Front( middle ) == '3' );
    REQUIRE( StringView_Back( middle ) == '6' );

    const string_view_t remainder =
        StringView_Subview( view, 7u, CY_STRING_VIEW_NPOS );
    REQUIRE( remainder.pData == data + 7u );
    REQUIRE( remainder.cchLength == 3u );

    const string_view_t clamped = StringView_Subview( view, 8u, 20u );
    REQUIRE( clamped.pData == data + 8u );
    REQUIRE( clamped.cchLength == 2u );

    const string_view_t atEnd = StringView_Subview( view, 10u, 4u );
    REQUIRE( atEnd.pData == data + 10u );
    REQUIRE( atEnd.cchLength == 0u );
}

TEST_CASE( "StringView prefix suffix and removal helpers clamp to the source",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "cypher";
    const string_view_t view = StringView_FromRange( data, 6u );

    const string_view_t prefix = StringView_Prefix( view, 3u );
    REQUIRE( prefix.pData == data );
    REQUIRE( prefix.cchLength == 3u );

    const string_view_t suffix = StringView_Suffix( view, 3u );
    REQUIRE( suffix.pData == data + 3u );
    REQUIRE( suffix.cchLength == 3u );

    const string_view_t emptySuffix = StringView_Suffix( view, 0u );
    REQUIRE( emptySuffix.pData == data + 6u );
    REQUIRE( emptySuffix.cchLength == 0u );

    const string_view_t removedPrefix = StringView_RemovePrefix( view, 2u );
    REQUIRE( removedPrefix.pData == data + 2u );
    REQUIRE( removedPrefix.cchLength == 4u );

    const string_view_t removedSuffix = StringView_RemoveSuffix( view, 2u );
    REQUIRE( removedSuffix.pData == data );
    REQUIRE( removedSuffix.cchLength == 4u );

    const string_view_t fullPrefix = StringView_Prefix( view, 100u );
    const string_view_t fullSuffix = StringView_Suffix( view, 100u );
    REQUIRE( fullPrefix.pData == data );
    REQUIRE( fullPrefix.cchLength == 6u );
    REQUIRE( fullSuffix.pData == data );
    REQUIRE( fullSuffix.cchLength == 6u );

    const string_view_t removeAllPrefix = StringView_RemovePrefix( view, 100u );
    const string_view_t removeAllSuffix = StringView_RemoveSuffix( view, 100u );
    REQUIRE( removeAllPrefix.pData == data + 6u );
    REQUIRE( removeAllPrefix.cchLength == 0u );
    REQUIRE( removeAllSuffix.pData == data );
    REQUIRE( removeAllSuffix.cchLength == 0u );
}

TEST_CASE( "StringView subview reports an out-of-range start and returns the end position",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "abc";
    const string_view_t view = StringView_FromRange( data, 3u );

    g_stringViewAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringViewAssert );

    const string_view_t result = StringView_Subview( view, 4u, 1u );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( result.pData == data + 3u );
    REQUIRE( result.cchLength == 0u );
    REQUIRE(
        g_stringViewAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "StringView character search is bounded and supports embedded null bytes",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = { 'a', 'b', '\0', 'a', 'b', '/', 'c' };
    const string_view_t view = StringView_FromRange( data, sizeof( data ) );

    REQUIRE( StringView_FindChar( view, 'a' ) == 0u );
    REQUIRE( StringView_FindChar( view, 'a', 1u ) == 3u );
    REQUIRE( StringView_FindChar( view, '\0' ) == 2u );
    REQUIRE( StringView_FindChar( view, 'z' ) == CY_STRING_VIEW_NPOS );
    REQUIRE( StringView_FindChar( view, 'c', view.cchLength ) == CY_STRING_VIEW_NPOS );

    REQUIRE( StringView_FindLastChar( view, 'a' ) == 3u );
    REQUIRE( StringView_FindLastChar( view, '\0' ) == 2u );
    REQUIRE( StringView_FindLastChar( view, 'z' ) == CY_STRING_VIEW_NPOS );
    REQUIRE( StringView_FindLastChar( {}, 'a' ) == CY_STRING_VIEW_NPOS );
}

TEST_CASE( "StringView substring search handles starts empty needles and embedded null bytes",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = { 'a', 'b', 'a', 'b', 'a', '\0', 'z' };
    const char repeated[] = { 'a', 'b', 'a' };
    const char embedded[] = { 'a', '\0', 'z' };
    const string_view_t view = StringView_FromRange( data, sizeof( data ) );
    const string_view_t repeatedView =
        StringView_FromRange( repeated, sizeof( repeated ) );
    const string_view_t embeddedView =
        StringView_FromRange( embedded, sizeof( embedded ) );

    REQUIRE( StringView_Find( view, repeatedView ) == 0u );
    REQUIRE( StringView_Find( view, repeatedView, 1u ) == 2u );
    REQUIRE( StringView_Find( view, embeddedView ) == 4u );
    REQUIRE(
        StringView_Find( view, StringView_FromCString( "missing" ) ) ==
        CY_STRING_VIEW_NPOS );
    REQUIRE( StringView_Find( view, {}, 3u ) == 3u );
    REQUIRE( StringView_Find( view, {}, view.cchLength ) == view.cchLength );
    REQUIRE(
        StringView_Find( view, {}, view.cchLength + 1u ) ==
        CY_STRING_VIEW_NPOS );
}

TEST_CASE( "StringView ASCII-insensitive search and containment share search semantics",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view =
        StringView_FromCString( "Materials/World/WALL_01.DDS" );
    const string_view_t wall = StringView_FromCString( "wall_01" );
    const string_view_t extension = StringView_FromCString( ".dds" );
    const string_view_t absent = StringView_FromCString( "normal" );

    REQUIRE( StringView_FindInsensitiveAscii( view, wall ) == 16u );
    REQUIRE( StringView_FindInsensitiveAscii( view, extension ) == 23u );
    REQUIRE(
        StringView_FindInsensitiveAscii( view, wall, 17u ) ==
        CY_STRING_VIEW_NPOS );
    REQUIRE( StringView_ContainsInsensitiveAscii( view, wall ) );
    REQUIRE_FALSE( StringView_Contains( view, wall ) );
    REQUIRE_FALSE( StringView_ContainsInsensitiveAscii( view, absent ) );
    REQUIRE( StringView_Contains( view, {} ) );
}

TEST_CASE( "StringView trimming recognizes the complete ASCII whitespace set",
           "[CypherCommon][Tier1][StringView]" )
{
    const char data[] = "\t \nCypher\r\n ";
    const string_view_t view = StringView_FromRange( data, sizeof( data ) - 1u );

    const string_view_t left = StringView_TrimLeft( view );
    REQUIRE( left.pData == data + 3u );
    REQUIRE( left.cchLength == 9u );

    const string_view_t right = StringView_TrimRight( view );
    REQUIRE( right.pData == data );
    REQUIRE( right.cchLength == 9u );

    const string_view_t both = StringView_Trim( view );
    REQUIRE( both.pData == data + 3u );
    REQUIRE( both.cchLength == 6u );
    REQUIRE( StringView_Equals( both, StringView_FromCString( "Cypher" ) ) );

    const char allWhitespace[] = { ' ', '\t', '\n', '\r', '\v', '\f' };
    const string_view_t whitespaceView =
        StringView_FromRange( allWhitespace, sizeof( allWhitespace ) );
    const string_view_t trimmedWhitespace = StringView_Trim( whitespaceView );
    REQUIRE( trimmedWhitespace.pData == allWhitespace + sizeof( allWhitespace ) );
    REQUIRE( trimmedWhitespace.cchLength == 0u );

    const string_view_t canonicalEmpty = StringView_Trim( {} );
    REQUIRE( canonicalEmpty.pData == nullptr );
    REQUIRE( canonicalEmpty.cchLength == 0u );
}

TEST_CASE( "StringView copy writes a terminated C string and reports required length",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromCString( "cypher" );
    char dest[16]{};

    const usize cchRequired = StringView_CopyToCString( view, dest, sizeof( dest ) );

    REQUIRE( cchRequired == 6u );
    REQUIRE( dest[0] == 'c' );
    REQUIRE( dest[1] == 'y' );
    REQUIRE( dest[2] == 'p' );
    REQUIRE( dest[3] == 'h' );
    REQUIRE( dest[4] == 'e' );
    REQUIRE( dest[5] == 'r' );
    REQUIRE( dest[6] == '\0' );
}

TEST_CASE( "StringView copy truncates safely and supports size queries",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t view = StringView_FromCString( "cypher" );
    char truncated[4] = { 'x', 'x', 'x', 'x' };
    char terminatorOnly[1] = { 'x' };

    REQUIRE( StringView_CopyToCString( view, truncated, sizeof( truncated ) ) == 6u );
    REQUIRE( truncated[0] == 'c' );
    REQUIRE( truncated[1] == 'y' );
    REQUIRE( truncated[2] == 'p' );
    REQUIRE( truncated[3] == '\0' );

    REQUIRE( StringView_CopyToCString( view, terminatorOnly, 1u ) == 6u );
    REQUIRE( terminatorOnly[0] == '\0' );
    REQUIRE( StringView_CopyToCString( view, nullptr, 0u ) == 6u );
}

TEST_CASE( "StringView copy preserves bounded bytes and permits overlapping storage",
           "[CypherCommon][Tier1][StringView]" )
{
    const char bounded[] = { 'a', '\0', 'b', 'c' };
    char boundedDest[5] = {};
    const string_view_t boundedView =
        StringView_FromRange( bounded, sizeof( bounded ) );

    REQUIRE(
        StringView_CopyToCString( boundedView, boundedDest, sizeof( boundedDest ) ) ==
        sizeof( bounded ) );
    REQUIRE( boundedDest[0] == 'a' );
    REQUIRE( boundedDest[1] == '\0' );
    REQUIRE( boundedDest[2] == 'b' );
    REQUIRE( boundedDest[3] == 'c' );
    REQUIRE( boundedDest[4] == '\0' );

    char overlap[] = "abcdef";
    const string_view_t overlapView = StringView_FromRange( overlap + 1u, 5u );
    REQUIRE( StringView_CopyToCString( overlapView, overlap, 6u ) == 5u );
    REQUIRE( overlap[0] == 'b' );
    REQUIRE( overlap[1] == 'c' );
    REQUIRE( overlap[2] == 'd' );
    REQUIRE( overlap[3] == 'e' );
    REQUIRE( overlap[4] == 'f' );
    REQUIRE( overlap[5] == '\0' );
}

TEST_CASE( "StringView operations report invalid views and keep release behavior safe",
           "[CypherCommon][Tier1][StringView]" )
{
    const string_view_t invalidView{ nullptr, 2u };
    const string_view_t empty{};
    char dest[2] = { 'x', 'x' };

    g_stringViewAssertCount = 0u;
    Cy_AssertSetHandler( CaptureStringViewAssert );

    const bool_t bEndsWith = StringView_EndsWith( invalidView, empty );
    const string_view_t prefix = StringView_Prefix( invalidView, 1u );
    const usize iFound = StringView_FindChar( invalidView, 'a' );
    const string_view_t trimmed = StringView_Trim( invalidView );
    const usize cchCopied = StringView_CopyToCString( invalidView, dest, sizeof( dest ) );
    const usize cchInvalidDest = StringView_CopyToCString( empty, nullptr, 1u );

    Cy_AssertSetHandler( nullptr );

    REQUIRE( bEndsWith );
    REQUIRE( prefix.pData == nullptr );
    REQUIRE( prefix.cchLength == 0u );
    REQUIRE( iFound == CY_STRING_VIEW_NPOS );
    REQUIRE( trimmed.pData == nullptr );
    REQUIRE( trimmed.cchLength == 0u );
    REQUIRE( cchCopied == 0u );
    REQUIRE( dest[0] == '\0' );
    REQUIRE( cchInvalidDest == 0u );
    REQUIRE(
        g_stringViewAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
