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

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct cpu_monitor_sample_t {
    f32 total_usage;
    f32 process_usage;
    u32 logical_thread_count;
};

bool_t CPUMonitoring_Sample( cpu_monitor_sample_t *pOutSample );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUMONITORING_H
