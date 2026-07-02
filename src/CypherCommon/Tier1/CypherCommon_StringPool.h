//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPool.h
//  Purpose: Declares CypherCommon Tier1 StringPool support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGPOOL_H
#define CYPHER_COMMON_TIER1_STRINGPOOL_H
#pragma once

/*
================
CypherCommon String Pool

Interned string pool declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct string_pool_t;

bool_t StringPool_Init( string_pool_t *pPool, void *pMemory, usize cbMemory );
void StringPool_Shutdown( string_pool_t *pPool );
const char *StringPool_Intern( string_pool_t *pPool, const char *pString );
bool_t StringPool_Contains( const string_pool_t *pPool, const char *pString );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPOOL_H
