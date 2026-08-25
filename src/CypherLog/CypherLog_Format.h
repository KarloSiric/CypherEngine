//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherLog/CypherLog_Format.h
//  Purpose: Declares the CypherLog Log Format module.
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

#ifndef CYPHER_ENGINE_LOG_FORMAT_H
#define CYPHER_ENGINE_LOG_FORMAT_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherLog_Error.h"
#include "CypherLog_Types.h"

namespace cypher::engine::log
{

/*
================
Formatting functions

Used for formatting properly all of the logging system
================
*/
log_error_t Log_FormatRecord(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    char *bufferOut,
    common::usize nOutBufferSize );

log_error_t Log_FormatDetailed(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    char *bufferOut,
    const common::usize nOutBufferSize );

log_error_t Log_FormatCompact(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    char *bufferOut,
    const common::usize nOutBufferSize );

bool Log_FormatTimestamp( const record_t &record, char *bufferOut, const common::usize nOutBufferSize );

const char *Log_LevelColor( const level_t level );

}       // namespace cypher::engine::log

#endif // CYPHER_ENGINE_LOG_FORMAT_H
