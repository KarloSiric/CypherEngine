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

/*
================
Tool Event Contract

These are stable tool-neutral contracts shared by CLI applications, future GUI hosts, tests, and
compiler modules. They must not depend on Qt or terminal state.
================
*/

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
    OPERATION_BEGIN = 0u, // Starts a top-level or nested operation.
    OPERATION_END,        // Completes the matching operation.
    PHASE_BEGIN,          // Starts one named phase within an operation.
    PHASE_END,            // Completes the matching phase.
    MESSAGE               // Informational event without lifetime semantics.
};

struct tool_event_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID }; // Record owner.
    tool_operation_id_t parentOperationId{ CY_TOOL_INVALID_OPERATION_ID }; // Optional parent.
    tool_sequence_t sequence{ CY_TOOL_INVALID_SEQUENCE }; // Producer ordering value.
    tool_event_kind_t kind{ tool_event_kind_t::MESSAGE };  // Lifecycle transition.
    tool_status_t status{ tool_status_t::OK };             // End-state status where applicable.
    timer_tick_t timestamp{ 0u };                          // Monotonic emission time.
    string_view_t name{};                                 // Stable operation/phase label.
    string_view_t message{};                              // Optional human detail.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolEvent_Validate( const tool_event_t &event ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolEvent_KindName( tool_event_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLEVENT_H
