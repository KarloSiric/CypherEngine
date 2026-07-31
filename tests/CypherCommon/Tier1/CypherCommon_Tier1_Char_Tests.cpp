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

#include <bit>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

constexpr u32 kByteValueCount = 256u;

constexpr char ByteToChar( u32 nValue ) noexcept
{
    return std::bit_cast<char>( static_cast<u8>( nValue ) );
}

constexpr u8 ExpectedDigitValue( u32 nValue ) noexcept
{
    if ( nValue >= static_cast<u32>( '0' ) &&
         nValue <= static_cast<u32>( '9' ) ) {
        return static_cast<u8>( nValue - static_cast<u32>( '0' ) );
    }
    return CY_CHAR_INVALID_DIGIT_VALUE;
}

constexpr u8 ExpectedHexValue( u32 nValue ) noexcept
{
    if ( nValue >= static_cast<u32>( '0' ) &&
         nValue <= static_cast<u32>( '9' ) ) {
        return static_cast<u8>( nValue - static_cast<u32>( '0' ) );
    }
    if ( nValue >= static_cast<u32>( 'A' ) &&
         nValue <= static_cast<u32>( 'F' ) ) {
        return static_cast<u8>( nValue - static_cast<u32>( 'A' ) + 10u );
    }
    if ( nValue >= static_cast<u32>( 'a' ) &&
         nValue <= static_cast<u32>( 'f' ) ) {
        return static_cast<u8>( nValue - static_cast<u32>( 'a' ) + 10u );
    }
    return CY_CHAR_INVALID_DIGIT_VALUE;
}

} // namespace

