//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolReportWriter.cpp
//  Purpose: Implements deterministic text and JSON tool-report output.
//  Details: Fixed field ordering keeps reports easy to diff and lets later
//           consumers evolve through an explicit schema instead of UI parsing.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolReportWriter.h"

#include "CypherCommon_StringBuilder.h"

namespace cypher::common
{
namespace
{

constexpr usize CY_TOOL_REPORT_STACK_CAPACITY = 4096u;

bool_t OptionsAreValid( const tool_report_write_options_t &options ) noexcept
{
    return options.format <= tool_output_format_t::JSON;
}

void AppendTextReport(
    string_builder_t *pBuilder,
    const tool_report_t &report ) noexcept
{
    const f64 elapsedMs =
        Cy_TimerTicksToMilliseconds( ToolReport_ElapsedTicks( report ) );
    (void)StringBuilder_AppendFormat(
        pBuilder,
        "status: %s\n"
        "operation: %llu\n"
        "elapsed_ms: %.3f\n"
        "inputs: discovered=%llu processed=%llu succeeded=%llu failed=%llu skipped=%llu\n"
        "cache: hits=%llu misses=%llu\n"
        "diagnostics: warnings=%llu errors=%llu\n"
        "artifacts: %llu\n"
        "io: read=%llu written=%llu",
        ToolStatus_Name( report.status ),
        static_cast<unsigned long long>( report.operationId ),
        elapsedMs,
        static_cast<unsigned long long>( report.nInputsDiscovered ),
        static_cast<unsigned long long>( report.nInputsProcessed ),
        static_cast<unsigned long long>( report.nSucceeded ),
        static_cast<unsigned long long>( report.nFailed ),
        static_cast<unsigned long long>( report.nSkipped ),
        static_cast<unsigned long long>( report.nCacheHits ),
        static_cast<unsigned long long>( report.nCacheMisses ),
        static_cast<unsigned long long>( report.nWarnings ),
        static_cast<unsigned long long>( report.nErrors ),
        static_cast<unsigned long long>( report.nArtifacts ),
        static_cast<unsigned long long>( report.cbRead ),
        static_cast<unsigned long long>( report.cbWritten ) );
}

void AppendJsonReport(
    string_builder_t *pBuilder,
    const tool_report_t &report,
    bool_t bPretty ) noexcept
{
    const char *pNewline = bPretty ? "\n" : "";
    const char *pIndent = bPretty ? "  " : "";
    const char *pSpace = bPretty ? " " : "";
    const f64 elapsedSeconds = ToolReport_ElapsedSeconds( report );
    (void)StringBuilder_AppendFormat(
        pBuilder,
        "{%s"
        "%s\"schema\":%s\"cypher.tool-report.v1\",%s"
        "%s\"operation_id\":%s%llu,%s"
        "%s\"status\":%s\"%s\",%s"
        "%s\"start_ticks\":%s%llu,%s"
        "%s\"end_ticks\":%s%llu,%s"
        "%s\"elapsed_seconds\":%s%.9f,%s"
        "%s\"inputs_discovered\":%s%llu,%s"
        "%s\"inputs_processed\":%s%llu,%s"
        "%s\"succeeded\":%s%llu,%s"
        "%s\"failed\":%s%llu,%s"
        "%s\"skipped\":%s%llu,%s"
        "%s\"cache_hits\":%s%llu,%s"
        "%s\"cache_misses\":%s%llu,%s"
        "%s\"warnings\":%s%llu,%s"
        "%s\"errors\":%s%llu,%s"
        "%s\"artifacts\":%s%llu,%s"
        "%s\"bytes_read\":%s%llu,%s"
        "%s\"bytes_written\":%s%llu%s"
        "}",
        pNewline,
        pIndent, pSpace, pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.operationId ), pNewline,
        pIndent, pSpace, ToolStatus_Name( report.status ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nStartTicks ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nEndTicks ), pNewline,
        pIndent, pSpace, elapsedSeconds, pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nInputsDiscovered ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nInputsProcessed ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nSucceeded ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nFailed ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nSkipped ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nCacheHits ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nCacheMisses ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nWarnings ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nErrors ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.nArtifacts ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.cbRead ), pNewline,
        pIndent, pSpace, static_cast<unsigned long long>( report.cbWritten ), pNewline );
}

tool_report_write_result_t BuilderResult(
    const string_builder_t &builder ) noexcept
{
    switch ( builder.status ) {
        case string_builder_status_t::OK:
            return { tool_status_t::OK, builder.cchLength, builder.cchRequired };
        case string_builder_status_t::OUTPUT_TRUNCATED:
            return {
                tool_status_t::CAPACITY_EXCEEDED,
                builder.cchLength,
                builder.cchRequired
            };
        case string_builder_status_t::INVALID_ARGUMENT:
            return { tool_status_t::INVALID_ARGUMENT, 0u, builder.cchRequired };
        case string_builder_status_t::FORMAT_ERROR:
            return { tool_status_t::INTERNAL_ERROR, 0u, builder.cchRequired };
    }
    return { tool_status_t::INTERNAL_ERROR, 0u, 0u };
}

} // namespace

tool_report_write_result_t ToolReportWriter_Write(
    const tool_report_t &report,
    const tool_report_write_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( ToolStatus_Failed( ToolReport_Validate( report ) ) ||
         !OptionsAreValid( options ) ||
         ( cchDest != 0u && pDest == nullptr ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    string_builder_t builder{};
    if ( !StringBuilder_Init( &builder, pDest, cchDest ) ) {
        return { tool_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    if ( options.format == tool_output_format_t::JSON ) {
        AppendJsonReport( &builder, report, options.bPretty );
    } else {
        AppendTextReport( &builder, report );
    }
    if ( options.bFinalNewline ) {
        (void)StringBuilder_AppendChar( &builder, '\n' );
    }
    return BuilderResult( builder );
}

tool_status_t ToolReportWriter_WriteToSink(
    const tool_report_t &report,
    const tool_report_write_options_t &options,
    const tool_text_sink_t &sink ) noexcept
{
    if ( ToolStatus_Failed( ToolOutput_ValidateSink( sink ) ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    char buffer[CY_TOOL_REPORT_STACK_CAPACITY]{};
    const tool_report_write_result_t result = ToolReportWriter_Write(
        report,
        options,
        buffer,
        CY_TOOL_REPORT_STACK_CAPACITY );
    if ( ToolStatus_Failed( result.status ) ) {
        return result.status;
    }
    return ToolOutput_WriteText(
        sink,
        { buffer, result.cchWritten } );
}

} // namespace cypher::common
