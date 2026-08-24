//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliHelp.cpp
//  Purpose: Implements descriptor-driven command-line help generation.
//  Details: Output is intentionally plain text and deterministic. Terminal color
//           and paging are display concerns layered above this formatter.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Cli Help Implementation Notes

Presentation is a host concern layered over structured tool events. Terminal width, ANSI color,
and verbosity affect rendering only, never compiler decisions.
================
*/

#include "CypherCommon_ToolCliHelp.h"

#include "CypherCommon_StringBuilder.h"

namespace cypher::common
{
namespace
{

constexpr string_view_t CY_HELP_COLOR_RESET{ "\x1b[0m", 4u };   // Restore terminal attributes.
constexpr string_view_t CY_HELP_COLOR_BOLD{ "\x1b[1m", 4u };    // Product and section emphasis.
constexpr string_view_t CY_HELP_COLOR_CYAN{ "\x1b[36m", 5u };   // Product identity accent.
constexpr string_view_t CY_HELP_COLOR_YELLOW{ "\x1b[33m", 5u }; // Section heading accent.

bool_t OptionsAreValid( const tool_cli_help_options_t &options ) noexcept
{
    return options.nColumns >= 40u && options.nColumns <= 4096u &&
           StringView_IsValid( options.version ) &&
           StringView_IsValid( options.epilogue );
}

tool_cli_help_result_t BuilderResult(
    const string_builder_t &builder ) noexcept
{
    // Preserve required length when output is truncated so callers can size a
    // second pass without maintaining a separate help-measurement path.
    switch ( builder.status ) {
        case string_builder_status_t::OK:
            return { tool_status_t::OK, builder.cchLength, builder.cchRequired };
        case string_builder_status_t::OUTPUT_TRUNCATED:
            return {
                tool_status_t::CAPACITY_EXCEEDED,
                builder.cchLength,
                builder.cchRequired
            };
        case string_builder_status_t::INVALID_ARGUMENT:
            return { tool_status_t::INVALID_ARGUMENT, 0u, builder.cchRequired };
        case string_builder_status_t::FORMAT_ERROR:
            return { tool_status_t::INTERNAL_ERROR, 0u, builder.cchRequired };
    }
    return { tool_status_t::INTERNAL_ERROR, 0u, 0u };
}

void AppendView( string_builder_t *pBuilder, string_view_t text ) noexcept
{
    (void)StringBuilder_Append( pBuilder, text );
}

void AppendText( string_builder_t *pBuilder, const char *pText ) noexcept
{
    AppendView( pBuilder, StringView_FromCString( pText ) );
}

void AppendColor(
    string_builder_t *pBuilder,
    const tool_cli_help_options_t &options,
    string_view_t color ) noexcept
{
    // Color is injected only by this helper, keeping plain and colored layouts identical.
    if ( options.bUseColor ) {
        AppendView( pBuilder, color );
    }
}

void AppendHeading(
    string_builder_t *pBuilder,
    const tool_cli_help_options_t &options,
    const char *pHeading ) noexcept
{
    AppendColor( pBuilder, options, CY_HELP_COLOR_BOLD );
    AppendColor( pBuilder, options, CY_HELP_COLOR_YELLOW );
    AppendText( pBuilder, pHeading );
    AppendColor( pBuilder, options, CY_HELP_COLOR_RESET );
    (void)StringBuilder_AppendChar( pBuilder, '\n' );
}

void AppendOptionLabel(
    string_builder_t *pBuilder,
    const tool_option_desc_t &option ) noexcept
{
    // Labels use a fixed short-option column so descriptions align whether or
    // not a descriptor provides a one-character alias.
    AppendText( pBuilder, "  " );
    if ( option.shortName != '\0' ) {
        (void)StringBuilder_AppendFormat(
            pBuilder,
            "-%c, ",
            option.shortName );
    } else {
        AppendText( pBuilder, "    " );
    }
    AppendText( pBuilder, "--" );
    AppendView( pBuilder, option.name );
    if ( option.type != tool_option_type_t::BOOLEAN ) {
        (void)StringBuilder_AppendChar( pBuilder, ' ' );
        AppendView( pBuilder, option.valueName );
    }
}

} // namespace

tool_cli_help_result_t ToolCliHelp_WriteApplication(
    const tool_application_desc_t &application,
    const tool_command_desc_t *pCommands,
    usize nCommands,
    const tool_cli_help_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( ToolStatus_Failed( ToolApplication_CheckDescriptor( application ) ) ||
         ( nCommands != 0u && pCommands == nullptr ) || nCommands == 0u ||
         !OptionsAreValid( options ) ||
         ( cchDest != 0u && pDest == nullptr ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    // The bounded builder continues measuring after truncation, which makes a
    // null-destination sizing call follow the same code path as the final write.
    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, pDest, cchDest ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    AppendColor( &builder, options, CY_HELP_COLOR_BOLD );
    AppendColor( &builder, options, CY_HELP_COLOR_CYAN );
    AppendView( &builder, application.displayName );
    if ( options.version.cchLength != 0u ) {
        AppendText( &builder, "  " );
        AppendView( &builder, options.version );
    }
    AppendColor( &builder, options, CY_HELP_COLOR_RESET );
    (void)StringBuilder_AppendChar( &builder, '\n' );
    AppendView( &builder, application.summary );
    AppendText( &builder, "\n\n" );
    AppendHeading( &builder, options, "USAGE" );
    AppendText( &builder, "  " );
    AppendView( &builder, application.displayName );
    AppendText( &builder, " <command> [options] [inputs]\n\n" );
    AppendHeading( &builder, options, "COMMANDS" );

    // Validate every command while writing so help cannot advertise a malformed
    // descriptor that the parser would later reject.
    for ( usize i = 0u; i < nCommands; ++i ) {
        const tool_command_desc_t &command = pCommands[i];
        if ( ToolStatus_Failed( ToolCommand_CheckDescriptor( command ) ) ) {
            return { tool_status_t::INVALID_CONFIGURATION, 0u, 0u };
        }
        if ( !options.bIncludeHidden &&
             ( command.flags & TOOL_COMMAND_FLAG_HIDDEN ) != 0u ) {
            continue;
        }
        AppendText( &builder, "  " );
        AppendView( &builder, command.name );
        AppendText( &builder, "\n      " );
        AppendView( &builder, command.summary );
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    AppendText( &builder, "\n" );
    AppendHeading( &builder, options, "GLOBAL OPTIONS" );
    AppendText(
        &builder,
        "  -h, --help     Show application or command help.\n"
        "  -V, --version  Show the executable version.\n" );
    // Product-owned epilogues may already contain a final newline.
    if ( options.epilogue.cchLength != 0u ) {
        AppendText( &builder, "\n" );
        AppendView( &builder, options.epilogue );
        if ( options.epilogue.pData[options.epilogue.cchLength - 1u] != '\n' ) {
            (void)StringBuilder_AppendChar( &builder, '\n' );
        }
    }
    return BuilderResult( builder );
}

tool_cli_help_result_t ToolCliHelp_WriteCommand(
    const tool_application_desc_t &application,
    const tool_command_desc_t &command,
    const tool_cli_help_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( ToolStatus_Failed( ToolApplication_CheckDescriptor( application ) ) ||
         ToolStatus_Failed( ToolCommand_CheckDescriptor( command ) ) ||
         !OptionsAreValid( options ) ||
         ( cchDest != 0u && pDest == nullptr ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, pDest, cchDest ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    AppendColor( &builder, options, CY_HELP_COLOR_BOLD );
    AppendColor( &builder, options, CY_HELP_COLOR_CYAN );
    AppendView( &builder, application.displayName );
    (void)StringBuilder_AppendChar( &builder, ' ' );
    AppendView( &builder, command.name );
    AppendColor( &builder, options, CY_HELP_COLOR_RESET );
    AppendText( &builder, "\n" );
    AppendView( &builder, command.summary );
    if ( command.details.cchLength != 0u ) {
        AppendText( &builder, "\n\n" );
        AppendView( &builder, command.details );
    }
    AppendText( &builder, "\n\n" );
    AppendHeading( &builder, options, "USAGE" );
    AppendText( &builder, "  " );
    AppendView( &builder, application.displayName );
    (void)StringBuilder_AppendChar( &builder, ' ' );
    // Explicit usage text wins; otherwise derive a safe default from the command name.
    if ( command.usage.cchLength != 0u ) {
        AppendView( &builder, command.usage );
    } else {
        AppendView( &builder, command.name );
        AppendText( &builder, " [options]" );
    }

    AppendText( &builder, "\n\n" );
    AppendHeading( &builder, options, "OPTIONS" );
    // Descriptor order is presentation order and remains deterministic across hosts.
    for ( usize i = 0u; i < command.nOptions; ++i ) {
        const tool_option_desc_t &option = command.pOptions[i];
        if ( !options.bIncludeHidden &&
             ( option.flags & TOOL_OPTION_FLAG_HIDDEN ) != 0u ) {
            continue;
        }
        AppendOptionLabel( &builder, option );
        AppendText( &builder, "\n      " );
        AppendView( &builder, option.summary );
        if ( options.bIncludeDefaults && option.defaultValue.cchLength != 0u ) {
            AppendText( &builder, " (default: " );
            AppendView( &builder, option.defaultValue );
            (void)StringBuilder_AppendChar( &builder, ')' );
        }
        if ( ( option.flags & TOOL_OPTION_FLAG_REQUIRED ) != 0u ) {
            AppendText( &builder, " (required)" );
        }
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    AppendText( &builder, "  -h, --help\n      Show command help.\n" );
    return BuilderResult( builder );
}

} // namespace cypher::common
