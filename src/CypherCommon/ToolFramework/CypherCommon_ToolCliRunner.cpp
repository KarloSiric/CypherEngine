//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliRunner.cpp
//  Purpose: Implements the standard process lifecycle for Cypher CLI tools.
//  Details: Startup failures are reported through the same structured display as
//           compiler diagnostics. All temporary argument ownership remains alive
//           until the selected command callback has returned synchronously.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliRunner.h"

#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_ToolCliDisplay.h"
#include "CypherCommon_ToolCliHelp.h"
#include "CypherCommon_ToolCliSignal.h"
#include "CypherCommon_ToolCliTerminal.h"
#include "CypherCommon_Vector.h"

namespace cypher::common
{
namespace
{

inline constexpr usize CY_TOOL_CLI_MAX_HELP_BYTES = 8u * CY_MIB;
inline constexpr tool_diagnostic_code_t CY_TOOL_CLI_DIAGNOSTIC_SETUP = 1000u;
inline constexpr tool_diagnostic_code_t CY_TOOL_CLI_DIAGNOSTIC_RESPONSE = 1001u;
inline constexpr tool_diagnostic_code_t CY_TOOL_CLI_DIAGNOSTIC_ARGUMENT = 1002u;

struct tool_cli_runtime_t {
    tool_cli_terminal_t output{};
    tool_cli_terminal_t error{};
    tool_cli_display_t display{};
    tool_cli_signal_t signal{};
    tool_cli_response_file_result_t expanded{};
    vector_t<string_view_t> rawArguments{};
    vector_t<tool_option_value_t> optionStorage{};
    vector_t<string_view_t> inputStorage{};
    bool_t bOutputInitialized{ CY_FALSE };
    bool_t bErrorInitialized{ CY_FALSE };
    bool_t bDisplayInitialized{ CY_FALSE };
    bool_t bSignalInstalled{ CY_FALSE };
    bool_t bExpandedInitialized{ CY_FALSE };
};

void ShutdownRuntime( tool_cli_runtime_t *pRuntime ) noexcept
{
    if ( pRuntime == nullptr ) {
        return;
    }
    if ( pRuntime->bSignalInstalled ) {
        ToolCliSignal_Uninstall( &pRuntime->signal );
        pRuntime->bSignalInstalled = CY_FALSE;
    }
    if ( pRuntime->bDisplayInitialized ) {
        ToolCliDisplay_Shutdown( &pRuntime->display );
        pRuntime->bDisplayInitialized = CY_FALSE;
    }
    if ( pRuntime->bExpandedInitialized ) {
        ToolCliResponseFile_ShutdownResult( &pRuntime->expanded );
        pRuntime->bExpandedInitialized = CY_FALSE;
    }
    if ( pRuntime->bErrorInitialized ) {
        ToolCliTerminal_Shutdown( &pRuntime->error );
        pRuntime->bErrorInitialized = CY_FALSE;
    }
    if ( pRuntime->bOutputInitialized ) {
        ToolCliTerminal_Shutdown( &pRuntime->output );
        pRuntime->bOutputInitialized = CY_FALSE;
    }
}

void EmitDiagnostic(
    const tool_host_t &host,
    tool_diagnostic_code_t code,
    string_view_t message,
    string_view_t hint = {},
    string_view_t path = {},
    usize nLine = 0u,
    usize nColumn = 0u ) noexcept
{
    tool_diagnostic_t diagnostic{};
    diagnostic.code = code;
    diagnostic.severity = tool_diagnostic_severity_t::ERROR;
    diagnostic.category = tool_diagnostic_category_t::COMMAND_LINE;
    diagnostic.message = message;
    diagnostic.hint = hint;
    if ( hint.cchLength != 0u ) {
        diagnostic.flags |= TOOL_DIAGNOSTIC_FLAG_HAS_HINT;
    }
    if ( path.cchLength != 0u && nLine != 0u && nColumn != 0u &&
         nLine <= CY_U32_MAX && nColumn <= CY_U32_MAX ) {
        diagnostic.source.path = path;
        diagnostic.source.nLine = static_cast<u32>( nLine );
        diagnostic.source.nColumn = static_cast<u32>( nColumn );
        diagnostic.flags |= TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE;
    }
    ToolHost_EmitDiagnostic( &host, diagnostic );
}

tool_status_t InitializeRuntime(
    tool_cli_runtime_t *pRuntime,
    const tool_cli_run_options_t &options ) noexcept
{
    tool_status_t status = ToolCliTerminal_Init(
        &pRuntime->output,
        tool_cli_stream_t::STANDARD_OUTPUT );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    pRuntime->bOutputInitialized = CY_TRUE;

    status = ToolCliTerminal_Init(
        &pRuntime->error,
        tool_cli_stream_t::STANDARD_ERROR );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    pRuntime->bErrorInitialized = CY_TRUE;

    status = ToolCliDisplay_Init(
        &pRuntime->display,
        &pRuntime->output,
        &pRuntime->error,
        options.output );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    pRuntime->bDisplayInitialized = CY_TRUE;

    const allocator_t *pAllocator = Allocator_GetSystem();
    if ( !Vector_Init( &pRuntime->rawArguments, pAllocator ) ||
         !Vector_Init(
             &pRuntime->optionStorage,
             pAllocator,
             options.nOptionCapacity ) ||
         !Vector_Init(
             &pRuntime->inputStorage,
             pAllocator,
             options.nInputCapacity ) ||
         !Vector_Resize(
             &pRuntime->optionStorage,
             options.nOptionCapacity ) ||
         !Vector_Resize(
             &pRuntime->inputStorage,
             options.nInputCapacity ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }

    status = ToolCliResponseFile_InitResult(
        &pRuntime->expanded,
        pAllocator );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    pRuntime->bExpandedInitialized = CY_TRUE;
    return tool_status_t::OK;
}

tool_status_t BuildArgumentViews(
    i32 argc,
    const char *const *pArgv,
    vector_t<string_view_t> *pArguments ) noexcept
{
    if ( argc < 1 || pArgv == nullptr || pArgv[0] == nullptr ||
         !Vector_IsValid( pArguments ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    for ( i32 i = 1; i < argc; ++i ) {
        if ( pArgv[i] == nullptr ||
             !Vector_PushBack(
                 pArguments,
                 StringView_FromCString( pArgv[i] ) ) ) {
            return pArgv[i] == nullptr
                ? tool_status_t::INVALID_ARGUMENT
                : tool_status_t::OUT_OF_MEMORY;
        }
    }
    return tool_status_t::OK;
}

tool_status_t WriteHelp(
    tool_cli_runtime_t *pRuntime,
    const tool_cli_run_desc_t &desc,
    const tool_cli_parse_result_t &parseResult ) noexcept
{
    tool_cli_help_options_t helpOptions{};
    const u32 nTerminalColumns = ToolCliTerminal_Columns( &pRuntime->output );
    helpOptions.nColumns = nTerminalColumns < 40u
        ? 40u
        : ( nTerminalColumns > 4096u ? 4096u : nTerminalColumns );
    helpOptions.bUseColor = ToolCliDisplay_UsesColor( &pRuntime->display );
    helpOptions.version = desc.version;
    if ( parseResult.pCommand == nullptr && desc.pPresentation != nullptr ) {
        helpOptions.epilogue = desc.pPresentation->applicationDetails;
        const tool_status_t bannerStatus = ToolCliDisplay_WriteBanner(
            &pRuntime->display,
            desc.pPresentation->banner );
        if ( ToolStatus_Failed( bannerStatus ) ) {
            return bannerStatus;
        }
    }

    tool_cli_help_result_t measured = parseResult.pCommand != nullptr
        ? ToolCliHelp_WriteCommand(
              *desc.pApplication,
              *parseResult.pCommand,
              helpOptions,
              nullptr,
              0u )
        : ToolCliHelp_WriteApplication(
              *desc.pApplication,
              desc.pCommands,
              desc.nCommands,
              helpOptions,
              nullptr,
              0u );
    if ( measured.status != tool_status_t::CAPACITY_EXCEEDED &&
         ToolStatus_Failed( measured.status ) ) {
        return measured.status;
    }
    if ( measured.cchRequired > CY_TOOL_CLI_MAX_HELP_BYTES ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }

    text_buffer_t help{};
    if ( !TextBuffer_Init( &help, Allocator_GetSystem() ) ||
         !TextBuffer_Resize( &help, measured.cchRequired ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }
    const usize cchStorage = help.cchCapacity != 0u
        ? help.cchCapacity + 1u
        : 0u;
    const tool_cli_help_result_t written = parseResult.pCommand != nullptr
        ? ToolCliHelp_WriteCommand(
              *desc.pApplication,
              *parseResult.pCommand,
              helpOptions,
              help.pData,
              cchStorage )
        : ToolCliHelp_WriteApplication(
              *desc.pApplication,
              desc.pCommands,
              desc.nCommands,
              helpOptions,
              help.pData,
              cchStorage );
    if ( ToolStatus_Failed( written.status ) ) {
        return written.status;
    }
    if ( written.cchWritten != help.cchLength &&
         !TextBuffer_Resize( &help, written.cchWritten ) ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    return ToolCliDisplay_WriteText(
        &pRuntime->display,
        TextBuffer_View( &help ) );
}

tool_status_t WriteVersion(
    tool_cli_runtime_t *pRuntime,
    const tool_cli_run_desc_t &desc ) noexcept
{
    tool_status_t status = ToolCliDisplay_WriteText(
        &pRuntime->display,
        desc.pApplication->displayName );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( desc.version.cchLength != 0u ) {
        status = ToolCliDisplay_WriteText(
            &pRuntime->display,
            { " ", 1u } );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
        status = ToolCliDisplay_WriteText(
            &pRuntime->display,
            desc.version );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
    }
    return ToolCliDisplay_WriteText( &pRuntime->display, { "\n", 1u } );
}

} // namespace

tool_status_t ToolCliRunner_Validate(
    const tool_cli_run_desc_t &desc,
    const tool_cli_run_options_t &options ) noexcept
{
    if ( desc.pApplication == nullptr || desc.pCommands == nullptr ||
         desc.nCommands == 0u || desc.pfnExecute == nullptr ||
         !StringView_IsValid( desc.version ) ||
         options.nOptionCapacity == 0u || options.nInputCapacity == 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( desc.pPresentation != nullptr &&
         ( !StringView_IsValid( desc.pPresentation->banner ) ||
           !StringView_IsValid( desc.pPresentation->startupSummary ) ||
           !StringView_IsValid(
               desc.pPresentation->applicationDetails ) ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    tool_status_t status = ToolApplication_CheckDescriptor( *desc.pApplication );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( desc.pApplication->delivery != tool_delivery_t::COMMAND_LINE &&
         desc.pApplication->delivery != tool_delivery_t::HYBRID ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    status = ToolOutput_ValidatePolicy( options.output );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( options.responseFiles.nMaxDepth == 0u ||
         options.responseFiles.nMaxArguments == 0u ||
         options.responseFiles.cbMaxExpandedText == 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    for ( usize i = 0u; i < desc.nCommands; ++i ) {
        status = ToolCommand_CheckDescriptor( desc.pCommands[i] );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( StringView_Equals(
                     desc.pCommands[i].name,
                     desc.pCommands[j].name ) ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
        }
    }
    return tool_status_t::OK;
}

tool_exit_code_t ToolCliRunner_Run(
    const tool_cli_run_desc_t &desc,
    i32 argc,
    const char *const *pArgv,
    const tool_cli_run_options_t &options ) noexcept
{
    tool_status_t status = ToolCliRunner_Validate( desc, options );
    if ( ToolStatus_Failed( status ) || argc < 1 || pArgv == nullptr ) {
        return ToolStatus_ExitCode(
            ToolStatus_Failed( status )
                ? status
                : tool_status_t::INVALID_ARGUMENT );
    }

    tool_cli_runtime_t runtime{};
    status = InitializeRuntime( &runtime, options );
    if ( ToolStatus_Failed( status ) ) {
        ShutdownRuntime( &runtime );
        return ToolStatus_ExitCode( status );
    }
    tool_host_t host = ToolCliDisplay_MakeHost( &runtime.display );

    status = BuildArgumentViews( argc, pArgv, &runtime.rawArguments );
    if ( ToolStatus_Failed( status ) ) {
        EmitDiagnostic(
            host,
            CY_TOOL_CLI_DIAGNOSTIC_SETUP,
            { "failed to read process arguments", 32u } );
        ShutdownRuntime( &runtime );
        return ToolStatus_ExitCode( status );
    }

    span_t<const string_view_t> arguments{
        runtime.rawArguments.pData,
        runtime.rawArguments.nCount
    };
    if ( options.bExpandResponseFiles ) {
        status = ToolCliResponseFile_Expand(
            arguments,
            options.responseFiles,
            &runtime.expanded );
        if ( ToolStatus_Failed( status ) ) {
            EmitDiagnostic(
                host,
                CY_TOOL_CLI_DIAGNOSTIC_RESPONSE,
                { "failed to expand command-line response file", 43u },
                {},
                TextBuffer_View( &runtime.expanded.errorPath ),
                runtime.expanded.nErrorLine,
                runtime.expanded.nErrorColumn );
            ShutdownRuntime( &runtime );
            return ToolStatus_ExitCode( status );
        }
        arguments = {
            runtime.expanded.arguments.pData,
            runtime.expanded.arguments.nCount
        };
    }

    tool_cli_parse_result_t parseResult{};
    status = ToolCliArgumentParser_InitResult(
        &parseResult,
        runtime.optionStorage.pData,
        runtime.optionStorage.nCapacity,
        runtime.inputStorage.pData,
        runtime.inputStorage.nCapacity );
    if ( ToolStatus_Failed( status ) ) {
        EmitDiagnostic(
            host,
            CY_TOOL_CLI_DIAGNOSTIC_SETUP,
            { "failed to initialize command-line parser", 40u } );
        ShutdownRuntime( &runtime );
        return ToolStatus_ExitCode( status );
    }

    tool_cli_parse_error_t parseError{};
    status = ToolCliArgumentParser_Parse(
        arguments,
        desc.pCommands,
        desc.nCommands,
        &parseResult,
        &parseError );
    if ( ToolStatus_Failed( status ) ) {
        EmitDiagnostic(
            host,
            CY_TOOL_CLI_DIAGNOSTIC_ARGUMENT,
            parseError.message.cchLength != 0u
                ? parseError.message
                : string_view_t{ "invalid command-line arguments", 30u },
            parseError.argument );
        ShutdownRuntime( &runtime );
        return ToolStatus_ExitCode( status );
    }

    if ( desc.pfnResolveOutputPolicy != nullptr ) {
        tool_output_policy_t resolvedPolicy = runtime.display.policy;
        status = desc.pfnResolveOutputPolicy(
            parseResult,
            &resolvedPolicy,
            desc.pUserData );
        if ( ToolStatus_Succeeded( status ) ) {
            status = ToolOutput_ValidatePolicy( resolvedPolicy );
        }
        if ( ToolStatus_Failed( status ) ) {
            EmitDiagnostic(
                host,
                CY_TOOL_CLI_DIAGNOSTIC_ARGUMENT,
                { "invalid output policy", 21u } );
            ShutdownRuntime( &runtime );
            return ToolStatus_ExitCode( status );
        }
        runtime.display.policy = resolvedPolicy;
        host = ToolCliDisplay_MakeHost( &runtime.display );
    }

    if ( parseResult.action == tool_cli_parse_action_t::SHOW_HELP ) {
        status = WriteHelp( &runtime, desc, parseResult );
    } else if ( parseResult.action == tool_cli_parse_action_t::SHOW_VERSION ) {
        status = WriteVersion( &runtime, desc );
    } else {
        if ( options.bInstallInterruptHandler ) {
            status = ToolCliSignal_Install( &runtime.signal );
            if ( ToolStatus_Failed( status ) ) {
                EmitDiagnostic(
                    host,
                    CY_TOOL_CLI_DIAGNOSTIC_SETUP,
                    { "failed to install interrupt handler", 35u } );
                ShutdownRuntime( &runtime );
                return ToolStatus_ExitCode( status );
            }
            runtime.bSignalInstalled = CY_TRUE;
            host = ToolCliDisplay_MakeHost(
                &runtime.display,
                ToolCliSignal_AsCancellation( &runtime.signal ) );
        }
        if ( options.bWriteStartup ) {
            if ( desc.pPresentation != nullptr &&
                 desc.pPresentation->bShowBannerOnExecution ) {
                status = ToolCliDisplay_WriteBanner(
                    &runtime.display,
                    desc.pPresentation->banner );
            } else {
                const string_view_t startupSummary =
                    desc.pPresentation != nullptr
                    ? desc.pPresentation->startupSummary
                    : string_view_t{};
                status = ToolCliDisplay_WriteStartup(
                    &runtime.display,
                    *desc.pApplication,
                    desc.version,
                    startupSummary );
            }
        }
        if ( ToolStatus_Succeeded( status ) ) {
            status = desc.pfnExecute(
                parseResult,
                host,
                runtime.display.policy,
                desc.pUserData );
            if ( !ToolStatus_IsKnown( status ) ) {
                status = tool_status_t::INTERNAL_ERROR;
            }
        }
    }

    ShutdownRuntime( &runtime );
    return ToolStatus_ExitCode( status );
}

} // namespace cypher::common
