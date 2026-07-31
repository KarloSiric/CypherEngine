//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Char.h
//  Purpose: Declares CypherCommon Tier1 Char support.
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

#ifndef CYPHER_COMMON_TIER1_CHAR_H
#define CYPHER_COMMON_TIER1_CHAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

// Returned by digit conversion functions when the input is not valid.
constexpr u8 CY_CHAR_INVALID_DIGIT_VALUE = CY_U8_MAX;

/*
================
ASCII Layout
================
*/

// Returns true when ch belongs to the 7-bit ASCII character set.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsAscii( char ch ) noexcept;

// Returns true for ASCII control bytes 0x00-0x1F and 0x7F.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsControlAscii( char ch ) noexcept;

// Returns true for printable ASCII, including the space character.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsPrintableAscii( char ch ) noexcept;

// Returns true for visible ASCII characters, excluding the space character.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsGraphicalAscii( char ch ) noexcept;

/*
================
Alphabetic
================
*/

// Returns true for ASCII uppercase letters A-Z.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsUpperAscii( char ch ) noexcept;

// Returns true for ASCII lowercase letters a-z.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsLowerAscii( char ch ) noexcept;

// Returns true for ASCII letters A-Z or a-z.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsAlphaAscii( char ch ) noexcept;

// Returns true for ASCII letters or decimal digits.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsAlphaNumericAscii( char ch ) noexcept;

// Returns true for printable ASCII punctuation.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsPunctuationAscii( char ch ) noexcept;

/*
================
Numeric
================
*/

// Returns true for decimal digits 0-9.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsDigitAscii( char ch ) noexcept;

// Returns true for binary digits 0-1.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsBinaryDigitAscii( char ch ) noexcept;

// Returns true for octal digits 0-7.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsOctalDigitAscii( char ch ) noexcept;

// Returns true for hexadecimal digits 0-9, A-F, or a-f.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsHexDigitAscii( char ch ) noexcept;

/*
================
Whitespace
================
*/

// Returns true for ASCII space or horizontal tab.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsBlankAscii( char ch ) noexcept;

// Returns true for the six ASCII whitespace characters.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsWhitespaceAscii( char ch ) noexcept;

// Returns true for ASCII line feed or carriage return.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Char_IsNewLineAscii( char ch ) noexcept;

/*
================
Conversion
================
*/

// Converts an ASCII uppercase letter to lowercase and preserves other bytes.
CYPHER_NODISCARD CYPHER_COMMON_API char Char_ToLowerAscii( char ch ) noexcept;

// Converts an ASCII lowercase letter to uppercase and preserves other bytes.
CYPHER_NODISCARD CYPHER_COMMON_API char Char_ToUpperAscii( char ch ) noexcept;

// Converts an ASCII decimal digit to 0-9, or returns the invalid sentinel.
CYPHER_NODISCARD CYPHER_COMMON_API u8 Char_DigitValueAscii( char ch ) noexcept;

// Converts an ASCII hexadecimal digit to 0-15, or returns the invalid sentinel.
CYPHER_NODISCARD CYPHER_COMMON_API u8 Char_HexValueAscii( char ch ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHAR_H
