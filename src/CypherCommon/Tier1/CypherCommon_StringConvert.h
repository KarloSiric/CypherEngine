//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringConvert.h
//  Purpose: Declares deterministic primitive-to-text and binary-text conversion.
//  Details: Every writer is bounded, supports count-only queries, and reports
//           truncation without relying on locale or exceptions.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGCONVERT_H
#define CYPHER_COMMON_TIER1_STRINGCONVERT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class string_convert_status_t : u8 {
    OK = 0u,          // Complete conversion was written.
    INVALID_ARGUMENT, // Input view, destination, or options are invalid.
    INVALID_BASE,     // Integer base lies outside [2, 36].
    INVALID_TEXT,     // Source cannot be converted under requested rules.
    OUTPUT_TRUNCATED  // Destination contains only a valid prefix.
};

enum string_integer_format_flags_t : flags32_t {
    STRING_INTEGER_FORMAT_FLAG_NONE = 0u, // Lowercase digits without decoration.
    STRING_INTEGER_FORMAT_FLAG_UPPERCASE = CYPHER_BIT32( 0 ), // Use A-Z digits/prefix.
    STRING_INTEGER_FORMAT_FLAG_PREFIX = CYPHER_BIT32( 1 ), // Emit radix prefix where defined.
    STRING_INTEGER_FORMAT_FLAG_PLUS_SIGN = CYPHER_BIT32( 2 ) // Prefix non-negative values.
};

enum class string_float_style_t : u8 {
    GENERAL = 0u, // Select compact fixed or exponent notation.
    FIXED,        // Always use fixed-point decimal notation.
    SCIENTIFIC    // Always use exponent notation.
};

enum string_float_format_flags_t : flags32_t {
    STRING_FLOAT_FORMAT_FLAG_NONE = 0u, // Lowercase notation without decoration.
    STRING_FLOAT_FORMAT_FLAG_UPPERCASE = CYPHER_BIT32( 0 ), // Uppercase exponent/INF/NAN.
    STRING_FLOAT_FORMAT_FLAG_PLUS_SIGN = CYPHER_BIT32( 1 ), // Prefix non-negative values.
    STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO = CYPHER_BIT32( 2 ) // Trim fraction zeros.
};

struct string_integer_format_t {
    u8 nBase{ 10u };      // Radix in [2, 36].
    u8 nMinDigits{ 1u };  // Zero-padding width excluding sign and prefix.
    flags32_t flags{ STRING_INTEGER_FORMAT_FLAG_NONE }; // STRING_INTEGER_FORMAT_FLAG_*.
};

struct string_float_format_t {
    string_float_style_t style{ string_float_style_t::GENERAL }; // Notation policy.
    u8 nPrecision{ 6u }; // Significant digits or fractional digits, per style.
    flags32_t flags{ STRING_FLOAT_FORMAT_FLAG_NONE }; // STRING_FLOAT_FORMAT_FLAG_*.
};

struct string_convert_result_t {
    string_convert_status_t status{ string_convert_status_t::OK }; // Final conversion state.
    usize cchWritten{ 0u };  // Text characters or binary bytes stored.
    usize cchRequired{ 0u }; // Complete output size required.
    usize cchConsumed{ 0u }; // Source characters accepted by decode operations.
};

/*
================
Conversion Result Contract

- Text destinations include space for a trailing null terminator.
- A null destination is valid only with zero capacity for count-only measurement.
- cchRequired excludes the text terminator.
- HexToBinary reports byte counts in cchWritten/cchRequired and input characters
  in cchConsumed.
================
*/

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_U64(
    u64 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_I64(
    i64 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_U32(
    u32 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_I32(
    i32 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_F64(
    f64 value,
    const string_float_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_F32(
    f32 value,
    const string_float_format_t &format,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_Bool(
    bool_t value,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_BinaryToHex(
    const void *pData,
    usize cbData,
    bool_t bUppercase,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_convert_result_t StringConvert_HexToBinary(
    string_view_t text,
    void *pDest,
    usize cbDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGCONVERT_H
