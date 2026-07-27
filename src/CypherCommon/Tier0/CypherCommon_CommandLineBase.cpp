//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CommandLineBase.cpp
//  Purpose: Implements CypherCommon Tier0 command-line argument helpers.
//  Details: This low-level parser is intentionally tiny. Higher command systems
//           can build richer option parsing on top for tools and editor launch.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandLineBase.h"

#include <cstring>

namespace cypher::common
{
namespace
{

const char *CommandLineBase_SkipPrefix( const char *pszValue ) noexcept
{
    if ( pszValue == nullptr ) {
        return nullptr;
    }
    while ( *pszValue == '-' ) {
        ++pszValue;
    }
    return pszValue;
}

bool_t CommandLineBase_IsSwitchToken( const char *pszArg ) noexcept
{
    return pszArg != nullptr &&
           pszArg[0] == '-' &&
           pszArg[1] != '\0';
}

bool_t CommandLineBase_NameMatches(
    const char *pszArg,
    const char *pszName ) noexcept
{
    if ( !CommandLineBase_IsSwitchToken( pszArg ) ||
         pszName == nullptr ||
         pszName[0] == '\0' ) {
        return CY_FALSE;
    }

    pszArg = CommandLineBase_SkipPrefix( pszArg );
    pszName = CommandLineBase_SkipPrefix( pszName );
    if ( pszArg[0] == '\0' || pszName[0] == '\0' ) {
        return CY_FALSE;
    }

    const usize cchName = std::strlen( pszName );
    if ( std::strncmp( pszArg, pszName, cchName ) != 0 ) {
        return CY_FALSE;
    }

    return pszArg[cchName] == '\0' ||
           pszArg[cchName] == '=' ||
           pszArg[cchName] == ':';
}

const char *CommandLineBase_ValueFromArg( const char *pszArg ) noexcept
{
    if ( pszArg == nullptr ) {
        return nullptr;
    }

    const char *pEqual = std::strchr( pszArg, '=' );
    if ( pEqual != nullptr ) {
        return pEqual + 1;
    }

    const char *pColon = std::strchr( pszArg, ':' );
    return pColon != nullptr ? pColon + 1 : nullptr;
}

} // namespace

bool_t Cy_CommandLineBaseSet(
    command_line_base_t *pCommandLine,
    i32 nArgCount,
    const char *const *ppszArgs ) noexcept
{
    if ( pCommandLine == nullptr ) {
        return CY_FALSE;
    }

    *pCommandLine = {};
    if ( nArgCount < 0 || ( nArgCount > 0 && ppszArgs == nullptr ) ) {
        return CY_FALSE;
    }

    const usize nRequestedCount = static_cast<usize>( nArgCount );
    const usize nStoredCount =
        nRequestedCount < CY_COMMANDLINEBASE_MAX_ARGS ?
            nRequestedCount :
            CY_COMMANDLINEBASE_MAX_ARGS;

    for ( usize i = 0u; i < nStoredCount; ++i ) {
        pCommandLine->ppszArgs[i] = ppszArgs[i];
    }
    pCommandLine->nArgCount = nStoredCount;
    pCommandLine->isTruncated = nRequestedCount > nStoredCount;
    return CY_TRUE;
}

const char *Cy_CommandLineBaseFindValue(
    const command_line_base_t *pCommandLine,
    const char *pszName ) noexcept
{
    if ( pCommandLine == nullptr ||
         pszName == nullptr ||
         CommandLineBase_SkipPrefix( pszName )[0] == '\0' ) {
        return nullptr;
    }

    for ( usize i = 1u; i < pCommandLine->nArgCount; ++i ) {
        const char *pszArg = pCommandLine->ppszArgs[i];
        if ( pszArg != nullptr && std::strcmp( pszArg, "--" ) == 0 ) {
            break;
        }
        if ( !CommandLineBase_NameMatches( pszArg, pszName ) ) {
            continue;
        }

        const char *pszInlineValue = CommandLineBase_ValueFromArg( pszArg );
        if ( pszInlineValue != nullptr ) {
            return pszInlineValue;
        }

        if ( i + 1u < pCommandLine->nArgCount ) {
            const char *pszNext = pCommandLine->ppszArgs[i + 1u];
            if ( !CommandLineBase_IsSwitchToken( pszNext ) ) {
                return pszNext != nullptr ? pszNext : "";
            }
        }

        return "";
    }

    return nullptr;
}

bool_t Cy_CommandLineBaseHasSwitch(
    const command_line_base_t *pCommandLine,
    const char *pszName ) noexcept
{
    return Cy_CommandLineBaseFindValue( pCommandLine, pszName ) != nullptr;
}

usize Cy_CommandLineBaseGetCount(
    const command_line_base_t *pCommandLine ) noexcept
{
    return pCommandLine != nullptr ? pCommandLine->nArgCount : 0u;
}

const char *Cy_CommandLineBaseGetArg(
    const command_line_base_t *pCommandLine,
    usize nIndex ) noexcept
{
    if ( pCommandLine == nullptr || nIndex >= pCommandLine->nArgCount ) {
        return nullptr;
    }
    return pCommandLine->ppszArgs[nIndex];
}

} // namespace cypher::common
