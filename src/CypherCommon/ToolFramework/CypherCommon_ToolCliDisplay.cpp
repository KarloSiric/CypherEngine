//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliDisplay.cpp
//  Purpose: Implements terminal rendering of structured tool records.
//  Details: Interactive progress redraws one bounded line. Redirected and JSON
//           output emit append-only records so logs remain complete and parseable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliDisplay.h"

#include "CypherCommon_StringBuilder.h"
#include "CypherCommon_ToolReportWriter.h"

namespace cypher::common
{
namespace
{

constexpr usize CY_TOOL_DISPLAY_LINE_CAPACITY = 4096u; // Bounded record scratch buffer.

constexpr string_view_t CY_TOOL_COLOR_RESET{ "\x1b[0m", 4u };
constexpr string_view_t CY_TOOL_COLOR_BOLD{ "\x1b[1m", 4u };
constexpr string_view_t CY_TOOL_COLOR_DIM{ "\x1b[2m", 4u };
constexpr string_view_t CY_TOOL_COLOR_CYAN{ "\x1b[36m", 5u };
constexpr string_view_t CY_TOOL_COLOR_GREEN{ "\x1b[32m", 5u };
constexpr string_view_t CY_TOOL_COLOR_YELLOW{ "\x1b[33m", 5u };
constexpr string_view_t CY_TOOL_COLOR_RED{ "\x1b[31m", 5u };
constexpr string_view_t CY_TOOL_COLOR_BOLD_RED{ "\x1b[1;31m", 7u };
constexpr u32 CY_TOOL_PROGRESS_BAR_WIDTH = 32u; // Stable width for logs and redraws.

bool_t DisplayIsValid( const tool_cli_display_t *pDisplay ) noexcept
{
    return pDisplay != nullptr && pDisplay->pOutput != nullptr &&
           pDisplay->pError != nullptr &&
           ToolStatus_Succeeded( ToolOutput_ValidatePolicy( pDisplay->policy ) );
}

bool_t DisplayUsesColor(
    const tool_cli_display_t *pDisplay,
    const tool_cli_terminal_t *pTerminal ) noexcept
{
    if ( pDisplay->policy.diagnosticsFormat != tool_output_format_t::TEXT ||
         ( pDisplay->policy.flags & TOOL_OUTPUT_FLAG_COLOR ) == 0u ) {
        return CY_FALSE;
    }
    // Machine-readable output never receives ANSI escapes. Forced color applies
    // only to text and permits colored output through a pipe when requested.
    return ( pDisplay->policy.flags & TOOL_OUTPUT_FLAG_FORCE_COLOR ) != 0u ||
           ToolCliTerminal_SupportsColor( pTerminal );
}

string_view_t DiagnosticColor(
    tool_diagnostic_severity_t severity ) noexcept
{
    switch ( severity ) {
        case tool_diagnostic_severity_t::NOTE: return CY_TOOL_COLOR_CYAN;
        case tool_diagnostic_severity_t::WARNING: return CY_TOOL_COLOR_YELLOW;
        case tool_diagnostic_severity_t::ERROR: return CY_TOOL_COLOR_RED;
        case tool_diagnostic_severity_t::FATAL: return CY_TOOL_COLOR_BOLD_RED;
    }
    return {};
}

string_view_t ProgressColor( tool_progress_state_t state ) noexcept
{
    switch ( state ) {
        case tool_progress_state_t::COMPLETE: return CY_TOOL_COLOR_GREEN;
        case tool_progress_state_t::FAILED: return CY_TOOL_COLOR_RED;
        case tool_progress_state_t::CANCELLED: return CY_TOOL_COLOR_YELLOW;
        case tool_progress_state_t::BEGIN:
        case tool_progress_state_t::UPDATE: return CY_TOOL_COLOR_CYAN;
    }
    return CY_TOOL_COLOR_CYAN;
}

void AppendProgressBar(
    string_builder_t *pBuilder,
    f64 fraction ) noexcept
{
    // Progress validation guarantees a bounded fraction; clamp defensively for
    // callbacks supplied by external tool modules.
    u32 nFilled = static_cast<u32>(
        fraction * static_cast<f64>( CY_TOOL_PROGRESS_BAR_WIDTH ) );
    if ( nFilled > CY_TOOL_PROGRESS_BAR_WIDTH ) {
        nFilled = CY_TOOL_PROGRESS_BAR_WIDTH;
    }
    (void)StringBuilder_AppendChar( pBuilder, '[' );
    for ( u32 iCell = 0u; iCell < CY_TOOL_PROGRESS_BAR_WIDTH; ++iCell ) {
        const bool_t bHead = nFilled != 0u &&
            nFilled < CY_TOOL_PROGRESS_BAR_WIDTH &&
            iCell == nFilled - 1u;
        (void)StringBuilder_AppendChar(
            pBuilder,
            bHead ? '>' : ( iCell < nFilled ? '=' : ' ' ) );
    }
    (void)StringBuilder_Append( pBuilder, { "] ", 2u } );
}

void AppendJsonEscaped(
    string_builder_t *pBuilder,
    string_view_t text ) noexcept
{
    // Emit one JSON object per line. Control bytes must be escaped so records
    // remain parseable by streaming consumers.
    for ( usize i = 0u; i < text.cchLength; ++i ) {
        const char ch = text.pData[i];
        switch ( ch ) {
            case '"': (void)StringBuilder_Append( pBuilder, { "\\\"", 2u } ); break;
            case '\\': (void)StringBuilder_Append( pBuilder, { "\\\\", 2u } ); break;
            case '\n': (void)StringBuilder_Append( pBuilder, { "\\n", 2u } ); break;
            case '\r': (void)StringBuilder_Append( pBuilder, { "\\r", 2u } ); break;
            case '\t': (void)StringBuilder_Append( pBuilder, { "\\t", 2u } ); break;
            default:
                if ( static_cast<unsigned char>( ch ) < 0x20u ) {
                    (void)StringBuilder_AppendFormat(
                        pBuilder,
                        "\\u%04x",
                        static_cast<unsigned int>(
                            static_cast<unsigned char>( ch ) ) );
                } else {
                    (void)StringBuilder_AppendChar( pBuilder, ch );
                }
                break;
        }
    }
}

tool_status_t WriteBuilder(
    tool_cli_display_t *pDisplay,
    tool_cli_terminal_t *pTerminal,
    const string_builder_t &builder ) noexcept
{
    if ( builder.status != string_builder_status_t::OK ) {
        return builder.status == string_builder_status_t::OUTPUT_TRUNCATED
            ? tool_status_t::CAPACITY_EXCEEDED
            : tool_status_t::INTERNAL_ERROR;
    }
    const tool_status_t status = ToolCliTerminal_Write(
        pTerminal,
        { builder.pData, builder.cchLength } );
    if ( ToolStatus_Succeeded( status ) &&
         ( pDisplay->policy.flags &
           TOOL_OUTPUT_FLAG_FLUSH_EACH_RECORD ) != 0u ) {
        return ToolCliTerminal_Flush( pTerminal );
    }
    return status;
}

void FinishInteractiveProgress( tool_cli_display_t *pDisplay ) noexcept
{
    if ( pDisplay->bProgressVisible ) {
        // Any non-progress record first terminates the transient redraw line.
        (void)ToolCliTerminal_Write( pDisplay->pOutput, { "\n", 1u } );
        pDisplay->bProgressVisible = CY_FALSE;
        pDisplay->cchProgressLine = 0u;
        pDisplay->progressOperationId = CY_TOOL_INVALID_OPERATION_ID;
    }
}

void DiagnosticCallback(
    const tool_diagnostic_t &diagnostic,
    void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    if ( !DisplayIsValid( pDisplay ) ) {
        return;
    }
    FinishInteractiveProgress( pDisplay );

    char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, buffer, CY_TOOL_DISPLAY_LINE_CAPACITY ) ) {
        return;
    }
    // JSON diagnostics are append-only records. Text diagnostics select stderr
    // for errors/fatals and stdout for notes/warnings.
    if ( pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        (void)StringBuilder_AppendFormat(
            &builder,
            "{\"type\":\"diagnostic\",\"severity\":\"%s\",\"category\":\"%s\",\"code\":%u,\"message\":\"",
            ToolDiagnostic_SeverityName( diagnostic.severity ),
            ToolDiagnostic_CategoryName( diagnostic.category ),
            diagnostic.code );
        AppendJsonEscaped( &builder, diagnostic.message );
        (void)StringBuilder_AppendChar( &builder, '"' );
        if ( ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE ) != 0u ) {
            (void)StringBuilder_Append( &builder, { ",\"path\":\"", 9u } );
            AppendJsonEscaped( &builder, diagnostic.source.path );
            (void)StringBuilder_AppendFormat(
                &builder,
                "\",\"line\":%u,\"column\":%u",
                diagnostic.source.nLine,
                diagnostic.source.nColumn );
        }
        if ( ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_HINT ) != 0u ) {
            (void)StringBuilder_Append( &builder, { ",\"hint\":\"", 9u } );
            AppendJsonEscaped( &builder, diagnostic.hint );
            (void)StringBuilder_AppendChar( &builder, '"' );
        }
        (void)StringBuilder_Append( &builder, { "}\n", 2u } );
    } else {
        tool_cli_terminal_t *pTerminal =
            diagnostic.severity >= tool_diagnostic_severity_t::ERROR
                ? pDisplay->pError
                : pDisplay->pOutput;
        const bool_t bColor = DisplayUsesColor( pDisplay, pTerminal );
        if ( ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE ) != 0u ) {
            if ( bColor ) {
                (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_DIM );
            }
            (void)StringBuilder_AppendFormat(
                &builder,
                "%.*s:%u:%u: ",
                static_cast<int>( diagnostic.source.path.cchLength ),
                diagnostic.source.path.pData,
                diagnostic.source.nLine,
                diagnostic.source.nColumn );
            if ( bColor ) {
                (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
            }
        }
        if ( bColor ) {
            (void)StringBuilder_Append(
                &builder,
                DiagnosticColor( diagnostic.severity ) );
        }
        (void)StringBuilder_AppendFormat(
            &builder,
            "%s[%u]: ",
            ToolDiagnostic_SeverityName( diagnostic.severity ),
            diagnostic.code );
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
        }
        (void)StringBuilder_Append( &builder, diagnostic.message );
        (void)StringBuilder_AppendChar( &builder, '\n' );
        if ( ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_HINT ) != 0u ) {
            (void)StringBuilder_Append( &builder, { "  hint: ", 8u } );
            (void)StringBuilder_Append( &builder, diagnostic.hint );
            (void)StringBuilder_AppendChar( &builder, '\n' );
        }
    }
    tool_cli_terminal_t *pTerminal =
        diagnostic.severity >= tool_diagnostic_severity_t::ERROR
            ? pDisplay->pError
            : pDisplay->pOutput;
    (void)WriteBuilder( pDisplay, pTerminal, builder );
}

