//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringHtml.h
//  Purpose: Declares bounded HTML entity and plain-text helpers.
//  Details: This is a small text utility for diagnostics and tools. It is not an
//           HTML parser, sanitizer, DOM, browser, or security boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGHTML_H
#define CYPHER_COMMON_TIER1_STRINGHTML_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum html_text_flags_t : flags32_t {
    HTML_TEXT_FLAG_NONE                  = 0u,                // Default named-entity behavior.
    HTML_TEXT_FLAG_ENCODE_QUOTES         = CYPHER_BIT32( 0 ), // Escape single and double quotes.
    HTML_TEXT_FLAG_DECODE_NUMERIC        = CYPHER_BIT32( 1 ), // Accept decimal and hexadecimal entities.
    HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS  = CYPHER_BIT32( 2 ), // Convert simple block tags to newlines.
    HTML_TEXT_FLAG_COLLAPSE_WHITESPACE   = CYPHER_BIT32( 3 )  // Fold ASCII whitespace runs to one space.
};

constexpr flags32_t HTML_TEXT_VALID_FLAGS =
    HTML_TEXT_FLAG_ENCODE_QUOTES |
    HTML_TEXT_FLAG_DECODE_NUMERIC |
    HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS |
    HTML_TEXT_FLAG_COLLAPSE_WHITESPACE;

enum class html_text_status_t : u8 {
    OK = 0u,           // Complete source was converted.
    INVALID_ARGUMENT,  // Input, destination, or policy is invalid.
    INVALID_ENTITY,    // Named or numeric entity is malformed.
    INVALID_CODE_POINT, // Entity resolves outside valid Unicode scalar range.
    UNTERMINATED_TAG,  // Sanitizer reached source end inside markup.
    OUTPUT_TRUNCATED   // Destination contains only a valid prefix.
};

struct html_text_result_t {
    html_text_status_t status{ html_text_status_t::OK }; // Final conversion state.
    usize cchConsumed{ 0u };  // Input bytes accepted before termination.
    usize cchWritten{ 0u };   // Output characters physically stored.
    usize cchRequired{ 0u };  // Complete output size excluding NUL.
    usize iError{ CY_STRING_VIEW_NPOS }; // Input byte responsible for failure.
};

CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect HTML encoding truncation." )
CYPHER_COMMON_API html_text_result_t StringHtml_EncodeEntities(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect HTML decoding truncation." )
CYPHER_COMMON_API html_text_result_t StringHtml_DecodeEntities(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

// Removes syntactically simple tags. PRESERVE_LINE_BREAKS emits line breaks for
// common block tags and <br>; COLLAPSE_WHITESPACE folds ASCII whitespace runs.
// Do not use this function as a sanitizer.
CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect HTML stripping truncation." )
CYPHER_COMMON_API html_text_result_t StringHtml_StripTags(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGHTML_H
