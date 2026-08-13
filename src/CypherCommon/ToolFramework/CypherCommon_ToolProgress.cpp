//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolProgress.cpp
//  Purpose: Implements hierarchical tool progress validation and queries.
//  Details: Progress permits unknown totals explicitly and never assumes that a
//           renderer can redraw terminal lines or create graphical widgets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolProgress.h"

namespace cypher::common
{

tool_status_t ToolProgress_Validate( const tool_progress_t &progress ) noexcept
{
    if ( progress.operationId == CY_TOOL_INVALID_OPERATION_ID ||
         progress.parentOperationId == progress.operationId ||
         progress.sequence == CY_TOOL_INVALID_SEQUENCE ||
         progress.state > tool_progress_state_t::CANCELLED ||
         progress.unit > tool_progress_unit_t::STEPS ||
         !ToolStatus_IsKnown( progress.status ) ||
         !StringView_IsValid( progress.title ) ||
         progress.title.cchLength == 0u ||
         !StringView_IsValid( progress.detail ) ||
         ( progress.flags & ~TOOL_PROGRESS_FLAG_INDETERMINATE ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const bool_t bIndeterminate =
        ( progress.flags & TOOL_PROGRESS_FLAG_INDETERMINATE ) != 0u;
    if ( bIndeterminate ) {
        if ( progress.nTotal != 0u ) {
            return tool_status_t::INVALID_ARGUMENT;
        }
    } else if ( progress.nTotal == 0u || progress.nCompleted > progress.nTotal ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    if ( progress.state == tool_progress_state_t::COMPLETE &&
         ( ToolStatus_Failed( progress.status ) ||
           ( !bIndeterminate && progress.nCompleted != progress.nTotal ) ) ) {
        return tool_status_t::INVALID_STATE;
    }
    if ( progress.state == tool_progress_state_t::FAILED &&
         ToolStatus_Succeeded( progress.status ) ) {
        return tool_status_t::INVALID_STATE;
    }
    if ( progress.state == tool_progress_state_t::CANCELLED &&
         progress.status != tool_status_t::CANCELLED ) {
        return tool_status_t::INVALID_STATE;
    }

    return tool_status_t::OK;
}

f64 ToolProgress_Fraction( const tool_progress_t &progress ) noexcept
{
    if ( ( progress.flags & TOOL_PROGRESS_FLAG_INDETERMINATE ) != 0u ||
         progress.nTotal == 0u ) {
        return -1.0;
    }
    return static_cast<f64>( progress.nCompleted ) /
           static_cast<f64>( progress.nTotal );
}

const char *ToolProgress_StateName( tool_progress_state_t state ) noexcept
{
    switch ( state ) {
        case tool_progress_state_t::BEGIN: return "begin";
        case tool_progress_state_t::UPDATE: return "update";
        case tool_progress_state_t::COMPLETE: return "complete";
        case tool_progress_state_t::FAILED: return "failed";
        case tool_progress_state_t::CANCELLED: return "cancelled";
    }
    return "unknown";
}

const char *ToolProgress_UnitName( tool_progress_unit_t unit ) noexcept
{
    switch ( unit ) {
        case tool_progress_unit_t::NONE: return "none";
        case tool_progress_unit_t::ITEMS: return "items";
        case tool_progress_unit_t::BYTES: return "bytes";
        case tool_progress_unit_t::STEPS: return "steps";
    }
    return "unknown";
}

} // namespace cypher::common
