//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUMonitoring.h
//  Purpose: Declares caller-owned, delta-based CPU utilization sampling.
//  Details: Host counters are cumulative. A monitor retains the previous sample
//           so each new query can report utilization over a measured interval.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_CPUMONITORING_H
#define CYPHER_COMMON_TIER0_CPUMONITORING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon CPU Monitoring

CPU monitoring sample declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Timer.h"

namespace cypher::common
{

struct cy_cpu_monitor_t {
    u64 nPreviousIdleTicks;          // Previous cumulative host-idle counter.
    u64 nPreviousTotalTicks;         // Previous cumulative host-total counter.
    u64 nPreviousProcessTicks;       // Previous process user+kernel counter.
    timer_tick_t nPreviousWallTicks; // Monotonic time of the previous sample.
    bool_t hasSystemBaseline;        // System delta can be calculated next time.
    bool_t hasProcessBaseline;       // Process delta can be calculated next time.
    bool_t isInitialized;            // At least one host counter was available.
};

struct cy_cpu_monitor_sample_t {
    f32 totalUsagePercent;   // Whole-machine busy time in [0, 100].
    f32 processUsagePercent; // Process load normalized across logical CPUs.
    f64 intervalSeconds;     // Monotonic interval covered by this sample.
    u32 nLogicalThreadCount; // Normalization divisor used for process load.
    // A query can succeed before the host counters advance. Read a percentage
    // only when its corresponding validity flag is true.
    bool_t hasSystemUsage;  // totalUsagePercent contains a measured delta.
    bool_t hasProcessUsage; // processUsagePercent contains a measured delta.
};

// Captures the baseline for a caller-owned CPU monitor.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_CPUMonitorInit(
    cy_cpu_monitor_t *pMonitor ) noexcept;

// Replaces a monitor's baseline with the current counters.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_CPUMonitorReset(
    cy_cpu_monitor_t *pMonitor ) noexcept;

// Queries host counters, samples usage since the previous baseline, and advances
// that baseline. A true return means at least one host counter was queried; the
// per-value validity flags report whether a measurable delta was available.
// Process usage is normalized so 100 percent means all logical CPUs are busy.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_CPUMonitorSample(
    cy_cpu_monitor_t *pMonitor,
    cy_cpu_monitor_sample_t *pOutSample ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUMONITORING_H
