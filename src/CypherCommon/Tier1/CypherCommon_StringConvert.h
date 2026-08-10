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
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_BASE,
    INVALID_TEXT,
    OUTPUT_TRUNCATED
};

enum string_integer_format_flags_t : flags32_t {
    STRING_INTEGER_FORMAT_FLAG_NONE       = 0u,
    STRING_INTEGER_FORMAT_FLAG_UPPERCASE  = CYPHER_BIT32( 0 ),
    STRING_INTEGER_FORMAT_FLAG_PREFIX     = CYPHER_BIT32( 1 ),
    STRING_INTEGER_FORMAT_FLAG_PLUS_SIGN  = CYPHER_BIT32( 2 )
};

enum class string_float_style_t : u8 {
    GENERAL = 0u,
    FIXED,
    SCIENTIFIC
};

enum string_float_format_flags_t : flags32_t {
    STRING_FLOAT_FORMAT_FLAG_NONE                = 0u,
    STRING_FLOAT_FORMAT_FLAG_UPPERCASE           = CYPHER_BIT32( 0 ),
    STRING_FLOAT_FORMAT_FLAG_PLUS_SIGN           = CYPHER_BIT32( 1 ),
    STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO  = CYPHER_BIT32( 2 )
};

struct string_integer_format_t {
    u8 nBase{ 10u };
    u8 nMinDigits{ 1u };
    flags32_t flags{ STRING_INTEGER_FORMAT_FLAG_NONE };
};

struct string_float_format_t {
    string_float_style_t style{ string_float_style_t::GENERAL };
    u8 nPrecision{ 6u };
    flags32_t flags{ STRING_FLOAT_FORMAT_FLAG_NONE };
};

struct string_convert_result_t {
    string_convert_status_t status{ string_convert_status_t::OK };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
    usize cchConsumed{ 0u };
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
