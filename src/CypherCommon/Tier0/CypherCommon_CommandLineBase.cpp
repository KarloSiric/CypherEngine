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

bool_t CommandLineBase_NameMatches( const char *pArg, const char *pName )
{
    if ( pArg == nullptr || pName == nullptr || pName[0] == '\0' ) {
        return CY_FALSE;
    }

    while ( *pArg == '-' || *pArg == '/' ) {
        ++pArg;
    }
    while ( *pName == '-' || *pName == '/' ) {
        ++pName;
    }

    const usize cchName = std::strlen( pName );
    if ( std::strncmp( pArg, pName, cchName ) != 0 ) {
        return CY_FALSE;
    }

    return pArg[cchName] == '\0' || pArg[cchName] == '=' || pArg[cchName] == ':';
}

const char *CommandLineBase_ValueFromArg( const char *pArg )
{
    if ( pArg == nullptr ) {
        return nullptr;
    }

    const char *pEqual = std::strchr( pArg, '=' );
    if ( pEqual != nullptr ) {
        return pEqual + 1;
    }

    const char *pColon = std::strchr( pArg, ':' );
    return pColon != nullptr ? pColon + 1 : nullptr;
}

} // namespace

void CommandLineBase_Set( command_line_base_t *pCommandLine, i32 argc, const char **ppArgv )
{
    if ( pCommandLine == nullptr ) {
        return;
    }

    pCommandLine->argc = 0;
    for ( usize i = 0u; i < CY_COMMANDLINEBASE_MAX_ARGS; ++i ) {
        pCommandLine->ppArgv[i] = nullptr;
    }

    if ( argc <= 0 || ppArgv == nullptr ) {
        return;
    }

    const usize nCount = static_cast<usize>( argc ) < CY_COMMANDLINEBASE_MAX_ARGS ?
                          static_cast<usize>( argc ) :
                          CY_COMMANDLINEBASE_MAX_ARGS;

    for ( usize i = 0u; i < nCount; ++i ) {
        pCommandLine->ppArgv[i] = ppArgv[i];
    }
    pCommandLine->argc = static_cast<i32>( nCount );
}

const char *CommandLineBase_Find( const command_line_base_t *pCommandLine, const char *pName )
{
    if ( pCommandLine == nullptr || pName == nullptr || pName[0] == '\0' ) {
        return nullptr;
    }

    for ( i32 i = 0; i < pCommandLine->argc; ++i ) {
        const char *pArg = pCommandLine->ppArgv[i];
        if ( !CommandLineBase_NameMatches( pArg, pName ) ) {
            continue;
        }

        const char *pInlineValue = CommandLineBase_ValueFromArg( pArg );
        if ( pInlineValue != nullptr ) {
            return pInlineValue;
        }

        if ( i + 1 < pCommandLine->argc ) {
            const char *pNext = pCommandLine->ppArgv[i + 1];
            if ( pNext != nullptr && pNext[0] != '-' && pNext[0] != '/' ) {
                return pNext;
            }
        }

        return "";
    }

    return nullptr;
}

bool_t CommandLineBase_Has( const command_line_base_t *pCommandLine, const char *pName )
{
    return CommandLineBase_Find( pCommandLine, pName ) != nullptr;
}

} // namespace cypher::common
