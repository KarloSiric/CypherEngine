//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolReport.h
//  Purpose: Declares summary records produced by completed tool operations.
//  Details: Reports contain stable counters and timings that can be rendered as
//           terminal summaries, JSON, Mason build rows, or historical telemetry.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_Timer.h"

namespace cypher::common
{

struct tool_report_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_status_t status{ tool_status_t::OK };
    timer_tick_t nStartTicks{ 0u };
    timer_tick_t nEndTicks{ 0u };
    u64 nInputsDiscovered{ 0u };
    u64 nInputsProcessed{ 0u };
    u64 nSucceeded{ 0u };
    u64 nFailed{ 0u };
    u64 nSkipped{ 0u };
    u64 nCacheHits{ 0u };
    u64 nCacheMisses{ 0u };
    u64 nWarnings{ 0u };
    u64 nErrors{ 0u };
    u64 nArtifacts{ 0u };
    u64 cbRead{ 0u };
    u64 cbWritten{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolReport_Validate( const tool_report_t &report ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
timer_tick_t ToolReport_ElapsedTicks( const tool_report_t &report ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
f64 ToolReport_ElapsedSeconds( const tool_report_t &report ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORT_H
