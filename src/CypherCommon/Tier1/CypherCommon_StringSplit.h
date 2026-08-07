//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringSplit.h
//  Purpose: Declares CypherCommon Tier1 StringSplit support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGSPLIT_H
#define CYPHER_COMMON_TIER1_STRINGSPLIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String Split

Split and token iteration declarations for command parsing, config files,
path lists and editor search tools.

Rules:
- Returned tokens are non-owning views into the source storage.
- Delimiters are excluded from emitted tokens.
- Empty fields are preserved unless STRING_SPLIT_FLAG_SKIP_EMPTY is set.
- Trimming occurs before the empty-field decision.
- Array-writing functions continue scanning to report the full required count.
- Visitor callbacks may stop iteration by returning CY_FALSE.
================
*/

#include "CypherCommon_Tier0.h"

#include "CypherCommon_StringView.h"
#include "CypherCommon_CharacterSet.h"

namespace cypher::common
{

enum string_split_flags_t : flags32_t {
    STRING_SPLIT_FLAG_NONE                  = 0u,
    STRING_SPLIT_FLAG_SKIP_EMPTY            = CYPHER_BIT32( 0 ),
    STRING_SPLIT_FLAG_TRIM_WHITESPACE       = CYPHER_BIT32( 1 )
};

struct string_split_result_t {
    usize cTokensWritten{ 0u };
    usize cTokensRequired{ 0u };
};

struct string_split_visit_result_t {
    usize cTokensVisited{ 0u };
    bool_t bCompleted{ CY_FALSE };
};

constexpr flags32_t STRING_SPLIT_VALID_FLAGS =
    STRING_SPLIT_FLAG_SKIP_EMPTY |
    STRING_SPLIT_FLAG_TRIM_WHITESPACE;

using string_split_callback_t = bool_t ( * )(
    string_view_t token,
    usize iToken,
    void *pUserData ) noexcept;

/*
================
Array Output
================
*/

// Splits source at every occurrence of chDelimiter.
// Passing nullptr with zero capacity performs a count-only query.
CYPHER_NODISCARD_MSG( "Inspect cTokensRequired to detect output truncation." )
CYPHER_COMMON_API string_split_result_t StringSplit_ByChar(
    string_view_t source,
    char chDelimiter,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept;

// Splits source whenever a byte belongs to pDelimiters.
// An empty delimiter set emits the entire source as one field.
CYPHER_NODISCARD_MSG( "Inspect cTokensRequired to detect output truncation." )
CYPHER_COMMON_API string_split_result_t StringSplit_BySet(
    string_view_t source,
    const character_set_t *pDelimiters,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept;

// Splits source at non-overlapping, left-to-right matches of delimiter.
// The delimiter must be a valid, non-empty view.
CYPHER_NODISCARD_MSG( "Inspect cTokensRequired to detect output truncation." )
CYPHER_COMMON_API string_split_result_t StringSplit_ByString(
    string_view_t source,
    string_view_t delimiter,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept;

/*
================
Visitor Output
================
*/

// Visits fields separated by chDelimiter until input ends or the callback stops.
CYPHER_NODISCARD_MSG( "Inspect bCompleted to detect callback cancellation." )
CYPHER_COMMON_API string_split_visit_result_t StringSplit_VisitByChar(
    string_view_t source,
    char chDelimiter,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept;

// Visits fields separated by any byte contained in pDelimiters.
CYPHER_NODISCARD_MSG( "Inspect bCompleted to detect callback cancellation." )
CYPHER_COMMON_API string_split_visit_result_t StringSplit_VisitBySet(
    string_view_t source,
    const character_set_t *pDelimiters,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept;

// Visits fields separated by non-overlapping, left-to-right delimiter matches.
CYPHER_NODISCARD_MSG( "Inspect bCompleted to detect callback cancellation." )
CYPHER_COMMON_API string_split_visit_result_t StringSplit_VisitByString(
    string_view_t source,
    string_view_t delimiter,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGSPLIT_H
