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

#define CYPHER_COMMAND_MAX_COMMANDS        256u    // Fixed command-registry capacity.
#define CYPHER_COMMAND_MAX_ARGUMENTS       16u     // Maximum tokens passed to one callback.
#define CYPHER_COMMAND_MAX_LINE_LENGTH     1024u   // Dispatch storage including the terminator.

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
    const char *name;                    // Borrowed unique command name.
    command_fn_t pCallbackFn;            // Callback invoked after tokenization succeeds.
    void *pExtraData;                    // Borrowed registration context passed to callback.
    const char *description;             // Borrowed human-readable help text.
};

struct registry_t {
    cmd_t cmdCommands[CYPHER_COMMAND_MAX_COMMANDS]; // Compact registered-command array.
    common::u32 nCmdCount;                         // Initialized prefix of cmdCommands.
    bool initialized;                              // Registry lifetime guard.
};

/*
================
Command API
================
*/
cmd_error_t Cmd_Init( );

void Cmd_Shutdown();

// Read-only registry diagnostics. Indexed entries follow registration order and
// remain borrowed from the registry until command-system shutdown.
bool Cmd_IsInitialized();

common::u32 Cmd_Count();

const cmd_t *Cmd_GetByIndex( common::u32 index );

cmd_error_t Cmd_Register( const char *szCmdName, command_fn_t pCallbackFn, void *pExtraData, const char *szCmdDescription );

const cmd_t *Cmd_Find( const char *szCmdName );

// Tokenizes in place. argv must hold MAX_ARGUMENTS entries; argc is zero on failure.
cmd_error_t Cmd_Parse( char *nCommandLine, common::u32 &argc, char **argv );

// Rejects an oversized line before invoking any callback; input is never truncated.
cmd_error_t Cmd_Execute( const char *nCommandLine );

}

#endif // CYPHER_ENGINE_COMMAND_H
