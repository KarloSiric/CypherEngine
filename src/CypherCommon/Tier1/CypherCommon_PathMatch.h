//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PathMatch.h
//  Purpose: Declares allocation-free path filter matching.
//  Details: Path matching adds separator and include/exclude-list policy above the
//           generic wildcard matcher for VFS enumeration and editor asset filters.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PATHMATCH_H
#define CYPHER_COMMON_TIER1_PATHMATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringMatch.h"

namespace cypher::common
{

enum path_match_flags_t : flags32_t {
    PATH_MATCH_FLAG_NONE                    = 0u,
    PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII  = CYPHER_BIT32( 0 ),
    PATH_MATCH_FLAG_NORMALIZE_SEPARATORS    = CYPHER_BIT32( 1 ),
    PATH_MATCH_FLAG_STAR_CROSSES_SEPARATOR  = CYPHER_BIT32( 2 ),
    PATH_MATCH_FLAG_BASENAME_ONLY           = CYPHER_BIT32( 3 )
};

constexpr flags32_t PATH_MATCH_VALID_FLAGS =
    PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
    PATH_MATCH_FLAG_NORMALIZE_SEPARATORS |
    PATH_MATCH_FLAG_STAR_CROSSES_SEPARATOR |
    PATH_MATCH_FLAG_BASENAME_ONLY;

struct path_filter_t {
    const string_view_t *pIncludes{ nullptr };
    usize nIncludeCount{ 0u };
    const string_view_t *pExcludes{ nullptr };
    usize nExcludeCount{ 0u };
    flags32_t flags{ PATH_MATCH_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PathMatch_Wildcard(
    string_view_t path,
    string_view_t pattern,
    flags32_t flags ) noexcept;

// Path patterns support the generic matcher's '?', '*', and ASCII classes.

// Includes default to true when none are supplied; any matching exclusion wins.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PathMatch_Filter(
    string_view_t path,
    const path_filter_t &filter ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PATHMATCH_H
