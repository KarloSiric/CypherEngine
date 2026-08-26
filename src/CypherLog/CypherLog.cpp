//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherLog/CypherLog.cpp
//  Purpose: Implements the CypherLog Log module.
//  Details: This file participates in engine logging and formatted diagnostic output.
//           Keep it usable from early startup and failure paths without introducing
//           fragile dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-04-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherLog.h"
#include "CypherLog_Format.h"

#include <cctype>      // std::tolower for level parsing.
#include <cstdio>      // FILE and stdio logging sinks.
#include <ctime>       // std::time.
#include <cstring>     // std::strcmp / std::strncmp for level and sink config checks.
#include <mutex>       // std::mutex and std::lock_guard serialize logger state and sink access.

namespace cypher::engine::log
{

/*
================
Log Runtime State

Owns active configuration and concrete file handles for enabled file sinks.
================
*/
struct runtime_state_t {
    config_t config{};
    bool initialized{ false };

    std::FILE *pEngineFileHandle{ nullptr };
    std::FILE *pErrorFileHandle{ nullptr };
    std::FILE *pConsoleFileHandle{ nullptr };
    std::FILE *pEditorFileHandle{ nullptr };
    std::FILE *pGameFileHandle{ nullptr };

    bool bFileErrorReported{ false };
};

static runtime_state_t s_LogRuntimeState;

// A prepared sink either borrows the active handle or owns a newly opened one.
// Ownership transfers to runtime_state_t only after every requested sink opens.
struct prepared_sink_t {
    std::FILE *pFileHandle{ nullptr };
    bool bOwnsFileHandle{ false };
};

struct prepared_file_sinks_t {
    prepared_sink_t engineFile{};
    prepared_sink_t errorFile{};
    prepared_sink_t consoleFile{};
    prepared_sink_t editorFile{};
    prepared_sink_t gameFile{};
};

/*
================
Log Runtime Lock

Guards configuration, initialization state, sink handles and writes. Keeping
the lock outside runtime_state_t allows the remaining state to be reset as a
single value during initialization and shutdown.
================
*/
static std::mutex s_LogRuntimeMutex;

}

