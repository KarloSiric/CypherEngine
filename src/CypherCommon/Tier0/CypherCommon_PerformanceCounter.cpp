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

performance_counter_t PerformanceCounter_Now()
{
    return static_cast<performance_counter_t>( Timer_NowTicks() );
}

u64 PerformanceCounter_Frequency()
{
    return static_cast<u64>( Timer_GetFrequency() );
}

f64 PerformanceCounter_ToSeconds( performance_counter_t ticks )
{
    return Timer_TicksToSeconds( static_cast<timer_tick_t>( ticks ) );
}

} // namespace cypher::common
