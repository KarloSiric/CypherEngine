//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolHost.cpp
//  Purpose: Implements validated synchronous delivery to tool hosts.
//  Details: Missing callbacks are valid. Producers retain ownership and hosts must
//           copy borrowed text before returning if they need persistent records.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Host Implementation Notes

A tool run owns one invocation, host callback set, cancellation state, and final report.
Tool-specific code borrows that context only for the duration of execution.
================
*/

#include "CypherCommon_ToolHost.h"

namespace cypher::common
{

tool_status_t ToolHost_Validate( const tool_host_t &host ) noexcept
{
    // Callback thread safety is an explicit host promise; no other bits exist yet.
    if ( ( host.flags & ~TOOL_HOST_FLAG_THREAD_SAFE_CALLBACKS ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    // Opaque callback state without a callback is almost certainly stale setup.
    if ( host.cancellation.pfnQuery == nullptr &&
         host.cancellation.pUserData != nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    return tool_status_t::OK;
}

void ToolHost_EmitDiagnostic(
    const tool_host_t *pHost,
    const tool_diagnostic_t &diagnostic ) noexcept
{
    // Delivery is synchronous. Invalid records are dropped at this boundary so
    // every frontend callback can assume the public contract already holds.
    if ( pHost != nullptr && pHost->pfnDiagnostic != nullptr &&
         ToolStatus_Succeeded( ToolDiagnostic_Validate( diagnostic ) ) ) {
        pHost->pfnDiagnostic( diagnostic, pHost->pUserData );
    }
}

void ToolHost_EmitProgress(
    const tool_host_t *pHost,
    const tool_progress_t &progress ) noexcept
{
    // Record storage remains owned by the producer for the duration of this call.
    if ( pHost != nullptr && pHost->pfnProgress != nullptr &&
         ToolStatus_Succeeded( ToolProgress_Validate( progress ) ) ) {
        pHost->pfnProgress( progress, pHost->pUserData );
    }
}

void ToolHost_EmitEvent(
    const tool_host_t *pHost,
    const tool_event_t &event ) noexcept
{
    if ( pHost != nullptr && pHost->pfnEvent != nullptr &&
         ToolStatus_Succeeded( ToolEvent_Validate( event ) ) ) {
        pHost->pfnEvent( event, pHost->pUserData );
    }
}

void ToolHost_EmitDependency(
    const tool_host_t *pHost,
    const tool_dependency_t &dependency ) noexcept
{
    if ( pHost != nullptr && pHost->pfnDependency != nullptr &&
         ToolStatus_Succeeded( ToolDependency_Validate( dependency ) ) ) {
        pHost->pfnDependency( dependency, pHost->pUserData );
    }
}

void ToolHost_EmitArtifact(
    const tool_host_t *pHost,
    const tool_artifact_t &artifact ) noexcept
{
    if ( pHost != nullptr && pHost->pfnArtifact != nullptr &&
         ToolStatus_Succeeded( ToolArtifact_Validate( artifact ) ) ) {
        pHost->pfnArtifact( artifact, pHost->pUserData );
    }
}

void ToolHost_EmitReport(
    const tool_host_t *pHost,
    const tool_report_t &report ) noexcept
{
    if ( pHost != nullptr && pHost->pfnReport != nullptr &&
         ToolStatus_Succeeded( ToolReport_Validate( report ) ) ) {
        pHost->pfnReport( report, pHost->pUserData );
    }
}

tool_status_t ToolHost_WriteText(
    const tool_host_t *pHost,
    string_view_t text ) noexcept
{
    if ( pHost == nullptr || pHost->pfnText == nullptr ||
         !StringView_IsValid( text ) ) {
        return tool_status_t::UNSUPPORTED;
    }
    // Avoid turning an empty record into an observable sink write or flush.
    if ( text.cchLength == 0u ) {
        return tool_status_t::OK;
    }
    return pHost->pfnText( text, pHost->pUserData )
        ? tool_status_t::OK
        : tool_status_t::IO_ERROR;
}

bool_t ToolHost_IsCancellationRequested( const tool_host_t *pHost ) noexcept
{
    return pHost != nullptr
        ? ToolCancellation_IsRequested( &pHost->cancellation )
        : CY_FALSE;
}

} // namespace cypher::common
