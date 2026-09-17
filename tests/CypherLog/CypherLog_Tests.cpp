//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherLog/CypherLog_Tests.cpp
//  Purpose: Tests CypherLog lifecycle, sink replacement, and concurrency.
//  Details: These checks protect synchronized state snapshots, atomic runtime
//           reconfiguration, serialized records, and shutdown behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherLog.h"
#include "CypherLog_Format.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::engine::log;

namespace
{

struct logger_scope_t {
    logger_scope_t()
    {
        Log_Shutdown();
    }

    ~logger_scope_t()
    {
        Log_Shutdown();
    }
};

std::filesystem::path MakeTemporaryLogPath( const char *szPurpose )
{
    static std::atomic<unsigned long long> s_nPathSequence{ 0u };

    const auto nTimestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto nSequence = s_nPathSequence.fetch_add( 1u, std::memory_order_relaxed );

    return std::filesystem::temp_directory_path()
        / ( "cypher_log_" + std::string( szPurpose ) + "_"
            + std::to_string( nTimestamp ) + "_" + std::to_string( nSequence ) + ".log" );
}

struct temporary_log_t {
    std::filesystem::path path;

    ~temporary_log_t()
    {
        std::error_code error{};
        std::filesystem::remove( path, error );
    }
};

bool WriteTextFile( const std::filesystem::path &path, const char *text )
{
    std::ofstream stream( path, std::ios::binary | std::ios::trunc );
    stream << text;
    stream.close();
    return !stream.fail();
}

bool SetSinkPath( sink_config_t &sinkConfig, const std::filesystem::path &path )
{
    const std::string pathText = path.string();
    if ( pathText.size() >= sizeof( sinkConfig.path ) ) {
        return false;
    }

    std::snprintf( sinkConfig.path, sizeof( sinkConfig.path ), "%s", pathText.c_str() );
    return true;
}

config_t MakeDisabledSinkConfig()
{
    config_t config{};
    config.terminal.enabled = false;
    config.engineFile.enabled = false;
    config.errorFile.enabled = false;
    config.consoleFile.enabled = false;
    config.editorFile.enabled = false;
    config.gameFile.enabled = false;
    return config;
}

config_t MakeFileOnlyConfig( const std::filesystem::path &path )
{
    config_t config = MakeDisabledSinkConfig();
    config.engineFile.enabled = true;
    config.engineFile.nMinLevel = level_t::TRACE;
    config.engineFile.format = format_mode_t::COMPACT;
    config.engineFile.flush = flush_policy_t::EVERY_MESSAGE;
    config.engineFile.file = file_mode_t::TRUNCATE;
    config.engineFile.bIncludeTimestamps = false;
    config.engineFile.bIncludeSourceLocation = false;
    config.engineFile.bIncludeFunctionName = false;
    config.engineFile.bColorEnabled = false;
    SetSinkPath( config.engineFile, path );
    return config;
}

std::string ReadTextFile( const std::filesystem::path &path )
{
    std::ifstream stream( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
}

std::size_t CountLines( const std::string &text )
{
    std::size_t cLines = 0u;
    for ( const char character : text ) {
        if ( character == '\n' ) {
            ++cLines;
        }
    }

    return cLines;
}

} // namespace

TEST_CASE( "CypherLog compact formatting honors timestamps and rejects truncated records", "[CypherLog][Formatting]" )
{
    record_t record{};
    record.level = level_t::INFO;
    record.channel = channel_t::CORE;
    record.timestamp = static_cast<std::time_t>( 1 );
    std::snprintf( record.message, sizeof( record.message ), "compact timestamp" );

    sink_config_t sink{};
    sink.enabled = true;
    sink.format = format_mode_t::COMPACT;
    sink.bIncludeTimestamps = true;

    config_t config{};
    char formatted[256]{};
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );

    const std::string formattedText = formatted;
    REQUIRE( formattedText.size() > 10u );
    REQUIRE( formattedText[0] == '[' );
    REQUIRE( formattedText[3] == ':' );
    REQUIRE( formattedText[6] == ':' );
    REQUIRE( formattedText[9] == ']' );
    REQUIRE( formattedText.substr( 10u ) == "[INFO][CORE] compact timestamp\n" );

