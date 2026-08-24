//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandLine.cpp
//  Purpose: Implements allocation-free process command-line queries.
//  Details: The process has already resolved shell quoting before this layer receives
//           argv. Parsing therefore recognizes only switch prefixes and value forms.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandLine.h"

#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

bool_t CommandLine_NormalizeSwitchName(
    string_view_t name,
    string_view_t &normalizedOut ) noexcept
{
    normalizedOut = {};
    if ( !StringView_IsValid( name ) ) {
        return CY_FALSE;
    }

    // Callers may search for "name", "-name", or "--name"; all normalize to
    // the same borrowed switch-name view.
    usize cchPrefix = 0u;
    if ( name.cchLength > 0u && name.pData[0] == '-' ) {
        cchPrefix = 1u;
        if ( name.cchLength > 1u && name.pData[1] == '-' ) {
            cchPrefix = 2u;
        }
    }
    if ( cchPrefix < name.cchLength && name.pData[cchPrefix] == '-' ) {
        return CY_FALSE;
    }

    normalizedOut = StringView_RemovePrefix( name, cchPrefix );
    if ( normalizedOut.cchLength == 0u ) {
        return CY_FALSE;
    }
    for ( usize iCharacter = 0u;
          iCharacter < normalizedOut.cchLength;
          ++iCharacter ) {
        const char ch = normalizedOut.pData[iCharacter];
        if ( !Char_IsAlphaNumericAscii( ch ) &&
             ch != '_' && ch != '-' && ch != '.' ) {
            normalizedOut = {};
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t CommandLine_IsTerminator( string_view_t argument ) noexcept
{
    // POSIX -- ends option processing; every later argv entry is positional.
    return argument.cchLength == 2u &&
           argument.pData != nullptr &&
           argument.pData[0] == '-' &&
           argument.pData[1] == '-';
}

bool_t CommandLine_ParseSwitchArgument(
    string_view_t argument,
    command_line_switch_t &switchOut ) noexcept
{
    switchOut = {};
    if ( !StringView_IsValid( argument ) ||
         argument.cchLength < 2u ||
         argument.pData[0] != '-' ||
         CommandLine_IsTerminator( argument ) ) {
        return CY_FALSE;
    }

    usize cchPrefix = argument.pData[1] == '-' ? 2u : 1u;
    if ( cchPrefix >= argument.cchLength ||
         argument.pData[cchPrefix] == '-' ) {
        return CY_FALSE;
    }

    // Support both --name=value and the Windows-style --name:value spelling.
    usize iSeparator = CY_STRING_VIEW_NPOS;
    for ( usize iCharacter = cchPrefix;
          iCharacter < argument.cchLength;
          ++iCharacter ) {
        const char ch = argument.pData[iCharacter];
        if ( ch == '=' || ch == ':' ) {
            iSeparator = iCharacter;
            break;
        }
    }

    const usize cchName = iSeparator == CY_STRING_VIEW_NPOS
        ? argument.cchLength - cchPrefix
        : iSeparator - cchPrefix;
    const string_view_t rawName = StringView_FromRange(
        argument.pData + cchPrefix,
        cchName );
    if ( !CommandLine_NormalizeSwitchName( rawName, switchOut.name ) ) {
        switchOut = {};
        return CY_FALSE;
    }

    if ( iSeparator != CY_STRING_VIEW_NPOS ) {
        switchOut.value = StringView_FromRange(
            argument.pData + iSeparator + 1u,
            argument.cchLength - iSeparator - 1u );
        switchOut.bHasValue = CY_TRUE;
    }
    return CY_TRUE;
}

bool_t CommandLine_NamesEqual(
    string_view_t left,
    string_view_t right,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    return bCaseInsensitiveAscii
        ? StringView_EqualsInsensitiveAscii( left, right )
        : StringView_Equals( left, right );
}

} // namespace

bool_t CommandLine_Init(
    command_line_t *pCommandLine,
    i32 argc,
    const char *const *ppArgv ) noexcept
{
    const bool_t bValidDestination = pCommandLine != nullptr;
    const bool_t bValidCount = argc >= 0;
    const bool_t bValidArray =
        !bValidCount || argc == 0 || ppArgv != nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "CommandLine_Init requires a destination." );
    CY_ASSERT_MSG(
        bValidCount,
        "CommandLine_Init requires a non-negative argument count." );
    CY_ASSERT_MSG(
        bValidArray,
        "CommandLine_Init requires argv when argc is non-zero." );
    if ( !bValidDestination ) {
        return CY_FALSE;
    }

    *pCommandLine = {};
    if ( !bValidCount || !bValidArray ) {
        return CY_FALSE;
    }
    for ( i32 iArgument = 0; iArgument < argc; ++iArgument ) {
        const bool_t bValidArgument = ppArgv[iArgument] != nullptr;
        CY_ASSERT_MSG(
            bValidArgument,
            "CommandLine_Init does not accept null argv entries." );
        if ( !bValidArgument ) {
            return CY_FALSE;
        }
    }

    // argv storage belongs to the process entry point. This object only borrows
    // the pointer array and its NUL-terminated strings.
    pCommandLine->nArgumentCount = argc;
    pCommandLine->ppArguments = argc > 0 ? ppArgv : nullptr;
    return CY_TRUE;
}

bool_t CommandLine_IsValid( const command_line_t *pCommandLine ) noexcept
{
    if ( pCommandLine == nullptr || pCommandLine->nArgumentCount < 0 ) {
        return CY_FALSE;
    }
    if ( pCommandLine->nArgumentCount == 0 ) {
        return pCommandLine->ppArguments == nullptr;
    }
    if ( pCommandLine->ppArguments == nullptr ) {
        return CY_FALSE;
    }
    for ( i32 iArgument = 0;
          iArgument < pCommandLine->nArgumentCount;
          ++iArgument ) {
        if ( pCommandLine->ppArguments[iArgument] == nullptr ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

i32 CommandLine_Count( const command_line_t *pCommandLine ) noexcept
{
    const bool_t bValidCommandLine = CommandLine_IsValid( pCommandLine );
    CY_ASSERT_MSG(
        bValidCommandLine,
        "CommandLine_Count requires a valid command line." );
    return bValidCommandLine ? pCommandLine->nArgumentCount : 0;
}

string_view_t CommandLine_Program(
    const command_line_t *pCommandLine ) noexcept
{
    const bool_t bValidCommandLine = CommandLine_IsValid( pCommandLine );
    CY_ASSERT_MSG(
        bValidCommandLine,
        "CommandLine_Program requires a valid command line." );
    return bValidCommandLine && pCommandLine->nArgumentCount > 0
        ? StringView_FromCString( pCommandLine->ppArguments[0] )
        : string_view_t{};
}

string_view_t CommandLine_Argument(
    const command_line_t *pCommandLine,
    i32 iArgument ) noexcept
{
    const bool_t bValidCommandLine = CommandLine_IsValid( pCommandLine );
    const bool_t bValidIndex =
        bValidCommandLine && iArgument >= 0 &&
        iArgument < pCommandLine->nArgumentCount;
    CY_ASSERT_MSG(
        bValidCommandLine,
        "CommandLine_Argument requires a valid command line." );
    CY_ASSERT_MSG(
        bValidIndex,
        "CommandLine_Argument index is outside argv." );
    return bValidIndex
        ? StringView_FromCString( pCommandLine->ppArguments[iArgument] )
        : string_view_t{};
}

bool_t CommandLine_IsSwitchArgument(
    const command_line_t *pCommandLine,
    i32 iArgument ) noexcept
{
    const bool_t bValidCommandLine = CommandLine_IsValid( pCommandLine );
    const bool_t bValidIndex =
        bValidCommandLine && iArgument >= 0 &&
        iArgument < pCommandLine->nArgumentCount;
    CY_ASSERT_MSG(
        bValidCommandLine,
        "CommandLine_IsSwitchArgument requires a valid command line." );
    CY_ASSERT_MSG(
        bValidIndex,
        "CommandLine_IsSwitchArgument index is outside argv." );
    if ( !bValidIndex || iArgument == 0 ) {
        return CY_FALSE;
    }

    command_line_switch_t parsed{};
    return CommandLine_ParseSwitchArgument(
        StringView_FromCString( pCommandLine->ppArguments[iArgument] ),
        parsed );
}

bool_t CommandLine_FindSwitchInfo(
    const command_line_t *pCommandLine,
    string_view_t name,
    command_line_switch_t *pSwitchOut,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    if ( pSwitchOut != nullptr ) {
        *pSwitchOut = {};
    }
    const bool_t bValidCommandLine = CommandLine_IsValid( pCommandLine );
    string_view_t normalizedName{};
    const bool_t bValidName =
        CommandLine_NormalizeSwitchName( name, normalizedName );
    const bool_t bValidOutput = pSwitchOut != nullptr;
    CY_ASSERT_MSG(
        bValidCommandLine,
        "CommandLine_FindSwitchInfo requires a valid command line." );
    CY_ASSERT_MSG(
        bValidName,
        "CommandLine_FindSwitchInfo requires a valid switch name." );
    CY_ASSERT_MSG(
        bValidOutput,
        "CommandLine_FindSwitchInfo requires an output structure." );
    if ( !bValidCommandLine || !bValidName || !bValidOutput ) {
        return CY_FALSE;
    }

    for ( i32 iArgument = 1;
          iArgument < pCommandLine->nArgumentCount;
          ++iArgument ) {
        const string_view_t argument = StringView_FromCString(
            pCommandLine->ppArguments[iArgument] );
        if ( CommandLine_IsTerminator( argument ) ) {
            break;
        }

        command_line_switch_t parsed{};
        if ( !CommandLine_ParseSwitchArgument( argument, parsed ) ||
             !CommandLine_NamesEqual(
                 parsed.name,
                 normalizedName,
                 bCaseInsensitiveAscii ) ) {
            continue;
        }

        parsed.iArgument = iArgument;
        if ( !parsed.bHasValue &&
             iArgument + 1 < pCommandLine->nArgumentCount ) {
            // A following non-switch argument supplies the detached value form:
            // --output path. A terminator or another switch is never consumed.
            const string_view_t next = StringView_FromCString(
                pCommandLine->ppArguments[iArgument + 1] );
            command_line_switch_t nextSwitch{};
            if ( !CommandLine_IsTerminator( next ) &&
                 !CommandLine_ParseSwitchArgument( next, nextSwitch ) ) {
                parsed.value = next;
                parsed.bHasValue = CY_TRUE;
            }
        }

        *pSwitchOut = parsed;
        return CY_TRUE;
    }
    return CY_FALSE;
}

i32 CommandLine_FindSwitch(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    command_line_switch_t result{};
    return CommandLine_FindSwitchInfo(
        pCommandLine,
        name,
        &result,
        bCaseInsensitiveAscii )
        ? result.iArgument
        : CY_COMMAND_LINE_NOT_FOUND;
}

bool_t CommandLine_HasSwitch(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    return CommandLine_FindSwitch(
        pCommandLine,
        name,
        bCaseInsensitiveAscii ) != CY_COMMAND_LINE_NOT_FOUND;
}

bool_t CommandLine_TrySwitchValue(
    const command_line_t *pCommandLine,
    string_view_t name,
    string_view_t *pValueOut,
    bool_t *pHasValueOut,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    if ( pValueOut != nullptr ) {
        *pValueOut = {};
    }
    if ( pHasValueOut != nullptr ) {
        *pHasValueOut = CY_FALSE;
    }
    const bool_t bValidOutputs =
        pValueOut != nullptr && pHasValueOut != nullptr;
    CY_ASSERT_MSG(
        bValidOutputs,
        "CommandLine_TrySwitchValue requires value and state outputs." );
    if ( !bValidOutputs ) {
        return CY_FALSE;
    }

    command_line_switch_t result{};
    if ( !CommandLine_FindSwitchInfo(
             pCommandLine,
             name,
             &result,
             bCaseInsensitiveAscii ) ) {
        return CY_FALSE;
    }

    *pValueOut = result.value;
    *pHasValueOut = result.bHasValue;
    return CY_TRUE;
}

string_view_t CommandLine_SwitchValue(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    command_line_switch_t result{};
    return CommandLine_FindSwitchInfo(
        pCommandLine,
        name,
        &result,
        bCaseInsensitiveAscii ) && result.bHasValue
        ? result.value
        : string_view_t{};
}

} // namespace cypher::common
