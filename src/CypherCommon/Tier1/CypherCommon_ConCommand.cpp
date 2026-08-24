//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConCommand.cpp
//  Purpose: Implements console-command descriptors and argument parsing.
//  Details: Parsing is allocation-free and returns borrowed slices into one bounded
//           command line. Descriptor validation rejects ambiguous execution policy
//           before commands enter a registry.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConCommand.h"

#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD bool_t IsCommandSeparator( char ch ) noexcept
{
    return ch == ' ' || ch == '\t';
}

CYPHER_NODISCARD bool_t IsCommandNameBody( char ch ) noexcept
{
    return Char_IsAlphaNumericAscii( ch ) ||
           ch == '_' || ch == '.' || ch == '-';
}

CYPHER_NODISCARD bool_t HasEmbeddedNull( string_view_t text ) noexcept
{
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] == '\0' ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD command_parse_result_t ParseFailure(
    command_parse_status_t status,
    usize iError ) noexcept
{
    return { status, iError };
}

CYPHER_NODISCARD command_parse_result_t ValidateCommandByte(
    char ch,
    usize iByte ) noexcept
{
    if ( ch == '\0' ) {
        return ParseFailure( command_parse_status_t::EMBEDDED_NULL, iByte );
    }
    if ( Char_IsNewLineAscii( ch ) ) {
        return ParseFailure( command_parse_status_t::LINE_BREAK, iByte );
    }
    if ( Char_IsControlAscii( ch ) && ch != '\t' ) {
        return ParseFailure( command_parse_status_t::INVALID_CHARACTER, iByte );
    }
    return { command_parse_status_t::OK, CY_STRING_VIEW_NPOS };
}

} // namespace

