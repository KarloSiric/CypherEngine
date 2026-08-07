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
    HTML_TEXT_FLAG_NONE                  = 0u,
    HTML_TEXT_FLAG_ENCODE_QUOTES         = CYPHER_BIT32( 0 ),
    HTML_TEXT_FLAG_DECODE_NUMERIC        = CYPHER_BIT32( 1 ),
    HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS  = CYPHER_BIT32( 2 ),
    HTML_TEXT_FLAG_COLLAPSE_WHITESPACE   = CYPHER_BIT32( 3 )
};

enum class html_text_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_ENTITY,
    INVALID_CODE_POINT,
    UNTERMINATED_TAG,
    OUTPUT_TRUNCATED
};

struct html_text_result_t {
    html_text_status_t status{ html_text_status_t::OK };
    usize cchConsumed{ 0u };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
    usize iError{ CY_STRING_VIEW_NPOS };
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

// Removes syntactically simple tags. Do not use this function as a sanitizer.
CYPHER_NODISCARD_MSG( "Inspect cchRequired to detect HTML stripping truncation." )
CYPHER_COMMON_API html_text_result_t StringHtml_StripTags(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGHTML_H
