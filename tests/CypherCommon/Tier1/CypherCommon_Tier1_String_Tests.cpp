//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_String_Tests.cpp
//  Purpose: Tests Tier1 String Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_String.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace cypher::common;

namespace
{

i32 SignOf( i32 nValue )
{
    if ( nValue < 0 ) {
        return -1;
    }
    if ( nValue > 0 ) {
        return 1;
    }
    return 0;
}

} // namespace

TEST_CASE( "Cy_strlen returns zero for null and empty strings", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strlen( nullptr ) == 0u );
    REQUIRE( Cy_strlen( "" ) == 0u );
}

TEST_CASE( "Cy_strlen counts characters before the terminator", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strlen( "cypher" ) == 6u );
    REQUIRE( Cy_strlen( "cypher engine" ) == 13u );
}

TEST_CASE( "Cy_strnlen caps the scan at the requested maximum", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strnlen( nullptr, 8u ) == 0u );
    REQUIRE( Cy_strnlen( "cypher", 0u ) == 0u );
    REQUIRE( Cy_strnlen( "cypher", 3u ) == 3u );
    REQUIRE( Cy_strnlen( "cypher", 64u ) == 6u );
}

TEST_CASE( "Cy_strisempty treats null as empty", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strisempty( nullptr ) );
    REQUIRE( Cy_strisempty( "" ) );
    REQUIRE_FALSE( Cy_strisempty( "cypher" ) );
}

TEST_CASE( "Cy_strisblank accepts only empty or ASCII whitespace strings", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strisblank( nullptr ) );
    REQUIRE( Cy_strisblank( "" ) );
    REQUIRE( Cy_strisblank( " \t\r\n" ) );
    REQUIRE_FALSE( Cy_strisblank( " cypher " ) );
}

TEST_CASE( "Cy_strcmp compares null as an empty string", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strcmp( nullptr, nullptr ) == 0 );
    REQUIRE( Cy_strcmp( nullptr, "" ) == 0 );
    REQUIRE( Cy_strcmp( "abc", "abc" ) == 0 );
    REQUIRE( Cy_strcmp( "abc", "abd" ) < 0 );
    REQUIRE( Cy_strcmp( "abd", "abc" ) > 0 );
    REQUIRE( Cy_strcmp( "abc", "ab" ) > 0 );
    REQUIRE( Cy_strcmp( "ab", "abc" ) < 0 );
    REQUIRE( Cy_strcmp( "Apple", "apple" ) < 0 );
}

