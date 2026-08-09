//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Char.cpp
//  Purpose: Implements CypherCommon Tier1 Char support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Char.h"

namespace cypher::common
{

bool_t Char_IsAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue <= 0x7Fu );
}

bool_t Char_IsControlAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue <= 0x1Fu || nValue == 0x7Fu );
}

bool_t Char_IsPrintableAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= 0x20u && nValue <= 0x7Eu );
}

bool_t Char_IsGraphicalAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= 0x21u && nValue <= 0x7Eu );
}

bool_t Char_IsUpperAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= static_cast<u8>( 'A' ) &&
             nValue <= static_cast<u8>( 'Z' ) );
}

bool_t Char_IsLowerAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= static_cast<u8>( 'a' ) &&
             nValue <= static_cast<u8>( 'z' ) );
}

bool_t Char_IsAlphaAscii( char ch ) noexcept
{
    return ( Char_IsUpperAscii( ch ) || Char_IsLowerAscii( ch ) );
}

bool_t Char_IsAlphaNumericAscii( char ch ) noexcept
{
    return ( Char_IsAlphaAscii( ch ) || Char_IsDigitAscii( ch ) );
}

bool_t Char_IsPunctuationAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( ( nValue >= 0x21u && nValue <= 0x2Fu ) ||
             ( nValue >= 0x3Au && nValue <= 0x40u ) ||
             ( nValue >= 0x5Bu && nValue <= 0x60u ) ||
             ( nValue >= 0x7Bu && nValue <= 0x7Eu ) );
}

bool_t Char_IsDigitAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= static_cast<u8>( '0' ) &&
             nValue <= static_cast<u8>( '9' ) );
}

bool_t Char_IsBinaryDigitAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= static_cast<u8>( '0' ) &&
             nValue <= static_cast<u8>( '1' ) );
}

bool_t Char_IsOctalDigitAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue >= static_cast<u8>( '0' ) &&
             nValue <= static_cast<u8>( '7' ) );
}

bool_t Char_IsHexDigitAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( ( nValue >= static_cast<u8>( '0' ) &&
               nValue <= static_cast<u8>( '9' ) ) ||
             ( nValue >= static_cast<u8>( 'A' ) &&
               nValue <= static_cast<u8>( 'F' ) ) ||
             ( nValue >= static_cast<u8>( 'a' ) &&
               nValue <= static_cast<u8>( 'f' ) ) );
}

bool_t Char_IsBlankAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue == 0x09u || nValue == 0x20u );
}

bool_t Char_IsWhitespaceAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( ( nValue >= 0x09u && nValue <= 0x0Du ) ||
             nValue == 0x20u );
}

bool_t Char_IsNewLineAscii( char ch ) noexcept
{
    const u8 nValue = static_cast<u8>( ch );
    return ( nValue == 0x0Au || nValue == 0x0Du );
}

char Char_ToLowerAscii( char ch ) noexcept
{
    if ( Char_IsUpperAscii( ch ) ) {
        return static_cast<char>( ch + ( 'a' - 'A' ) );
    }
    return ch;
}

char Char_ToUpperAscii( char ch ) noexcept
{
    if ( Char_IsLowerAscii( ch ) ) {
        return static_cast<char>( ch - ( 'a' - 'A' ) );
    }
    return ch;
}

u8 Char_DigitValueAscii( char ch ) noexcept
{
    if ( Char_IsDigitAscii( ch ) ) {
        return static_cast<u8>( ch - '0' );
    }
    return CY_CHAR_INVALID_DIGIT_VALUE;
}

u8 Char_HexValueAscii( char ch ) noexcept
{
    if ( Char_IsDigitAscii( ch ) ) {
        return static_cast<u8>( ch - '0' );
    }
    if ( ch >= 'A' && ch <= 'F' ) {
        return static_cast<u8>( ch - 'A' + 10 );
    }
    if ( ch >= 'a' && ch <= 'f' ) {
        return static_cast<u8>( ch - 'a' + 10 );
    }
    return CY_CHAR_INVALID_DIGIT_VALUE;
}

} // namespace cypher::common