namespace cypher::engine::log
{

/*
================
Log_FileOpenMode
================
*/
const char *Log_FileOpenMode( const file_mode_t bFileMode )
{
    return ( bFileMode == file_mode_t::APPEND ) ? "a" : "w";
}

/*
================
Log_CloseFile
================
*/
void Log_CloseFile( std::FILE *&pFileHandle )
{
    if ( pFileHandle == nullptr ) {
        return;
    }
    std::fflush( pFileHandle );
    std::fclose( pFileHandle );
    pFileHandle = nullptr;
}

/*
================
Log_CloseFileSinks
================
*/
void Log_CloseFileSinks( runtime_state_t &pRuntimeState )
{
    Log_CloseFile( pRuntimeState.pEngineFileHandle );
    Log_CloseFile( pRuntimeState.pErrorFileHandle );
    Log_CloseFile( pRuntimeState.pConsoleFileHandle );
    Log_CloseFile( pRuntimeState.pEditorFileHandle );
    Log_CloseFile( pRuntimeState.pGameFileHandle );
}

/*
================
Log_OpenFileSink
================
*/
log_error_t Log_OpenFileSink( const sink_config_t &pSinkConfig, std::FILE *&pFileHandle )
{
    pFileHandle = nullptr;
    if ( !pSinkConfig.enabled ) {
        return log_error_t::OK;
    }
    if ( pSinkConfig.path[0] == '\0' ) {
        return log_error_t::ERR_INVALID_CONFIG;
    }
    pFileHandle = std::fopen( pSinkConfig.path, Log_FileOpenMode( pSinkConfig.file ) );
    if ( pFileHandle == nullptr ) {
        return log_error_t::ERR_FILE_OPEN_FAILED;
    }
    return log_error_t::OK;
}

/*
================
Log_FileSinkSameTarget
================
*/
bool Log_FileSinkSameTarget( const sink_config_t &oldConfig, const sink_config_t &newConfig )
{
    if ( oldConfig.enabled != newConfig.enabled ) {
        return false;
    }

    if ( oldConfig.file != newConfig.file ) {
        return false;
    }

    return std::strncmp( oldConfig.path, newConfig.path, CYPHER_LOG_FILE_PATH_MAX ) == 0;
}

/*
================
Log_PrepareFileSink

Opens a replacement without disturbing the active handle. Unchanged targets
borrow their existing handle so ordinary cvar updates do not truncate log files.
================
*/
log_error_t Log_PrepareFileSink(
    const sink_config_t &activeConfig,
    const sink_config_t &requestedConfig,
    std::FILE *pActiveFileHandle,
    prepared_sink_t &preparedSink )
{
    preparedSink = {};

    if ( !requestedConfig.enabled ) {
        return log_error_t::OK;
    }

    if ( pActiveFileHandle != nullptr && Log_FileSinkSameTarget( activeConfig, requestedConfig ) ) {
        preparedSink.pFileHandle = pActiveFileHandle;
        return log_error_t::OK;
    }

    const log_error_t openResult = Log_OpenFileSink( requestedConfig, preparedSink.pFileHandle );
    preparedSink.bOwnsFileHandle = openResult == log_error_t::OK;
    return openResult;
}

/*
================
Log_ClosePreparedFileSink
================
*/
void Log_ClosePreparedFileSink( prepared_sink_t &preparedSink )
{
    if ( preparedSink.bOwnsFileHandle ) {
        Log_CloseFile( preparedSink.pFileHandle );
    }

    preparedSink = {};
}

/*
================
Log_ClosePreparedFileSinks
================
*/
void Log_ClosePreparedFileSinks( prepared_file_sinks_t &preparedSinks )
{
    Log_ClosePreparedFileSink( preparedSinks.engineFile );
    Log_ClosePreparedFileSink( preparedSinks.errorFile );
    Log_ClosePreparedFileSink( preparedSinks.consoleFile );
    Log_ClosePreparedFileSink( preparedSinks.editorFile );
    Log_ClosePreparedFileSink( preparedSinks.gameFile );
}

/*
================
Log_PrepareFileSinks

Builds the complete candidate sink set. A failed candidate closes only handles
opened during this attempt; active runtime handles remain untouched.
================
*/
log_error_t Log_PrepareFileSinks(
    const runtime_state_t &activeState,
    const config_t &requestedConfig,
    prepared_file_sinks_t &preparedSinks )
{
    preparedSinks = {};

    log_error_t result = Log_PrepareFileSink(
        activeState.config.engineFile,
        requestedConfig.engineFile,
        activeState.pEngineFileHandle,
        preparedSinks.engineFile );
    if ( result != log_error_t::OK ) {
        return result;
    }

    result = Log_PrepareFileSink(
        activeState.config.errorFile,
        requestedConfig.errorFile,
        activeState.pErrorFileHandle,
        preparedSinks.errorFile );
    if ( result != log_error_t::OK ) {
        Log_ClosePreparedFileSinks( preparedSinks );
        return result;
    }

    result = Log_PrepareFileSink(
        activeState.config.consoleFile,
        requestedConfig.consoleFile,
        activeState.pConsoleFileHandle,
        preparedSinks.consoleFile );
    if ( result != log_error_t::OK ) {
        Log_ClosePreparedFileSinks( preparedSinks );
        return result;
    }

    result = Log_PrepareFileSink(
        activeState.config.editorFile,
        requestedConfig.editorFile,
        activeState.pEditorFileHandle,
        preparedSinks.editorFile );
    if ( result != log_error_t::OK ) {
        Log_ClosePreparedFileSinks( preparedSinks );
        return result;
    }

    result = Log_PrepareFileSink(
        activeState.config.gameFile,
        requestedConfig.gameFile,
        activeState.pGameFileHandle,
        preparedSinks.gameFile );
    if ( result != log_error_t::OK ) {
        Log_ClosePreparedFileSinks( preparedSinks );
        return result;
    }

    return log_error_t::OK;
}

/*
================
Log_CommitPreparedFileSink

Releases a superseded active handle and transfers the prepared handle into the
runtime state. Borrowed handles compare equal and therefore remain open.
================
*/
void Log_CommitPreparedFileSink( std::FILE *&pActiveFileHandle, prepared_sink_t &preparedSink )
{
    if ( pActiveFileHandle != preparedSink.pFileHandle ) {
        Log_CloseFile( pActiveFileHandle );
    }

    pActiveFileHandle = preparedSink.pFileHandle;
    preparedSink = {};
}

/*
================
Log_CommitPreparedFileSinks
================
*/
void Log_CommitPreparedFileSinks( runtime_state_t &activeState, prepared_file_sinks_t &preparedSinks )
{
    Log_CommitPreparedFileSink( activeState.pEngineFileHandle, preparedSinks.engineFile );
    Log_CommitPreparedFileSink( activeState.pErrorFileHandle, preparedSinks.errorFile );
    Log_CommitPreparedFileSink( activeState.pConsoleFileHandle, preparedSinks.consoleFile );
    Log_CommitPreparedFileSink( activeState.pEditorFileHandle, preparedSinks.editorFile );
    Log_CommitPreparedFileSink( activeState.pGameFileHandle, preparedSinks.gameFile );
}

/*
================
Log_ShouldFlush
================
*/
bool Log_ShouldFlush( const flush_policy_t flushPolicy, const level_t level )
{
    switch ( flushPolicy ) {
        case flush_policy_t::NEVER:
            return false;
        case flush_policy_t::ERRORS_AND_ABOVE:
            return Log_LevelPasses( level, level_t::ERR );
        case flush_policy_t::EVERY_MESSAGE:
            return true;
        default:
            return false;
    }
}

/*
================
Log_SinkAcceptsRecord
================
*/
bool Log_SinkAcceptsRecord( const record_t &record, const sink_config_t &pSinkConfig )
{
    if ( !pSinkConfig.enabled ) {
        return false;
    }

    return Log_LevelPasses( record.level, pSinkConfig.nMinLevel );
}

/*
================
Log_TerminalStreamForLevel
================
*/
std::FILE *Log_TerminalStreamForLevel( const level_t level )
{
    return Log_LevelPasses( level, level_t::WARNING ) ? stderr : stdout;
}

/*
================
Log_WriteFormattedRecord
================
*/
void Log_WriteFormattedRecord(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    std::FILE *pFileHandle )
{
    if ( pFileHandle == nullptr ) {
        return;
    }

    char szLineBuffer[CYPHER_LOG_MESSAGE_MAX + 512u]{};
    const log_error_t formatResult = Log_FormatRecord(
        record,
        pSinkConfig,
        config,
        szLineBuffer,
        sizeof( szLineBuffer )
    );

    if ( formatResult != log_error_t::OK ) {
        if ( !s_LogRuntimeState.bFileErrorReported ) {
            std::fputs( "[ERROR][LOG] failed formatting log record.\n", stderr );
            s_LogRuntimeState.bFileErrorReported = true;
        }

        return;
    }

    if ( std::fputs( szLineBuffer, pFileHandle ) < 0 ) {
        if ( !s_LogRuntimeState.bFileErrorReported ) {
            std::fputs( "[ERROR][LOG] failed writing log record.\n", stderr );
            s_LogRuntimeState.bFileErrorReported = true;
        }

        return;
    }

    if ( Log_ShouldFlush( pSinkConfig.flush, record.level ) ) {
        std::fflush( pFileHandle );
    }
}

/*
================
Log_Init

Installs active logging configuration and opens enabled file sinks.
================
*/
log_error_t Log_Init( const config_t &config )
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );

    if ( s_LogRuntimeState.initialized ) {
        return log_error_t::ERR_IS_INIT;
    }

    prepared_file_sinks_t preparedSinks{};
    const log_error_t openResult = Log_PrepareFileSinks( s_LogRuntimeState, config, preparedSinks );

    if ( openResult != log_error_t::OK ) {
        return openResult;
    }

    Log_CommitPreparedFileSinks( s_LogRuntimeState, preparedSinks );
    s_LogRuntimeState.config = config;
    s_LogRuntimeState.initialized = true;
    s_LogRuntimeState.bFileErrorReported = false;

    return log_error_t::OK;
}

