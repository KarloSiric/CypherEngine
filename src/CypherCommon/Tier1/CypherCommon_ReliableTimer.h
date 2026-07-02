//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ReliableTimer.h
//  Purpose: Declares CypherCommon Tier1 ReliableTimer support.
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

#ifndef CYPHER_COMMON_TIER1_RELIABLETIMER_H
#define CYPHER_COMMON_TIER1_RELIABLETIMER_H
#pragma once

/*
================
CypherCommon Reliable Timer

Stable interval timer declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct reliable_timer_t {
    u64 start_ticks;
    u64 interval_ticks;
};

void ReliableTimer_Start( reliable_timer_t *pTimer, f64 interval_seconds );
bool_t ReliableTimer_HasElapsed( const reliable_timer_t *pTimer );
void ReliableTimer_Restart( reliable_timer_t *pTimer );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RELIABLETIMER_H
