//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherLog/CypherLog.cpp
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

}

namespace cypher::engine::log
{

/*
================
CypherLog_FileOpenMode
================
*/
const char *CypherLog_FileOpenMode( const file_mode_t bFileMode )
{
    return ( bFileMode == file_mode_t::APPEND ) ? "a" : "w";
}

/*
================
CypherLog_CloseFile
================
*/
void CypherLog_CloseFile( std::FILE *&pFileHandle )
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
CypherLog_CloseFileSinks
================
*/
void CypherLog_CloseFileSinks( runtime_state_t &pRuntimeState )
{
    CypherLog_CloseFile( pRuntimeState.pEngineFileHandle );
    CypherLog_CloseFile( pRuntimeState.pErrorFileHandle );
    CypherLog_CloseFile( pRuntimeState.pConsoleFileHandle );
    CypherLog_CloseFile( pRuntimeState.pEditorFileHandle );
    CypherLog_CloseFile( pRuntimeState.pGameFileHandle );
}

/*
================
CypherLog_OpenFileSink
================
*/
log_error_t CypherLog_OpenFileSink( const sink_config_t &pSinkConfig, std::FILE *&pFileHandle )
{
    pFileHandle = nullptr;

    if ( !pSinkConfig.enabled ) {
        return log_error_t::OK;
    }

    if ( pSinkConfig.path[0] == '\0' ) {
        return log_error_t::ERR_INVALID_CONFIG;
    }

    pFileHandle = std::fopen( pSinkConfig.path, CypherLog_FileOpenMode( pSinkConfig.file ) );

    if ( pFileHandle == nullptr ) {
        return log_error_t::ERR_FILE_OPEN_FAILED;
    }

    return log_error_t::OK;
}

/*
================
CypherLog_FileSinkSameTarget
================
*/
bool CypherLog_FileSinkSameTarget( const sink_config_t &oldConfig, const sink_config_t &newConfig )
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
CypherLog_UpdateFileSink

Keeps unchanged file handles alive. This avoids truncating the active log file
when runtime cvars are applied without changing the sink target.
================
*/
log_error_t CypherLog_UpdateFileSink( const sink_config_t &oldConfig, const sink_config_t &newConfig, std::FILE *&pFileHandle )
{
    if ( !newConfig.enabled ) {
        CypherLog_CloseFile( pFileHandle );
        return log_error_t::OK;
    }

    if ( pFileHandle != nullptr && CypherLog_FileSinkSameTarget( oldConfig, newConfig ) ) {
        return log_error_t::OK;
    }

    CypherLog_CloseFile( pFileHandle );
    return CypherLog_OpenFileSink( newConfig, pFileHandle );
}

/*
================
CypherLog_OpenFileSinks
================
*/
log_error_t CypherLog_OpenFileSinks(
    const config_t &config,
    std::FILE *&pEngineFileHandle,
    std::FILE *&pErrorFileHandle,
    std::FILE *&pConsoleFileHandle,
    std::FILE *&pEditorFileHandle,
    std::FILE *&pGameFileHandle )
{
    log_error_t result = log_error_t::OK;

    result = CypherLog_OpenFileSink( config.engineFile, pEngineFileHandle );
    if ( result != log_error_t::OK ) {
        return result;
    }

    result = CypherLog_OpenFileSink( config.errorFile, pErrorFileHandle );
    if ( result != log_error_t::OK ) {
        CypherLog_CloseFile( pEngineFileHandle );
        return result;
    }

    result = CypherLog_OpenFileSink( config.consoleFile, pConsoleFileHandle );
    if ( result != log_error_t::OK ) {
        CypherLog_CloseFile( pEngineFileHandle );
        CypherLog_CloseFile( pErrorFileHandle );
        return result;
    }

    result = CypherLog_OpenFileSink( config.editorFile, pEditorFileHandle );
    if ( result != log_error_t::OK ) {
        CypherLog_CloseFile( pEngineFileHandle );
        CypherLog_CloseFile( pErrorFileHandle );
        CypherLog_CloseFile( pConsoleFileHandle );
        return result;
    }

    result = CypherLog_OpenFileSink( config.gameFile, pGameFileHandle );
    if ( result != log_error_t::OK ) {
        CypherLog_CloseFile( pEngineFileHandle );
        CypherLog_CloseFile( pErrorFileHandle );
        CypherLog_CloseFile( pConsoleFileHandle );
        CypherLog_CloseFile( pEditorFileHandle );
        return result;
    }

    return log_error_t::OK;
}

/*
================
CypherLog_ShouldFlush
================
*/
bool CypherLog_ShouldFlush( const flush_policy_t flushPolicy, const level_t level )
{
    switch ( flushPolicy ) {
        case flush_policy_t::NEVER:
            return false;
        case flush_policy_t::ERRORS_AND_ABOVE:
            return CypherLog_LevelPasses( level, level_t::ERR );
        case flush_policy_t::EVERY_MESSAGE:
            return true;
        default:
            return false;
    }
}

/*
================
CypherLog_SinkAcceptsRecord
================
*/
bool CypherLog_SinkAcceptsRecord( const record_t &record, const sink_config_t &pSinkConfig )
{
    if ( !pSinkConfig.enabled ) {
        return false;
    }

    return CypherLog_LevelPasses( record.level, pSinkConfig.nMinLevel );
}

/*
================
CypherLog_TerminalStreamForLevel
================
*/
std::FILE *CypherLog_TerminalStreamForLevel( const level_t level )
{
    return CypherLog_LevelPasses( level, level_t::WARNING ) ? stderr : stdout;
}

/*
================
CypherLog_WriteFormattedRecord
================
*/
void CypherLog_WriteFormattedRecord(
    const record_t &record,
    const sink_config_t &pSinkConfig,
    const config_t &config,
    std::FILE *pFileHandle )
{
    if ( pFileHandle == nullptr ) {
        return;
    }

    char szLineBuffer[CYPHER_LOG_MESSAGE_MAX + 512u]{};
    const log_error_t formatResult = CypherLog_FormatRecord(
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

    if ( CypherLog_ShouldFlush( pSinkConfig.flush, record.level ) ) {
        std::fflush( pFileHandle );
    }
}

/*
================
CypherLog_Init

Installs active logging configuration and opens enabled file sinks.
================
*/
log_error_t CypherLog_Init( const config_t &config )
{
    CypherLog_CloseFileSinks( s_LogRuntimeState );
    s_LogRuntimeState = {};

    std::FILE *pEngineFileHandle = nullptr;
    std::FILE *pErrorFileHandle = nullptr;
    std::FILE *pConsoleFileHandle = nullptr;
    std::FILE *pEditorFileHandle = nullptr;
    std::FILE *pGameFileHandle = nullptr;

    const log_error_t openResult = CypherLog_OpenFileSinks(
        config,
        pEngineFileHandle,
        pErrorFileHandle,
        pConsoleFileHandle,
        pEditorFileHandle,
        pGameFileHandle
    );

    if ( openResult != log_error_t::OK ) {
        return openResult;
    }

    s_LogRuntimeState.config = config;
    s_LogRuntimeState.pEngineFileHandle = pEngineFileHandle;
    s_LogRuntimeState.pErrorFileHandle = pErrorFileHandle;
    s_LogRuntimeState.pConsoleFileHandle = pConsoleFileHandle;
    s_LogRuntimeState.pEditorFileHandle = pEditorFileHandle;
    s_LogRuntimeState.pGameFileHandle = pGameFileHandle;
    s_LogRuntimeState.initialized = true;
    s_LogRuntimeState.bFileErrorReported = false;

    return log_error_t::OK;
}

/*
================
CypherLog_Shutdown
================
*/
void CypherLog_Shutdown()
{
    CypherLog_CloseFileSinks( s_LogRuntimeState );
    s_LogRuntimeState.initialized = false;
    s_LogRuntimeState = {};
}

/*
================
CypherLog_GetConfig
================
*/
const config_t &CypherLog_GetConfig()
{
    return s_LogRuntimeState.config;
}

/*
================
CypherLog_IsInitialized
================
*/
bool CypherLog_IsInitialized()
{
    return s_LogRuntimeState.initialized;
}

/*
================
CypherLog_LevelFromString

Parses config/console level strings into logger severity values.
================
*/
log_error_t CypherLog_LevelFromString( const char *szLevelName, level_t &levelOut )
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
CypherLog_SetConfig

Replaces active configuration and rotates file sinks if requested.
================
*/
log_error_t CypherLog_SetConfig( const config_t &config )
{
    if ( !s_LogRuntimeState.initialized ) {
        return log_error_t::ERR_NOT_INIT;
    }

    log_error_t updateResult = log_error_t::OK;

    updateResult = CypherLog_UpdateFileSink( s_LogRuntimeState.config.engineFile, config.engineFile, s_LogRuntimeState.pEngineFileHandle );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    updateResult = CypherLog_UpdateFileSink( s_LogRuntimeState.config.errorFile, config.errorFile, s_LogRuntimeState.pErrorFileHandle );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    updateResult = CypherLog_UpdateFileSink( s_LogRuntimeState.config.consoleFile, config.consoleFile, s_LogRuntimeState.pConsoleFileHandle );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    updateResult = CypherLog_UpdateFileSink( s_LogRuntimeState.config.editorFile, config.editorFile, s_LogRuntimeState.pEditorFileHandle );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    updateResult = CypherLog_UpdateFileSink( s_LogRuntimeState.config.gameFile, config.gameFile, s_LogRuntimeState.pGameFileHandle );
    if ( updateResult != log_error_t::OK ) {
        return updateResult;
    }

    s_LogRuntimeState.config = config;
    s_LogRuntimeState.bFileErrorReported = false;

    return log_error_t::OK;
}

/*
================
CypherLog_LevelEnabled

Checks global severity and channel filters before building a log record.
================
*/
bool CypherLog_LevelEnabled( const level_t level, const channel_t channel )
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

    if ( !CypherLog_LevelPasses( level, s_LogRuntimeState.config.nMinLevel ) ) {
        return false;
    }

    return CypherLog_ChannelEnabled( s_LogRuntimeState.config.nChannelMask, channel );
}

/*
================
CypherLog_ChannelEnabled

Checks a channel bit against the active channel mask.
================
*/
bool CypherLog_ChannelEnabled( const common::u32 nChannelMask, const channel_t channel )
{
    if ( channel == channel_t::NONE || channel == channel_t::COUNT ) {
        return false;
    }

    const auto szChannelAsInt = static_cast<common::u32>( channel );

    if ( szChannelAsInt >= 32u ) {
        return false;
    }

    return ( nChannelMask & CypherLog_ChannelBit( channel ) ) != 0u;
}

/*
================
CypherLog_Emit

Routes a fully built log record to all enabled sinks requested by its sink mask.
================
*/
void CypherLog_Emit( const record_t &record )
{
    if ( record.message[0] == '\0' ) {
        return;
    }

    if ( !CypherLog_LevelEnabled( record.level, record.channel ) ) {
        return;
    }

    const config_t &config = CypherLog_GetConfig();
    const common::u32 nSinkMask = ( record.nSinkMask != 0u ) ? record.nSinkMask : CypherLog_DefaultSinkMaskForLevel( record.level );

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::TERMINAL ) && CypherLog_SinkAcceptsRecord( record, config.terminal ) ) {
        CypherLog_WriteFormattedRecord( record, config.terminal, config, CypherLog_TerminalStreamForLevel( record.level ) );
    }

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::ENGINE_FILE ) && CypherLog_SinkAcceptsRecord( record, config.engineFile ) ) {
        CypherLog_WriteFormattedRecord( record, config.engineFile, config, s_LogRuntimeState.pEngineFileHandle );
    }

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::ERROR_FILE ) && CypherLog_SinkAcceptsRecord( record, config.errorFile ) ) {
        CypherLog_WriteFormattedRecord( record, config.errorFile, config, s_LogRuntimeState.pErrorFileHandle );
    }

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::CONSOLE_FILE ) && CypherLog_SinkAcceptsRecord( record, config.consoleFile ) ) {
        CypherLog_WriteFormattedRecord( record, config.consoleFile, config, s_LogRuntimeState.pConsoleFileHandle );
    }

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::EDITOR_FILE ) && CypherLog_SinkAcceptsRecord( record, config.editorFile ) ) {
        CypherLog_WriteFormattedRecord( record, config.editorFile, config, s_LogRuntimeState.pEditorFileHandle );
    }

    if ( CypherLog_SinkMaskHas( nSinkMask, sink_flag_t::GAME_FILE ) && CypherLog_SinkAcceptsRecord( record, config.gameFile ) ) {
        CypherLog_WriteFormattedRecord( record, config.gameFile, config, s_LogRuntimeState.pGameFileHandle );
    }
}