    char truncated[16]{};
    REQUIRE( Log_FormatRecord( record, sink, config, truncated, sizeof( truncated ) ) == log_error_t::ERR_FORMAT_FAILED );

    sink.format = format_mode_t::DETAILED;
    REQUIRE( Log_FormatRecord( record, sink, config, truncated, sizeof( truncated ) ) == log_error_t::ERR_FORMAT_FAILED );
}

TEST_CASE( "CypherLog console formatting keeps INFO quiet and diagnostics explicit", "[CypherLog][Formatting]" )
{
    record_t record{};
    record.level = level_t::INFO;
    record.channel = channel_t::SYSTEM;
    record.timestamp = static_cast<std::time_t>( 1 );
    std::snprintf( record.message, sizeof( record.message ), "System ready" );

    sink_config_t sink{};
    sink.enabled = true;
    sink.format = format_mode_t::CONSOLE;

    config_t config{};
    char formatted[256]{};
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );
    REQUIRE( std::string( formatted ) == "System ready\n" );

    sink.bColorEnabled = true;
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );
    REQUIRE( std::string( formatted ) == "System ready\n" );

    record.level = level_t::WARNING;
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );
    REQUIRE( std::string( formatted ) == "\033[33m[WARNING][SYSTEM] System ready\033[0m\n" );

    record.level = level_t::FATAL;
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );
    REQUIRE( std::string( formatted ) == "\033[1;31m[FATAL][SYSTEM] System ready\033[0m\n" );

    record.level = level_t::INFO;
    sink.bColorEnabled = false;
    sink.bIncludeTimestamps = true;
    REQUIRE( Log_FormatRecord( record, sink, config, formatted, sizeof( formatted ) ) == log_error_t::OK );

    const std::string timestamped = formatted;
    REQUIRE( timestamped.size() > 10u );
    REQUIRE( timestamped[0] == '[' );
    REQUIRE( timestamped[3] == ':' );
    REQUIRE( timestamped[6] == ':' );
    REQUIRE( timestamped[9] == ']' );
    REQUIRE( timestamped[10] == ' ' );
    REQUIRE( timestamped.substr( 11u ) == "System ready\n" );
}

TEST_CASE( "CypherLog marks producer-side message truncation", "[CypherLog][Formatting]" )
{
    temporary_log_t output{ MakeTemporaryLogPath( "message_truncation" ) };
    logger_scope_t loggerScope{};
    config_t config = MakeFileOnlyConfig( output.path );
    REQUIRE( Log_Init( config ) == log_error_t::OK );

    const std::string oversized( CYPHER_LOG_MESSAGE_MAX + 128u, 'x' );
    Log_Emitf(
        level_t::INFO,
        channel_t::CORE,
        __FILE__,
        __func__,
        __LINE__,
        "%s",
        oversized.c_str() );
    Log_Shutdown();

    const std::string text = ReadTextFile( output.path );
    REQUIRE( text.find( " [truncated]" ) != std::string::npos );
    REQUIRE( text.find( oversized ) == std::string::npos );
    REQUIRE( CountLines( text ) == 1u );
}

TEST_CASE( "CypherLog protects lifecycle state and returns configuration snapshots", "[CypherLog][Lifecycle]" )
{
    logger_scope_t loggerScope{};
    config_t config = MakeDisabledSinkConfig();

    REQUIRE_FALSE( Log_IsInitialized() );
    REQUIRE( Log_Init( config ) == log_error_t::OK );
    REQUIRE( Log_IsInitialized() );
    REQUIRE( Log_Init( config ) == log_error_t::ERR_IS_INIT );

    config_t snapshot = Log_GetConfig();
    snapshot.nMinLevel = level_t::FATAL;

    REQUIRE( Log_GetConfig().nMinLevel == level_t::TRACE );
    REQUIRE( Log_LevelEnabled( level_t::INFO, channel_t::CORE ) );

    record_t record{};
    record.level = level_t::INFO;
    record.channel = channel_t::CORE;
    std::snprintf( record.message, sizeof( record.message ), "direct record" );
    Log_Emit( record );

    Log_Shutdown();
    REQUIRE_FALSE( Log_IsInitialized() );
    REQUIRE( Log_SetConfig( config ) == log_error_t::ERR_NOT_INIT );
}

