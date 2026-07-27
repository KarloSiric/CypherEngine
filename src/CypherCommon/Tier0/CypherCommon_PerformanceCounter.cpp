//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PerformanceCounter.cpp
//  Purpose: Implements CypherCommon Tier0 performance counter helpers.
//  Details: This is a thin compatibility layer over the Tier0 timer for systems
//           that want counter/frequency terminology.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PerformanceCounter.h"

#include "CypherCommon_Timer.h"

namespace cypher::common
{

performance_counter_t Cy_PerformanceCounterNow() noexcept
{
    return static_cast<performance_counter_t>( Cy_TimerNowTicks() );
}

u64 Cy_PerformanceCounterFrequency() noexcept
{
    return static_cast<u64>( Cy_TimerGetFrequency() );
}

f64 Cy_PerformanceCounterToSeconds(
    performance_counter_t nTicks ) noexcept
{
    return Cy_TimerTicksToSeconds( static_cast<timer_tick_t>( nTicks ) );
}

} // namespace cypher::common
