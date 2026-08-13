//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliSignal.cpp
//  Purpose: Implements Ctrl+C translation into cooperative cancellation.
//  Details: Installation is process-global and intentionally permits one active
//           command-line signal scope. Nested or concurrent runners are rejected.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliSignal.h"

#include <csignal>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace cypher::common
{
namespace
{

atomic_ptr_t<tool_cli_signal_t> g_pActiveSignal{ nullptr };

void RequestCancellation() noexcept
{
    tool_cli_signal_t *pSignal = g_pActiveSignal.load(
        std::memory_order_relaxed );
    if ( pSignal != nullptr ) {
        pSignal->requested.store( CY_TRUE, std::memory_order_relaxed );
    }
}

#if CYPHER_PLATFORM_WINDOWS

BOOL WINAPI ConsoleControlHandler( DWORD nControlType ) noexcept
{
    if ( nControlType == CTRL_C_EVENT || nControlType == CTRL_BREAK_EVENT ) {
        RequestCancellation();
        return TRUE;
    }
    return FALSE;
}

#else

void InterruptHandler( int ) noexcept
{
    RequestCancellation();
}

#endif

} // namespace

tool_status_t ToolCliSignal_Install( tool_cli_signal_t *pSignal ) noexcept
{
    if ( pSignal == nullptr || pSignal->bInstalled ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    tool_cli_signal_t *pExpected = nullptr;
    if ( !g_pActiveSignal.compare_exchange_strong(
             pExpected,
             pSignal,
             std::memory_order_acq_rel,
             std::memory_order_acquire ) ) {
        return tool_status_t::ALREADY_EXISTS;
    }
    pSignal->requested.store( CY_FALSE, std::memory_order_relaxed );

#if CYPHER_PLATFORM_WINDOWS
    if ( !SetConsoleCtrlHandler( &ConsoleControlHandler, TRUE ) ) {
        g_pActiveSignal.store( nullptr, std::memory_order_release );
        return tool_status_t::OPERATION_FAILED;
    }
#else
    const tool_cli_interrupt_handler_t previous =
        std::signal( SIGINT, &InterruptHandler );
    if ( previous == SIG_ERR ) {
        g_pActiveSignal.store( nullptr, std::memory_order_release );
        return tool_status_t::OPERATION_FAILED;
    }
    pSignal->pPreviousInterruptHandler = previous;
#endif

    pSignal->bInstalled = CY_TRUE;
    return tool_status_t::OK;
}

void ToolCliSignal_Uninstall( tool_cli_signal_t *pSignal ) noexcept
{
    if ( pSignal == nullptr || !pSignal->bInstalled ) {
        return;
    }
#if CYPHER_PLATFORM_WINDOWS
    (void)SetConsoleCtrlHandler( &ConsoleControlHandler, FALSE );
#else
    (void)std::signal(
        SIGINT,
        pSignal->pPreviousInterruptHandler );
#endif
    tool_cli_signal_t *pExpected = pSignal;
    (void)g_pActiveSignal.compare_exchange_strong(
        pExpected,
        nullptr,
        std::memory_order_acq_rel,
        std::memory_order_acquire );
    pSignal->bInstalled = CY_FALSE;
    pSignal->pPreviousInterruptHandler = nullptr;
}

bool_t ToolCliSignal_IsRequested(
    const tool_cli_signal_t *pSignal ) noexcept
{
    return pSignal != nullptr &&
           pSignal->requested.load( std::memory_order_relaxed );
}

tool_cancellation_t ToolCliSignal_AsCancellation(
    const tool_cli_signal_t *pSignal ) noexcept
{
    return pSignal != nullptr
        ? tool_cancellation_t{ &pSignal->requested, nullptr, nullptr }
        : tool_cancellation_t{};
}

} // namespace cypher::common
