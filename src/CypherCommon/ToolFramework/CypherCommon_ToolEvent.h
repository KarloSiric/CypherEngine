//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolEvent.h
//  Purpose: Declares structured lifecycle events for tool operations.
//  Details: Lifecycle events complement diagnostics and progress with stable
//           operation boundaries suitable for logs, timelines, and automation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLEVENT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLEVENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class tool_event_kind_t : u8 {
    OPERATION_BEGIN = 0u,
    OPERATION_END,
    PHASE_BEGIN,
    PHASE_END,
    MESSAGE
};

struct tool_event_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_operation_id_t parentOperationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_sequence_t sequence{ CY_TOOL_INVALID_SEQUENCE };
    tool_event_kind_t kind{ tool_event_kind_t::MESSAGE };
    tool_status_t status{ tool_status_t::OK };
    timer_tick_t timestamp{ 0u };
    string_view_t name{};
    string_view_t message{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolEvent_Validate( const tool_event_t &event ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolEvent_KindName( tool_event_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLEVENT_H
