//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringMatch.h
//  Purpose: Declares bounded literal and wildcard string matching.
//  Details: Matching is allocation-free and exposes path and ASCII-case policies
//           explicitly for VFS, console, asset, and editor filters.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGMATCH_H
#define CYPHER_COMMON_TIER1_STRINGMATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum string_match_flags_t : flags32_t {
    STRING_MATCH_FLAG_NONE                    = 0u,
    STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII  = CYPHER_BIT32( 0 ),
    STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL   = CYPHER_BIT32( 1 ),
    STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR  = CYPHER_BIT32( 2 ),
    STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS   = CYPHER_BIT32( 3 )
};

constexpr flags32_t STRING_MATCH_VALID_FLAGS =
    STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
    STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL |
    STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR |
    STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringMatch_Equals(
    string_view_t text,
    string_view_t expected,
    flags32_t flags ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringMatch_StartsWith(
    string_view_t text,
    string_view_t prefix,
    flags32_t flags ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringMatch_EndsWith(
    string_view_t text,
    string_view_t suffix,
    flags32_t flags ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringMatch_Contains(
    string_view_t text,
    string_view_t search,
    flags32_t flags ) noexcept;

// Supports '*', '?', and optionally ASCII character classes such as '[a-z]'.
// Wildcards do not match path separators unless STAR_MATCHES_SEPARATOR is set.
// Malformed character classes fail the match instead of being treated as literals.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringMatch_Wildcard(
    string_view_t text,
    string_view_t pattern,
    flags32_t flags ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGMATCH_H
