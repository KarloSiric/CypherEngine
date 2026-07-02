//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryPool.h
//  Purpose: Declares CypherCommon Tier1 MemoryPool support.
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

#ifndef CYPHER_COMMON_TIER1_MEMORYPOOL_H
#define CYPHER_COMMON_TIER1_MEMORYPOOL_H
#pragma once

/*
================
CypherCommon Memory Pool

Fixed-size object pool declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct memory_pool_t;

bool_t MemoryPool_Init( memory_pool_t *pPool, void *pMemory, usize cbMemory, usize cbElement );
void *MemoryPool_Alloc( memory_pool_t *pPool );
void MemoryPool_Free( memory_pool_t *pPool, void *pElement );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYPOOL_H