/*
================
Log_Shutdown
================
*/
void Log_Shutdown()
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );

    s_LogRuntimeState.initialized = false;
    Log_CloseFileSinks( s_LogRuntimeState );
    s_LogRuntimeState = {};
}

/*
================
Log_GetConfig
================
*/
config_t Log_GetConfig()
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );
    return s_LogRuntimeState.config;
}

/*
================
Log_IsInitialized
================
*/
bool Log_IsInitialized()
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );
    return s_LogRuntimeState.initialized;
}

/*
================
Log_LevelFromString

Parses config/console level strings into logger severity values.
================
*/
log_error_t Log_LevelFromString( const char *szLevelName, level_t &levelOut )
{
    if ( szLevelName == nullptr || szLevelName[0] == '\0' ) {
        return log_error_t::ERR_INVALID_LEVEL;
    }

    char lower[32]{};
    common::u32 i = 0u;

    for ( ; i + 1u < sizeof( lower ) && szLevelName[i] != '\0'; ++i ) {
        lower[i] = static_cast<char>( std::tolower( static_cast<unsigned char>( szLevelName[i] ) ) );
    }

    lower[i] = '\0';

    if ( std::strcmp( lower, "trace" ) == 0 || std::strcmp( lower, "0" ) == 0 ) {
        levelOut = level_t::TRACE;
        return log_error_t::OK;
    }

    if ( std::strcmp( lower, "debug" ) == 0 || std::strcmp( lower, "1" ) == 0 ) {
        levelOut = level_t::DEBUG;
        return log_error_t::OK;
    }

    if ( std::strcmp( lower, "info" ) == 0 || std::strcmp( lower, "2" ) == 0 ) {
        levelOut = level_t::INFO;
        return log_error_t::OK;
    }

    if ( std::strcmp( lower, "warning" ) == 0 || std::strcmp( lower, "warn" ) == 0 || std::strcmp( lower, "3" ) == 0 ) {
        levelOut = level_t::WARNING;
        return log_error_t::OK;
    }

    if ( std::strcmp( lower, "error" ) == 0 || std::strcmp( lower, "4" ) == 0 ) {
        levelOut = level_t::ERR;
        return log_error_t::OK;
    }

    if ( std::strcmp( lower, "fatal" ) == 0 || std::strcmp( lower, "5" ) == 0 ) {
        levelOut = level_t::FATAL;
        return log_error_t::OK;
    }

    return log_error_t::ERR_INVALID_LEVEL;
}

