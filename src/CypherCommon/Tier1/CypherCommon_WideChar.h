//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_WideChar.h
//  Purpose: Declares platform-wide-character text helpers.
//  Details: Public wide-string manipulation belongs to the text layer; Tier0
//           platform services keep only private conversion code at OS boundaries.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_WIDECHAR_H
#define CYPHER_COMMON_TIER1_WIDECHAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Wide Char

Wide character declarations for platform boundaries.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using wchar_engine_t = wchar_t;

usize WChar_Length( const wchar_engine_t *pString );

i32 WChar_Compare( const wchar_engine_t *pStringA, const wchar_engine_t *pStringB );

usize WChar_Copy( wchar_engine_t *pDest, const wchar_engine_t *pSrc, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_WIDECHAR_H
