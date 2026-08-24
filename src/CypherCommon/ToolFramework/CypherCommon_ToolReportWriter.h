//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolReportWriter.h
//  Purpose: Declares stable text and JSON serialization for tool reports.
//  Details: Report serialization is host-neutral so CLI, Mason, CI, and tests
//           produce the same field names and counter interpretation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Report Writer Contract

Reports summarize timings, counts, diagnostics, dependencies, and artifacts after a run. Writers
serialize the same data for human or machine consumption without recomputing results.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORTWRITER_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORTWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolOutput.h"
#include "CypherCommon_ToolReport.h"

namespace cypher::common
{

struct tool_report_write_options_t {
    tool_output_format_t format{ tool_output_format_t::TEXT }; // Selected serialization.
    bool_t bPretty{ CY_TRUE };        // Add human-friendly whitespace where supported.
    bool_t bFinalNewline{ CY_TRUE };  // Terminate the complete record with '\n'.
};

struct tool_report_write_result_t {
    tool_status_t status{ tool_status_t::OK }; // Serialization/capacity result.
    usize cchWritten{ 0u };  // Characters initialized in destination storage.
    usize cchRequired{ 0u }; // Characters required excluding the terminator.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_report_write_result_t ToolReportWriter_Write(
    const tool_report_t &report,
    const tool_report_write_options_t &options,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolReportWriter_WriteToSink(
    const tool_report_t &report,
    const tool_report_write_options_t &options,
    const tool_text_sink_t &sink ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLREPORTWRITER_H