void ProgressCallback(
    const tool_progress_t &progress,
    void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    if ( !DisplayIsValid( pDisplay ) ||
         pDisplay->policy.progressMode == tool_progress_mode_t::NONE ) {
        return;
    }
    // AUTO redraws an interactive terminal; plain and JSON modes append records
    // so redirected build logs retain every update.
    const bool_t bJson =
        pDisplay->policy.progressMode == tool_progress_mode_t::JSON;
    const bool_t bInteractive =
        !bJson &&
        pDisplay->policy.progressMode == tool_progress_mode_t::AUTO &&
        ToolCliTerminal_IsInteractive( pDisplay->pOutput );

    char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, buffer, CY_TOOL_DISPLAY_LINE_CAPACITY ) ) {
        return;
    }
    const f64 fraction = ToolProgress_Fraction( progress );
    if ( bJson ) {
        (void)StringBuilder_AppendFormat(
            &builder,
            "{\"type\":\"progress\",\"operation_id\":%llu,\"state\":\"%s\",\"completed\":%llu,\"total\":%llu,\"title\":\"",
            static_cast<unsigned long long>( progress.operationId ),
            ToolProgress_StateName( progress.state ),
            static_cast<unsigned long long>( progress.nCompleted ),
            static_cast<unsigned long long>( progress.nTotal ) );
        AppendJsonEscaped( &builder, progress.title );
        (void)StringBuilder_Append( &builder, { "\"}\n", 3u } );
    } else if ( fraction >= 0.0 ) {
        const bool_t bColor = DisplayUsesColor(
            pDisplay,
            pDisplay->pOutput );
        if ( bColor ) {
            (void)StringBuilder_Append(
                &builder,
                ProgressColor( progress.state ) );
        }
        AppendProgressBar( &builder, fraction );
        (void)StringBuilder_AppendFormat(
            &builder,
            "%3u%%  ",
            static_cast<unsigned int>( fraction * 100.0 ) );
        if ( progress.unit == tool_progress_unit_t::ITEMS ) {
            (void)StringBuilder_AppendFormat(
                &builder,
                "(%llu/%llu)  ",
                static_cast<unsigned long long>( progress.nCompleted ),
                static_cast<unsigned long long>( progress.nTotal ) );
        }
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
        }
        (void)StringBuilder_Append( &builder, progress.title );
        if ( progress.detail.cchLength != 0u ) {
            (void)StringBuilder_Append( &builder, { " - ", 3u } );
            (void)StringBuilder_Append( &builder, progress.detail );
        }
    } else {
        (void)StringBuilder_Append( &builder, progress.title );
        if ( progress.detail.cchLength != 0u ) {
            (void)StringBuilder_Append( &builder, { " - ", 3u } );
            (void)StringBuilder_Append( &builder, progress.detail );
        }
    }

    if ( bInteractive ) {
        // Erase and replace the current line without emitting log spam.
        (void)ToolCliTerminal_ClearCurrentLine( pDisplay->pOutput );
        (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
        pDisplay->bProgressVisible = CY_TRUE;
        pDisplay->cchProgressLine = builder.cchLength;
        pDisplay->progressOperationId = progress.operationId;
        if ( progress.state == tool_progress_state_t::COMPLETE ||
             progress.state == tool_progress_state_t::FAILED ||
             progress.state == tool_progress_state_t::CANCELLED ) {
            FinishInteractiveProgress( pDisplay );
        }
    } else {
        if ( !bJson ) {
            (void)StringBuilder_AppendChar( &builder, '\n' );
        }
        (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
    }
}

void EventCallback( const tool_event_t &event, void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    if ( !DisplayIsValid( pDisplay ) ||
         pDisplay->policy.verbosity == tool_verbosity_t::QUIET ||
         ( event.kind != tool_event_kind_t::MESSAGE &&
           pDisplay->policy.verbosity < tool_verbosity_t::VERBOSE ) ) {
        return;
    }
    // Events are durable records and cannot share the transient progress line.
    FinishInteractiveProgress( pDisplay );
    char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
    string_builder_t builder{};
    (void)StringBuilder_Init( &builder, buffer, CY_TOOL_DISPLAY_LINE_CAPACITY );
    if ( pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        (void)StringBuilder_AppendFormat(
            &builder,
            "{\"type\":\"event\",\"operation_id\":%llu,\"parent_operation_id\":%llu,\"sequence\":%llu,\"kind\":\"%s\",\"status\":\"%s\",\"name\":\"",
            static_cast<unsigned long long>( event.operationId ),
            static_cast<unsigned long long>( event.parentOperationId ),
            static_cast<unsigned long long>( event.sequence ),
            ToolEvent_KindName( event.kind ),
            ToolStatus_Name( event.status ) );
        AppendJsonEscaped( &builder, event.name );
        (void)StringBuilder_Append( &builder, { "\",\"message\":\"", 13u } );
        AppendJsonEscaped( &builder, event.message );
        (void)StringBuilder_Append( &builder, { "\"}\n", 3u } );
    } else if ( event.kind == tool_event_kind_t::MESSAGE ) {
        const bool_t bColor = DisplayUsesColor(
            pDisplay,
            pDisplay->pOutput );
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_CYAN );
        }
        (void)StringBuilder_Append( &builder, { "  ", 2u } );
        (void)StringBuilder_Append( &builder, event.name );
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
        }
        if ( event.message.cchLength != 0u ) {
            (void)StringBuilder_Append( &builder, { "\n      ", 7u } );
            (void)StringBuilder_Append( &builder, event.message );
        }
        (void)StringBuilder_AppendChar( &builder, '\n' );
    } else {
        (void)StringBuilder_AppendFormat(
            &builder,
            "[%s] ",
            ToolEvent_KindName( event.kind ) );
        (void)StringBuilder_Append( &builder, event.name );
        if ( event.message.cchLength != 0u ) {
            (void)StringBuilder_Append( &builder, { ": ", 2u } );
            (void)StringBuilder_Append( &builder, event.message );
        }
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
}

