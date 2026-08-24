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

/*
================
Tool Diagnostic Contract

Diagnostics carry severity, stable code, source location, and message as structured data. CLI
and GUI hosts decide presentation without changing compiler behavior.
================
*/

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
    NOTE = 0u, // Informational context that does not affect success.
    WARNING,   // Suspicious input or fallback that permits completion.
    ERROR,     // Current input or operation failed.
    FATAL      // Tool cannot continue processing further input.
};

enum class tool_diagnostic_category_t : u8 {
    GENERAL = 0u, // No narrower shared category applies.
    COMMAND_LINE, // Argument parsing or CLI policy.
    PROJECT,      // Project manifest or workspace configuration.
    SOURCE,       // Authored source text or binary input.
    SCHEMA,       // Structural schema validation.
    COMPILER,     // Resource compiler implementation.
    VALIDATION,   // Semantic validation after parsing.
    FILESYSTEM,   // Path, VFS, or native I/O operation.
    CACHE,        // Cache lookup or publication.
    PACKAGE,      // Package/archive construction or inspection.
    INTERNAL      // Invariant or host callback contract.
};

enum tool_diagnostic_flags_t : flags32_t {
    TOOL_DIAGNOSTIC_FLAG_NONE = 0u,                   // No optional fields are active.
    TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE = CYPHER_BIT32( 0 ), // source contains a valid span.
    TOOL_DIAGNOSTIC_FLAG_HAS_HINT = CYPHER_BIT32( 1 ),   // hint contains remediation text.
    TOOL_DIAGNOSTIC_FLAG_TRANSIENT = CYPHER_BIT32( 2 )   // Host may replace rather than retain it.
};

struct tool_source_span_t {
    string_view_t path{}; // Source identity displayed by the host.
    u32 nLine{ 0u };      // One-based start line.
    u32 nColumn{ 0u };    // One-based start column.
    u32 nEndLine{ 0u };   // Optional one-based inclusive end line.
    u32 nEndColumn{ 0u }; // Optional one-based inclusive end column.
};

struct tool_diagnostic_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID }; // Record owner.
    tool_diagnostic_code_t code{ CY_TOOL_DIAGNOSTIC_NONE }; // Stable machine code.
    tool_diagnostic_severity_t severity{ tool_diagnostic_severity_t::ERROR }; // Impact.
    tool_diagnostic_category_t category{ tool_diagnostic_category_t::GENERAL }; // Origin.
    tool_source_span_t source{}; // Optional source location selected by flags.
    string_view_t message{};     // Required human-readable description.
    string_view_t hint{};        // Optional remediation selected by flags.
    flags32_t flags{ TOOL_DIAGNOSTIC_FLAG_NONE }; // tool_diagnostic_flags_t bitset.
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
