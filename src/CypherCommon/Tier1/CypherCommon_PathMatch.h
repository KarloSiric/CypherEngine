//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PathMatch.h
//  Purpose: Declares CypherCommon Tier1 PathMatch support.
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

#ifndef CYPHER_COMMON_TIER1_PATHMATCH_H
#define CYPHER_COMMON_TIER1_PATHMATCH_H
#pragma once

/*
================
CypherCommon Path Match

Virtual-path matching declarations for VFS searches, pak manifests, tool
filters and asset dependency scans.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum path_match_flags_t : flags32_t {
    PATH_MATCH_FLAG_NONE = 0u,
    PATH_MATCH_FLAG_CASE_INSENSITIVE = CYPHER_BIT32( 0 ),
    PATH_MATCH_FLAG_ALLOW_RECURSIVE_WILDCARD = CYPHER_BIT32( 1 ),
    PATH_MATCH_FLAG_REQUIRE_EXTENSION = CYPHER_BIT32( 2 )
};

struct path_match_rule_t {
    const char *pPattern;
    const char *pRequiredExtension;
    flags32_t flags;
};

bool_t PathMatch_Matches( const char *pPath, const path_match_rule_t &rule );
bool_t PathMatch_Wildcard( const char *pPath, const char *pPattern, flags32_t flags );
bool_t PathMatch_Extension( const char *pPath, const char *pExtension, flags32_t flags );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PATHMATCH_H
