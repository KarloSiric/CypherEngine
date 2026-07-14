//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStack.h
//  Purpose: Declares CypherCommon Tier1 MemoryStack support.
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

#ifndef CYPHER_COMMON_TIER1_MEMORYSTACK_H
#define CYPHER_COMMON_TIER1_MEMORYSTACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Memory Stack

Linear stack allocator declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct memory_stack_t;

bool_t MemoryStack_Init( memory_stack_t *pStack, void *pMemory, usize cbMemory );
void *MemoryStack_Alloc( memory_stack_t *pStack, usize cbSize, usize alignment );
void MemoryStack_Reset( memory_stack_t *pStack );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYSTACK_H
