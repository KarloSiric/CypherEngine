//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringParse.h
//  Purpose: Declares CypherCommon Tier1 StringParse support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGPARSE_H
#define CYPHER_COMMON_TIER1_STRINGPARSE_H
#pragma once

/*
================
CypherCommon String Parse

Cursor-based text parsing declarations for command lines, config files,
materials, pak manifests and tool formats.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct string_parser_t {
    const char *pBegin;
    const char *pCursor;
    const char *pEnd;
    u32 line;
    u32 column;
};

void StringParse_Init( string_parser_t *pParser, const char *pText, usize cchText );
bool_t StringParse_AtEnd( const string_parser_t *pParser );
void StringParse_SkipWhitespace( string_parser_t *pParser );
bool_t StringParse_ReadToken( string_parser_t *pParser, char *pDest, usize cchDest );
bool_t StringParse_ReadQuoted( string_parser_t *pParser, char *pDest, usize cchDest );
bool_t StringParse_ReadI32( string_parser_t *pParser, i32 *pOutValue );
bool_t StringParse_ReadF32( string_parser_t *pParser, f32 *pOutValue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPARSE_H
