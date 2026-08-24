//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringEscape.h
//  Purpose: Declares bounded escaped-text encoding and decoding.
//  Details: The API supports strict C/CYKV-style and JSON-style escape rules while
//           preserving exact input and output accounting.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGESCAPE_H
#define CYPHER_COMMON_TIER1_STRINGESCAPE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class string_escape_style_t : u8 {
    CYPHER = 0u, // CYKV/Cypher authored-text escape rules.
    C,           // C/C++ string-literal escape rules.
    JSON         // Strict JSON string escape rules.
};

enum string_escape_flags_t : flags32_t {
    STRING_ESCAPE_FLAG_NONE = 0u, // Copy bytes where the style permits.
    STRING_ESCAPE_FLAG_QUOTES = CYPHER_BIT32( 0 ), // Escape quote delimiters.
    STRING_ESCAPE_FLAG_BACKSLASH = CYPHER_BIT32( 1 ), // Escape backslashes.
    STRING_ESCAPE_FLAG_CONTROL_CHARS = CYPHER_BIT32( 2 ), // Escape control bytes.
    STRING_ESCAPE_FLAG_NON_ASCII = CYPHER_BIT32( 3 ), // Emit Unicode escapes.
    STRING_ESCAPE_FLAG_PATH_SLASHES = CYPHER_BIT32( 4 ) // Escape forward slashes.
};

enum class string_escape_status_t : u8 {
    OK = 0u,          // Complete source was converted.
    INVALID_ARGUMENT, // View, destination, style, or flags are invalid.
    INVALID_ESCAPE,   // Escape spelling is malformed or unsupported.
    INVALID_CODE_POINT, // Unicode scalar is invalid for the selected style.
    OUTPUT_TRUNCATED  // Destination contains only a valid prefix.
};

struct string_escape_result_t {
    string_escape_status_t status{ string_escape_status_t::OK }; // Final operation state.
    usize cchConsumed{ 0u };  // Source bytes consumed before success or failure.
    usize cchWritten{ 0u };   // Destination characters physically stored.
    usize cchRequired{ 0u };  // Complete destination size excluding NUL.
    usize iError{ CY_STRING_VIEW_NPOS }; // Source byte responsible for failure.
};

// Output is always null terminated when pDest is non-null and cchDest is nonzero.
// cchRequired and cchWritten exclude that terminator. A null destination with zero
// capacity performs a complete validation and required-length measurement pass.

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringEscape_NeedsEscaping(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_escape_result_t StringEscape_Encode(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_escape_result_t StringEscape_Decode(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGESCAPE_H
