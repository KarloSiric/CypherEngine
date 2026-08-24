//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Config.cpp
//  Purpose: Implements command and ConVar configuration text I/O.
//  Details: Loading executes one bounded command per physical line with explicit
//           permissions. Writing streams archived ConVars without intermediate files.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Config Implementation Notes

Configuration composition is deterministic: later layers override earlier layers only through
documented precedence rules. Source storage remains owned by the caller or parsed document.
================
*/

#include "CypherCommon_Config.h"

namespace cypher::common
{

namespace
{

string_view_t StripComment( string_view_t line ) noexcept
{
    // Comment markers inside quoted arguments are data. Escapes affect quote
    // termination only while a quoted span is active.
    char chQuote = '\0';
    bool_t bEscaped = CY_FALSE;
    for ( usize iByte = 0u; iByte < line.cchLength; ++iByte ) {
        const char ch = line.pData[iByte];
        if ( chQuote != '\0' ) {
            if ( bEscaped ) {
                bEscaped = CY_FALSE;
            } else if ( ch == '\\' ) {
                bEscaped = CY_TRUE;
            } else if ( ch == chQuote ) {
                chQuote = '\0';
            }
            continue;
        }
        if ( ch == '"' || ch == '\'' ) {
            chQuote = ch;
            continue;
        }
        if ( ch == '#' ||
             ( ch == '/' && iByte + 1u < line.cchLength &&
               line.pData[iByte + 1u] == '/' ) ) {
            return StringView_Prefix( line, iByte );
        }
    }
    return line;
}

bool_t DecodeConfigValue(
    string_view_t value,
    char *pDest,
    usize cchDest,
    string_view_t &decodedOut ) noexcept
{
    if ( pDest == nullptr || cchDest == 0u || value.cchLength >= cchDest ) {
        return CY_FALSE;
    }
    usize cchWritten = 0u;
    for ( usize iByte = 0u; iByte < value.cchLength; ++iByte ) {
        char ch = value.pData[iByte];
        if ( ch == '\\' && iByte + 1u < value.cchLength ) {
            const char chEscaped = value.pData[iByte + 1u];
            switch ( chEscaped ) {
                case '\\': ch = '\\'; ++iByte; break;
                case '"':  ch = '"';  ++iByte; break;
                case 'n':   ch = '\n'; ++iByte; break;
                case 'r':   ch = '\r'; ++iByte; break;
                case 't':   ch = '\t'; ++iByte; break;
                default: break;
            }
        }
        pDest[cchWritten++] = ch;
    }
    pDest[cchWritten] = '\0';
    decodedOut = { pDest, cchWritten };
    return CY_TRUE;
}

void RecordLoadError(
    config_load_result_t &result,
    error_code_t error,
    usize iErrorByte ) noexcept
{
    ++result.nErrors;
    if ( Cy_ErrorSucceeded( result.error ) ) {
        result.error = error;
        result.iErrorByte = iErrorByte;
    }
}

bool_t WriteChunk(
    const config_writer_t &writer,
    string_view_t text ) noexcept
{
    return writer.pfnWrite( text, writer.pUserData );
}

bool_t WriteEscapedString(
    const config_writer_t &writer,
    string_view_t text ) noexcept
{
    if ( !WriteChunk( writer, { "\"", 1u } ) ) {
        return CY_FALSE;
    }
    usize iRun = 0u;
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        const char ch = text.pData[iByte];
        const char *pEscape = nullptr;
        switch ( ch ) {
            case '\\': pEscape = "\\\\"; break;
            case '"':  pEscape = "\\\""; break;
            case '\n': pEscape = "\\n"; break;
            case '\r': pEscape = "\\r"; break;
            case '\t': pEscape = "\\t"; break;
            default: break;
        }
        if ( pEscape == nullptr ) {
            continue;
        }
        if ( iByte > iRun &&
             !WriteChunk( writer, { text.pData + iRun, iByte - iRun } ) ) {
            return CY_FALSE;
        }
        if ( !WriteChunk( writer, { pEscape, 2u } ) ) {
            return CY_FALSE;
        }
        iRun = iByte + 1u;
    }
    if ( iRun < text.cchLength &&
         !WriteChunk(
             writer,
             { text.pData + iRun, text.cchLength - iRun } ) ) {
        return CY_FALSE;
    }
    return WriteChunk( writer, { "\"", 1u } );
}

struct config_write_context_t {
    config_writer_t writer{};              // Caller-supplied streaming sink.
    error_code_t error{ CY_ERROR_OK };      // First write error retained across enumeration.
};

bool_t WriteArchivedConVar(
    convar_handle_t,
    const convar_desc_t &desc,
    const convar_value_t &value,
    void *pUserData ) noexcept
{
    if ( ( desc.flags & CONVAR_FLAG_ARCHIVE ) == 0u ) {
        return CY_TRUE;
    }
    auto &context = *static_cast<config_write_context_t *>( pUserData );
    bool_t bWritten =
        WriteChunk( context.writer, desc.name ) &&
        WriteChunk( context.writer, { " ", 1u } );
    if ( bWritten && desc.type == convar_type_t::STRING ) {
        string_view_t text{};
        bWritten = Variant_GetString( value.value, &text ) &&
                   WriteEscapedString( context.writer, text );
    } else if ( bWritten ) {
        char formatted[128]{};
        const usize cchRequired = ConVar_FormatValue(
            value,
            formatted,
            sizeof( formatted ) );
        bWritten = cchRequired < sizeof( formatted ) &&
                   WriteChunk( context.writer, { formatted, cchRequired } );
    }
    if ( bWritten ) {
        bWritten = WriteChunk( context.writer, { "\n", 1u } );
    }
    if ( !bWritten ) {
        context.error = Config_MakeError( config_error_t::WRITE_FAILED );
    }
    return bWritten;
}

} // namespace

