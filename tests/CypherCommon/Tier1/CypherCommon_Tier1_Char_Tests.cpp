//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Char_Tests.cpp
//  Purpose: Tests Tier1 Char Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Char.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Char ASCII classification handles control and printable ranges", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsAscii( '\0' ) );
    REQUIRE( Char_IsAscii( 'A' ) );
    REQUIRE( Char_IsAscii( '~' ) );
    REQUIRE_FALSE( Char_IsAscii( static_cast<char>( 0x80u ) ) );

    REQUIRE( Char_IsControlAscii( '\0' ) );
    REQUIRE( Char_IsControlAscii( '\n' ) );
    REQUIRE( Char_IsControlAscii( static_cast<char>( 0x7Fu ) ) );
    REQUIRE_FALSE( Char_IsControlAscii( ' ' ) );

    REQUIRE( Char_IsPrintableAscii( ' ' ) );
    REQUIRE( Char_IsPrintableAscii( '~' ) );
    REQUIRE_FALSE( Char_IsPrintableAscii( '\n' ) );
}

TEST_CASE( "Char alpha digit and alphanumeric checks are ASCII only", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsUpperAscii( 'A' ) );
    REQUIRE( Char_IsUpperAscii( 'Z' ) );
    REQUIRE_FALSE( Char_IsUpperAscii( 'a' ) );

    REQUIRE( Char_IsLowerAscii( 'a' ) );
    REQUIRE( Char_IsLowerAscii( 'z' ) );
    REQUIRE_FALSE( Char_IsLowerAscii( 'A' ) );

    REQUIRE( Char_IsAlphaAscii( 'C' ) );
    REQUIRE( Char_IsAlphaAscii( 'c' ) );
    REQUIRE_FALSE( Char_IsAlphaAscii( '7' ) );

    REQUIRE( Char_IsDigitAscii( '0' ) );
    REQUIRE( Char_IsDigitAscii( '9' ) );
    REQUIRE_FALSE( Char_IsDigitAscii( 'a' ) );

    REQUIRE( Char_IsAlphaNumericAscii( 'q' ) );
    REQUIRE( Char_IsAlphaNumericAscii( '5' ) );
    REQUIRE_FALSE( Char_IsAlphaNumericAscii( '_' ) );
}

TEST_CASE( "Char whitespace newline and separator checks follow engine ASCII policy", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsWhitespaceAscii( ' ' ) );
    REQUIRE( Char_IsWhitespaceAscii( '\t' ) );
    REQUIRE( Char_IsWhitespaceAscii( '\n' ) );
    REQUIRE( Char_IsWhitespaceAscii( '\r' ) );
    REQUIRE_FALSE( Char_IsWhitespaceAscii( 'x' ) );

    REQUIRE( Char_IsNewLineAscii( '\n' ) );
    REQUIRE( Char_IsNewLineAscii( '\r' ) );
    REQUIRE_FALSE( Char_IsNewLineAscii( '\t' ) );

    REQUIRE( Char_IsSlash( '/' ) );
    REQUIRE( Char_IsSlash( '\\' ) );
    REQUIRE( Char_IsPathSeparator( '/' ) );
    REQUIRE( Char_IsPathSeparator( '\\' ) );
    REQUIRE( Char_IsDriveSeparator( ':' ) );
    REQUIRE_FALSE( Char_IsDriveSeparator( '/' ) );
}

TEST_CASE( "Char identifier checks follow current engine token policy", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsIdentifierStart( 'a' ) );
    REQUIRE( Char_IsIdentifierStart( 'Z' ) );
    REQUIRE( Char_IsIdentifierStart( '-' ) );
    REQUIRE_FALSE( Char_IsIdentifierStart( '0' ) );
    REQUIRE_FALSE( Char_IsIdentifierStart( '_' ) );

    REQUIRE( Char_IsIdentifierBody( 'a' ) );
    REQUIRE( Char_IsIdentifierBody( 'Z' ) );
    REQUIRE( Char_IsIdentifierBody( '7' ) );
    REQUIRE( Char_IsIdentifierBody( '-' ) );
    REQUIRE_FALSE( Char_IsIdentifierBody( '_' ) );
}

TEST_CASE( "Char path name check accepts current virtual path character set", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsPathNameChar( 'a' ) );
    REQUIRE( Char_IsPathNameChar( 'Z' ) );
    REQUIRE( Char_IsPathNameChar( '7' ) );
    REQUIRE( Char_IsPathNameChar( '_' ) );
    REQUIRE( Char_IsPathNameChar( '-' ) );
    REQUIRE( Char_IsPathNameChar( '.' ) );
    REQUIRE( Char_IsPathNameChar( '/' ) );
    REQUIRE( Char_IsPathNameChar( '\\' ) );

    REQUIRE_FALSE( Char_IsPathNameChar( ':' ) );
    REQUIRE_FALSE( Char_IsPathNameChar( '*' ) );
    REQUIRE_FALSE( Char_IsPathNameChar( ' ' ) );
}

TEST_CASE( "Char ASCII case conversion leaves non alphabetic characters unchanged", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_ToLowerAscii( 'A' ) == 'a' );
    REQUIRE( Char_ToLowerAscii( 'Z' ) == 'z' );
    REQUIRE( Char_ToLowerAscii( 'a' ) == 'a' );
    REQUIRE( Char_ToLowerAscii( '7' ) == '7' );

    REQUIRE( Char_ToUpperAscii( 'a' ) == 'A' );
    REQUIRE( Char_ToUpperAscii( 'z' ) == 'Z' );
    REQUIRE( Char_ToUpperAscii( 'A' ) == 'A' );
    REQUIRE( Char_ToUpperAscii( '7' ) == '7' );
}

TEST_CASE( "Char hex digit validation accepts decimal and ASCII hex letters", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_IsHexDigitAscii( '0' ) );
    REQUIRE( Char_IsHexDigitAscii( '9' ) );
    REQUIRE( Char_IsHexDigitAscii( 'A' ) );
    REQUIRE( Char_IsHexDigitAscii( 'F' ) );
    REQUIRE( Char_IsHexDigitAscii( 'a' ) );
    REQUIRE( Char_IsHexDigitAscii( 'f' ) );

    REQUIRE_FALSE( Char_IsHexDigitAscii( 'G' ) );
    REQUIRE_FALSE( Char_IsHexDigitAscii( 'g' ) );
    REQUIRE_FALSE( Char_IsHexDigitAscii( '/' ) );
}

TEST_CASE( "Char hex value conversion returns nibble values and invalid sentinel", "[CypherCommon][Tier1][Char]" )
{
    REQUIRE( Char_HexValueAscii( '0' ) == 0u );
    REQUIRE( Char_HexValueAscii( '1' ) == 1u );
    REQUIRE( Char_HexValueAscii( '9' ) == 9u );

    REQUIRE( Char_HexValueAscii( 'A' ) == 10u );
    REQUIRE( Char_HexValueAscii( 'B' ) == 11u );
    REQUIRE( Char_HexValueAscii( 'F' ) == 15u );

    REQUIRE( Char_HexValueAscii( 'a' ) == 10u );
    REQUIRE( Char_HexValueAscii( 'b' ) == 11u );
    REQUIRE( Char_HexValueAscii( 'f' ) == 15u );

    REQUIRE( Char_HexValueAscii( 'x' ) == 0xFFu );
    REQUIRE( Char_HexValueAscii( '\0' ) == 0xFFu );
}
