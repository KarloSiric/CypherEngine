//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliTerminal.cpp
//  Purpose: Implements portable terminal detection and writes.
//  Details: POSIX writes use file descriptors. Windows enables virtual-terminal
//           processing when available and restores the original console mode at
//           shutdown. Redirected output never receives terminal control codes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliTerminal.h"

#include "CypherCommon_Environment.h"

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <cerrno>
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace cypher::common
{
namespace
{

inline constexpr flags32_t CY_TOOL_TERMINAL_INTERNAL_INITIALIZED =
    CYPHER_BIT32( 0 );
inline constexpr flags32_t CY_TOOL_TERMINAL_INTERNAL_MODE_CHANGED =
    CYPHER_BIT32( 1 );
inline constexpr u32 CY_TOOL_TERMINAL_DEFAULT_COLUMNS = 80u;

bool_t TerminalIsValid( const tool_cli_terminal_t *pTerminal ) noexcept
{
    return pTerminal != nullptr &&
           ( pTerminal->internalFlags &
             CY_TOOL_TERMINAL_INTERNAL_INITIALIZED ) != 0u &&
           pTerminal->stream <= tool_cli_stream_t::STANDARD_ERROR;
}

bool_t EnvironmentDisablesColor() noexcept
{
    return Cy_EnvironmentHas( "NO_COLOR" );
}

bool_t TerminalSinkWrite( string_view_t text, void *pUserData ) noexcept
{
    return ToolStatus_Succeeded( ToolCliTerminal_Write(
        static_cast<tool_cli_terminal_t *>( pUserData ),
        text ) );
}

#if CYPHER_PLATFORM_WINDOWS

HANDLE NativeHandle( tool_cli_stream_t stream ) noexcept
{
    return GetStdHandle(
        stream == tool_cli_stream_t::STANDARD_OUTPUT
            ? STD_OUTPUT_HANDLE
            : STD_ERROR_HANDLE );
}

#else

int NativeDescriptor( tool_cli_stream_t stream ) noexcept
{
    return stream == tool_cli_stream_t::STANDARD_OUTPUT
        ? STDOUT_FILENO
        : STDERR_FILENO;
}

#endif

} // namespace

tool_status_t ToolCliTerminal_Init(
    tool_cli_terminal_t *pTerminal,
    tool_cli_stream_t stream ) noexcept
{
    if ( pTerminal == nullptr || stream > tool_cli_stream_t::STANDARD_ERROR ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *pTerminal = {};
    pTerminal->stream = stream;
    pTerminal->nColumns = CY_TOOL_TERMINAL_DEFAULT_COLUMNS;

#if CYPHER_PLATFORM_WINDOWS
    const HANDLE handle = NativeHandle( stream );
    if ( handle == nullptr || handle == INVALID_HANDLE_VALUE ) {
        return tool_status_t::IO_ERROR;
    }
    pTerminal->nNativeHandle = reinterpret_cast<uintptr>( handle );
    DWORD mode = 0u;
    if ( GetConsoleMode( handle, &mode ) ) {
        pTerminal->flags |=
            TOOL_CLI_TERMINAL_FLAG_INTERACTIVE |
            TOOL_CLI_TERMINAL_FLAG_UNICODE;
        pTerminal->nOriginalMode = mode;

        const DWORD desiredMode = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if ( SetConsoleMode( handle, desiredMode ) ) {
            pTerminal->flags |=
                TOOL_CLI_TERMINAL_FLAG_CURSOR_CONTROL;
            pTerminal->internalFlags |=
                CY_TOOL_TERMINAL_INTERNAL_MODE_CHANGED;
            if ( !EnvironmentDisablesColor() ) {
                pTerminal->flags |= TOOL_CLI_TERMINAL_FLAG_COLOR;
            }
        }
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if ( GetConsoleScreenBufferInfo( handle, &info ) ) {
            const SHORT width = info.srWindow.Right - info.srWindow.Left + 1;
            if ( width > 0 ) {
                pTerminal->nColumns = static_cast<u32>( width );
            }
        }
    }
#else
    const int descriptor = NativeDescriptor( stream );
    pTerminal->nNativeHandle = static_cast<uintptr>( descriptor );
    if ( isatty( descriptor ) == 1 ) {
        pTerminal->flags |=
            TOOL_CLI_TERMINAL_FLAG_INTERACTIVE |
            TOOL_CLI_TERMINAL_FLAG_CURSOR_CONTROL |
            TOOL_CLI_TERMINAL_FLAG_UNICODE;
        if ( !EnvironmentDisablesColor() ) {
            pTerminal->flags |= TOOL_CLI_TERMINAL_FLAG_COLOR;
        }
        struct winsize dimensions{};
        if ( ioctl( descriptor, TIOCGWINSZ, &dimensions ) == 0 &&
             dimensions.ws_col != 0u ) {
            pTerminal->nColumns = dimensions.ws_col;
        }
    }
#endif

    pTerminal->internalFlags |= CY_TOOL_TERMINAL_INTERNAL_INITIALIZED;
    return tool_status_t::OK;
}

void ToolCliTerminal_Shutdown( tool_cli_terminal_t *pTerminal ) noexcept
{
    if ( !TerminalIsValid( pTerminal ) ) {
        return;
    }
#if CYPHER_PLATFORM_WINDOWS
    if ( ( pTerminal->internalFlags &
           CY_TOOL_TERMINAL_INTERNAL_MODE_CHANGED ) != 0u ) {
        (void)SetConsoleMode(
            reinterpret_cast<HANDLE>( pTerminal->nNativeHandle ),
            pTerminal->nOriginalMode );
    }
#endif
    *pTerminal = {};
}

bool_t ToolCliTerminal_IsInteractive(
    const tool_cli_terminal_t *pTerminal ) noexcept
{
    return TerminalIsValid( pTerminal ) &&
           ( pTerminal->flags & TOOL_CLI_TERMINAL_FLAG_INTERACTIVE ) != 0u;
}

bool_t ToolCliTerminal_SupportsColor(
    const tool_cli_terminal_t *pTerminal ) noexcept
{
    return TerminalIsValid( pTerminal ) &&
           ( pTerminal->flags & TOOL_CLI_TERMINAL_FLAG_COLOR ) != 0u;
}

u32 ToolCliTerminal_Columns( const tool_cli_terminal_t *pTerminal ) noexcept
{
    return TerminalIsValid( pTerminal )
        ? pTerminal->nColumns
        : CY_TOOL_TERMINAL_DEFAULT_COLUMNS;
}

tool_status_t ToolCliTerminal_Write(
    tool_cli_terminal_t *pTerminal,
    string_view_t text ) noexcept
{
    if ( !TerminalIsValid( pTerminal ) || !StringView_IsValid( text ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    usize cbWritten = 0u;
#if CYPHER_PLATFORM_WINDOWS
    HANDLE handle = reinterpret_cast<HANDLE>( pTerminal->nNativeHandle );
    while ( cbWritten < text.cchLength ) {
        const usize cbRemaining = text.cchLength - cbWritten;
        const DWORD cbChunk = static_cast<DWORD>(
            cbRemaining < static_cast<usize>( CY_U32_MAX )
                ? cbRemaining
                : static_cast<usize>( CY_U32_MAX ) );
        DWORD cbCurrent = 0u;
        if ( !WriteFile(
                 handle,
                 text.pData + cbWritten,
                 cbChunk,
                 &cbCurrent,
                 nullptr ) ||
             cbCurrent == 0u ) {
            return tool_status_t::IO_ERROR;
        }
        cbWritten += cbCurrent;
    }
#else
    const int descriptor = static_cast<int>( pTerminal->nNativeHandle );
    while ( cbWritten < text.cchLength ) {
        const ssize_t cbCurrent = write(
            descriptor,
            text.pData + cbWritten,
            text.cchLength - cbWritten );
        if ( cbCurrent < 0 ) {
            if ( errno == EINTR ) {
                continue;
            }
            return tool_status_t::IO_ERROR;
        }
        if ( cbCurrent == 0 ) {
            return tool_status_t::IO_ERROR;
        }
        cbWritten += static_cast<usize>( cbCurrent );
    }
#endif
    return tool_status_t::OK;
}

tool_status_t ToolCliTerminal_Flush(
    tool_cli_terminal_t *pTerminal ) noexcept
{
    if ( !TerminalIsValid( pTerminal ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
#if CYPHER_PLATFORM_WINDOWS
    return FlushFileBuffers(
               reinterpret_cast<HANDLE>( pTerminal->nNativeHandle ) ) ||
           GetLastError() == ERROR_INVALID_HANDLE
        ? tool_status_t::OK
        : tool_status_t::IO_ERROR;
#else
    return tool_status_t::OK;
#endif
}

tool_status_t ToolCliTerminal_ClearCurrentLine(
    tool_cli_terminal_t *pTerminal ) noexcept
{
    if ( !TerminalIsValid( pTerminal ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( ( pTerminal->flags &
           TOOL_CLI_TERMINAL_FLAG_CURSOR_CONTROL ) == 0u ) {
        return tool_status_t::OK;
    }
    return ToolCliTerminal_Write( pTerminal, { "\r\x1b[2K", 5u } );
}

tool_text_sink_t ToolCliTerminal_AsSink(
    tool_cli_terminal_t *pTerminal ) noexcept
{
    return TerminalIsValid( pTerminal )
        ? tool_text_sink_t{ &TerminalSinkWrite, pTerminal }
        : tool_text_sink_t{};
}

} // namespace cypher::common