TEST_CASE( "CypherLog keeps active sinks when a replacement cannot be opened", "[CypherLog][Configuration]" )
{
    logger_scope_t loggerScope{};
    const std::filesystem::path activePath = MakeTemporaryLogPath( "active" );
    const std::filesystem::path replacementPath = MakeTemporaryLogPath( "replacement" );
    const std::filesystem::path missingDirectory = MakeTemporaryLogPath( "missing_directory" );
    const std::filesystem::path invalidPath = missingDirectory / "error.log";

    std::filesystem::remove( activePath );
    std::filesystem::remove( replacementPath );
    std::filesystem::remove_all( missingDirectory );

    config_t activeConfig = MakeFileOnlyConfig( activePath );
    REQUIRE( SetSinkPath( activeConfig.engineFile, activePath ) );
    REQUIRE( Log_Init( activeConfig ) == log_error_t::OK );

    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "before failed update" );

    config_t requestedConfig = activeConfig;
    REQUIRE( SetSinkPath( requestedConfig.engineFile, replacementPath ) );
    requestedConfig.errorFile.enabled = true;
    requestedConfig.errorFile.file = file_mode_t::TRUNCATE;
    REQUIRE( SetSinkPath( requestedConfig.errorFile, invalidPath ) );

    REQUIRE( Log_SetConfig( requestedConfig ) == log_error_t::ERR_FILE_OPEN_FAILED );
    REQUIRE( std::string( Log_GetConfig().engineFile.path ) == activePath.string() );

    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "after failed update" );
    Log_Shutdown();

    const std::string activeText = ReadTextFile( activePath );
    REQUIRE( activeText.find( "before failed update" ) != std::string::npos );
    REQUIRE( activeText.find( "after failed update" ) != std::string::npos );

    std::filesystem::remove( activePath );
    std::filesystem::remove( replacementPath );
}

TEST_CASE( "CypherLog serializes concurrent writers and configuration updates", "[CypherLog][Concurrency]" )
{
    logger_scope_t loggerScope{};
    const std::filesystem::path logPath = MakeTemporaryLogPath( "concurrent" );
    std::filesystem::remove( logPath );

    config_t config = MakeFileOnlyConfig( logPath );
    REQUIRE( SetSinkPath( config.engineFile, logPath ) );
    REQUIRE( Log_Init( config ) == log_error_t::OK );

    constexpr unsigned int cWriterThreads = 6u;
    constexpr unsigned int cRecordsPerThread = 250u;
    constexpr unsigned int cConfigUpdates = 200u;

    std::atomic<bool> bStart{ false };
    std::atomic<unsigned int> cConfigurationFailures{ 0u };
    std::vector<std::thread> writers;
    writers.reserve( cWriterThreads );

    for ( unsigned int iThread = 0u; iThread < cWriterThreads; ++iThread ) {
        writers.emplace_back( [iThread, &bStart]() {
            while ( !bStart.load( std::memory_order_acquire ) ) {
                std::this_thread::yield();
            }

            for ( unsigned int iRecord = 0u; iRecord < cRecordsPerThread; ++iRecord ) {
                Log_Emitf(
                    level_t::INFO,
                    channel_t::CORE,
                    __FILE__,
                    __func__,
                    __LINE__,
                    "writer=%u record=%u",
                    iThread,
                    iRecord );
            }
        } );
    }

    std::thread configWriter( [&config, &bStart, &cConfigurationFailures]() {
        while ( !bStart.load( std::memory_order_acquire ) ) {
            std::this_thread::yield();
        }

        for ( unsigned int iUpdate = 0u; iUpdate < cConfigUpdates; ++iUpdate ) {
            config.nMinLevel = ( iUpdate & 1u ) == 0u ? level_t::TRACE : level_t::DEBUG;
            if ( Log_SetConfig( config ) != log_error_t::OK ) {
                cConfigurationFailures.fetch_add( 1u, std::memory_order_relaxed );
            }
        }
    } );

    bStart.store( true, std::memory_order_release );

    for ( std::thread &writer : writers ) {
        writer.join();
    }
    configWriter.join();

    REQUIRE( cConfigurationFailures.load( std::memory_order_relaxed ) == 0u );
    Log_Shutdown();

    const std::string logText = ReadTextFile( logPath );
    REQUIRE( CountLines( logText ) == cWriterThreads * cRecordsPerThread );

    std::filesystem::remove( logPath );
}