config_load_result_t Config_Load(
    const config_source_t &source,
    command_system_t *pCommandSystem,
    const command_context_t &context ) noexcept
{
    config_load_result_t result{};
    if ( !StringView_IsValid( source.name ) ||
         !StringView_IsValid( source.text ) ||
         !CommandSystem_IsValid( pCommandSystem ) ||
         ( source.flags & ~CONFIG_VALID_FLAGS ) != 0u ) {
        result.error = Config_MakeError( config_error_t::INVALID_ARGUMENT );
        result.iErrorByte = 0u;
        result.nErrors = 1u;
        return result;
    }

    // Config text cannot grant itself permissions. It can only narrow the
    // caller's command context according to source flags.
    command_context_t effectiveContext = context;
    effectiveContext.source = command_source_t::CONFIG;
    effectiveContext.bCheatsAllowed =
        context.bCheatsAllowed &&
        ( source.flags & CONFIG_FLAG_ALLOW_CHEATS ) != 0u;

    // Execute physical lines independently so diagnostics retain a byte offset
    // into the original file and STOP_ON_ERROR has an unambiguous boundary.
    usize iLineBegin = 0u;
    while ( iLineBegin < source.text.cchLength ) {
        usize iLineEnd = iLineBegin;
        while ( iLineEnd < source.text.cchLength &&
                source.text.pData[iLineEnd] != '\n' &&
                source.text.pData[iLineEnd] != '\r' ) {
            ++iLineEnd;
        }
        ++result.nLinesRead;
        string_view_t line = StringView_Trim( StripComment(
            { source.text.pData + iLineBegin, iLineEnd - iLineBegin } ) );

        if ( !StringView_IsEmpty( line ) ) {
            command_args_t args{};
            const command_parse_result_t parse = ConCommand_ParseArgs( line, &args );
            error_code_t error = CY_ERROR_OK;
            if ( !ConCommand_ParseSucceeded( parse ) ) {
                error = Config_MakeError( config_error_t::PARSE_FAILED );
            } else {
                const command_handle_t command = CommandSystem_FindCommand(
                    pCommandSystem,
                    args.arguments[0] );
                const convar_handle_t convar = CommandSystem_FindConVar(
                    pCommandSystem,
                    args.arguments[0] );
                if ( Cy_Handle32IsValid( command ) ) {
                    // Explicit commands require opt-in; archived-only loads also
                    // reject commands that are not marked safe for persistence.
                    concommand_desc_t desc{};
                    const bool_t bAllowed =
                        ( source.flags & CONFIG_FLAG_ALLOW_COMMANDS ) != 0u &&
                        CommandSystem_GetCommandDesc(
                            pCommandSystem,
                            command,
                            &desc ) &&
                        ( ( source.flags & CONFIG_FLAG_ARCHIVED_ONLY ) == 0u ||
                          ( desc.flags & CONCOMMAND_FLAG_ARCHIVE ) != 0u );
                    error = bAllowed
                        ? CommandSystem_ExecuteLine(
                            pCommandSystem,
                            line,
                            effectiveContext )
                        : Config_MakeError( config_error_t::PERMISSION_DENIED );
                } else if ( Cy_Handle32IsValid( convar ) ) {
                    convar_desc_t desc{};
                    const bool_t bAllowed =
                        CommandSystem_GetConVarDesc(
                            pCommandSystem,
                            convar,
                            &desc ) &&
                        ( ( source.flags & CONFIG_FLAG_ARCHIVED_ONLY ) == 0u ||
                          ( desc.flags & CONVAR_FLAG_ARCHIVE ) != 0u );
                    if ( !bAllowed ) {
                        error = Config_MakeError( config_error_t::PERMISSION_DENIED );
                    } else if ( args.nArgumentCount != 2u ) {
                        error = Config_MakeError( config_error_t::PARSE_FAILED );
                    } else {
                        char decoded[CY_COMMAND_MAX_LINE_BYTES + 1u]{};
                        string_view_t value{};
                        if ( !DecodeConfigValue(
                                 args.arguments[1],
                                 decoded,
                                 sizeof( decoded ),
                                 value ) ) {
                            error = Config_MakeError( config_error_t::PARSE_FAILED );
                        } else {
                            error = CommandSystem_SetConVar(
                                pCommandSystem,
                                convar,
                                value,
                                effectiveContext );
                        }
                    }
                } else {
                    error = CommandSystem_MakeError(
                        command_system_error_t::NOT_FOUND );
                }
            }

            if ( Cy_ErrorFailed( error ) ) {
                const usize iRelative = parse.iError == CY_STRING_VIEW_NPOS
                    ? 0u
                    : parse.iError;
                RecordLoadError( result, error, iLineBegin + iRelative );
                if ( ( source.flags & CONFIG_FLAG_STOP_ON_ERROR ) != 0u ) {
                    break;
                }
            } else {
                ++result.nCommandsExecuted;
            }
        }

        if ( iLineEnd >= source.text.cchLength ) {
            break;
        }
        if ( source.text.pData[iLineEnd] == '\r' &&
             iLineEnd + 1u < source.text.cchLength &&
             source.text.pData[iLineEnd + 1u] == '\n' ) {
            iLineBegin = iLineEnd + 2u;
        } else {
            iLineBegin = iLineEnd + 1u;
        }
    }
    return result;
}

error_code_t Config_WriteArchivedConVars(
    const command_system_t *pCommandSystem,
    const config_writer_t &writer ) noexcept
{
    if ( !CommandSystem_IsValid( pCommandSystem ) || writer.pfnWrite == nullptr ) {
        return Config_MakeError( config_error_t::INVALID_ARGUMENT );
    }
    config_write_context_t context{ writer, CY_ERROR_OK };
    // Enumeration stops when the writer callback returns false; context.error
    // carries the stable reason back across the callback boundary.
    static_cast<void>( CommandSystem_ForEachConVar(
        const_cast<command_system_t *>( pCommandSystem ),
        WriteArchivedConVar,
        &context ) );
    return context.error;
}

} // namespace cypher::common