bool_t ConCommand_IsValidName( string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) ||
         name.cchLength == 0u ||
         name.cchLength > CY_COMMAND_MAX_NAME_BYTES ) {
        return CY_FALSE;
    }

    const char chFirst = name.pData[0];
    if ( !Char_IsAlphaAscii( chFirst ) && chFirst != '_' ) {
        return CY_FALSE;
    }

    for ( usize iByte = 1u; iByte < name.cchLength; ++iByte ) {
        if ( !IsCommandNameBody( name.pData[iByte] ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t ConCommand_ValidateDesc( const concommand_desc_t &desc ) noexcept
{
    if ( !ConCommand_IsValidName( desc.name ) ||
         !StringView_IsValid( desc.help ) ||
         !StringView_IsValid( desc.usage ) ||
         HasEmbeddedNull( desc.help ) ||
         HasEmbeddedNull( desc.usage ) ||
         desc.pfnExecute == nullptr ) {
        return CY_FALSE;
    }

    if ( ( desc.flags & ~CONCOMMAND_VALID_FLAGS ) != 0u ) {
        return CY_FALSE;
    }

    // Mutually exclusive execution domains would make dispatch policy ambiguous.
    const bool_t bServerOnly =
        ( desc.flags & CONCOMMAND_FLAG_SERVER_ONLY ) != 0u;
    const bool_t bClientOnly =
        ( desc.flags & CONCOMMAND_FLAG_CLIENT_ONLY ) != 0u;
    return !( bServerOnly && bClientOnly );
}

bool_t ConCommand_ParseSucceeded( command_parse_result_t result ) noexcept
{
    return result.status == command_parse_status_t::OK;
}

const char *ConCommand_ParseStatusName( command_parse_status_t status ) noexcept
{
    switch ( status ) {
        case command_parse_status_t::OK:                         return "OK";
        case command_parse_status_t::INVALID_ARGUMENT:           return "INVALID_ARGUMENT";
        case command_parse_status_t::EMPTY_LINE:                 return "EMPTY_LINE";
        case command_parse_status_t::LINE_TOO_LONG:              return "LINE_TOO_LONG";
        case command_parse_status_t::EMBEDDED_NULL:              return "EMBEDDED_NULL";
        case command_parse_status_t::LINE_BREAK:                 return "LINE_BREAK";
        case command_parse_status_t::INVALID_CHARACTER:          return "INVALID_CHARACTER";
        case command_parse_status_t::UNEXPECTED_QUOTE:           return "UNEXPECTED_QUOTE";
        case command_parse_status_t::UNTERMINATED_QUOTE:         return "UNTERMINATED_QUOTE";
        case command_parse_status_t::TRAILING_BYTES_AFTER_QUOTE: return "TRAILING_BYTES_AFTER_QUOTE";
        case command_parse_status_t::TOO_MANY_ARGUMENTS:         return "TOO_MANY_ARGUMENTS";
        case command_parse_status_t::INVALID_COMMAND_NAME:       return "INVALID_COMMAND_NAME";
    }
    return "UNKNOWN_COMMAND_PARSE_STATUS";
}

command_parse_result_t ConCommand_ParseArgs(
    string_view_t commandLine,
    command_args_t *pArgsOut ) noexcept
{
    if ( pArgsOut == nullptr || !StringView_IsValid( commandLine ) ) {
        return ParseFailure( command_parse_status_t::INVALID_ARGUMENT, 0u );
    }
    if ( commandLine.cchLength > CY_COMMAND_MAX_LINE_BYTES ) {
        return ParseFailure(
            command_parse_status_t::LINE_TOO_LONG,
            CY_COMMAND_MAX_LINE_BYTES );
    }

    // Parse into a temporary result. The caller's output remains unchanged on
    // every syntax or capacity failure.
    command_args_t parsed{};
    parsed.commandLine = commandLine;
    usize iCursor = 0u;

    while ( iCursor < commandLine.cchLength ) {
        while ( iCursor < commandLine.cchLength &&
                IsCommandSeparator( commandLine.pData[iCursor] ) ) {
            ++iCursor;
        }
        if ( iCursor == commandLine.cchLength ) {
            break;
        }

        const command_parse_result_t byteResult =
            ValidateCommandByte( commandLine.pData[iCursor], iCursor );
        if ( !ConCommand_ParseSucceeded( byteResult ) ) {
            return byteResult;
        }
        if ( parsed.nArgumentCount == CY_COMMAND_MAX_ARGUMENTS ) {
            return ParseFailure(
                command_parse_status_t::TOO_MANY_ARGUMENTS,
                iCursor );
        }

        string_view_t argument{};
        const char chFirst = commandLine.pData[iCursor];
        if ( chFirst == '\'' || chFirst == '"' ) {
            // Quotes delimit one borrowed argument. Escape pairs are validated
            // here but deliberately remain encoded in the original command line.
            const char chQuote = chFirst;
            const usize iQuote = iCursor;
            const usize iArgument = ++iCursor;

            while ( iCursor < commandLine.cchLength &&
                    commandLine.pData[iCursor] != chQuote ) {
                if ( commandLine.pData[iCursor] == '\\' &&
                     iCursor + 1u < commandLine.cchLength ) {
                    const command_parse_result_t escapedByteResult =
                        ValidateCommandByte(
                            commandLine.pData[iCursor + 1u],
                            iCursor + 1u );
                    if ( !ConCommand_ParseSucceeded( escapedByteResult ) ) {
                        return escapedByteResult;
                    }
                    iCursor += 2u;
                    continue;
                }
                const command_parse_result_t quotedByteResult =
                    ValidateCommandByte( commandLine.pData[iCursor], iCursor );
                if ( !ConCommand_ParseSucceeded( quotedByteResult ) ) {
                    return quotedByteResult;
                }
                ++iCursor;
            }
            if ( iCursor == commandLine.cchLength ) {
                return ParseFailure(
                    command_parse_status_t::UNTERMINATED_QUOTE,
                    iQuote );
            }

            argument = StringView_FromRange(
                commandLine.pData + iArgument,
                iCursor - iArgument );
            ++iCursor;

            if ( iCursor < commandLine.cchLength ) {
                const command_parse_result_t trailingByteResult =
                    ValidateCommandByte( commandLine.pData[iCursor], iCursor );
                if ( !ConCommand_ParseSucceeded( trailingByteResult ) ) {
                    return trailingByteResult;
                }
                if ( !IsCommandSeparator( commandLine.pData[iCursor] ) ) {
                    return ParseFailure(
                        command_parse_status_t::TRAILING_BYTES_AFTER_QUOTE,
                        iCursor );
                }
            }
        } else {
            const usize iArgument = iCursor;
            while ( iCursor < commandLine.cchLength &&
                    !IsCommandSeparator( commandLine.pData[iCursor] ) ) {
                const char ch = commandLine.pData[iCursor];
                const command_parse_result_t argumentByteResult =
                    ValidateCommandByte( ch, iCursor );
                if ( !ConCommand_ParseSucceeded( argumentByteResult ) ) {
                    return argumentByteResult;
                }
                if ( ch == '\'' || ch == '"' ) {
                    return ParseFailure(
                        command_parse_status_t::UNEXPECTED_QUOTE,
                        iCursor );
                }
                ++iCursor;
            }
            argument = StringView_FromRange(
                commandLine.pData + iArgument,
                iCursor - iArgument );
        }

        parsed.arguments[parsed.nArgumentCount] = argument; // View borrows commandLine storage.
        ++parsed.nArgumentCount;
    }

    if ( parsed.nArgumentCount == 0u ) {
        return ParseFailure( command_parse_status_t::EMPTY_LINE, 0u );
    }
    if ( !ConCommand_IsValidName( parsed.arguments[0] ) ) {
        return ParseFailure(
            command_parse_status_t::INVALID_COMMAND_NAME,
            static_cast<usize>(
                parsed.arguments[0].pData - commandLine.pData ) );
    }

    *pArgsOut = parsed;
    return { command_parse_status_t::OK, CY_STRING_VIEW_NPOS };
}

} // namespace cypher::common
