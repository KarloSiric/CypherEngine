//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringEscape.h
//  Purpose: Declares CypherCommon Tier1 StringEscape support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGESCAPE_H
#define CYPHER_COMMON_TIER1_STRINGESCAPE_H
#pragma once

/*
================
CypherCommon String Escape

Escaped text declarations for config files, command lines, tools, JSON-like
data and quoted editor strings.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum string_escape_flags_t : flags32_t {
    STRING_ESCAPE_FLAG_NONE = 0u,
    STRING_ESCAPE_FLAG_QUOTES = CYPHER_BIT32( 0 ),
    STRING_ESCAPE_FLAG_CONTROL_CHARS = CYPHER_BIT32( 1 ),
    STRING_ESCAPE_FLAG_PATH_SLASHES = CYPHER_BIT32( 2 )
};

usize Cy_strescape( const char *pString, char *pDest, usize cchDest, flags32_t flags );
usize Cy_strunescape( const char *pString, char *pDest, usize cchDest, flags32_t flags );
bool_t Cy_strneedsescape( const char *pString, flags32_t flags );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGESCAPE_H
