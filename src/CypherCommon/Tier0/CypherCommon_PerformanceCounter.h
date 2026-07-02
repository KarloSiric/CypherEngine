//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PerformanceCounter.h
//  Purpose: Declares CypherCommon Tier0 PerformanceCounter support.
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

#ifndef CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
#define CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
#pragma once

/*
================
CypherCommon Performance Counter

High-resolution counter declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using performance_counter_t = u64;

performance_counter_t PerformanceCounter_Now();
u64 PerformanceCounter_Frequency();
f64 PerformanceCounter_ToSeconds( performance_counter_t ticks );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PERFORMANCECOUNTER_H
