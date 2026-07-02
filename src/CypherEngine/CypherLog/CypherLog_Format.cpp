//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherLog/CypherLog_Format.cpp
//  Purpose: Implements the CypherLog Log Format module.
//  Details: This file participates in engine logging and formatted diagnostic output.
//           Keep it usable from early startup and failure paths without introducing
//           fragile dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-06-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherLog_Format.h"
#include "CypherSystem_Platform.h"

#include <cstdio>      // std::snprintf.
#include <ctime>       // std::strftime.

namespace cypher::engine::log
{

/*
================
CypherLog_LevelColor

Returns ANSI terminal color for compact terminal output. File sinks keep colors
disabled so escape sequences do not pollute log files.
================
*/
const char *CypherLog_LevelColor( const level_t level )
{
    switch ( level ) {
        case level_t::TRACE:   return "\033[90m";
        case level_t::DEBUG:   return "\033[36m";
        case level_t::INFO:    return "\033[37m";
        case level_t::WARNING: return "\033[33m";
        case level_t::ERR:     return "\033[31m";
        case level_t::FATAL:   return "\033[35m";
        default:               return "";
    }
}

/*
================
CypherLog_FormatTimestamp
================
*/
bool CypherLog_FormatTimestamp( const record_t &record, char *bufferOut, const common::usize nOutBufferSize )
{
    if ( bufferOut == nullptr || nOutBufferSize == 0u || record.timestamp == std::time_t{} ) {
        return false;
    }

    std::tm tmValue{};

    if ( !sys::CypherSystem_LocalTime( record.timestamp, tmValue ) ) {
        return false;
    }

    return std::strftime( bufferOut, nOutBufferSize, "%H:%M:%S", &tmValue ) > 0u;
}

/*
================
CypherLog_FormatCompact
================
*/
log_error_t CypherLog_FormatCompact(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    char *bufferOut,
    const common::usize nOutBufferSize )
{
    const char *colorBegin = pSinkConfig.bColorEnabled ? CypherLog_LevelColor( record.level ) : "";
    const char *colorEnd = pSinkConfig.bColorEnabled ? "\033[0m" : "";

    const int written = std::snprintf(
        bufferOut,
        nOutBufferSize,
        "%s[%s][%s] %s%s\n",
        colorBegin,
        CypherLog_LevelName( record.level ),
        CypherLog_ChannelName( record.channel ),
        record.message,
        colorEnd
    );

    return ( written < 0 ) ? log_error_t::ERR_FORMAT_FAILED : log_error_t::OK;
}

/*
================
CypherLog_FormatDetailed
================
*/
log_error_t CypherLog_FormatDetailed(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    char *bufferOut,
    const common::usize nOutBufferSize )
{
    char pTimestampBuffer[32]{};

    if ( pSinkConfig.bIncludeTimestamps ) {
        CypherLog_FormatTimestamp( record, pTimestampBuffer, sizeof( pTimestampBuffer ) );
    }

    const bool bHasSourceLocation = record.file != nullptr && record.file[0] != '\0';
    const bool bHasFunctionName = record.function != nullptr && record.function[0] != '\0';
    const bool bIncludeSourceLocation = pSinkConfig.bIncludeSourceLocation && bHasSourceLocation;
    const bool bIncludeFunctionName = pSinkConfig.bIncludeFunctionName && bHasFunctionName;

    const char *pszSourceFile = bHasSourceLocation ? record.file : "";

    if ( bIncludeSourceLocation && config.szSourcePath == source_path_mode_t::BASENAME ) {
        pszSourceFile = sys::CypherSystem_PathBasename( pszSourceFile );
    }

    const char *szFunctionName = bHasFunctionName ? record.function : "";

    int written = 0;

    if ( pTimestampBuffer[0] != '\0' && bIncludeSourceLocation && bIncludeFunctionName ) {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s][%s] %s:%d (%s) %s\n",
            pTimestampBuffer,
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            pszSourceFile,
            record.line,
            szFunctionName,
            record.message
        );
    } else if ( pTimestampBuffer[0] != '\0' && bIncludeSourceLocation ) {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s][%s] %s:%d %s\n",
            pTimestampBuffer,
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            pszSourceFile,
            record.line,
            record.message
        );
    } else if ( pTimestampBuffer[0] != '\0' ) {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s][%s] %s\n",
            pTimestampBuffer,
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            record.message
        );
    } else if ( bIncludeSourceLocation && bIncludeFunctionName ) {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s] %s:%d (%s) %s\n",
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            pszSourceFile,
            record.line,
            szFunctionName,
            record.message
        );
    } else if ( bIncludeSourceLocation ) {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s] %s:%d %s\n",
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            pszSourceFile,
            record.line,
            record.message
        );
    } else {
        written = std::snprintf(
            bufferOut,
            nOutBufferSize,
            "[%s][%s] %s\n",
            CypherLog_LevelName( record.level ),
            CypherLog_ChannelName( record.channel ),
            record.message
        );
    }

    return ( written < 0 ) ? log_error_t::ERR_FORMAT_FAILED : log_error_t::OK;
}

/*
================
CypherLog_FormatRecord
================
*/
log_error_t CypherLog_FormatRecord(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    char *bufferOut,
    const common::usize nOutBufferSize )
{
    if ( bufferOut == nullptr || nOutBufferSize == 0u ) {
        return log_error_t::ERR_FORMAT_FAILED;
    }

    bufferOut[0] = '\0';

    if ( record.message[0] == '\0' ) {
        return log_error_t::ERR_FORMAT_FAILED;
    }

    switch ( pSinkConfig.format ) {
        case format_mode_t::COMPACT:
            return CypherLog_FormatCompact( record, pSinkConfig, bufferOut, nOutBufferSize );
        case format_mode_t::DETAILED:
            return CypherLog_FormatDetailed( record, pSinkConfig, config, bufferOut, nOutBufferSize );
        default:
            return log_error_t::ERR_FORMAT_FAILED;
    }
}

}       // namespace cypher::engine::log
