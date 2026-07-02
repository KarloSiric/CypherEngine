//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Lexer.h
//  Purpose: Declares CypherCommon Tier1 Lexer support.
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

#ifndef CYPHER_COMMON_TIER1_LEXER_H
#define CYPHER_COMMON_TIER1_LEXER_H
#pragma once

/*
================
CypherCommon Lexer

Configurable lexer declarations for tools and data formats.
================
*/

#include "CypherCommon_Tier0.h"
#include "CypherCommon_TokenReader.h"

namespace cypher::common
{

struct lexer_rules_t {
    bool_t allow_comments;
    bool_t allow_quoted_strings;
    bool_t case_sensitive;
};

struct lexer_t;

void Lexer_Init( lexer_t *pLexer, const char *pText, const lexer_rules_t *pRules );
bool_t Lexer_ReadToken( lexer_t *pLexer, token_t *pOutToken );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LEXER_H
