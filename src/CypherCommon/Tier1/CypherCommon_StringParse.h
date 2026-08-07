//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringParse.h
//  Purpose: Declares deterministic bounded text-to-value conversion.
//  Details: StringParse converts one non-owning token into primitive values without
//           allocation, exceptions, locale state, or cursor ownership.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGPARSE_H
#define CYPHER_COMMON_TIER1_STRINGPARSE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class string_parse_status_t : u8 {
    OK = 0u,
    EMPTY_INPUT,
    INVALID_ARGUMENT,
    INVALID_BASE,
    INVALID_CHARACTER,
    NUMERIC_OVERFLOW,
    NUMERIC_UNDERFLOW,
    TRAILING_CHARACTERS,
    NON_FINITE_VALUE
};

enum string_parse_flags_t : flags32_t {
    STRING_PARSE_FLAG_NONE                       = 0u,
    STRING_PARSE_FLAG_TRIM_WHITESPACE            = CYPHER_BIT32( 0 ),
    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN            = CYPHER_BIT32( 1 ),
    // Base prefixes and digit separators apply to integer parsers.
    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX          = CYPHER_BIT32( 2 ),
    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR      = CYPHER_BIT32( 3 ),
    STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS  = CYPHER_BIT32( 4 ),
    // Boolean policy flags apply only to StringParse_Bool.
    STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL      = CYPHER_BIT32( 5 ),
    STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL         = CYPHER_BIT32( 6 ),
    // Non-finite values are rejected by floating-point parsers unless enabled.
    STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT     = CYPHER_BIT32( 7 )
};

constexpr flags32_t STRING_PARSE_VALID_FLAGS =
    STRING_PARSE_FLAG_TRIM_WHITESPACE |
    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR |
    STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS |
    STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL |
    STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL |
    STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT;

struct string_parse_options_t {
    // Integer base in [2, 36]. Zero selects prefix-aware automatic detection.
    u8 nBase{ 10u };
    flags32_t flags{ STRING_PARSE_FLAG_NONE };
};

struct string_parse_result_t {
    string_parse_status_t status{ string_parse_status_t::EMPTY_INPUT };
    usize cchConsumed{ 0u };
    usize iError{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringParse_Succeeded( string_parse_result_t result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *StringParse_StatusName( string_parse_status_t status ) noexcept;

// Parses an unsigned 64-bit integer. Output remains unchanged on failure.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_U64(
    string_view_t text,
    const string_parse_options_t &options,
    u64 *pValueOut ) noexcept;

// Parses a signed 64-bit integer. Output remains unchanged on failure.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_I64(
    string_view_t text,
    const string_parse_options_t &options,
    i64 *pValueOut ) noexcept;

// Parses and range-checks an unsigned 32-bit integer.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_U32(
    string_view_t text,
    const string_parse_options_t &options,
    u32 *pValueOut ) noexcept;

// Parses and range-checks a signed 32-bit integer.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_I32(
    string_view_t text,
    const string_parse_options_t &options,
    i32 *pValueOut ) noexcept;

// Parses a locale-independent 64-bit floating-point value.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_F64(
    string_view_t text,
    flags32_t flags,
    f64 *pValueOut ) noexcept;

// Parses a locale-independent 32-bit floating-point value directly as f32.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_F32(
    string_view_t text,
    flags32_t flags,
    f32 *pValueOut ) noexcept;

// Parses true/false and optionally case-insensitive or numeric Boolean forms.
CYPHER_NODISCARD CYPHER_COMMON_API
string_parse_result_t StringParse_Bool(
    string_view_t text,
    flags32_t flags,
    bool_t *pValueOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPARSE_H
