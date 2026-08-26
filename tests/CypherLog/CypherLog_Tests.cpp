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

#include <atomic>
#include <chrono>
#include <cstdio>
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
