//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolSession.cpp
//  Purpose: Implements thread-safe tool execution identity and state changes.
//  Details: State changes use acquire-release ordering so final status and work
//           published before completion are visible to observing host threads.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Session Implementation Notes

A tool run owns one invocation, host callback set, cancellation state, and final report.
Tool-specific code borrows that context only for the duration of execution.
================
*/

#include "CypherCommon_ToolSession.h"

namespace cypher::common
{
namespace
{

inline constexpr u32 CY_TOOL_SESSION_STATE_FINISHING =
    static_cast<u32>( tool_session_state_t::CANCELLED ) + 1u; // Internal publication state.

u64 NextSaturatingId( atomic_u64_t *pNext ) noexcept
{
    // Saturation preserves zero as the invalid sentinel instead of wrapping a
    // long-running process back onto an identifier that may still be visible.
    u64 current = Cy_AtomicLoad( pNext, CY_MEMORY_ORDER_RELAXED );
    while ( current != CY_U64_MAX ) {
        const u64 desired = current + 1u;
        if ( Cy_AtomicCompareExchangeWeak(
                 pNext,
                 &current,
                 desired,
                 CY_MEMORY_ORDER_RELAXED,
                 CY_MEMORY_ORDER_RELAXED ) ) {
            return current;
        }
    }
    return 0u;
}

} // namespace

void ToolSession_Init( tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ) {
        return;
    }
    // Publish READY last so observers cannot see partially reset counters.
    Cy_AtomicStore(
        &pSession->nNextOperationId,
        static_cast<u64>( 1u ),
        CY_MEMORY_ORDER_RELAXED );
    Cy_AtomicStore(
        &pSession->nNextSequence,
        static_cast<u64>( 1u ),
        CY_MEMORY_ORDER_RELAXED );
    Cy_AtomicStore(
        &pSession->status,
        static_cast<u32>( tool_status_t::OK ),
        CY_MEMORY_ORDER_RELAXED );
    Cy_AtomicStore(
        &pSession->state,
        static_cast<u32>( tool_session_state_t::READY ),
        CY_MEMORY_ORDER_RELEASE );
}

tool_status_t ToolSession_Begin( tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    // A session is single-use between Init calls; only READY may enter RUNNING.
    u32 expected = static_cast<u32>( tool_session_state_t::READY );
    return Cy_AtomicCompareExchange(
               &pSession->state,
               &expected,
               static_cast<u32>( tool_session_state_t::RUNNING ),
               CY_MEMORY_ORDER_ACQ_REL,
               CY_MEMORY_ORDER_ACQUIRE )
        ? tool_status_t::OK
        : tool_status_t::INVALID_STATE;
}

tool_operation_id_t ToolSession_NextOperationId(
    tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ||
         ToolSession_State( pSession ) != tool_session_state_t::RUNNING ) {
        return CY_TOOL_INVALID_OPERATION_ID;
    }
    return NextSaturatingId( &pSession->nNextOperationId );
}

tool_sequence_t ToolSession_NextSequence( tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ||
         ToolSession_State( pSession ) != tool_session_state_t::RUNNING ) {
        return CY_TOOL_INVALID_SEQUENCE;
    }
    return NextSaturatingId( &pSession->nNextSequence );
}

tool_status_t ToolSession_Finish(
    tool_session_t *pSession,
    tool_status_t status ) noexcept
{
    if ( pSession == nullptr || !ToolStatus_IsKnown( status ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Collapse detailed failure statuses into the public session state while
    // preserving the exact result in the separate status field.
    tool_session_state_t terminal = tool_session_state_t::FAILED;
    if ( status == tool_status_t::OK ) {
        terminal = tool_session_state_t::SUCCEEDED;
    } else if ( status == tool_status_t::CANCELLED ) {
        terminal = tool_session_state_t::CANCELLED;
    }

    // FINISHING reserves the right to publish terminal status. Only one thread
    // can win this transition and complete the session.
    u32 expected = static_cast<u32>( tool_session_state_t::RUNNING );
    if ( !Cy_AtomicCompareExchange(
             &pSession->state,
             &expected,
             CY_TOOL_SESSION_STATE_FINISHING,
             CY_MEMORY_ORDER_ACQ_REL,
             CY_MEMORY_ORDER_ACQUIRE ) ) {
        return tool_status_t::INVALID_STATE;
    }
    // Publish status before terminal state. An acquire read of terminal state
    // therefore observes the corresponding final status as well.
    Cy_AtomicStore(
        &pSession->status,
        static_cast<u32>( status ),
        CY_MEMORY_ORDER_RELEASE );
    Cy_AtomicStore(
        &pSession->state,
        static_cast<u32>( terminal ),
        CY_MEMORY_ORDER_RELEASE );
    return tool_status_t::OK;
}

tool_session_state_t ToolSession_State(
    const tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ) {
        return tool_session_state_t::FAILED;
    }
    const u32 value = Cy_AtomicLoad(
        &pSession->state,
        CY_MEMORY_ORDER_ACQUIRE );
    // FINISHING is private; callers continue to observe RUNNING until commit.
    if ( value == CY_TOOL_SESSION_STATE_FINISHING ) {
        return tool_session_state_t::RUNNING;
    }
    return value <= static_cast<u32>( tool_session_state_t::CANCELLED )
        ? static_cast<tool_session_state_t>( value )
        : tool_session_state_t::FAILED;
}

tool_status_t ToolSession_Status( const tool_session_t *pSession ) noexcept
{
    if ( pSession == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    const u32 value = Cy_AtomicLoad(
        &pSession->status,
        CY_MEMORY_ORDER_ACQUIRE );
    return value <= static_cast<u32>( tool_status_t::INTERNAL_ERROR )
        ? static_cast<tool_status_t>( value )
        : tool_status_t::INTERNAL_ERROR;
}

} // namespace cypher::common
