//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliArgumentParser.cpp
//  Purpose: Implements descriptor-driven command-line argument parsing.
//  Details: Long and short options, Boolean negation, explicit option termination,
//           defaults, repeated values, and input cardinality share one bounded path.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliArgumentParser.h"

namespace cypher::common
{
namespace
{

constexpr string_view_t VIEW_TRUE{ "true", 4u };       // Implicit Boolean option value.
constexpr string_view_t VIEW_FALSE{ "false", 5u };     // Value produced by --no-name.
constexpr string_view_t VIEW_HELP{ "help", 4u };       // Built-in long help name.
constexpr string_view_t VIEW_VERSION{ "version", 7u }; // Built-in long version name.

bool_t ResultStorageIsValid( const tool_cli_parse_result_t *pResult ) noexcept
{
    return pResult != nullptr &&
           pResult->options.nCount <= pResult->options.nCapacity &&
           ( pResult->options.nCapacity == 0u ||
             pResult->options.pValues != nullptr ) &&
           pResult->nInputs <= pResult->nInputCapacity &&
           ( pResult->nInputCapacity == 0u || pResult->pInputs != nullptr );
}

tool_status_t SetError(
    tool_cli_parse_error_t *pError,
    tool_status_t status,
    usize iArgument,
    string_view_t argument,
    const char *pMessage ) noexcept
{
    // Error storage is optional, but the returned status is identical either way.
    if ( pError != nullptr ) {
        *pError = {
            status,
            iArgument,
            argument,
            StringView_FromCString( pMessage )
        };
    }
    return status;
}

const tool_command_desc_t *FindCommand(
    const tool_command_desc_t *pCommands,
    usize nCommands,
    string_view_t name ) noexcept
{
    for ( usize i = 0u; i < nCommands; ++i ) {
        if ( StringView_Equals( pCommands[i].name, name ) ) {
            return &pCommands[i];
        }
    }
    return nullptr;
}

bool_t IsLongOption( string_view_t argument ) noexcept
{
    return argument.cchLength > 2u && argument.pData[0] == '-' &&
           argument.pData[1] == '-';
}

bool_t IsShortOption( string_view_t argument ) noexcept
{
    return argument.cchLength > 1u && argument.pData[0] == '-' &&
           argument.pData[1] != '-';
}

bool_t IsBuiltIn(
    string_view_t argument,
    string_view_t longName,
    char shortName ) noexcept
{
    if ( IsLongOption( argument ) ) {
        return StringView_Equals(
            StringView_RemovePrefix( argument, 2u ),
            longName );
    }
    return argument.cchLength == 2u && argument.pData[0] == '-' &&
           argument.pData[1] == shortName;
}

tool_status_t ResolveDefaults(
    const tool_command_desc_t &command,
    tool_option_set_t *pOptions ) noexcept
{
    // Defaults enter through the same typed validator as explicit arguments and
    // remain weaker than every later configuration source.
    for ( usize i = 0u; i < command.nOptions; ++i ) {
        const tool_option_desc_t &option = command.pOptions[i];
        if ( option.defaultValue.cchLength == 0u ) {
            continue;
        }
        const tool_status_t status = ToolOptionSet_Resolve(
            pOptions,
            &option,
            option.defaultValue,
            tool_option_source_t::DEFAULT_VALUE );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
    }
    return tool_status_t::OK;
}

tool_status_t AddInput(
    tool_cli_parse_result_t *pResult,
    string_view_t input ) noexcept
{
    if ( pResult->nInputs == pResult->nInputCapacity ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    // Positional order is semantically significant for deterministic commands.
    pResult->pInputs[pResult->nInputs++] = input;
    return tool_status_t::OK;
}

tool_status_t ResolveOption(
    tool_cli_parse_result_t *pResult,
    const tool_option_desc_t *pOption,
    string_view_t value ) noexcept
{
    return ToolOptionSet_Resolve(
        &pResult->options,
        pOption,
        value,
        tool_option_source_t::COMMAND_LINE );
}

tool_status_t ParseLongOption(
    span_t<const string_view_t> arguments,
    usize *piArgument,
    tool_cli_parse_result_t *pResult,
    tool_cli_parse_error_t *pError ) noexcept
{
    const usize iArgument = *piArgument;
    const string_view_t raw = arguments.pData[iArgument];
    string_view_t body = StringView_RemovePrefix( raw, 2u );
    // Long options accept both --name=value and --name value forms.
    const usize iEquals = StringView_FindChar( body, '=' );
    const bool_t bHasInlineValue = iEquals != CY_STRING_VIEW_NPOS;
    string_view_t name = bHasInlineValue
        ? StringView_Prefix( body, iEquals )
        : body;
    string_view_t value = bHasInlineValue
        ? StringView_RemovePrefix( body, iEquals + 1u )
        : string_view_t{};

    bool_t bNegated = CY_FALSE;
    const tool_option_desc_t *pOption =
        ToolCommand_FindOption( *pResult->pCommand, name );
    // --no-name is recognized only for Boolean descriptors and cannot also carry
    // an inline value.
    if ( pOption == nullptr && !bHasInlineValue &&
         StringView_StartsWith( name, { "no-", 3u } ) ) {
        const string_view_t positiveName = StringView_RemovePrefix( name, 3u );
        pOption = ToolCommand_FindOption( *pResult->pCommand, positiveName );
        bNegated = pOption != nullptr &&
                   pOption->type == tool_option_type_t::BOOLEAN;
    }
    if ( pOption == nullptr ) {
        return SetError(
            pError,
            tool_status_t::INVALID_OPTION,
            iArgument,
            raw,
            "unknown command option" );
    }

    // Boolean presence implies true. Other types consume the next argument when
    // no inline value follows the equals sign.
    if ( pOption->type == tool_option_type_t::BOOLEAN ) {
        if ( bNegated ) {
            value = VIEW_FALSE;
        } else if ( !bHasInlineValue ) {
            value = VIEW_TRUE;
        }
    } else if ( !bHasInlineValue ) {
        if ( iArgument + 1u >= arguments.nCount ) {
            return SetError(
                pError,
                tool_status_t::INVALID_OPTION,
                iArgument,
                raw,
                "option requires a value" );
        }
        ++( *piArgument );
        value = arguments.pData[*piArgument];
    }

    const tool_status_t status = ResolveOption( pResult, pOption, value );
    return ToolStatus_Failed( status )
        ? SetError( pError, status, iArgument, raw, "invalid option value" )
        : status;
}

tool_status_t ParseShortOption(
    span_t<const string_view_t> arguments,
    usize *piArgument,
    tool_cli_parse_result_t *pResult,
    tool_cli_parse_error_t *pError ) noexcept
{
    const usize iArgument = *piArgument;
    const string_view_t raw = arguments.pData[iArgument];
    const char shortName = raw.pData[1];
    const tool_option_desc_t *pOption =
        ToolCommand_FindShortOption( *pResult->pCommand, shortName );
    if ( pOption == nullptr ) {
        return SetError(
            pError,
            tool_status_t::INVALID_OPTION,
            iArgument,
            raw,
            "unknown short option" );
    }

    // Short options support -ovalue, -o=value, and -o value. Boolean grouping is
    // rejected so every descriptor receives one unambiguous occurrence.
    string_view_t value{};
    if ( pOption->type == tool_option_type_t::BOOLEAN ) {
        if ( raw.cchLength != 2u ) {
            return SetError(
                pError,
                tool_status_t::INVALID_OPTION,
                iArgument,
                raw,
                "Boolean short options cannot be grouped" );
        }
        value = VIEW_TRUE;
    } else if ( raw.cchLength > 2u ) {
        usize iValue = 2u;
        if ( raw.pData[iValue] == '=' ) {
            ++iValue;
        }
        value = StringView_RemovePrefix( raw, iValue );
    } else {
        if ( iArgument + 1u >= arguments.nCount ) {
            return SetError(
                pError,
                tool_status_t::INVALID_OPTION,
                iArgument,
                raw,
                "option requires a value" );
        }
        ++( *piArgument );
        value = arguments.pData[*piArgument];
    }

    const tool_status_t status = ResolveOption( pResult, pOption, value );
    return ToolStatus_Failed( status )
        ? SetError( pError, status, iArgument, raw, "invalid option value" )
        : status;
}

tool_status_t ValidateRequiredOptions(
    const tool_cli_parse_result_t &result,
    tool_cli_parse_error_t *pError ) noexcept
{
    for ( usize i = 0u; i < result.pCommand->nOptions; ++i ) {
        const tool_option_desc_t &option = result.pCommand->pOptions[i];
        if ( ( option.flags & TOOL_OPTION_FLAG_REQUIRED ) != 0u &&
             ToolOptionSet_Find( &result.options, option.name ) == nullptr ) {
            return SetError(
                pError,
                tool_status_t::INVALID_OPTION,
                CY_INVALID_SIZE,
                option.name,
                "required option is missing" );
        }
    }
    return tool_status_t::OK;
}

} // namespace

tool_status_t ToolCliArgumentParser_InitResult(
    tool_cli_parse_result_t *pResult,
    tool_option_value_t *pOptionStorage,
    usize nOptionCapacity,
    string_view_t *pInputStorage,
    usize nInputCapacity ) noexcept
{
    if ( pResult == nullptr ||
         ( nInputCapacity != 0u && pInputStorage == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *pResult = {};
    const tool_status_t status = ToolOptionSet_Init(
        &pResult->options,
        pOptionStorage,
        nOptionCapacity );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    pResult->pInputs = pInputStorage;
    pResult->nInputCapacity = nInputCapacity;
    return tool_status_t::OK;
}

void ToolCliArgumentParser_ClearResult(
    tool_cli_parse_result_t *pResult ) noexcept
{
    if ( !ResultStorageIsValid( pResult ) ) {
        return;
    }
    pResult->pCommand = nullptr;
    ToolOptionSet_Clear( &pResult->options );
    pResult->nInputs = 0u;
    // Empty input intentionally defaults to application help.
    pResult->action = tool_cli_parse_action_t::SHOW_HELP;
}

tool_status_t ToolCliArgumentParser_Parse(
    span_t<const string_view_t> arguments,
    const tool_command_desc_t *pCommands,
    usize nCommands,
    tool_cli_parse_result_t *pResult,
    tool_cli_parse_error_t *pErrorOut ) noexcept
{
    if ( pErrorOut != nullptr ) {
        *pErrorOut = {};
    }
    if ( !Span_IsValid( arguments ) ||
         ( nCommands != 0u && pCommands == nullptr ) || nCommands == 0u ||
         !ResultStorageIsValid( pResult ) ) {
        return SetError(
            pErrorOut,
            tool_status_t::INVALID_ARGUMENT,
            CY_INVALID_SIZE,
            {},
            "invalid argument parser contract" );
    }
    ToolCliArgumentParser_ClearResult( pResult );

    // Descriptor errors are configuration failures and must be caught before any
    // argument is interpreted against them.
    for ( usize i = 0u; i < nCommands; ++i ) {
        const tool_status_t status = ToolCommand_CheckDescriptor( pCommands[i] );
        if ( ToolStatus_Failed( status ) ) {
            return SetError(
                pErrorOut,
                status,
                CY_INVALID_SIZE,
                pCommands[i].name,
                "invalid command descriptor" );
        }
    }

    if ( arguments.nCount == 0u ) {
        return tool_status_t::OK;
    }
    for ( usize i = 0u; i < arguments.nCount; ++i ) {
        if ( !StringView_IsValid( arguments.pData[i] ) ) {
            return SetError(
                pErrorOut,
                tool_status_t::INVALID_ARGUMENT,
                i,
                {},
                "invalid argument view" );
        }
    }

    const string_view_t first = arguments.pData[0];
    if ( IsBuiltIn( first, VIEW_HELP, 'h' ) ) {
        pResult->action = tool_cli_parse_action_t::SHOW_HELP;
        return tool_status_t::OK;
    }
    if ( IsBuiltIn( first, VIEW_VERSION, 'V' ) ) {
        pResult->action = tool_cli_parse_action_t::SHOW_VERSION;
        return tool_status_t::OK;
    }

    pResult->pCommand = FindCommand( pCommands, nCommands, first );
    if ( pResult->pCommand == nullptr ) {
        return SetError(
            pErrorOut,
            tool_status_t::INVALID_COMMAND,
            0u,
            first,
            "unknown tool command" );
    }
    pResult->action = tool_cli_parse_action_t::EXECUTE;

    tool_status_t status = ResolveDefaults(
        *pResult->pCommand,
        &pResult->options );
    if ( ToolStatus_Failed( status ) ) {
        return SetError(
            pErrorOut,
            status,
            CY_INVALID_SIZE,
            {},
            "failed to resolve command defaults" );
    }

    // The command name is consumed first; -- then permanently switches the rest
    // of this invocation to positional-input parsing.
    bool_t bOptionsEnded = CY_FALSE;
    for ( usize i = 1u; i < arguments.nCount; ++i ) {
        const string_view_t argument = arguments.pData[i];
        if ( !bOptionsEnded && StringView_Equals( argument, { "--", 2u } ) ) {
            bOptionsEnded = CY_TRUE;
            continue;
        }
        if ( !bOptionsEnded && IsBuiltIn( argument, VIEW_HELP, 'h' ) ) {
            pResult->action = tool_cli_parse_action_t::SHOW_HELP;
            return tool_status_t::OK;
        }

        if ( !bOptionsEnded && IsLongOption( argument ) ) {
            status = ParseLongOption(
                arguments,
                &i,
                pResult,
                pErrorOut );
        } else if ( !bOptionsEnded && IsShortOption( argument ) ) {
            status = ParseShortOption(
                arguments,
                &i,
                pResult,
                pErrorOut );
        } else {
            status = AddInput( pResult, argument );
            if ( ToolStatus_Failed( status ) ) {
                status = SetError(
                    pErrorOut,
                    status,
                    i,
                    argument,
                    "input storage capacity exceeded" );
            }
        }
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
    }

    // Cardinality is checked after parsing so diagnostics can refer to the full
    // positional set rather than failing partway through it.
    const flags32_t commandFlags = pResult->pCommand->flags;
    if ( pResult->nInputs != 0u &&
         ( commandFlags & TOOL_COMMAND_FLAG_ACCEPTS_INPUTS ) == 0u ) {
        return SetError(
            pErrorOut,
            tool_status_t::INVALID_COMMAND,
            CY_INVALID_SIZE,
            {},
            "command does not accept positional inputs" );
    }
    if ( pResult->nInputs > 1u &&
         ( commandFlags & TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS ) == 0u ) {
        return SetError(
            pErrorOut,
            tool_status_t::INVALID_COMMAND,
            CY_INVALID_SIZE,
            {},
            "command accepts only one positional input" );
    }
    return ValidateRequiredOptions( *pResult, pErrorOut );
}

} // namespace cypher::common