TEST_CASE( "CypherLog failed initialization does not truncate an earlier candidate file", "[CypherLog][Configuration]" )
{
    temporary_log_t existing{ MakeTemporaryLogPath( "init_sentinel" ) };
    temporary_log_t missingDirectory{ MakeTemporaryLogPath( "init_missing" ) };
    logger_scope_t loggerScope{};
    REQUIRE( WriteTextFile( existing.path, "existing session\n" ) );
    config_t config = MakeFileOnlyConfig( existing.path );
    config.errorFile.enabled = true;
    REQUIRE( SetSinkPath( config.errorFile, missingDirectory.path / "error.log" ) );
    REQUIRE( Log_Init( config ) == log_error_t::ERR_FILE_OPEN_FAILED );
    REQUIRE_FALSE( Log_IsInitialized() );
    REQUIRE( ReadTextFile( existing.path ) == "existing session\n" );

    config.errorFile.enabled = false;
    REQUIRE( Log_Init( config ) == log_error_t::OK );
    REQUIRE( ReadTextFile( existing.path ).empty() );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "successful retry" );
    REQUIRE( ReadTextFile( existing.path ).find( "successful retry" ) != std::string::npos );
}

TEST_CASE( "CypherLog failed replacement preserves active file contents across sink aliases", "[CypherLog][Configuration]" )
{
    temporary_log_t active{ MakeTemporaryLogPath( "rollback_active" ) };
    temporary_log_t missingDirectory{ MakeTemporaryLogPath( "rollback_missing" ) };
    logger_scope_t loggerScope{};
    config_t config = MakeFileOnlyConfig( active.path );
    config.engineFile.file = file_mode_t::APPEND;
    REQUIRE( Log_Init( config ) == log_error_t::OK );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "before rejected truncate" );
    const std::string before = ReadTextFile( active.path );
    REQUIRE_FALSE( before.empty() );
    config_t requested = config;

    SECTION( "Mode change reopens the same active sink" ) {
        requested.engineFile.file = file_mode_t::TRUNCATE;
        requested.errorFile.enabled = true;
        REQUIRE( SetSinkPath( requested.errorFile, missingDirectory.path / "error.log" ) );
    }
    SECTION( "New sink aliases an unchanged active sink" ) {
        requested.errorFile = requested.engineFile;
        requested.errorFile.file = file_mode_t::TRUNCATE;
        requested.consoleFile.enabled = true;
        REQUIRE( SetSinkPath( requested.consoleFile, missingDirectory.path / "console.log" ) );
    }

    REQUIRE( Log_SetConfig( requested ) == log_error_t::ERR_FILE_OPEN_FAILED );
    REQUIRE( Log_GetConfig().engineFile.file == file_mode_t::APPEND );
    REQUIRE( std::string( Log_GetConfig().engineFile.path ) == active.path.string() );
    REQUIRE_FALSE( Log_GetConfig().errorFile.enabled );
    REQUIRE( ReadTextFile( active.path ) == before );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "after rejected truncate" );
    const std::string after = ReadTextFile( active.path );
    REQUIRE( after.find( "before rejected truncate" ) != std::string::npos );
    REQUIRE( after.find( "after rejected truncate" ) != std::string::npos );
}

