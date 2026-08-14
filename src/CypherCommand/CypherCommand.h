//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommand/CypherCommand.h
//  Purpose: Declares the CypherCommand Command module.
//  Details: This file participates in command registration and dispatch for runtime
//           and developer workflows. Keep parsing and execution separate so commands
//           remain testable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_COMMAND_H
#define CYPHER_ENGINE_COMMAND_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommand_Error.h"
#include "Engine/CypherCommon.h"

#define CYPHER_COMMAND_MAX_COMMANDS        256u
#define CYPHER_COMMAND_MAX_ARGUMENTS       16u

namespace cypher::engine::cmd
{

/*
================
Command Types

Commands bind a text name to a callback used by configs, console and tools.
================
*/
using command_fn_t = void (*)( void *pExtraData, common::u32 argc, char **argv );

struct cmd_t {
    const char  *name;
    command_fn_t    pCallbackFn;
    void        *pExtraData;
    const char  *description;
};

struct registry_t {
    cmd_t cmdCommands[CYPHER_COMMAND_MAX_COMMANDS];
    common::u32 nCmdCount;
    bool initialized;
};

/*
================
Command API
================
*/
cmd_error_t CypherCommand_Init( );

void CypherCommand_Shutdown();

cmd_error_t CypherCommand_Register( const char *szCmdName, command_fn_t pCallbackFn, void *pExtraData, const char *szCmdDescription );

const cmd_t *CypherCommand_Find( const char *szCmdName );

cmd_error_t CypherCommand_Parse( char *nCommandLine, common::u32 &argc, char **argv );

cmd_error_t CypherCommand_Execute( const char *nCommandLine );

}

#endif // CYPHER_ENGINE_COMMAND_H
