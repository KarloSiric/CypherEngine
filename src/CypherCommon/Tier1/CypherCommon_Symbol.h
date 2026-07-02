//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Symbol.h
//  Purpose: Declares CypherCommon Tier1 Symbol support.
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

#ifndef CYPHER_COMMON_TIER1_SYMBOL_H
#define CYPHER_COMMON_TIER1_SYMBOL_H
#pragma once

/*
================
CypherCommon Symbol

String symbol table declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using symbol_id_t = u32;

constexpr symbol_id_t CY_SYMBOL_INVALID = CY_U32_MAX;

struct symbol_table_t;

symbol_id_t SymbolTable_Add( symbol_table_t *pTable, const char *pString );
symbol_id_t SymbolTable_Find( const symbol_table_t *pTable, const char *pString );
const char *SymbolTable_GetString( const symbol_table_t *pTable, symbol_id_t symbol );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SYMBOL_H
