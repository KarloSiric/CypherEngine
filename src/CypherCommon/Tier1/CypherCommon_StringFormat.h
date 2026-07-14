//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringFormat.h
//  Purpose: Declares CypherCommon Tier1 StringFormat support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGFORMAT_H
#define CYPHER_COMMON_TIER1_STRINGFORMAT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String Format

Safe formatted string helpers and readable number formatting.
================
*/

#include "CypherCommon_Tier0.h"

#include <cstdarg>

namespace cypher::common
{

/*
================
Printf-Style Formatting
================
*/
// Writes formatted text into pDest and always terminates when cchDest > 0.
i32 Cy_snprintf( char *pDest, usize cchDest, const char *pFormat, ... );

// va_list version of Cy_snprintf.
i32 Cy_vsnprintf( char *pDest, usize cchDest, const char *pFormat, std::va_list args );

// Safe sprintf-style wrapper with destination capacity.
i32 Cy_sprintf_safe( char *pDest, usize cchDest, const char *pFormat, ... );

// va_list version of Cy_sprintf_safe.
i32 Cy_vsprintf_safe( char *pDest, usize cchDest, const char *pFormat, std::va_list args );

// Appends formatted text to pDest and always terminates when cchDest > 0.
i32 Cy_sprintfcat_safe( char *pDest, usize cchDest, const char *pFormat, ... );

// va_list version of Cy_sprintfcat_safe.
i32 Cy_vsprintfcat_safe( char *pDest, usize cchDest, const char *pFormat, std::va_list args );

/*
================
Readable Formatting
================
*/
// Formats an integer with readable separators.
usize Cy_pretifynum( i64 nValue, char *pDest, usize cchDest );

// Formats a byte count as a readable memory string.
usize Cy_pretifymem( u64 cbMemory, char *pDest, usize cchDest );

// Normalizes a floating-point string in place.
void Cy_normalizefloatstring( char *pString );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGFORMAT_H