/*
================
CypherLog_Emitf

Formats and emits a log event from variadic arguments.
================
*/
void CypherLog_Emitf( const level_t level, const channel_t channel,
                const char *file, const char *function, const common::i32 line,
                const char *format, ... )
{
    if ( !CypherLog_LevelEnabled( level, channel ) ) {
        return;
    }

    va_list args;
    va_start( args, format );
    CypherLog_Emitfv( level, channel, file, function, line, format, args );
    va_end( args );
}

/*
================
CypherLog_Emitfv

Builds a log record from an existing va_list.
================
*/
void CypherLog_Emitfv( const level_t level, const channel_t channel,
                 const char *file, const char *function, const common::i32 line,
                 const char *format, va_list args )
{
    if ( !CypherLog_LevelEnabled( level, channel ) ) {
        return;
    }

    record_t record{};
    record.level = level;
    record.channel = channel;
    record.nSinkMask = CypherLog_DefaultSinkMaskForLevel( level );
    record.file = file ? file : "<unknown_file>";
    record.function = function ? function : "<unknown_function>";
    record.line = line;
    record.timestamp = std::time( nullptr );

    const char *bSafeFormat = format ? format : "<null format>";
    std::vsnprintf( record.message, sizeof( record.message ), bSafeFormat, args );

    CypherLog_Emit( record );
}

}       // namespace cypher::engine::log
