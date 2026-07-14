//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherCommand/CypherCommand_Error.h
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

#include "CypherCommon_Error.h"
#include "CypherCommon.h"

namespace cypher::engine::cmd
{

/*
================
Command Error Codes
================
*/
enum class cmd_error_t : common::u8 {
    OK,

    ERR_NOT_INIT,
    ERR_IS_INIT,
    ERR_INVALID_COMMAND,
    ERR_COMMAND_ALREADY_EXISTS,
    ERR_COMMAND_NOT_FOUND,
    ERR_REGISTRY_FULL,
    ERR_PARSE_FAILED,
    ERR_INVALID_CALLBACK
};

/*
================
Command Error Helpers
================
*/
constexpr inline const char *CypherCommand_ErrorName( const cmd_error_t error ) {
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

constexpr inline const char *CypherCommand_ErrorDesc( const cmd_error_t error ) {
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

constexpr inline common::error_t CypherCommand_ErrorCode( cmd_error_t error ) {
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_CMD , static_cast<common::u16>( error ) );
}

}

#endif // CYPHER_ENGINE_COMMAND_ERROR_H
