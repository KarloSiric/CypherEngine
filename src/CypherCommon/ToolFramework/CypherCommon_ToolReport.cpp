//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolReport.cpp
//  Purpose: Implements consistency checks and timing queries for tool reports.
//  Details: Counter equations are checked before reports are consumed by CI,
//           editors, or performance history where malformed data is misleading.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolReport.h"

namespace cypher::common
{

tool_status_t ToolReport_Validate( const tool_report_t &report ) noexcept
{
    if ( report.operationId == CY_TOOL_INVALID_OPERATION_ID ||
         !ToolStatus_IsKnown( report.status ) ||
         report.nEndTicks < report.nStartTicks ||
         report.nInputsProcessed > report.nInputsDiscovered ||
         report.nSucceeded > report.nInputsProcessed ||
         report.nFailed > report.nInputsProcessed ||
         report.nSkipped > report.nInputsDiscovered ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    if ( report.nSucceeded > CY_U64_MAX - report.nFailed ||
         report.nSucceeded + report.nFailed > CY_U64_MAX - report.nSkipped ||
         report.nSucceeded + report.nFailed + report.nSkipped >
             report.nInputsDiscovered ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( ToolStatus_Succeeded( report.status ) &&
         ( report.nFailed != 0u || report.nErrors != 0u ) ) {
        return tool_status_t::INVALID_STATE;
    }
    return tool_status_t::OK;
}

timer_tick_t ToolReport_ElapsedTicks( const tool_report_t &report ) noexcept
{
    return Cy_TimerElapsedTicks( report.nStartTicks, report.nEndTicks );
}

f64 ToolReport_ElapsedSeconds( const tool_report_t &report ) noexcept
{
    return Cy_TimerTicksToSeconds( ToolReport_ElapsedTicks( report ) );
}

} // namespace cypher::common