void DependencyCallback(
    const tool_dependency_t &dependency,
    void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    // Dependency lists are noisy and are reserved for trace output or JSON logs.
    if ( !DisplayIsValid( pDisplay ) ||
         pDisplay->policy.verbosity < tool_verbosity_t::TRACE ) {
        return;
    }
    FinishInteractiveProgress( pDisplay );
    char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
    string_builder_t builder{};
    (void)StringBuilder_Init( &builder, buffer, CY_TOOL_DISPLAY_LINE_CAPACITY );
    if ( pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        (void)StringBuilder_AppendFormat(
            &builder,
            "{\"type\":\"dependency\",\"kind\":\"%s\",\"flags\":%u,\"path\":\"",
            ToolDependency_KindName( dependency.kind ),
            dependency.flags );
        AppendJsonEscaped( &builder, dependency.path );
        (void)StringBuilder_Append( &builder, { "\"}\n", 3u } );
    } else {
        (void)StringBuilder_AppendFormat(
            &builder,
            "dependency[%s]: ",
            ToolDependency_KindName( dependency.kind ) );
        (void)StringBuilder_Append( &builder, dependency.path );
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
}

void ArtifactCallback(
    const tool_artifact_t &artifact,
    void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    if ( !DisplayIsValid( pDisplay ) ||
         pDisplay->policy.verbosity == tool_verbosity_t::QUIET ) {
        return;
    }
    FinishInteractiveProgress( pDisplay );
    char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
    string_builder_t builder{};
    (void)StringBuilder_Init( &builder, buffer, CY_TOOL_DISPLAY_LINE_CAPACITY );
    if ( pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        (void)StringBuilder_AppendFormat(
            &builder,
            "{\"type\":\"artifact\",\"kind\":\"%s\",\"flags\":%u,\"bytes\":%llu,\"path\":\"",
            ToolArtifact_KindName( artifact.kind ),
            artifact.flags,
            static_cast<unsigned long long>( artifact.cbSize ) );
        AppendJsonEscaped( &builder, artifact.path );
        (void)StringBuilder_Append( &builder, { "\",\"media_type\":\"", 16u } );
        AppendJsonEscaped( &builder, artifact.mediaType );
        (void)StringBuilder_Append( &builder, { "\"}\n", 3u } );
    } else {
        (void)StringBuilder_AppendFormat(
            &builder,
            "artifact[%s]: ",
            ToolArtifact_KindName( artifact.kind ) );
        (void)StringBuilder_Append( &builder, artifact.path );
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
}

void ReportCallback( const tool_report_t &report, void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    if ( !DisplayIsValid( pDisplay ) ||
         pDisplay->policy.verbosity == tool_verbosity_t::QUIET ) {
        return;
    }
    FinishInteractiveProgress( pDisplay );
    // Human text receives a compact summary. Structured formats delegate to the
    // report writer so CLI and file output share one schema.
    if ( pDisplay->policy.diagnosticsFormat == tool_output_format_t::TEXT ) {
        char buffer[CY_TOOL_DISPLAY_LINE_CAPACITY]{};
        string_builder_t builder{};
        if ( !StringBuilder_Init(
                 &builder,
                 buffer,
                 CY_TOOL_DISPLAY_LINE_CAPACITY ) ) {
            return;
        }
        const bool_t bColor = DisplayUsesColor(
            pDisplay,
            pDisplay->pOutput );
        const string_view_t statusColor = ToolStatus_Succeeded( report.status )
            ? CY_TOOL_COLOR_GREEN
            : CY_TOOL_COLOR_RED;
        const f64 elapsedMs = Cy_TimerTicksToMilliseconds(
            ToolReport_ElapsedTicks( report ) );
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_BOLD );
            (void)StringBuilder_Append( &builder, statusColor );
        }
        (void)StringBuilder_AppendFormat(
            &builder,
            "\n  RESULT  %s",
            ToolStatus_Name( report.status ) );
        if ( bColor ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
        }
        (void)StringBuilder_AppendFormat(
            &builder,
            "\n  Time         %.3f ms"
            "\n  Inputs       %llu processed  |  %llu succeeded  |  %llu failed  |  %llu skipped"
            "\n  Diagnostics  %llu warnings   |  %llu errors"
            "\n  Cache        %llu hits       |  %llu misses"
            "\n  Artifacts    %llu"
            "\n  I/O          %llu bytes read |  %llu bytes written\n",
            elapsedMs,
            static_cast<unsigned long long>( report.nInputsProcessed ),
            static_cast<unsigned long long>( report.nSucceeded ),
            static_cast<unsigned long long>( report.nFailed ),
            static_cast<unsigned long long>( report.nSkipped ),
            static_cast<unsigned long long>( report.nWarnings ),
            static_cast<unsigned long long>( report.nErrors ),
            static_cast<unsigned long long>( report.nCacheHits ),
            static_cast<unsigned long long>( report.nCacheMisses ),
            static_cast<unsigned long long>( report.nArtifacts ),
            static_cast<unsigned long long>( report.cbRead ),
            static_cast<unsigned long long>( report.cbWritten ) );
        (void)WriteBuilder( pDisplay, pDisplay->pOutput, builder );
        return;
    }

    const tool_report_write_options_t options{
        pDisplay->policy.diagnosticsFormat,
        CY_FALSE,
        CY_TRUE
    };
    (void)ToolReportWriter_WriteToSink(
        report,
        options,
        ToolCliTerminal_AsSink( pDisplay->pOutput ) );
    if ( ( pDisplay->policy.flags &
           TOOL_OUTPUT_FLAG_FLUSH_EACH_RECORD ) != 0u ) {
        (void)ToolCliTerminal_Flush( pDisplay->pOutput );
    }
}

