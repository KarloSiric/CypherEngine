//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDiagnostic.h
//  Purpose: Declares structured diagnostics emitted by Cypher tools.
//  Details: Diagnostics preserve machine-readable codes and source spans while
//           borrowing text that remains valid only during synchronous delivery.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLDIAGNOSTIC_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLDIAGNOSTIC_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class tool_diagnostic_severity_t : u8 {
    NOTE = 0u,
    WARNING,
    ERROR,
    FATAL
};

enum class tool_diagnostic_category_t : u8 {
    GENERAL = 0u,
    COMMAND_LINE,
    PROJECT,
    SOURCE,
    SCHEMA,
    COMPILER,
    VALIDATION,
    FILESYSTEM,
    CACHE,
    PACKAGE,
    INTERNAL
};

enum tool_diagnostic_flags_t : flags32_t {
    TOOL_DIAGNOSTIC_FLAG_NONE = 0u,
    TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE = CYPHER_BIT32( 0 ),
    TOOL_DIAGNOSTIC_FLAG_HAS_HINT = CYPHER_BIT32( 1 ),
    TOOL_DIAGNOSTIC_FLAG_TRANSIENT = CYPHER_BIT32( 2 )
};

struct tool_source_span_t {
    string_view_t path{};
    u32 nLine{ 0u };
    u32 nColumn{ 0u };
    u32 nEndLine{ 0u };
    u32 nEndColumn{ 0u };
};

struct tool_diagnostic_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_diagnostic_code_t code{ CY_TOOL_DIAGNOSTIC_NONE };
    tool_diagnostic_severity_t severity{ tool_diagnostic_severity_t::ERROR };
    tool_diagnostic_category_t category{ tool_diagnostic_category_t::GENERAL };
    tool_source_span_t source{};
    string_view_t message{};
    string_view_t hint{};
    flags32_t flags{ TOOL_DIAGNOSTIC_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolDiagnostic_Validate(
    const tool_diagnostic_t &diagnostic ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolDiagnostic_SeverityName(
    tool_diagnostic_severity_t severity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolDiagnostic_CategoryName(
    tool_diagnostic_category_t category ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLDIAGNOSTIC_H