TEST_CASE( "Char classifies all byte values using explicit ASCII ranges", "[CypherCommon][Tier1][Char]" )
{
    u32 nAsciiCount = 0u;
    u32 nControlCount = 0u;
    u32 nPrintableCount = 0u;
    u32 nGraphicalCount = 0u;
    u32 nUpperCount = 0u;
    u32 nLowerCount = 0u;
    u32 nAlphaCount = 0u;
    u32 nAlphaNumericCount = 0u;
    u32 nPunctuationCount = 0u;
    u32 nDigitCount = 0u;
    u32 nBinaryDigitCount = 0u;
    u32 nOctalDigitCount = 0u;
    u32 nHexDigitCount = 0u;
    u32 nBlankCount = 0u;
    u32 nWhitespaceCount = 0u;
    u32 nNewLineCount = 0u;

    for ( u32 nValue = 0u; nValue < kByteValueCount; ++nValue ) {
        CAPTURE( nValue );

        const char ch = ByteToChar( nValue );
        const bool_t isAscii = ( nValue <= 0x7Fu );
        const bool_t isControl = ( nValue <= 0x1Fu || nValue == 0x7Fu );
        const bool_t isPrintable = ( nValue >= 0x20u && nValue <= 0x7Eu );
        const bool_t isGraphical = ( nValue >= 0x21u && nValue <= 0x7Eu );
        const bool_t isUpper =
            ( nValue >= static_cast<u32>( 'A' ) &&
              nValue <= static_cast<u32>( 'Z' ) );
        const bool_t isLower =
            ( nValue >= static_cast<u32>( 'a' ) &&
              nValue <= static_cast<u32>( 'z' ) );
        const bool_t isAlpha = ( isUpper || isLower );
        const bool_t isDigit =
            ( nValue >= static_cast<u32>( '0' ) &&
              nValue <= static_cast<u32>( '9' ) );
        const bool_t isAlphaNumeric = ( isAlpha || isDigit );
        const bool_t isPunctuation =
            ( ( nValue >= 0x21u && nValue <= 0x2Fu ) ||
              ( nValue >= 0x3Au && nValue <= 0x40u ) ||
              ( nValue >= 0x5Bu && nValue <= 0x60u ) ||
              ( nValue >= 0x7Bu && nValue <= 0x7Eu ) );
        const bool_t isBinaryDigit =
            ( nValue >= static_cast<u32>( '0' ) &&
              nValue <= static_cast<u32>( '1' ) );
        const bool_t isOctalDigit =
            ( nValue >= static_cast<u32>( '0' ) &&
              nValue <= static_cast<u32>( '7' ) );
        const bool_t isHexDigit =
            ( isDigit ||
              ( nValue >= static_cast<u32>( 'A' ) &&
                nValue <= static_cast<u32>( 'F' ) ) ||
              ( nValue >= static_cast<u32>( 'a' ) &&
                nValue <= static_cast<u32>( 'f' ) ) );
        const bool_t isBlank = ( nValue == 0x09u || nValue == 0x20u );
        const bool_t isWhitespace =
            ( ( nValue >= 0x09u && nValue <= 0x0Du ) ||
              nValue == 0x20u );
        const bool_t isNewLine = ( nValue == 0x0Au || nValue == 0x0Du );

        CHECK( Char_IsAscii( ch ) == isAscii );
        CHECK( Char_IsControlAscii( ch ) == isControl );
        CHECK( Char_IsPrintableAscii( ch ) == isPrintable );
        CHECK( Char_IsGraphicalAscii( ch ) == isGraphical );
        CHECK( Char_IsUpperAscii( ch ) == isUpper );
        CHECK( Char_IsLowerAscii( ch ) == isLower );
        CHECK( Char_IsAlphaAscii( ch ) == isAlpha );
        CHECK( Char_IsAlphaNumericAscii( ch ) == isAlphaNumeric );
        CHECK( Char_IsPunctuationAscii( ch ) == isPunctuation );
        CHECK( Char_IsDigitAscii( ch ) == isDigit );
        CHECK( Char_IsBinaryDigitAscii( ch ) == isBinaryDigit );
        CHECK( Char_IsOctalDigitAscii( ch ) == isOctalDigit );
        CHECK( Char_IsHexDigitAscii( ch ) == isHexDigit );
        CHECK( Char_IsBlankAscii( ch ) == isBlank );
        CHECK( Char_IsWhitespaceAscii( ch ) == isWhitespace );
        CHECK( Char_IsNewLineAscii( ch ) == isNewLine );

        nAsciiCount += Char_IsAscii( ch ) ? 1u : 0u;
        nControlCount += Char_IsControlAscii( ch ) ? 1u : 0u;
        nPrintableCount += Char_IsPrintableAscii( ch ) ? 1u : 0u;
        nGraphicalCount += Char_IsGraphicalAscii( ch ) ? 1u : 0u;
        nUpperCount += Char_IsUpperAscii( ch ) ? 1u : 0u;
        nLowerCount += Char_IsLowerAscii( ch ) ? 1u : 0u;
        nAlphaCount += Char_IsAlphaAscii( ch ) ? 1u : 0u;
        nAlphaNumericCount += Char_IsAlphaNumericAscii( ch ) ? 1u : 0u;
        nPunctuationCount += Char_IsPunctuationAscii( ch ) ? 1u : 0u;
        nDigitCount += Char_IsDigitAscii( ch ) ? 1u : 0u;
        nBinaryDigitCount += Char_IsBinaryDigitAscii( ch ) ? 1u : 0u;
        nOctalDigitCount += Char_IsOctalDigitAscii( ch ) ? 1u : 0u;
        nHexDigitCount += Char_IsHexDigitAscii( ch ) ? 1u : 0u;
        nBlankCount += Char_IsBlankAscii( ch ) ? 1u : 0u;
        nWhitespaceCount += Char_IsWhitespaceAscii( ch ) ? 1u : 0u;
        nNewLineCount += Char_IsNewLineAscii( ch ) ? 1u : 0u;
    }

    REQUIRE( nAsciiCount == 128u );
    REQUIRE( nControlCount == 33u );
    REQUIRE( nPrintableCount == 95u );
    REQUIRE( nGraphicalCount == 94u );
    REQUIRE( nUpperCount == 26u );
    REQUIRE( nLowerCount == 26u );
    REQUIRE( nAlphaCount == 52u );
    REQUIRE( nAlphaNumericCount == 62u );
    REQUIRE( nPunctuationCount == 32u );
    REQUIRE( nDigitCount == 10u );
    REQUIRE( nBinaryDigitCount == 2u );
    REQUIRE( nOctalDigitCount == 8u );
    REQUIRE( nHexDigitCount == 22u );
    REQUIRE( nBlankCount == 2u );
    REQUIRE( nWhitespaceCount == 6u );
    REQUIRE( nNewLineCount == 2u );
}

TEST_CASE( "Char conversions cover every byte and preserve non targets", "[CypherCommon][Tier1][Char]" )
{
    for ( u32 nValue = 0u; nValue < kByteValueCount; ++nValue ) {
        CAPTURE( nValue );

        const char ch = ByteToChar( nValue );
        char chExpectedLower = ch;
        char chExpectedUpper = ch;

        if ( nValue >= static_cast<u32>( 'A' ) &&
             nValue <= static_cast<u32>( 'Z' ) ) {
            chExpectedLower = ByteToChar( nValue + ( 'a' - 'A' ) );
        }
        if ( nValue >= static_cast<u32>( 'a' ) &&
             nValue <= static_cast<u32>( 'z' ) ) {
            chExpectedUpper = ByteToChar( nValue - ( 'a' - 'A' ) );
        }

        CHECK( Char_ToLowerAscii( ch ) == chExpectedLower );
        CHECK( Char_ToUpperAscii( ch ) == chExpectedUpper );
        CHECK( Char_DigitValueAscii( ch ) == ExpectedDigitValue( nValue ) );
        CHECK( Char_HexValueAscii( ch ) == ExpectedHexValue( nValue ) );
    }
}