TEST_CASE( "Cy_strcmp matches unsigned byte ordering across mismatch positions", "[CypherCommon][Tier1][String]" )
{
    const char pEqualA[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char pEqualB[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char pEarlyA[] = "xbcdefghijklmnopqrstuvwxyz0123456789";
    const char pEarlyB[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char pMiddleA[] = "abcdefghijklMnopqrstuvwxyz0123456789";
    const char pMiddleB[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char pLateA[] = "abcdefghijklmnopqrstuvwxyz012345678X";
    const char pLateB[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char pHighA[] = { static_cast<char>( 0x80u ), '\0' };
    const char pHighB[] = { static_cast<char>( 0x7Fu ), '\0' };

    REQUIRE( SignOf( Cy_strcmp( pEqualA, pEqualB ) ) == SignOf( std::strcmp( pEqualA, pEqualB ) ) );
    REQUIRE( SignOf( Cy_strcmp( pEarlyA, pEarlyB ) ) == SignOf( std::strcmp( pEarlyA, pEarlyB ) ) );
    REQUIRE( SignOf( Cy_strcmp( pMiddleA, pMiddleB ) ) == SignOf( std::strcmp( pMiddleA, pMiddleB ) ) );
    REQUIRE( SignOf( Cy_strcmp( pLateA, pLateB ) ) == SignOf( std::strcmp( pLateA, pLateB ) ) );
    REQUIRE( SignOf( Cy_strcmp( pHighA, pHighB ) ) == SignOf( std::strcmp( pHighA, pHighB ) ) );
}

TEST_CASE( "Cy_strncmp respects the maximum character count", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strncmp( "abc", "xyz", 0u ) == 0 );
    REQUIRE( Cy_strncmp( "abc", "abd", 2u ) == 0 );
    REQUIRE( Cy_strncmp( "abc", "abd", 3u ) < 0 );
    REQUIRE( Cy_strncmp( "abd", "abc", 3u ) > 0 );
    REQUIRE( Cy_strncmp( "ab", "abc", 2u ) == 0 );
    REQUIRE( Cy_strncmp( "ab", "abc", 3u ) < 0 );
}

TEST_CASE( "Cy_strncmp matches unsigned byte ordering for capped comparisons", "[CypherCommon][Tier1][String]" )
{
    const char pStringA[] = "textures/world/industrial/wall_panel_01_albedo.dds";
    const char pStringB[] = "textures/world/industrial/wall_panel_01_normal.dds";
    const char pHighA[] = { 'a', static_cast<char>( 0x80u ), '\0' };
    const char pHighB[] = { 'a', static_cast<char>( 0x7Fu ), '\0' };

    REQUIRE( SignOf( Cy_strncmp( pStringA, pStringB, 0u ) ) == SignOf( std::strncmp( pStringA, pStringB, 0u ) ) );
    REQUIRE( SignOf( Cy_strncmp( pStringA, pStringB, 16u ) ) == SignOf( std::strncmp( pStringA, pStringB, 16u ) ) );
    REQUIRE( SignOf( Cy_strncmp( pStringA, pStringB, 40u ) ) == SignOf( std::strncmp( pStringA, pStringB, 40u ) ) );
    REQUIRE( SignOf( Cy_strncmp( pHighA, pHighB, 3u ) ) == SignOf( std::strncmp( pHighA, pHighB, 3u ) ) );
}

TEST_CASE( "Cy_stricmp compares ASCII strings ignoring case", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_stricmp( nullptr, nullptr ) == 0 );
    REQUIRE( Cy_stricmp( nullptr, "" ) == 0 );
    REQUIRE( Cy_stricmp( "Player", "player" ) == 0 );
    REQUIRE( Cy_stricmp( "ABC", "abc" ) == 0 );
    REQUIRE( Cy_stricmp( "abc", "abd" ) < 0 );
    REQUIRE( Cy_stricmp( "abd", "abc" ) > 0 );
    REQUIRE( Cy_stricmp( "", "A" ) < 0 );
}

TEST_CASE( "Cy_strnicmp compares ASCII strings ignoring case up to a maximum", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strnicmp( "ABC", "abc", 3u ) == 0 );
    REQUIRE( Cy_strnicmp( "ABC", "abd", 2u ) == 0 );
    REQUIRE( Cy_strnicmp( "ABC", "abd", 3u ) < 0 );
    REQUIRE( Cy_strnicmp( "ABD", "abc", 3u ) > 0 );
    REQUIRE( Cy_strnicmp( "ab", "ABC", 2u ) == 0 );
    REQUIRE( Cy_strnicmp( "ab", "ABC", 3u ) < 0 );
}

TEST_CASE( "Cy string equality helpers wrap the compare functions", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strequal( "abc", "abc" ) );
    REQUIRE_FALSE( Cy_strequal( "ABC", "abc" ) );
    REQUIRE( Cy_striequal( "ABC", "abc" ) );
    REQUIRE( Cy_strnequal( "abc", "abd", 2u ) );
    REQUIRE_FALSE( Cy_strnequal( "abc", "abd", 3u ) );
    REQUIRE( Cy_strniequal( "ABC", "abd", 2u ) );
    REQUIRE_FALSE( Cy_strniequal( "ABC", "abd", 3u ) );
}

TEST_CASE( "Cy_strncpy copies safely and reports required source length", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[8]{};

    REQUIRE( Cy_strncpy( pBuffer, "cypher", sizeof( pBuffer ) ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cypher" ) );

    REQUIRE( Cy_strncpy( pBuffer, "engine-runtime", sizeof( pBuffer ) ) == 14u );
    REQUIRE( Cy_strequal( pBuffer, "engine-" ) );

    REQUIRE( Cy_strncpy( pBuffer, nullptr, sizeof( pBuffer ) ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strncpy( pBuffer, "abc", 0u ) == 3u );
    REQUIRE( Cy_strncpy( nullptr, "abc", 8u ) == 3u );

    char pExact[4]{};
    REQUIRE( Cy_strncpy( pExact, "abc", sizeof( pExact ) ) == 3u );
    REQUIRE( Cy_strequal( pExact, "abc" ) );

    char pOneByte[1] = { 'x' };
    REQUIRE( Cy_strncpy( pOneByte, "abc", sizeof( pOneByte ) ) == 3u );
    REQUIRE( pOneByte[0] == '\0' );
}

TEST_CASE( "Cy_strncpy_max copies at most the requested source count", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[8]{};

    REQUIRE( Cy_strncpy_max( pBuffer, "cypher", sizeof( pBuffer ), 64u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cypher" ) );

    REQUIRE( Cy_strncpy_max( pBuffer, "cypherengine", sizeof( pBuffer ), 6u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cypher" ) );

    REQUIRE( Cy_strncpy_max( pBuffer, "cypherengine", 4u, 12u ) == 12u );
    REQUIRE( Cy_strequal( pBuffer, "cyp" ) );

    REQUIRE( Cy_strncpy_max( pBuffer, nullptr, sizeof( pBuffer ), 10u ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strncpy_max( pBuffer, "abc", sizeof( pBuffer ), 0u ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strncpy_max( pBuffer, "abc", 0u, 2u ) == 2u );
    REQUIRE( Cy_strncpy_max( nullptr, "abc", 8u, 2u ) == 2u );

    char pOneByte[1] = { 'x' };
    REQUIRE( Cy_strncpy_max( pOneByte, "abc", sizeof( pOneByte ), 2u ) == 2u );
    REQUIRE( pOneByte[0] == '\0' );
}

TEST_CASE( "Cy_strncat appends safely and reports required final length", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[16] = "cypher";

    REQUIRE( Cy_strncat( pBuffer, "engine", sizeof( pBuffer ) ) == 12u );
    REQUIRE( Cy_strequal( pBuffer, "cypherengine" ) );

    char pSmall[10] = "cypher";
    REQUIRE( Cy_strncat( pSmall, "engine", sizeof( pSmall ) ) == 12u );
    REQUIRE( Cy_strequal( pSmall, "cyphereng" ) );

    REQUIRE( Cy_strncat( pSmall, nullptr, sizeof( pSmall ) ) == 9u );
    REQUIRE( Cy_strequal( pSmall, "cyphereng" ) );

    REQUIRE( Cy_strncat( nullptr, "abc", 8u ) == 3u );
    REQUIRE( Cy_strncat( pSmall, "abc", 0u ) == 3u );

    char pNoTerm[4] = { 'a', 'b', 'c', 'd' };
    REQUIRE( Cy_strncat( pNoTerm, "x", sizeof( pNoTerm ) ) == 5u );
    REQUIRE( pNoTerm[3] == '\0' );

    char pExact[7] = "abc";
    REQUIRE( Cy_strncat( pExact, "def", sizeof( pExact ) ) == 6u );
    REQUIRE( Cy_strequal( pExact, "abcdef" ) );

    char pFull[4] = "abc";
    REQUIRE( Cy_strncat( pFull, "d", sizeof( pFull ) ) == 4u );
    REQUIRE( Cy_strequal( pFull, "abc" ) );
}

TEST_CASE( "Cy_strncat_max appends at most the requested source count", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[16] = "cypher";

    REQUIRE( Cy_strncat_max( pBuffer, "engine-runtime", sizeof( pBuffer ), 6u ) == 12u );
    REQUIRE( Cy_strequal( pBuffer, "cypherengine" ) );

    char pSmall[10] = "cypher";
    REQUIRE( Cy_strncat_max( pSmall, "engine-runtime", sizeof( pSmall ), 6u ) == 12u );
    REQUIRE( Cy_strequal( pSmall, "cyphereng" ) );

    REQUIRE( Cy_strncat_max( pSmall, "abc", sizeof( pSmall ), 0u ) == 9u );
    REQUIRE( Cy_strequal( pSmall, "cyphereng" ) );

    REQUIRE( Cy_strncat_max( nullptr, "abc", 8u, 2u ) == 2u );
    REQUIRE( Cy_strncat_max( pSmall, "abc", 0u, 2u ) == 2u );

    char pExact[7] = "abc";
    REQUIRE( Cy_strncat_max( pExact, "defghi", sizeof( pExact ), 3u ) == 6u );
    REQUIRE( Cy_strequal( pExact, "abcdef" ) );
}

TEST_CASE( "Cy string character search helpers return pointers inside the source buffer", "[CypherCommon][Tier1][String]" )
{
    const char *pText = "textures/world/wall.dds";

    REQUIRE( Cy_strchr( pText, '/' ) == pText + 8u );
    REQUIRE( Cy_strchr( pText, 'z' ) == nullptr );
    REQUIRE( Cy_strchr( pText, '\0' ) == pText + Cy_strlen( pText ) );

    REQUIRE( Cy_strrchr( pText, '/' ) == pText + 14u );
    REQUIRE( Cy_strrchr( pText, 'z' ) == nullptr );
    REQUIRE( Cy_strrchr( pText, '\0' ) == pText + Cy_strlen( pText ) );

    REQUIRE( Cy_strnchr( pText, '/', 8u ) == nullptr );
    REQUIRE( Cy_strnchr( pText, '/', 9u ) == pText + 8u );
    REQUIRE( Cy_strnchr( static_cast<const char *>( nullptr ), '/', 9u ) == nullptr );

    char pMutable[] = "textures/world/wall.dds";
    REQUIRE( Cy_strchr( pMutable, '/' ) == pMutable + 8u );
    REQUIRE( Cy_strrchr( pMutable, '/' ) == pMutable + 14u );
    REQUIRE( Cy_strnchr( pMutable, '/', 9u ) == pMutable + 8u );
}

TEST_CASE( "Cy string substring search helpers handle case and capped scans", "[CypherCommon][Tier1][String]" )
{
    const char *pText = "Textures/World/Wall.DDS";

    REQUIRE( Cy_strstr( pText, "World" ) == pText + 9u );
    REQUIRE( Cy_strstr( pText, "world" ) == nullptr );
    REQUIRE( Cy_strstr( pText, "" ) == pText );
    REQUIRE( Cy_strstr( static_cast<const char *>( nullptr ), "World" ) == nullptr );

    REQUIRE( Cy_stristr( pText, "world" ) == pText + 9u );
    REQUIRE( Cy_stristr( pText, "WALL.dds" ) == pText + 15u );

    REQUIRE( Cy_strnstr( pText, "World", 14u ) == pText + 9u );
    REQUIRE( Cy_strnstr( pText, "World", 13u ) == nullptr );
    REQUIRE( Cy_strnistr( pText, "wall.dds", 23u ) == pText + 15u );
    REQUIRE( Cy_strnistr( pText, "wall.dds", 18u ) == nullptr );

    REQUIRE( Cy_strstr( pText, nullptr ) == pText );
    REQUIRE( Cy_strnstr( pText, "", 0u ) == pText );
    REQUIRE( Cy_strnstr( pText, "Textures", 8u ) == pText );
    REQUIRE( Cy_strnstr( pText, "Textures", 7u ) == nullptr );
    REQUIRE( Cy_strnistr( pText, "textures", 8u ) == pText );
    REQUIRE( Cy_strnistr( static_cast<const char *>( nullptr ), "textures", 8u ) == nullptr );

    char pMutable[] = "Textures/World/Wall.DDS";
    REQUIRE( Cy_strstr( pMutable, "World" ) == pMutable + 9u );
    REQUIRE( Cy_stristr( pMutable, "wall.dds" ) == pMutable + 15u );
    REQUIRE( Cy_strnstr( pMutable, "World", 14u ) == pMutable + 9u );
    REQUIRE( Cy_strnistr( pMutable, "wall.dds", 23u ) == pMutable + 15u );
}

TEST_CASE( "Cy string prefix and suffix helpers support case-sensitive and insensitive checks", "[CypherCommon][Tier1][String]" )
{
    REQUIRE( Cy_strstarts( "textures/world/wall.dds", "textures" ) );
    REQUIRE_FALSE( Cy_strstarts( "textures/world/wall.dds", "Textures" ) );
    REQUIRE( Cy_stristarts( "textures/world/wall.dds", "Textures" ) );
    REQUIRE( Cy_strstarts( "textures/world/wall.dds", "" ) );

    REQUIRE( Cy_strends( "textures/world/wall.dds", ".dds" ) );
    REQUIRE_FALSE( Cy_strends( "textures/world/wall.dds", ".DDS" ) );
    REQUIRE( Cy_striends( "textures/world/wall.dds", ".DDS" ) );
    REQUIRE_FALSE( Cy_strends( "wall", "longer-wall" ) );

    REQUIRE( Cy_strstarts( nullptr, nullptr ) );
    REQUIRE( Cy_strstarts( "textures/world/wall.dds", nullptr ) );
    REQUIRE_FALSE( Cy_strstarts( nullptr, "textures" ) );
    REQUIRE( Cy_strends( nullptr, nullptr ) );
    REQUIRE( Cy_strends( "textures/world/wall.dds", nullptr ) );
    REQUIRE_FALSE( Cy_strends( nullptr, ".dds" ) );
    REQUIRE( Cy_stristarts( "textures/world/wall.dds", "TEXTURES/WORLD" ) );
    REQUIRE( Cy_striends( "textures/world/wall.dds", "WALL.DDS" ) );
}

TEST_CASE( "Cy string case helpers mutate ASCII text in place", "[CypherCommon][Tier1][String]" )
{
    char pLower[] = "Cypher123!";
    REQUIRE( Cy_strlower( pLower ) == pLower );
    REQUIRE( Cy_strequal( pLower, "cypher123!" ) );

    char pUpper[] = "Cypher123!";
    REQUIRE( Cy_strupper( pUpper ) == pUpper );
    REQUIRE( Cy_strequal( pUpper, "CYPHER123!" ) );

    char pPartialLower[] = "ABCDEF";
    REQUIRE( Cy_strnlower( pPartialLower, 3u ) == pPartialLower );
    REQUIRE( Cy_strequal( pPartialLower, "abcDEF" ) );

    char pPartialUpper[] = "abcdef";
    REQUIRE( Cy_strnupper( pPartialUpper, 3u ) == pPartialUpper );
    REQUIRE( Cy_strequal( pPartialUpper, "ABCdef" ) );

    REQUIRE( Cy_strlower( nullptr ) == nullptr );
    REQUIRE( Cy_strupper( nullptr ) == nullptr );

    char pZeroLower[] = "ABC";
    REQUIRE( Cy_strnlower( pZeroLower, 0u ) == pZeroLower );
    REQUIRE( Cy_strequal( pZeroLower, "ABC" ) );

    char pZeroUpper[] = "abc";
    REQUIRE( Cy_strnupper( pZeroUpper, 0u ) == pZeroUpper );
    REQUIRE( Cy_strequal( pZeroUpper, "abc" ) );

    REQUIRE( Cy_strislower( "" ) );
    REQUIRE( Cy_strislower( "abc123!" ) );
    REQUIRE_FALSE( Cy_strislower( "abcD" ) );
    REQUIRE( Cy_strisupper( "" ) );
    REQUIRE( Cy_strisupper( "ABC123!" ) );
    REQUIRE_FALSE( Cy_strisupper( "ABc" ) );
}

TEST_CASE( "Cy string whitespace and quote helpers trim in place", "[CypherCommon][Tier1][String]" )
{
    char pText[] = " \t  cypher engine \r\n";
    REQUIRE( Cy_strskipwhite( pText ) == pText + 4u );

    Cy_strtrimleft( pText );
    REQUIRE( Cy_strequal( pText, "cypher engine \r\n" ) );

    Cy_strtrimright( pText );
    REQUIRE( Cy_strequal( pText, "cypher engine" ) );

    char pBoth[] = "\n\t cypher \r\n";
    Cy_strtrim( pBoth );
    REQUIRE( Cy_strequal( pBoth, "cypher" ) );

    char pQuoted[] = "\"cypher\"";
    Cy_strstripquotes( pQuoted );
    REQUIRE( Cy_strequal( pQuoted, "cypher" ) );

    char pSingleQuoted[] = "'engine'";
    Cy_strstripquotes( pSingleQuoted );
    REQUIRE( Cy_strequal( pSingleQuoted, "engine" ) );

    char pMismatched[] = "\"engine'";
    Cy_strstripquotes( pMismatched );
    REQUIRE( Cy_strequal( pMismatched, "\"engine'" ) );

    char pAllWhitespace[] = " \t\r\n";
    Cy_strtrim( pAllWhitespace );
    REQUIRE( Cy_strequal( pAllWhitespace, "" ) );

    char pEmpty[] = "";
    Cy_strtrim( pEmpty );
    REQUIRE( Cy_strequal( pEmpty, "" ) );

    char pSingleQuote[] = "\"";
    Cy_strstripquotes( pSingleQuote );
    REQUIRE( Cy_strequal( pSingleQuote, "\"" ) );

    Cy_strtrimleft( nullptr );
    Cy_strtrimright( nullptr );
    Cy_strtrim( nullptr );
    Cy_strstripquotes( nullptr );
}

TEST_CASE( "Cy string slice helpers copy selected ranges safely", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[16]{};

    REQUIRE( Cy_strleft( "cypherengine", pBuffer, sizeof( pBuffer ), 6u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cypher" ) );

    REQUIRE( Cy_strright( "cypherengine", pBuffer, sizeof( pBuffer ), 6u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "engine" ) );

    REQUIRE( Cy_strslice( "cypherengine", pBuffer, sizeof( pBuffer ), 6u, 6u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "engine" ) );

    REQUIRE( Cy_strslice( "cypherengine", pBuffer, sizeof( pBuffer ), 64u, 6u ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strleft( "cypherengine", pBuffer, 4u, 6u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cyp" ) );

    REQUIRE( Cy_strright( "cypher", pBuffer, sizeof( pBuffer ), 64u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "cypher" ) );

    REQUIRE( Cy_strslice( "cypherengine", pBuffer, sizeof( pBuffer ), 6u, 64u ) == 6u );
    REQUIRE( Cy_strequal( pBuffer, "engine" ) );

    REQUIRE( Cy_strslice( "cypherengine", pBuffer, sizeof( pBuffer ), 6u, 0u ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strright( nullptr, pBuffer, sizeof( pBuffer ), 4u ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );
}

TEST_CASE( "Cy string substitution and count helpers report required output size", "[CypherCommon][Tier1][String]" )
{
    char pBuffer[32]{};

    REQUIRE( Cy_strsubst( "materials/wall.mat", "wall", "floor", pBuffer, sizeof( pBuffer ) ) == 19u );
    REQUIRE( Cy_strequal( pBuffer, "materials/floor.mat" ) );

    REQUIRE( Cy_strsubst( "aaaa", "aa", "b", pBuffer, sizeof( pBuffer ) ) == 2u );
    REQUIRE( Cy_strequal( pBuffer, "bb" ) );

    REQUIRE( Cy_strsubst( "materials/wall.mat", "wall", "floor", pBuffer, 12u ) == 19u );
    REQUIRE( Cy_strequal( pBuffer, "materials/f" ) );

    REQUIRE( Cy_strsubst( "abc", "", "x", pBuffer, sizeof( pBuffer ) ) == 3u );
    REQUIRE( Cy_strequal( pBuffer, "abc" ) );

    REQUIRE( Cy_strsubst( "abc", "b", nullptr, pBuffer, sizeof( pBuffer ) ) == 2u );
    REQUIRE( Cy_strequal( pBuffer, "ac" ) );

    REQUIRE( Cy_strsubst( nullptr, "b", "x", pBuffer, sizeof( pBuffer ) ) == 0u );
    REQUIRE( Cy_strequal( pBuffer, "" ) );

    REQUIRE( Cy_strsubst( "abc", "b", "xyz", pBuffer, 1u ) == 5u );
    REQUIRE( pBuffer[0] == '\0' );

    REQUIRE( Cy_strsubst( "abc", "b", "xyz", nullptr, 0u ) == 5u );

    REQUIRE( Cy_strcountchar( "a/b/c", '/' ) == 2u );
    REQUIRE( Cy_strcountchar( nullptr, '/' ) == 0u );

    REQUIRE( Cy_strcountstring( "aaaa", "aa" ) == 2u );
    REQUIRE( Cy_strcountstring( "aaa", "aa" ) == 1u );
    REQUIRE( Cy_strcountstring( "abcabcabc", "abc" ) == 3u );
    REQUIRE( Cy_strcountstring( "aaaa", "" ) == 0u );
    REQUIRE( Cy_strcountstring( nullptr, "aa" ) == 0u );
}