/*
================
Log_SetConfig

Replaces active configuration and rotates file sinks if requested.
================
*/
log_error_t Log_SetConfig( const config_t &config )
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );

    if ( !s_LogRuntimeState.initialized ) {
        return log_error_t::ERR_NOT_INIT;
    }

    prepared_file_sinks_t preparedSinks{};
    const log_error_t updateResult = Log_PrepareFileSinks( s_LogRuntimeState, config, preparedSinks );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    Log_CommitPreparedFileSinks( s_LogRuntimeState, preparedSinks );
    s_LogRuntimeState.config = config;
    s_LogRuntimeState.bFileErrorReported = false;

    return log_error_t::OK;
}

/*
================
Log_LevelEnabledLocked

Checks active filters while the caller owns s_LogRuntimeMutex.
================
*/
static bool Log_LevelEnabledLocked( const level_t level, const channel_t channel )
{
    if ( !s_LogRuntimeState.initialized ) {
        return false;
    }

    if ( channel == channel_t::NONE || channel == channel_t::COUNT ) {
        return false;
    }

    if ( level == level_t::COUNT ) {
        return false;
    }

    if ( !Log_LevelPasses( level, s_LogRuntimeState.config.nMinLevel ) ) {
        return false;
    }

    return Log_ChannelEnabled( s_LogRuntimeState.config.nChannelMask, channel );
}

