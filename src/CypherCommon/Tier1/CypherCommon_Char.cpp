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
    
bool_t Char_IsAscii( char c )
{
    const u8 value = static_cast<u8>( c );
    return ( value <= 0x7Fu );   
}

bool_t Char_IsControlAscii( char c )
{
    const u8 value = static_cast<u8>( c );
    return ( value < 0x20u || value == 0x7Fu );
}

bool_t Char_IsPrintableAscii( char c )
{
    const u8 value = static_cast<u8>( c );
    return ( value >= 0x20u && value <= 0x7Eu );
}

bool_t Char_IsUpperAscii( char c )
{
    return ( c >= 'A' && c <= 'Z' );
}

bool_t Char_IsLowerAscii( char c )
{
    return ( c >= 'a' && c <= 'z' );
}

bool_t Char_IsAlphaAscii( char c )
{
    return ( Char_IsUpperAscii( c ) || Char_IsLowerAscii( c ) );
}

bool_t Char_IsDigitAscii( char c )
{
    return ( c >= '0' && c <= '9' );
}

bool_t Char_IsIdentifierStart( char c )
{
    return ( Char_IsAlphaAscii( c ) || c == '-' );
}

bool_t Char_IsIdentifierBody( char c )
{
    return ( Char_IsIdentifierStart( c ) || Char_IsDigitAscii( c ) );
}

bool_t Char_IsPathNameChar( char c )
{
    return ( Char_IsAlphaNumericAscii( c ) ||
           c == '_' ||
           c == '-' ||
           c == '.' ||
           Char_IsPathSeparator( c ) );
}

bool_t Char_IsHexDigitAscii( char c )
{
    return ( Char_IsDigitAscii( c ) || c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E' || c == 'F' || c == 'a' || c == 'b' || c == 'c' || c == 'd' || c == 'e' || c == 'f' );
}

bool_t Char_IsAlphaNumericAscii( char c )
{
    return ( Char_IsAlphaAscii( c ) || Char_IsDigitAscii( c ) );
}

bool_t Char_IsWhitespaceAscii( char c )
{
    const u8 value = static_cast<u8>( c );
    return ( value == 0x20u || value == 0x09u || value == 0x0Au || value == 0x0Bu || value == 0x0Cu || value == 0x0Du );
}

bool_t Char_IsNewLineAscii( char c )
{
    return ( c == '\n' || c == '\r' ); 
}

bool_t Char_IsSlash( char c )
{
    return ( c == '\\' || c == '/' );
}

bool_t Char_IsPathSeparator( char c )
{
    return Char_IsSlash( c );
}

bool_t Char_IsDriveSeparator( char c )
{
    return ( c == ':' );
}

char Char_ToLowerAscii( char c )
{
    if ( Char_IsUpperAscii( c ) ) {
        return c + ( 'a' - 'A' );
    } else {
        return c;
    }
}

char Char_ToUpperAscii( char c )
{
    if ( Char_IsLowerAscii( c ) ) {
        return c - ( 'a' - 'A' );
    } else {
        return c;
    }
}

u8 Char_HexValueAscii( char c )
{
    if ( c >= '0' && c <= '9' ) {
        return static_cast<u8>( c - '0' );
    }
    if ( c >= 'A' && c <= 'F' ) {
        return static_cast<u8>( c - 'A' + 10u );
    }
    if ( c >= 'a' && c <= 'f' ) {
        return static_cast<u8>( c - 'a' + 10u );
    }
    return 0xFFu;
}

}       // namespace cypher::common
