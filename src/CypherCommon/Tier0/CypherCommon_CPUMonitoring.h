//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUMonitoring.h
//  Purpose: Declares CypherCommon Tier0 CPUMonitoring support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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
    u64 nPreviousIdleTicks;
    u64 nPreviousTotalTicks;
    u64 nPreviousProcessTicks;
    timer_tick_t nPreviousWallTicks;
    bool_t hasSystemBaseline;
    bool_t hasProcessBaseline;
    bool_t isInitialized;
};

struct cy_cpu_monitor_sample_t {
    f32 totalUsagePercent;
    f32 processUsagePercent;
    f64 intervalSeconds;
    u32 nLogicalThreadCount;
    // A query can succeed before the host counters advance. Read a percentage
    // only when its corresponding validity flag is true.
    bool_t hasSystemUsage;
    bool_t hasProcessUsage;
};

// Captures the baseline for a caller-owned CPU monitor.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_CPUMonitorInit(
    cy_cpu_monitor_t *pMonitor ) noexcept;

// Replaces a monitor's baseline with the current counters.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_CPUMonitorReset(
    cy_cpu_monitor_t *pMonitor ) noexcept;

// Queries host counters, samples usage since the previous baseline, and advances
// that baseline. A true return means at least one host counter was queried; the
// per-value validity flags report whether a measurable delta was available.
// Process usage is normalized so 100 percent means all logical CPUs are busy.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_CPUMonitorSample(
    cy_cpu_monitor_t *pMonitor,
    cy_cpu_monitor_sample_t *pOutSample ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUMONITORING_H