bool_t TextCallback( string_view_t text, void *pUserData ) noexcept
{
    auto *pDisplay = static_cast<tool_cli_display_t *>( pUserData );
    return ToolStatus_Succeeded(
        ToolCliDisplay_WriteText( pDisplay, text ) );
}

} // namespace

tool_status_t ToolCliDisplay_Init(
    tool_cli_display_t *pDisplay,
    tool_cli_terminal_t *pOutput,
    tool_cli_terminal_t *pError,
    const tool_output_policy_t &policy ) noexcept
{
    const tool_status_t status = ToolOutput_ValidatePolicy( policy );
    if ( pDisplay == nullptr || pOutput == nullptr || pError == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    *pDisplay = {};
    pDisplay->pOutput = pOutput;
    pDisplay->pError = pError;
    pDisplay->policy = policy;
    return tool_status_t::OK;
}

void ToolCliDisplay_Shutdown( tool_cli_display_t *pDisplay ) noexcept
{
    if ( pDisplay != nullptr ) {
        FinishInteractiveProgress( pDisplay );
        *pDisplay = {};
    }
}

bool_t ToolCliDisplay_UsesColor(
    const tool_cli_display_t *pDisplay ) noexcept
{
    return DisplayIsValid( pDisplay ) &&
           DisplayUsesColor( pDisplay, pDisplay->pOutput );
}

tool_status_t ToolCliDisplay_WriteBanner(
    tool_cli_display_t *pDisplay,
    string_view_t banner ) noexcept
{
    if ( !DisplayIsValid( pDisplay ) || !StringView_IsValid( banner ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( banner.cchLength == 0u ||
         pDisplay->policy.verbosity == tool_verbosity_t::QUIET ||
         pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        return tool_status_t::OK;
    }

    FinishInteractiveProgress( pDisplay );
    const bool_t bColor = DisplayUsesColor( pDisplay, pDisplay->pOutput );
    if ( bColor ) {
        const tool_status_t status = ToolCliTerminal_Write(
            pDisplay->pOutput,
            CY_TOOL_COLOR_CYAN );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
    }
    tool_status_t status = ToolCliTerminal_Write( pDisplay->pOutput, banner );
    if ( ToolStatus_Succeeded( status ) && bColor ) {
        status = ToolCliTerminal_Write(
            pDisplay->pOutput,
            CY_TOOL_COLOR_RESET );
    }
    return status;
}

tool_status_t ToolCliDisplay_WriteStartup(
    tool_cli_display_t *pDisplay,
    const tool_application_desc_t &application,
    string_view_t version,
    string_view_t summary ) noexcept
{
    if ( !DisplayIsValid( pDisplay ) ||
         ToolStatus_Failed( ToolApplication_CheckDescriptor( application ) ) ||
         !StringView_IsValid( version ) || !StringView_IsValid( summary ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( pDisplay->policy.verbosity == tool_verbosity_t::QUIET ||
         pDisplay->policy.diagnosticsFormat == tool_output_format_t::JSON ) {
        return tool_status_t::OK;
    }
    char buffer[512]{};
    string_builder_t builder{};
    (void)StringBuilder_Init( &builder, buffer, sizeof( buffer ) );
    if ( DisplayUsesColor( pDisplay, pDisplay->pOutput ) ) {
        (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_BOLD );
        (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_CYAN );
    }
    (void)StringBuilder_Append( &builder, application.displayName );
    if ( version.cchLength != 0u ) {
        (void)StringBuilder_AppendChar( &builder, ' ' );
        (void)StringBuilder_Append( &builder, version );
    }
    if ( DisplayUsesColor( pDisplay, pDisplay->pOutput ) ) {
        (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
    }
    (void)StringBuilder_AppendChar( &builder, '\n' );
    if ( summary.cchLength != 0u ) {
        if ( DisplayUsesColor( pDisplay, pDisplay->pOutput ) ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_DIM );
        }
        (void)StringBuilder_Append( &builder, summary );
        if ( DisplayUsesColor( pDisplay, pDisplay->pOutput ) ) {
            (void)StringBuilder_Append( &builder, CY_TOOL_COLOR_RESET );
        }
        (void)StringBuilder_Append( &builder, { "\n\n", 2u } );
    }
    return WriteBuilder( pDisplay, pDisplay->pOutput, builder );
}

tool_status_t ToolCliDisplay_WriteText(
    tool_cli_display_t *pDisplay,
    string_view_t text ) noexcept
{
    if ( !DisplayIsValid( pDisplay ) || !StringView_IsValid( text ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    FinishInteractiveProgress( pDisplay );
    const tool_status_t status =
        ToolCliTerminal_Write( pDisplay->pOutput, text );
    if ( ToolStatus_Succeeded( status ) &&
         ( pDisplay->policy.flags &
           TOOL_OUTPUT_FLAG_FLUSH_EACH_RECORD ) != 0u ) {
        return ToolCliTerminal_Flush( pDisplay->pOutput );
    }
    return status;
}

tool_host_t ToolCliDisplay_MakeHost(
    tool_cli_display_t *pDisplay,
    tool_cancellation_t cancellation ) noexcept
{
    if ( !DisplayIsValid( pDisplay ) ) {
        return {};
    }
    // The returned function table borrows pDisplay and must not outlive it.
    return {
        &DiagnosticCallback,
        &ProgressCallback,
        &EventCallback,
        &DependencyCallback,
        &ArtifactCallback,
        &ReportCallback,
        &TextCallback,
        cancellation,
        pDisplay,
        TOOL_HOST_FLAG_NONE
    };
}

} // namespace cypher::common
