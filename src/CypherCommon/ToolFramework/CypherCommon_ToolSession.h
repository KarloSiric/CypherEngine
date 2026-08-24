//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolSession.h
//  Purpose: Declares thread-safe identity and state for one tool execution.
//  Details: Sessions allocate operation and event sequence IDs for concurrent
//           producers while enforcing a single terminal execution state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Session Contract

A tool run owns one invocation, host callback set, cancellation state, and final report.
Tool-specific code borrows that context only for the duration of execution.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLSESSION_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLSESSION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_Atomic.h"

namespace cypher::common
{

enum class tool_session_state_t : u8 {
    READY = 0u, // Initialized and waiting for its single run.
    RUNNING,    // Producers may allocate operation and event identifiers.
    SUCCEEDED,  // Terminal state for a successful run.
    FAILED,     // Terminal state for any non-cancellation failure.
    CANCELLED   // Terminal state selected by cooperative cancellation.
};

struct tool_session_t {
    atomic_u64_t nNextOperationId{ 1u }; // Zero remains the invalid operation sentinel.
    atomic_u64_t nNextSequence{ 1u };    // Monotonic ordering key shared by all producers.
    atomic_u32_t state{ static_cast<u32>( tool_session_state_t::READY ) }; // Atomic state machine.
    atomic_u32_t status{ static_cast<u32>( tool_status_t::OK ) }; // Final tool_status_t value.
};

CYPHER_COMMON_API void ToolSession_Init( tool_session_t *pSession ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolSession_Begin( tool_session_t *pSession ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_operation_id_t ToolSession_NextOperationId(
    tool_session_t *pSession ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_sequence_t ToolSession_NextSequence( tool_session_t *pSession ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolSession_Finish(
    tool_session_t *pSession,
    tool_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_session_state_t ToolSession_State(
    const tool_session_t *pSession ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolSession_Status(
    const tool_session_t *pSession ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLSESSION_H
