//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolStatus.h
//  Purpose: Declares stable tool operation statuses and process exit classes.
//  Details: Library operations return tool statuses. Command-line frontends map
//           those statuses to documented process exit codes through this API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Status Contract

These are stable tool-neutral contracts shared by CLI applications, future GUI hosts, tests, and
compiler modules. They must not depend on Qt or terminal state.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLSTATUS_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLSTATUS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum class tool_status_t : u8 {
    OK = 0u,              // Operation completed successfully.
    CANCELLED,            // Cooperative cancellation was observed.
    INVALID_ARGUMENT,     // Direct API argument violates its contract.
    INVALID_STATE,        // Object state cannot perform the requested operation.
    INVALID_COMMAND,      // Command name or positional usage is invalid.
    INVALID_OPTION,       // Option name or typed value is invalid.
    INVALID_PROJECT,      // Project description cannot be consumed.
    INVALID_CONFIGURATION,// Tool configuration is contradictory or incomplete.
    VALIDATION_FAILED,    // Authored input is structurally or semantically invalid.
    OPERATION_FAILED,     // Tool-specific work failed after valid setup.
    IO_ERROR,             // Filesystem, stream, or transport operation failed.
    CACHE_ERROR,          // Cache lookup, validation, or publication failed.
    OUT_OF_MEMORY,        // Required storage could not be acquired.
    UNSUPPORTED,          // Valid request is not implemented by this module.
    ALREADY_EXISTS,       // Unique registration or destination already exists.
    NOT_FOUND,            // Requested compiler, resource, or path was absent.
    CAPACITY_EXCEEDED,    // Caller-owned bounded storage is full.
    INTERNAL_ERROR        // Invariant or callback contract was violated.
};

enum class tool_exit_code_t : i32 {
    SUCCESS = 0,              // Successful process termination.
    OPERATION_FAILED = 1,     // Input validation or compilation failed.
    USAGE_ERROR = 2,          // Command-line usage was invalid.
    CONFIGURATION_ERROR = 3,  // Project or tool configuration was invalid.
    INFRASTRUCTURE_ERROR = 4, // I/O, cache, or allocation failure.
    INTERNAL_ERROR = 5,       // Tool invariant or callback contract failed.
    CANCELLED = 6             // User or host cancelled the process.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolStatus_IsKnown( tool_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolStatus_Succeeded( tool_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolStatus_Failed( tool_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolStatus_Name( tool_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_exit_code_t ToolStatus_ExitCode( tool_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLSTATUS_H
