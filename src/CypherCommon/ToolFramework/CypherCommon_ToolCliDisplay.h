//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliDisplay.h
//  Purpose: Declares the standard terminal presentation for Cypher tools.
//  Details: Display adapts structured tool callbacks into text or JSON records,
//           including startup banners, diagnostics, progress, artifacts, and final
//           reports. It owns presentation state but no compiler behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIDISPLAY_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIDISPLAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"
#include "CypherCommon_ToolCliTerminal.h"
#include "CypherCommon_ToolHost.h"

namespace cypher::common
{

struct tool_cli_display_t {
    tool_cli_terminal_t *pOutput{ nullptr };
    tool_cli_terminal_t *pError{ nullptr };
    tool_output_policy_t policy{};
    tool_operation_id_t progressOperationId{ CY_TOOL_INVALID_OPERATION_ID };
    usize cchProgressLine{ 0u };
    bool_t bProgressVisible{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliDisplay_Init(
    tool_cli_display_t *pDisplay,
    tool_cli_terminal_t *pOutput,
    tool_cli_terminal_t *pError,
    const tool_output_policy_t &policy ) noexcept;

CYPHER_COMMON_API void ToolCliDisplay_Shutdown(
    tool_cli_display_t *pDisplay ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCliDisplay_UsesColor(
    const tool_cli_display_t *pDisplay ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliDisplay_WriteBanner(
    tool_cli_display_t *pDisplay,
    string_view_t banner ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliDisplay_WriteStartup(
    tool_cli_display_t *pDisplay,
    const tool_application_desc_t &application,
    string_view_t version,
    string_view_t summary = {} ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliDisplay_WriteText(
    tool_cli_display_t *pDisplay,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_host_t ToolCliDisplay_MakeHost(
    tool_cli_display_t *pDisplay,
    tool_cancellation_t cancellation = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIDISPLAY_H
