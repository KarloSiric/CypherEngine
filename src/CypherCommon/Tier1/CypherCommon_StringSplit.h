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
    usize cTokensWritten;
    usize cTokensRequired;      
};

struct string_split_visit_result_t {
    usize cTokensVisited;
    bool_t bCompleted;
};

constexpr flags32_t STRING_SPLIT_VALID_FLAGS = STRING_SPLIT_FLAG_SKIP_EMPTY | STRING_SPLIT_FLAG_TRIM_WHITESPACE;

using string_split_callback_t = bool_t ( * )( string_view_t token, usize iToken, void *pUserData ) noexcept;




} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGSPLIT_H
