//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedMemory.h
//  Purpose: Declares CypherCommon Tier1 FixedMemory support.
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

#ifndef CYPHER_COMMON_TIER1_FIXEDMEMORY_H
#define CYPHER_COMMON_TIER1_FIXEDMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Fixed Memory

Fixed buffer memory declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct fixed_memory_t {
    void *pMemory;
    usize cbSize;
};

void FixedMemory_Init( fixed_memory_t *pMemory, void *pData, usize cbSize );
bool_t FixedMemory_Contains( const fixed_memory_t *pMemory, const void *pAddress );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDMEMORY_H
