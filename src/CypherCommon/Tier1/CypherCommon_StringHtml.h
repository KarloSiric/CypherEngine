//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringHtml.h
//  Purpose: Declares CypherCommon Tier1 StringHtml support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGHTML_H
#define CYPHER_COMMON_TIER1_STRINGHTML_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String HTML

Small HTML entity helpers for tools, logs, launcher/server browser UI and docs.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

// Encodes basic HTML entities such as <, >, &, quotes.
usize Cy_HtmlEntityEncode( const char *pString, char *pDest, usize cchDest );

// Decodes basic HTML entities into text.
usize Cy_HtmlEntityDecode( const char *pString, char *pDest, usize cchDest );

// Strips HTML tags while preserving visible text.
usize Cy_StripHtml( const char *pString, char *pDest, usize cchDest );

// Strips HTML tags while preserving supported formatting markers.
usize Cy_StripAndPreserveHtml( const char *pString, char *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGHTML_H
