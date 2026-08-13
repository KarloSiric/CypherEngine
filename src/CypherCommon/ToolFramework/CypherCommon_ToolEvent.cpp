//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolEvent.cpp
//  Purpose: Implements validation and names for tool lifecycle events.
//  Details: Event strings are borrowed for synchronous delivery and operation IDs
//           permit consumers to reconstruct nested timelines independently.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolEvent.h"

namespace cypher::common
{

tool_status_t ToolEvent_Validate( const tool_event_t &event ) noexcept
{
    if ( event.operationId == CY_TOOL_INVALID_OPERATION_ID ||
         event.parentOperationId == event.operationId ||
         event.sequence == CY_TOOL_INVALID_SEQUENCE ||
         event.kind > tool_event_kind_t::MESSAGE ||
         !ToolStatus_IsKnown( event.status ) ||
         !StringView_IsValid( event.name ) ||
         event.name.cchLength == 0u ||
         !StringView_IsValid( event.message ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const bool_t bEnd = event.kind == tool_event_kind_t::OPERATION_END ||
                        event.kind == tool_event_kind_t::PHASE_END;
    if ( !bEnd && ToolStatus_Failed( event.status ) ) {
        return tool_status_t::INVALID_STATE;
    }
    return tool_status_t::OK;
}

const char *ToolEvent_KindName( tool_event_kind_t kind ) noexcept
{
    switch ( kind ) {
        case tool_event_kind_t::OPERATION_BEGIN: return "operation-begin";
        case tool_event_kind_t::OPERATION_END: return "operation-end";
        case tool_event_kind_t::PHASE_BEGIN: return "phase-begin";
        case tool_event_kind_t::PHASE_END: return "phase-end";
        case tool_event_kind_t::MESSAGE: return "message";
    }
    return "unknown";
}

} // namespace cypher::common
