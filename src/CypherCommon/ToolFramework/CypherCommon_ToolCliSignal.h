//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliSignal.h
//  Purpose: Declares process-interrupt cancellation for command-line tools.
//  Details: One installed signal scope translates Ctrl+C into an atomic request.
//           Signal handlers do no allocation, logging, locking, or compiler work;
//           normal execution observes the request cooperatively through ToolHost.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLISIGNAL_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLISIGNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCancellation.h"
#include "CypherCommon_ToolStatus.h"

namespace cypher::common
{

using tool_cli_interrupt_handler_t = void ( * )( int );

struct tool_cli_signal_t {
    atomic_bool_t requested{ CY_FALSE };
    tool_cli_interrupt_handler_t pPreviousInterruptHandler{ nullptr };
    bool_t bInstalled{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliSignal_Install( tool_cli_signal_t *pSignal ) noexcept;

CYPHER_COMMON_API void ToolCliSignal_Uninstall(
    tool_cli_signal_t *pSignal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCliSignal_IsRequested(
    const tool_cli_signal_t *pSignal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_cancellation_t ToolCliSignal_AsCancellation(
    const tool_cli_signal_t *pSignal ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLISIGNAL_H
