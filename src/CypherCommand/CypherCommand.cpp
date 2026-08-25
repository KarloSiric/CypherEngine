//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommand/CypherCommand.cpp
//  Purpose: Implements the CypherCommand Command module.
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

#include "CypherCommand.h"
#include "CypherCommand_Error.h"
#include "Engine/CypherCommon_Print.h"
#include "CypherLog.h"

#include <cctype>      // std::isspace while tokenizing command lines.
#include <cstring>     // strcmp / strncpy for fixed command strings.

namespace cypher::engine::cmd
{

static registry_t s_CmdRegistry{};

/*
================
Cmd_Init
================
*/
cmd_error_t Cmd_Init( ) {
    if ( s_CmdRegistry.initialized ) {
        LOG_WARNING( log::channel_t::CMD, "command system init requested while already initialized." );
        return cmd_error_t::ERR_IS_INIT;
    }

    s_CmdRegistry = {};

    s_CmdRegistry.nCmdCount = 0;
    s_CmdRegistry.initialized = true;

    LOG_INFO( log::channel_t::CMD, "command system initialized." );

    return cmd_error_t::OK;
}

/*
================
Cmd_Shutdown
================
*/
void Cmd_Shutdown() {
    if ( !s_CmdRegistry.initialized ) {
        LOG_WARNING( log::channel_t::CMD, "command system shutdown requested while not initialized." );
        return ;
    }

    LOG_INFO( log::channel_t::CMD, "command system shutdown: commands=%u.", s_CmdRegistry.nCmdCount );

    s_CmdRegistry = {};
    s_CmdRegistry.nCmdCount = 0;
    s_CmdRegistry.initialized = false;

    return ;
}

/*
================
Cmd_Register

Adds a named command callback to the fixed registry.
================
*/
cmd_error_t Cmd_Register( const char *szCmdName, command_fn_t pCallbackFn, void *pExtraData, const char *szCmdDescription ) {
    if ( !s_CmdRegistry.initialized ) {
        LOG_ERROR( log::channel_t::CMD, "command register failed for '%s': command system is not initialized.", szCmdName ? szCmdName : "<null>" );
        return cmd_error_t::ERR_NOT_INIT;
    }

    if ( szCmdName == nullptr || szCmdName[0] == '\0' ) {
        LOG_ERROR( log::channel_t::CMD, "command register failed: invalid command name." );
        return cmd_error_t::ERR_INVALID_COMMAND;
    }

    const cmd_t *command = Cmd_Find( szCmdName );

    if ( command != nullptr ) {
        LOG_WARNING( log::channel_t::CMD, "command register skipped: '%s' already exists.", szCmdName );
        return cmd_error_t::ERR_COMMAND_ALREADY_EXISTS;
    }

    if ( pCallbackFn == nullptr ) {
        LOG_ERROR( log::channel_t::CMD, "command register failed for '%s': invalid callback.", szCmdName );
        return cmd_error_t::ERR_INVALID_CALLBACK;
    }

    common::u32 count = s_CmdRegistry.nCmdCount;

    if ( s_CmdRegistry.nCmdCount >= CYPHER_COMMAND_MAX_COMMANDS ) {
        LOG_ERROR( log::channel_t::CMD, "command register failed for '%s': registry full (%u).", szCmdName, CYPHER_COMMAND_MAX_COMMANDS );
        return cmd_error_t::ERR_REGISTRY_FULL;
    }

    s_CmdRegistry.cmdCommands[count].name = szCmdName;
    s_CmdRegistry.cmdCommands[count].pCallbackFn = pCallbackFn;
    s_CmdRegistry.cmdCommands[count].pExtraData = pExtraData;
    s_CmdRegistry.cmdCommands[count].description = szCmdDescription;
    count++;

    s_CmdRegistry.nCmdCount = count;

    LOG_DEBUG( log::channel_t::CMD, "registered command '%s'.", szCmdName );

    return cmd_error_t::OK;
}

/*
================
Cmd_Find
================
*/
const cmd_t *Cmd_Find( const char *szCmdName ) {
    if ( !s_CmdRegistry.initialized ) {
        LOG_ERROR( log::channel_t::CMD, "command find failed for '%s': command system is not initialized.", szCmdName ? szCmdName : "<null>" );
        return nullptr;
    }

    if ( szCmdName == nullptr || szCmdName[0] == '\0' ) {
        LOG_ERROR( log::channel_t::CMD, "command find failed: invalid command name." );
        return nullptr;
    }

    for ( common::u32 i = 0; i < s_CmdRegistry.nCmdCount; i++ ) {
        if ( std::strcmp( szCmdName, s_CmdRegistry.cmdCommands[i].name ) == 0 ) {
            return &s_CmdRegistry.cmdCommands[i];
        }
    }

    return nullptr;
}

/*
================
Cmd_Parse

Splits a mutable command line into argv-style tokens.
================
*/
cmd_error_t Cmd_Parse( char *nCommandLine, common::u32 &argc, char **argv ) {

    if ( nCommandLine == nullptr || nCommandLine[0] == '\0' ) {
        LOG_ERROR( log::channel_t::CMD, "command parse failed: invalid command line." );
        return cmd_error_t::ERR_INVALID_COMMAND;
    }

    argc = 0;

    char *pCursorPtr = nCommandLine;

    while ( *pCursorPtr != '\0' ) {
        while ( *pCursorPtr != '\0' && std::isspace( static_cast<unsigned char>( *pCursorPtr ) ) ) {
            pCursorPtr++;
        }
        if ( *pCursorPtr == '\0' ) {
            break;
        }
        if ( argc >= CYPHER_COMMAND_MAX_ARGUMENTS ) {
            return cmd_error_t::ERR_PARSE_FAILED;
        }
        argv[argc++] = pCursorPtr;
        while( *pCursorPtr != '\0' && !std::isspace( static_cast<unsigned char>( *pCursorPtr ) ) ) {
            pCursorPtr++;
        }

        if ( *pCursorPtr == '\0' ) {
            break;
        }

        *pCursorPtr = '\0';
        pCursorPtr++;
    }

    return ( argc > 0u ) ? cmd_error_t::OK : cmd_error_t::ERR_INVALID_COMMAND;
}

/*
================
Cmd_Execute

Parses a command line, finds the command, and calls its callback.
================
*/
cmd_error_t Cmd_Execute( const char *nCommandLine ) {
    if ( !s_CmdRegistry.initialized ) {
        LOG_ERROR( log::channel_t::CMD, "command execute failed: command system is not initialized." );
        return cmd_error_t::ERR_NOT_INIT;
    }
    if ( nCommandLine == nullptr || nCommandLine[0] == '\0' ) {
        LOG_ERROR( log::channel_t::CMD, "command execute failed: invalid command line." );
        return cmd_error_t::ERR_INVALID_COMMAND;
    }
    common::u32 nCmdArgc{};
    char *ppszCmdArgv[CYPHER_COMMAND_MAX_ARGUMENTS]{};
    char buffer[1024]{};
    strncpy( buffer, nCommandLine, sizeof( buffer ) - 1 );
    cmd_error_t err = Cmd_Parse( buffer, nCmdArgc, ppszCmdArgv );
    if ( err != cmd_error_t::OK ) {
        COM_ERRORF( Cmd_ErrorCode( err ), "Cmd_Execute: Cmd_Parse: invalid parsing command line." );
        LOG_ERROR( log::channel_t::CMD, "command execute failed: parse failed for '%s'.", nCommandLine );
        return err;
    }
    if ( nCmdArgc == 0u || ppszCmdArgv[0] == nullptr || ppszCmdArgv[0][0] == '\0' ) {
        LOG_WARNING( log::channel_t::CMD, "command execute skipped: parsed command line is empty." );
        return cmd_error_t::ERR_INVALID_COMMAND;
    }
    const cmd_t *cmd = Cmd_Find( ppszCmdArgv[0] );
    if ( cmd == nullptr ) {
        LOG_WARNING( log::channel_t::CMD, "command execute failed: command '%s' not found.", ppszCmdArgv[0] );
        return cmd_error_t::ERR_COMMAND_NOT_FOUND;
    }
    if ( cmd->pCallbackFn == nullptr ) {
        LOG_ERROR( log::channel_t::CMD, "command execute failed: command '%s' has invalid callback.", ppszCmdArgv[0] );
        return cmd_error_t::ERR_INVALID_CALLBACK;
    }
    cmd->pCallbackFn( cmd->pExtraData, nCmdArgc, ppszCmdArgv );
    return cmd_error_t::OK;
}

} // namespace cypher::engine::cmd
