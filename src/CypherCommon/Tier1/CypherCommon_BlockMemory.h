//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BlockMemory.h
//  Purpose: Declares CypherCommon Tier1 BlockMemory support.
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

#ifndef CYPHER_COMMON_TIER1_BLOCKMEMORY_H
#define CYPHER_COMMON_TIER1_BLOCKMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Block Memory

Block-based memory declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct block_memory_t;

bool_t BlockMemory_Init( block_memory_t *pMemory, usize cbBlockSize, usize block_count );
void BlockMemory_Shutdown( block_memory_t *pMemory );
void *BlockMemory_AllocBlock( block_memory_t *pMemory );
void BlockMemory_FreeBlock( block_memory_t *pMemory, void *pBlock );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BLOCKMEMORY_H
