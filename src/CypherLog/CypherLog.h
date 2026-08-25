//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherLog/CypherLog.h
//  Purpose: Declares the CypherLog Log module.
//  Details: This file participates in engine logging and formatted diagnostic output.
//           Keep it usable from early startup and failure paths without introducing
//           fragile dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_LOG_H
#define CYPHER_ENGINE_LOG_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherLog_Error.h"
#include "CypherLog_Types.h"

#include <cstdarg>     // va_list for log formatting.

namespace cypher::engine::log
{

/*
================
Log API

Structured logging with severity levels, channels and optional file output.
================
*/
log_error_t Log_Init( const config_t &config = {} );

void Log_Shutdown();

bool Log_IsInitialized();

const config_t &Log_GetConfig();

log_error_t Log_SetConfig( const config_t &config );

log_error_t Log_LevelFromString( const char *szLevelName, level_t &levelOut );

bool Log_LevelEnabled( const level_t logLevel, const channel_t channel );

bool Log_ChannelEnabled( const common::u32 nChannelMask, const channel_t channel );

void Log_Emit( const record_t &record );

void Log_Emitf( const level_t logLevel, const channel_t channel,
                const char *file, const char *function, const common::i32 line,
                const char *format, ... );

void Log_Emitfv( const level_t logLevel, const channel_t channel,
                 const char *file, const char *function, const common::i32 line,
                 const char *format, va_list args );

}

/*
================
Log Macros

Capture source file, function and line while keeping call sites compact.
================
*/
#define CYPHER_LOG_IF_ENABLED( LOG_LEVEL, LOG_CHANNEL )                                                           \
    if ( cypher::engine::log::Log_LevelEnabled( ( LOG_LEVEL ), ( LOG_CHANNEL ) ) )

#define CYPHER_LOG( LOG_LEVEL, LOG_CHANNEL, LOG_MESSAGE )                                                         \
    do {                                                                                                          \
        const cypher::engine::log::level_t cypher_log_level_ = ( LOG_LEVEL );                                     \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher_log_level_, cypher_log_channel_ ) ) {            \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher_log_level_, cypher_log_channel_, __FILE__, __func__, __LINE__, "%s", ( LOG_MESSAGE ) );    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOGF( LOG_LEVEL, LOG_CHANNEL, LOG_FORMAT, ... )                                                    \
    do {                                                                                                          \
        const cypher::engine::log::level_t cypher_log_level_ = ( LOG_LEVEL );                                     \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher_log_level_, cypher_log_channel_ ) ) {            \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher_log_level_, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT )              \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOGF_IF( LOG_CONDITION, LOG_LEVEL, LOG_CHANNEL, LOG_FORMAT, ... )                                  \
    do {                                                                                                          \
        const cypher::engine::log::level_t cypher_log_level_ = ( LOG_LEVEL );                                     \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( ( LOG_CONDITION ) && cypher::engine::log::Log_LevelEnabled( cypher_log_level_, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher_log_level_, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT )              \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_TRACE( LOG_CHANNEL, LOG_FORMAT, ... )                                                          \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::TRACE, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::TRACE, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_DEBUG( LOG_CHANNEL, LOG_FORMAT, ... )                                                          \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::DEBUG, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::DEBUG, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_INFO( LOG_CHANNEL, LOG_FORMAT, ... )                                                           \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::INFO, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::INFO, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_WARNING( LOG_CHANNEL, LOG_FORMAT, ... )                                                        \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::WARNING, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::WARNING, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_ERROR( LOG_CHANNEL, LOG_FORMAT, ... )                                                          \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::ERR, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::ERR, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_FATAL( LOG_CHANNEL, LOG_FORMAT, ... )                                                          \
    do {                                                                                                          \
        const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                               \
        if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::FATAL, cypher_log_channel_ ) ) { \
            cypher::engine::log::Log_Emitf(                                                                 \
                cypher::engine::log::level_t::FATAL, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT ) \
                __VA_OPT__( , ) __VA_ARGS__ );                                                                    \
        }                                                                                                         \
    } while ( false )

#define CYPHER_LOG_INFO_MESSAGE( LOG_CHANNEL, LOG_MESSAGE )       CYPHER_LOG( cypher::engine::log::level_t::INFO, ( LOG_CHANNEL ), ( LOG_MESSAGE ) )

#define CYPHER_LOG_WARNING_MESSAGE( LOG_CHANNEL, LOG_MESSAGE )    CYPHER_LOG( cypher::engine::log::level_t::WARNING, ( LOG_CHANNEL ), ( LOG_MESSAGE ) )

#define CYPHER_LOG_ERROR_MESSAGE( LOG_CHANNEL, LOG_MESSAGE )      CYPHER_LOG( cypher::engine::log::level_t::ERR, ( LOG_CHANNEL ), ( LOG_MESSAGE ) )

#define CYPHER_LOG_CHECK( CONDITION, LOG_CHANNEL, LOG_FORMAT, ... )                                                                   \
    do {                                                                                                                            \
        if ( !CONDITION ) {                                                                                                         \
            const cypher::engine::log::channel_t cypher_log_channel_ = ( LOG_CHANNEL );                                             \
            if ( cypher::engine::log::Log_LevelEnabled( cypher::engine::log::level_t::ERR, cypher_log_channel_ ) ) {          \
                cypher::engine::log::Log_Emitf(                                                                               \
                    cypher::engine::log::level_t::ERR, cypher_log_channel_, __FILE__, __func__, __LINE__, ( LOG_FORMAT )            \
                    __VA_OPT__( , ) __VA_ARGS__ );                                                                                  \
            }                                                                                                                       \
        }                                                                                                                           \
    } while ( false )

/*
================
Short Log Aliases

Preferred call-site names. The CYPHER_LOG_* macros remain the canonical backing
layer so older code and external users do not break while engine code stays compact.
================
*/
#define LOG_IF_ENABLED                  CYPHER_LOG_IF_ENABLED
#define LOG                             CYPHER_LOG
#define LOGF                            CYPHER_LOGF
#define LOGF_IF                         CYPHER_LOGF_IF

#define LOG_TRACE                       CYPHER_LOG_TRACE
#define LOG_DEBUG                       CYPHER_LOG_DEBUG
#define LOG_INFO                        CYPHER_LOG_INFO
#define LOG_WARNING                     CYPHER_LOG_WARNING
#define LOG_ERROR                       CYPHER_LOG_ERROR
#define LOG_FATAL                       CYPHER_LOG_FATAL

#define LOG_INFO_MESSAGE                CYPHER_LOG_INFO_MESSAGE
#define LOG_WARNING_MESSAGE             CYPHER_LOG_WARNING_MESSAGE
#define LOG_ERROR_MESSAGE               CYPHER_LOG_ERROR_MESSAGE
#define LOG_CHECK                       CYPHER_LOG_CHECK

/*
================
Additional aliases for proper fallback dependencies.

It is not advised to use global logging
================
*/



#endif // CYPHER_ENGINE_LOG_H
