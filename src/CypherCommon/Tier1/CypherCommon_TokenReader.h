//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_TokenReader.h
//  Purpose: Declares CypherCommon Tier1 TokenReader support.
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

#ifndef CYPHER_COMMON_TIER1_TOKENREADER_H
#define CYPHER_COMMON_TIER1_TOKENREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Token Reader

Token stream reader declarations for config, tools and command parsing.
================
*/

#include "CypherCommon_Tier0.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class token_type_t : u32 {
    End = 0u,
    Identifier,
    String,
    Number,
    Symbol
};

struct token_t {
    token_type_t type;
    string_view_t text;
    u32 line;
    u32 column;
};

struct token_reader_t;

void TokenReader_Init( token_reader_t *pReader, const char *pText );
bool_t TokenReader_Read( token_reader_t *pReader, token_t *pOutToken );
bool_t TokenReader_Peek( token_reader_t *pReader, token_t *pOutToken );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_TOKENREADER_H
