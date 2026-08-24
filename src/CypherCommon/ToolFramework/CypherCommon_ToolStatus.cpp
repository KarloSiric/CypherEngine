//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolStatus.cpp
//  Purpose: Implements stable tool status queries and process exit mapping.
//  Details: Exit classes remain coarse and automation-friendly while detailed
//           diagnostics preserve the subsystem-specific reason for a failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Status Implementation Notes

These are stable tool-neutral contracts shared by CLI applications, future GUI hosts, tests, and
compiler modules. They must not depend on Qt or terminal state.
================
*/

#include "CypherCommon_ToolStatus.h"

namespace cypher::common
{

bool_t ToolStatus_IsKnown( tool_status_t status ) noexcept
{
    return status >= tool_status_t::OK &&
           status <= tool_status_t::INTERNAL_ERROR;
}

bool_t ToolStatus_Succeeded( tool_status_t status ) noexcept
{
    return status == tool_status_t::OK;
}

bool_t ToolStatus_Failed( tool_status_t status ) noexcept
{
    return !ToolStatus_Succeeded( status );
}

const char *ToolStatus_Name( tool_status_t status ) noexcept
{
    // Names are stable protocol text for logs, reports, and editor output.
    switch ( status ) {
        case tool_status_t::OK: return "OK";
        case tool_status_t::CANCELLED: return "CANCELLED";
        case tool_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case tool_status_t::INVALID_STATE: return "INVALID_STATE";
        case tool_status_t::INVALID_COMMAND: return "INVALID_COMMAND";
        case tool_status_t::INVALID_OPTION: return "INVALID_OPTION";
        case tool_status_t::INVALID_PROJECT: return "INVALID_PROJECT";
        case tool_status_t::INVALID_CONFIGURATION: return "INVALID_CONFIGURATION";
        case tool_status_t::VALIDATION_FAILED: return "VALIDATION_FAILED";
        case tool_status_t::OPERATION_FAILED: return "OPERATION_FAILED";
        case tool_status_t::IO_ERROR: return "IO_ERROR";
        case tool_status_t::CACHE_ERROR: return "CACHE_ERROR";
        case tool_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case tool_status_t::UNSUPPORTED: return "UNSUPPORTED";
        case tool_status_t::ALREADY_EXISTS: return "ALREADY_EXISTS";
        case tool_status_t::NOT_FOUND: return "NOT_FOUND";
        case tool_status_t::CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
        case tool_status_t::INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

tool_exit_code_t ToolStatus_ExitCode( tool_status_t status ) noexcept
{
    // Many detailed statuses collapse into a small process-exit vocabulary so
    // scripts can distinguish usage, configuration, infrastructure, and work failures.
    switch ( status ) {
        case tool_status_t::OK:
            return tool_exit_code_t::SUCCESS;
        case tool_status_t::CANCELLED:
            return tool_exit_code_t::CANCELLED;
        case tool_status_t::INVALID_ARGUMENT:
        case tool_status_t::INVALID_COMMAND:
        case tool_status_t::INVALID_OPTION:
            return tool_exit_code_t::USAGE_ERROR;
        case tool_status_t::INVALID_PROJECT:
        case tool_status_t::INVALID_CONFIGURATION:
            return tool_exit_code_t::CONFIGURATION_ERROR;
        case tool_status_t::IO_ERROR:
        case tool_status_t::CACHE_ERROR:
            return tool_exit_code_t::INFRASTRUCTURE_ERROR;
        case tool_status_t::OUT_OF_MEMORY:
        case tool_status_t::INTERNAL_ERROR:
            return tool_exit_code_t::INTERNAL_ERROR;
        case tool_status_t::INVALID_STATE:
        case tool_status_t::VALIDATION_FAILED:
        case tool_status_t::OPERATION_FAILED:
        case tool_status_t::UNSUPPORTED:
        case tool_status_t::ALREADY_EXISTS:
        case tool_status_t::NOT_FOUND:
        case tool_status_t::CAPACITY_EXCEEDED:
            return tool_exit_code_t::OPERATION_FAILED;
    }
    return tool_exit_code_t::INTERNAL_ERROR;
}

} // namespace cypher::common
