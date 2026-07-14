//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CallQueue.h
//  Purpose: Declares CypherCommon Tier1 CallQueue support.
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

#ifndef CYPHER_COMMON_TIER1_CALLQUEUE_H
#define CYPHER_COMMON_TIER1_CALLQUEUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Call Queue

Deferred call queue declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using call_queue_proc_t = void ( * )( void *pUserData );

struct call_queue_t;

bool_t CallQueue_Push( call_queue_t *pQueue, call_queue_proc_t proc, void *pUserData );
void CallQueue_Drain( call_queue_t *pQueue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CALLQUEUE_H
