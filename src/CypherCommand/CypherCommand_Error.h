//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommand/CypherCommand_Error.h
//  Purpose: Declares the CypherCommand Command Error module.
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

#ifndef CYPHER_ENGINE_COMMAND_ERROR_H
#define CYPHER_ENGINE_COMMAND_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon_Error.h"
#include "Engine/CypherCommon.h"

namespace cypher::engine::cmd
{

/*
================
Command Error Codes
================
*/
enum class cmd_error_t : common::u8 {
    OK,                            // Operation completed successfully.

    ERR_NOT_INIT,                  // Command registry is not initialized.
    ERR_IS_INIT,                   // Initialization was requested for a live registry.
    ERR_INVALID_COMMAND,           // Command name or input line is invalid.
    ERR_COMMAND_ALREADY_EXISTS,    // A command already owns this name.
    ERR_COMMAND_NOT_FOUND,         // Dispatch could not resolve the first token.
    ERR_REGISTRY_FULL,             // Fixed command capacity is exhausted.
    ERR_PARSE_FAILED,              // Tokenizer rejected or truncated the command line.
    ERR_INVALID_CALLBACK           // Registration callback is null.
};

/*
================
Command Error Helpers
================
*/
constexpr inline const char *Cmd_ErrorName( const cmd_error_t error ) {
    switch ( error ) {
    case cmd_error_t::OK:
        return "OK";
    case cmd_error_t::ERR_NOT_INIT:
        return "ERR_NOT_INIT";
    case cmd_error_t::ERR_IS_INIT:
        return "ERR_IS_INIT";
    case cmd_error_t::ERR_INVALID_COMMAND:
        return "ERR_INVALID_COMMAND";
    case cmd_error_t::ERR_COMMAND_ALREADY_EXISTS:
        return "ERR_COMMAND_ALREADY_EXISTS";
    case cmd_error_t::ERR_COMMAND_NOT_FOUND:
        return "ERR_COMMAND_NOT_FOUND";
    case cmd_error_t::ERR_REGISTRY_FULL:
        return "ERR_REGISTRY_FULL";
    case cmd_error_t::ERR_PARSE_FAILED:
        return "ERR_PARSE_FAILED";
    case cmd_error_t::ERR_INVALID_CALLBACK:
        return "ERR_INVALID_CALLBACK";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *Cmd_ErrorDesc( const cmd_error_t error ) {
    switch ( error ) {
    case cmd_error_t::OK:
        return "operation completed successfully";
    case cmd_error_t::ERR_NOT_INIT:
        return "cmd subsystem is not initialized";
    case cmd_error_t::ERR_IS_INIT:
        return "cmd subsystem is already initialized";
    case cmd_error_t::ERR_INVALID_COMMAND:
        return "invalid command name or command line";
    case cmd_error_t::ERR_COMMAND_ALREADY_EXISTS:
        return "command is already registered";
    case cmd_error_t::ERR_COMMAND_NOT_FOUND:
        return "command was not found";
    case cmd_error_t::ERR_REGISTRY_FULL:
        return "command registry is full";
    case cmd_error_t::ERR_PARSE_FAILED:
        return "command line parsing failed";
    case cmd_error_t::ERR_INVALID_CALLBACK:
        return "invalid command callback";
    default:
        return "unknown cmd error";
    }
}

constexpr inline common::error_t Cmd_ErrorCode( cmd_error_t error ) {
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_CMD , static_cast<common::u16>( error ) );
}

}

#endif // CYPHER_ENGINE_COMMAND_ERROR_H
