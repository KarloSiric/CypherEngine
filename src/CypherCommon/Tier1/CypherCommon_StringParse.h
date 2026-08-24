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
    OK = 0u,             // A value was parsed and committed to output.
    EMPTY_INPUT,         // No token remains after optional trimming.
    INVALID_ARGUMENT,    // Output, view, or flag contract is invalid.
    INVALID_BASE,        // Integer base lies outside [2, 36].
    INVALID_CHARACTER,   // A byte cannot participate in the requested value.
    NUMERIC_OVERFLOW,    // Magnitude exceeds the positive destination limit.
    NUMERIC_UNDERFLOW,   // Negative magnitude exceeds the signed lower limit.
    TRAILING_CHARACTERS, // A valid prefix was followed by disallowed bytes.
    NON_FINITE_VALUE     // NaN or infinity was rejected by policy.
};

enum string_parse_flags_t : flags32_t {
    STRING_PARSE_FLAG_NONE = 0u, // Strict whole-view parsing.
    STRING_PARSE_FLAG_TRIM_WHITESPACE = CYPHER_BIT32( 0 ), // Ignore outer ASCII whitespace.
    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN = CYPHER_BIT32( 1 ), // Accept leading '+'.
    // Base prefixes and digit separators apply to integer parsers.
    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX = CYPHER_BIT32( 2 ), // Accept 0x/0o/0b prefixes.
    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR = CYPHER_BIT32( 3 ), // Accept '_' between digits.
    STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS = CYPHER_BIT32( 4 ), // Parse a valid prefix.
    // Boolean policy flags apply only to StringParse_Bool.
    STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL = CYPHER_BIT32( 5 ), // Fold true/false case.
    STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL = CYPHER_BIT32( 6 ), // Accept zero/one boolean text.
    // Non-finite values are rejected by floating-point parsers unless enabled.
    STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT = CYPHER_BIT32( 7 ) // Accept NaN and infinity.
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
    u8 nBase{ 10u }; // Integer radix: zero for auto-detect, otherwise 2 through 36.
    flags32_t flags{ STRING_PARSE_FLAG_NONE }; // Combination of STRING_PARSE_FLAG_*.
};

struct string_parse_result_t {
    string_parse_status_t status{ string_parse_status_t::EMPTY_INPUT }; // Final parse state.
    usize cchConsumed{ 0u }; // Bytes accepted from the original, untrimmed view.
    usize iError{ 0u };      // Source byte index responsible for failure.
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