/*
================
Log_LevelEnabled

Checks global severity and channel filters before building a log record.
================
*/
bool Log_LevelEnabled( const level_t level, const channel_t channel )
{
    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );
    return Log_LevelEnabledLocked( level, channel );
}

/*
================
Log_ChannelEnabled

Checks a channel bit against the active channel mask.
================
*/
bool Log_ChannelEnabled( const common::u32 nChannelMask, const channel_t channel )
{
    if ( channel == channel_t::NONE || channel == channel_t::COUNT ) {
        return false;
    }

    const auto szChannelAsInt = static_cast<common::u32>( channel );

    if ( szChannelAsInt >= 32u ) {
        return false;
    }

    return ( nChannelMask & Log_ChannelBit( channel ) ) != 0u;
}

/*
================
Log_Emit

Routes a fully built log record to all enabled sinks requested by its sink mask.
================
*/
void Log_Emit( const record_t &record )
{
    if ( record.message[0] == '\0' ) {
        return;
    }

    const std::lock_guard<std::mutex> lock( s_LogRuntimeMutex );

    if ( !Log_LevelEnabledLocked( record.level, record.channel ) ) {
        return;
    }

    const config_t &config = s_LogRuntimeState.config;
    const common::u32 nSinkMask = ( record.nSinkMask != 0u ) ? record.nSinkMask : Log_DefaultSinkMaskForLevel( record.level );

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::TERMINAL ) && Log_SinkAcceptsRecord( record, config.terminal ) ) {
        Log_WriteFormattedRecord( record, config.terminal, config, Log_TerminalStreamForLevel( record.level ) );
    }

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::ENGINE_FILE ) && Log_SinkAcceptsRecord( record, config.engineFile ) ) {
        Log_WriteFormattedRecord( record, config.engineFile, config, s_LogRuntimeState.pEngineFileHandle );
    }

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::ERROR_FILE ) && Log_SinkAcceptsRecord( record, config.errorFile ) ) {
        Log_WriteFormattedRecord( record, config.errorFile, config, s_LogRuntimeState.pErrorFileHandle );
    }

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::CONSOLE_FILE ) && Log_SinkAcceptsRecord( record, config.consoleFile ) ) {
        Log_WriteFormattedRecord( record, config.consoleFile, config, s_LogRuntimeState.pConsoleFileHandle );
    }

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::EDITOR_FILE ) && Log_SinkAcceptsRecord( record, config.editorFile ) ) {
        Log_WriteFormattedRecord( record, config.editorFile, config, s_LogRuntimeState.pEditorFileHandle );
    }

    if ( Log_SinkMaskHas( nSinkMask, sink_flag_t::GAME_FILE ) && Log_SinkAcceptsRecord( record, config.gameFile ) ) {
        Log_WriteFormattedRecord( record, config.gameFile, config, s_LogRuntimeState.pGameFileHandle );
    }
}

/*
================
Log_Emitf

Formats and emits a log event from variadic arguments.
================
*/
void Log_Emitf( const level_t level, const channel_t channel,
                const char *file, const char *function, const common::i32 line,
                const char *format, ... )
{
    va_list args;
    va_start( args, format );
    Log_Emitfv( level, channel, file, function, line, format, args );
    va_end( args );
}

/*
================
Log_Emitfv

Builds a log record from an existing va_list.
================
*/
void Log_Emitfv( const level_t level, const channel_t channel,
                 const char *file, const char *function, const common::i32 line,
                 const char *format, va_list args )
{
    if ( !Log_LevelEnabled( level, channel ) ) {
        return;
    }

    record_t record{};
    record.level = level;
    record.channel = channel;
    record.nSinkMask = Log_DefaultSinkMaskForLevel( level );
    record.file = file ? file : "<unknown_file>";
    record.function = function ? function : "<unknown_function>";
    record.line = line;
    record.timestamp = std::time( nullptr );

    const char *bSafeFormat = format ? format : "<null format>";
    std::vsnprintf( record.message, sizeof( record.message ), bSafeFormat, args );

    Log_Emit( record );
}

}       // namespace cypher::engine::log
