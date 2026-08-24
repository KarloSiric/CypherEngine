//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PerformanceCounter.h
//  Purpose: Declares the legacy high-resolution counter facade over Timer.
//  Details: Counter values are monotonic process-relative ticks. This API remains
//           small so old call sites do not depend on platform clock types.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
#define CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Performance Counter

High-resolution counter declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using performance_counter_t = u64; // Same units returned by Cy_TimerNowTicks.

CYPHER_NODISCARD CYPHER_COMMON_API performance_counter_t
Cy_PerformanceCounterNow() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API u64 Cy_PerformanceCounterFrequency() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API f64 Cy_PerformanceCounterToSeconds(
    performance_counter_t nTicks ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
