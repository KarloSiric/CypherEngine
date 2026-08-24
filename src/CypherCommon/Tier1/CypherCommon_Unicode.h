//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Unicode.h
//  Purpose: Declares strict bounded Unicode encoding primitives.
//  Details: Tier1 validates and transcodes UTF-8/16/32 without allocation. Unicode
//           normalization, collation, shaping, and locale policy require dedicated libraries.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_UNICODE_H
#define CYPHER_COMMON_TIER1_UNICODE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

using utf16_unit_t = u16;
using unicode_code_point_t = u32;

constexpr unicode_code_point_t CY_UNICODE_REPLACEMENT = 0x0000FFFDu; // Standard replacement scalar.
constexpr unicode_code_point_t CY_UNICODE_MAX = 0x0010FFFFu;         // Highest legal Unicode scalar.

enum unicode_flags_t : flags32_t {
    UNICODE_FLAG_NONE                 = 0u,                // Stop on malformed input and write data only.
    UNICODE_FLAG_REPLACE_INVALID      = CYPHER_BIT32( 0 ), // Substitute U+FFFD and continue transcoding.
    UNICODE_FLAG_REJECT_NUL           = CYPHER_BIT32( 1 ), // Treat embedded U+0000 as invalid input.
    UNICODE_FLAG_WRITE_TERMINATOR     = CYPHER_BIT32( 2 )  // Append a terminator when capacity permits.
};

enum class unicode_status_t : u8 {
    OK = 0u,           // Validation or transcoding completed successfully.
    INVALID_ARGUMENT, // Pointer, capacity, or source-view contract was invalid.
    INVALID_SEQUENCE, // Encoded units violate the selected Unicode encoding.
    INVALID_CODE_POINT,// Decoded value is not a legal Unicode scalar.
    TRUNCATED_SEQUENCE,// Input ended before the current encoded value completed.
    OUTPUT_TRUNCATED  // Destination received only a prefix of the required output.
};

struct unicode_result_t {
    unicode_status_t status{ unicode_status_t::OK }; // Final validation/transcode state.
    usize nInputConsumed{ 0u };  // Input units consumed; unit depends on source encoding.
    usize nOutputWritten{ 0u };  // Output units physically stored.
    usize nOutputRequired{ 0u }; // Complete output units, excluding optional terminator.
    usize iError{ CY_STRING_VIEW_NPOS }; // Input-unit index responsible for failure.
};

/*
================
Unicode Result Contract

- Output counts exclude an optional null terminator.
- UTF-16 counts are code units; UTF-32 counts are scalar values; UTF-8 counts bytes.
- WRITE_TERMINATOR requires one additional output unit and never counts it as data.
- A null output with zero capacity performs validation and required-size measurement.
================
*/

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Unicode_IsScalarValue( unicode_code_point_t codePoint ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Unicode_IsSurrogate( unicode_code_point_t codePoint ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_ValidateUtf8( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_DecodeUtf8(
    string_view_t text,
    unicode_code_point_t *pCodePointOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_EncodeUtf8(
    unicode_code_point_t codePoint,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_CountUtf8CodePoints( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_Utf8ToUtf16(
    string_view_t source,
    flags32_t flags,
    span_t<utf16_unit_t> dest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_Utf16ToUtf8(
    span_t<const utf16_unit_t> source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_Utf8ToUtf32(
    string_view_t source,
    flags32_t flags,
    span_t<unicode_code_point_t> dest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
unicode_result_t Unicode_Utf32ToUtf8(
    span_t<const unicode_code_point_t> source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNICODE_H
