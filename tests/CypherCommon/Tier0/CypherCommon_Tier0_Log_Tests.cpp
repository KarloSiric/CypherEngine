//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Log_Tests.cpp
//  Purpose: Tests Tier0 callback-based logging.
//  Details: These checks protect record metadata, callback registration,
//           reentrant callback behavior, null messages, and name fallbacks.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Log.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct log_capture_t {
    u32 callCount{ 0u };
    log_record_t record{};
};

void CaptureLog( const log_record_t &record, void *pUserData ) noexcept
{
    auto *pCapture = static_cast<log_capture_t *>( pUserData );
    ++pCapture->callCount;
    pCapture->record = record;
}

void ReentrantLog( const log_record_t &, void *pUserData ) noexcept
{
    auto *pCallCount = static_cast<u32 *>( pUserData );
    ++( *pCallCount );

    Cy_LogSetCallback( nullptr, nullptr );
    Cy_LogWrite( log_level_t::Debug, log_channel_t::Common, "nested fallback" );
}

} // namespace

TEST_CASE( "Log forwards complete records to the installed callback", "[CypherCommon][Tier0][Log]" )
{
    log_capture_t capture{};
    Cy_LogSetCallback( CaptureLog, &capture );

    const error_code_t errorCode = CY_ERROR( FILESYSTEM, 17u );
    const source_location_t location{ "log_test.cpp", "TestFunction", 42u, 3u };
    Cy_LogWriteErrorAt(
        log_level_t::Error,
        log_channel_t::FileSystem,
        errorCode,
        "read failed",
        location );

    REQUIRE( capture.callCount == 1u );
    REQUIRE( capture.record.level == log_level_t::Error );
    REQUIRE( capture.record.channel == log_channel_t::FileSystem );
    REQUIRE( capture.record.errorCode == errorCode );
    REQUIRE( capture.record.location.line == 42u );
    REQUIRE( std::strcmp( capture.record.pMessage, "read failed" ) == 0 );

    log_callback_t pCallback = nullptr;
    void *pUserData = nullptr;
    Cy_LogGetCallback( &pCallback, &pUserData );
    REQUIRE( pCallback == CaptureLog );
    REQUIRE( pUserData == &capture );

    Cy_LogSetCallback( nullptr, nullptr );
}

TEST_CASE( "Log normalizes null messages", "[CypherCommon][Tier0][Log]" )
{
    log_capture_t capture{};
    Cy_LogSetCallback( CaptureLog, &capture );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, nullptr );

    REQUIRE( capture.callCount == 1u );
    REQUIRE( capture.record.pMessage != nullptr );
    REQUIRE( capture.record.pMessage[0] == '\0' );

    Cy_LogSetCallback( nullptr, nullptr );
}

TEST_CASE( "Log callbacks may change registration without deadlocking", "[CypherCommon][Tier0][Log]" )
{
    u32 nCallCount = 0u;
    Cy_LogSetCallback( ReentrantLog, &nCallCount );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, "outer callback" );

    REQUIRE( nCallCount == 1u );

    log_callback_t pCallback = ReentrantLog;
    void *pUserData = &nCallCount;
    Cy_LogGetCallback( &pCallback, &pUserData );
    REQUIRE( pCallback == nullptr );
    REQUIRE( pUserData == nullptr );
}

TEST_CASE( "Log name functions handle all sentinels", "[CypherCommon][Tier0][Log]" )
{
    REQUIRE( std::strcmp( Cy_LogLevelName( log_level_t::Trace ), "Trace" ) == 0 );
    REQUIRE( std::strcmp( Cy_LogLevelName( log_level_t::Count ), "Unknown" ) == 0 );
    REQUIRE( std::strcmp( Cy_LogChannelName( log_channel_t::Network ), "Network" ) == 0 );
    REQUIRE( std::strcmp( Cy_LogChannelName( log_channel_t::Count ), "Unknown" ) == 0 );
}