TEST_CASE( "CypherLog successful replacement truncates only after flushing superseded buffers", "[CypherLog][Configuration]" )
{
    temporary_log_t active{ MakeTemporaryLogPath( "successful_truncate" ) };
    logger_scope_t loggerScope{};
    REQUIRE( WriteTextFile( active.path, "previous session\n" ) );
    config_t config = MakeFileOnlyConfig( active.path );
    config.engineFile.file = file_mode_t::APPEND;
    config.engineFile.flush = flush_policy_t::NEVER;
    REQUIRE( Log_Init( config ) == log_error_t::OK );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "buffered old record" );
    config_t requested = config;
    requested.engineFile.flush = flush_policy_t::EVERY_MESSAGE;
    SECTION( "Same sink changes to truncate" ) {
        requested.engineFile.file = file_mode_t::TRUNCATE;
    }
    SECTION( "New sink truncates a file shared with the retained engine sink" ) {
        requested.errorFile = requested.engineFile;
        requested.errorFile.file = file_mode_t::TRUNCATE;
    }
    REQUIRE( Log_SetConfig( requested ) == log_error_t::OK );
    REQUIRE( ReadTextFile( active.path ).empty() );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "new session" );
    Log_Shutdown();
    const std::string after = ReadTextFile( active.path );
    REQUIRE( after.find( "new session" ) != std::string::npos );
    REQUIRE( after.find( "previous session" ) == std::string::npos );
    REQUIRE( after.find( "buffered old record" ) == std::string::npos );
    REQUIRE( after.find( '\0' ) == std::string::npos );
    REQUIRE( CountLines( after ) == 1u );
}

TEST_CASE( "CypherLog validates configuration before initialization or replacement can touch files", "[CypherLog][Configuration]" )
{
    temporary_log_t existing{ MakeTemporaryLogPath( "invalid_config" ) };
    logger_scope_t loggerScope{};
    REQUIRE( WriteTextFile( existing.path, "validation sentinel\n" ) );
    config_t valid = MakeFileOnlyConfig( existing.path );
    valid.engineFile.file = file_mode_t::APPEND;
    config_t invalid = valid;
    // A malformed late sink must also be found before an earlier sink truncates.
    invalid.engineFile.file = file_mode_t::TRUNCATE;
    SECTION( "Invalid file mode" ) { invalid.engineFile.file = static_cast<file_mode_t>( 255u ); }
    SECTION( "Unterminated path" ) { std::memset( invalid.engineFile.path, 'x', sizeof( invalid.engineFile.path ) ); }
    SECTION( "Empty path in a later enabled sink" ) { invalid.gameFile.enabled = true; }
    SECTION( "Invalid global severity" ) { invalid.nMinLevel = level_t::COUNT; }
    SECTION( "Invalid source path policy" ) { invalid.szSourcePath = static_cast<source_path_mode_t>( 255u ); }
    SECTION( "Invalid sink severity" ) { invalid.engineFile.nMinLevel = level_t::COUNT; }
    SECTION( "Invalid formatting policy" ) { invalid.engineFile.format = static_cast<format_mode_t>( 255u ); }
    SECTION( "Invalid flush policy" ) { invalid.engineFile.flush = static_cast<flush_policy_t>( 255u ); }

    REQUIRE( Log_Init( invalid ) == log_error_t::ERR_INVALID_CONFIG );
    REQUIRE_FALSE( Log_IsInitialized() );
    REQUIRE( ReadTextFile( existing.path ) == "validation sentinel\n" );
    REQUIRE( Log_Init( valid ) == log_error_t::OK );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "active configuration" );
    const std::string before = ReadTextFile( existing.path );
    REQUIRE( Log_SetConfig( invalid ) == log_error_t::ERR_INVALID_CONFIG );
    REQUIRE( Log_IsInitialized() );
    REQUIRE( Log_GetConfig().nMinLevel == valid.nMinLevel );
    REQUIRE( Log_GetConfig().engineFile.file == file_mode_t::APPEND );
    REQUIRE( std::string( Log_GetConfig().engineFile.path ) == existing.path.string() );
    REQUIRE( ReadTextFile( existing.path ) == before );
    Log_Emitf( level_t::INFO, channel_t::CORE, __FILE__, __func__, __LINE__, "after invalid configuration" );
    REQUIRE( ReadTextFile( existing.path ).find( "after invalid configuration" ) != std::string::npos );
}
