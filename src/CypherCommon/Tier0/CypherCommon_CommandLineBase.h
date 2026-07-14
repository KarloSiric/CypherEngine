//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CommandLineBase.h
//  Purpose: Declares CypherCommon Tier0 CommandLineBase support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
#define CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Command Line Base

Low-level process command line declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

constexpr usize CY_COMMANDLINEBASE_MAX_ARGS = 128u;

struct command_line_base_t {
    i32 argc;
    const char *ppArgv[CY_COMMANDLINEBASE_MAX_ARGS];
};

void CommandLineBase_Set( command_line_base_t *pCommandLine, i32 argc, const char **ppArgv );
const char *CommandLineBase_Find( const command_line_base_t *pCommandLine, const char *pName );
bool_t CommandLineBase_Has( const command_line_base_t *pCommandLine, const char *pName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
