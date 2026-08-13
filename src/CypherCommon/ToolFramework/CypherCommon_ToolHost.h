//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolHost.h
//  Purpose: Declares the host callback boundary used by reusable tool libraries.
//  Details: CLI, Mason, Qt applications, CI reporters, and tests provide different
//           callbacks while invoking identical compiler and validator code.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLHOST_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLHOST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCancellation.h"
#include "CypherCommon_ToolArtifact.h"
#include "CypherCommon_ToolDependency.h"
#include "CypherCommon_ToolDiagnostic.h"
#include "CypherCommon_ToolEvent.h"
#include "CypherCommon_ToolProgress.h"
#include "CypherCommon_ToolReport.h"

namespace cypher::common
{

using tool_diagnostic_callback_t = void ( * )(
    const tool_diagnostic_t &diagnostic,
    void *pUserData ) noexcept;

using tool_progress_callback_t = void ( * )(
    const tool_progress_t &progress,
    void *pUserData ) noexcept;

using tool_event_callback_t = void ( * )(
    const tool_event_t &event,
    void *pUserData ) noexcept;

using tool_dependency_callback_t = void ( * )(
    const tool_dependency_t &dependency,
    void *pUserData ) noexcept;

using tool_artifact_callback_t = void ( * )(
    const tool_artifact_t &artifact,
    void *pUserData ) noexcept;

using tool_report_callback_t = void ( * )(
    const tool_report_t &report,
    void *pUserData ) noexcept;

// Writes command-owned text such as generated shell completion definitions.
// Structured compiler records should continue to use their dedicated callbacks.
using tool_text_callback_t = bool_t ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

enum tool_host_flags_t : flags32_t {
    TOOL_HOST_FLAG_NONE = 0u,
    TOOL_HOST_FLAG_THREAD_SAFE_CALLBACKS = CYPHER_BIT32( 0 )
};

struct tool_host_t {
    tool_diagnostic_callback_t pfnDiagnostic{ nullptr };
    tool_progress_callback_t pfnProgress{ nullptr };
    tool_event_callback_t pfnEvent{ nullptr };
    tool_dependency_callback_t pfnDependency{ nullptr };
    tool_artifact_callback_t pfnArtifact{ nullptr };
    tool_report_callback_t pfnReport{ nullptr };
    tool_text_callback_t pfnText{ nullptr };
    tool_cancellation_t cancellation{};
    void *pUserData{ nullptr };
    flags32_t flags{ TOOL_HOST_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolHost_Validate( const tool_host_t &host ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitDiagnostic(
    const tool_host_t *pHost,
    const tool_diagnostic_t &diagnostic ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitProgress(
    const tool_host_t *pHost,
    const tool_progress_t &progress ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitEvent(
    const tool_host_t *pHost,
    const tool_event_t &event ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitDependency(
    const tool_host_t *pHost,
    const tool_dependency_t &dependency ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitArtifact(
    const tool_host_t *pHost,
    const tool_artifact_t &artifact ) noexcept;

CYPHER_COMMON_API void ToolHost_EmitReport(
    const tool_host_t *pHost,
    const tool_report_t &report ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolHost_WriteText(
    const tool_host_t *pHost,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolHost_IsCancellationRequested(
    const tool_host_t *pHost ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLHOST_H
