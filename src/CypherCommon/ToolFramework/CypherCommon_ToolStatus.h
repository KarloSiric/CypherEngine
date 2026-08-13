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
    OK = 0u,
    CANCELLED,
    INVALID_ARGUMENT,
    INVALID_STATE,
    INVALID_COMMAND,
    INVALID_OPTION,
    INVALID_PROJECT,
    INVALID_CONFIGURATION,
    VALIDATION_FAILED,
    OPERATION_FAILED,
    IO_ERROR,
    CACHE_ERROR,
    OUT_OF_MEMORY,
    UNSUPPORTED,
    ALREADY_EXISTS,
    NOT_FOUND,
    CAPACITY_EXCEEDED,
    INTERNAL_ERROR
};

enum class tool_exit_code_t : i32 {
    SUCCESS = 0,
    OPERATION_FAILED = 1,
    USAGE_ERROR = 2,
    CONFIGURATION_ERROR = 3,
    INFRASTRUCTURE_ERROR = 4,
    INTERNAL_ERROR = 5,
    CANCELLED = 6
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
