//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDiagnostic.cpp
//  Purpose: Implements validation and naming for structured tool diagnostics.
//  Details: Source spans use one-based line and column coordinates; an empty span
//           remains valid only when the has-source flag is not present.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Diagnostic Implementation Notes

Diagnostics carry severity, stable code, source location, and message as structured data. CLI
and GUI hosts decide presentation without changing compiler behavior.
================
*/

#include "CypherCommon_ToolDiagnostic.h"

namespace cypher::common
{

tool_status_t ToolDiagnostic_Validate(
    const tool_diagnostic_t &diagnostic ) noexcept
{
    // Flags explicitly state which optional payloads are meaningful.
    constexpr flags32_t knownFlags =
        TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE |
        TOOL_DIAGNOSTIC_FLAG_HAS_HINT |
        TOOL_DIAGNOSTIC_FLAG_TRANSIENT;

    if ( diagnostic.severity > tool_diagnostic_severity_t::FATAL ||
         diagnostic.category > tool_diagnostic_category_t::INTERNAL ||
         !StringView_IsValid( diagnostic.message ) ||
         diagnostic.message.cchLength == 0u ||
         !StringView_IsValid( diagnostic.hint ) ||
         !StringView_IsValid( diagnostic.source.path ) ||
         ( diagnostic.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Source coordinates are one-based. End coordinates are optional, but must
    // be supplied as a complete ordered pair when present.
    const bool_t bHasSource =
        ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_SOURCE ) != 0u;
    if ( bHasSource ) {
        if ( diagnostic.source.path.cchLength == 0u ||
             diagnostic.source.nLine == 0u ||
             diagnostic.source.nColumn == 0u ) {
            return tool_status_t::INVALID_ARGUMENT;
        }

        const bool_t bHasEnd = diagnostic.source.nEndLine != 0u ||
                               diagnostic.source.nEndColumn != 0u;
        if ( bHasEnd &&
             ( diagnostic.source.nEndLine == 0u ||
               diagnostic.source.nEndColumn == 0u ||
               diagnostic.source.nEndLine < diagnostic.source.nLine ||
               ( diagnostic.source.nEndLine == diagnostic.source.nLine &&
                 diagnostic.source.nEndColumn < diagnostic.source.nColumn ) ) ) {
            return tool_status_t::INVALID_ARGUMENT;
        }
    } else if ( diagnostic.source.path.cchLength != 0u ||
                diagnostic.source.nLine != 0u ||
                diagnostic.source.nColumn != 0u ||
                diagnostic.source.nEndLine != 0u ||
                diagnostic.source.nEndColumn != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Keep the hint flag and payload synchronized for binary and JSON writers.
    const bool_t bHasHint =
        ( diagnostic.flags & TOOL_DIAGNOSTIC_FLAG_HAS_HINT ) != 0u;
    if ( bHasHint != ( diagnostic.hint.cchLength != 0u ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    return tool_status_t::OK;
}

const char *ToolDiagnostic_SeverityName(
    tool_diagnostic_severity_t severity ) noexcept
{
    // Names are part of the stable diagnostic serialization contract.
    switch ( severity ) {
        case tool_diagnostic_severity_t::NOTE: return "note";
        case tool_diagnostic_severity_t::WARNING: return "warning";
        case tool_diagnostic_severity_t::ERROR: return "error";
        case tool_diagnostic_severity_t::FATAL: return "fatal";
    }
    return "unknown";
}

const char *ToolDiagnostic_CategoryName(
    tool_diagnostic_category_t category ) noexcept
{
    switch ( category ) {
        case tool_diagnostic_category_t::GENERAL: return "general";
        case tool_diagnostic_category_t::COMMAND_LINE: return "command-line";
        case tool_diagnostic_category_t::PROJECT: return "project";
        case tool_diagnostic_category_t::SOURCE: return "source";
        case tool_diagnostic_category_t::SCHEMA: return "schema";
        case tool_diagnostic_category_t::COMPILER: return "compiler";
        case tool_diagnostic_category_t::VALIDATION: return "validation";
        case tool_diagnostic_category_t::FILESYSTEM: return "filesystem";
        case tool_diagnostic_category_t::CACHE: return "cache";
        case tool_diagnostic_category_t::PACKAGE: return "package";
        case tool_diagnostic_category_t::INTERNAL: return "internal";
    }
    return "unknown";
}

} // namespace cypher::common
