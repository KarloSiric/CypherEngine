//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringMatch.h
//  Purpose: Declares CypherCommon Tier1 StringMatch support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGMATCH_H
#define CYPHER_COMMON_TIER1_STRINGMATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String Match

String matching declarations for wildcard search, console auto-complete,
asset filters and editor find tools.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum string_match_flags_t : flags32_t {
    STRING_MATCH_FLAG_NONE = 0u,
    STRING_MATCH_FLAG_CASE_INSENSITIVE = CYPHER_BIT32( 0 ),
    STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL = CYPHER_BIT32( 1 )
};

bool_t Cy_strmatch( const char *pString, const char *pPattern, flags32_t flags );
bool_t Cy_strwildcardmatch( const char *pString, const char *pPattern, flags32_t flags );
bool_t Cy_strcontainsmatch( const char *pString, const char *pSearch, flags32_t flags );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGMATCH_H
