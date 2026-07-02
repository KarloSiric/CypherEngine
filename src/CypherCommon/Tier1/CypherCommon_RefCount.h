//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RefCount.h
//  Purpose: Declares CypherCommon Tier1 RefCount support.
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

#ifndef CYPHER_COMMON_TIER1_REFCOUNT_H
#define CYPHER_COMMON_TIER1_REFCOUNT_H
#pragma once

/*
================
CypherCommon Ref Count

Reference counting declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct ref_count_t {
    u32 count;
};

void RefCount_Init( ref_count_t *pRefCount, u32 initial_count );
u32 RefCount_AddRef( ref_count_t *pRefCount );
u32 RefCount_Release( ref_count_t *pRefCount );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_REFCOUNT_H
