//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Services_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 service modules.
//  Details: This test file covers low-level diagnostics, platform wrappers,
//           metadata helpers, and tool-facing utility services.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_BuildId.h"
#include "CypherCommon/Tier0/CypherCommon_CacheHints.h"
#include "CypherCommon/Tier0/CypherCommon_CommandLineBase.h"
#include "CypherCommon/Tier0/CypherCommon_CPUMonitoring.h"
#include "CypherCommon/Tier0/CypherCommon_Debug.h"
#include "CypherCommon/Tier0/CypherCommon_DynamicLibrary.h"
#include "CypherCommon/Tier0/CypherCommon_Environment.h"
#include "CypherCommon/Tier0/CypherCommon_Error.h"
#include "CypherCommon/Tier0/CypherCommon_Handle.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier0/CypherCommon_LogToggle.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryDebug.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryTracker.h"
#include "CypherCommon/Tier0/CypherCommon_Minidump.h"
#include "CypherCommon/Tier0/CypherCommon_Module.h"
#include "CypherCommon/Tier0/CypherCommon_PageAllocator.h"
#include "CypherCommon/Tier0/CypherCommon_PerformanceCounter.h"
#include "CypherCommon/Tier0/CypherCommon_PlatformMemory.h"
#include "CypherCommon/Tier0/CypherCommon_Process.h"
#include "CypherCommon/Tier0/CypherCommon_Profile.h"
#include "CypherCommon/Tier0/CypherCommon_SourceLocation.h"
#include "CypherCommon/Tier0/CypherCommon_Stats.h"
#include "CypherCommon/Tier0/CypherCommon_TsList.h"
#include "CypherCommon/Tier0/CypherCommon_Validator.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <filesystem>
#include <string>

using namespace cypher::common;

namespace
{

bool_t g_logCallbackCalled = CY_FALSE;
bool_t g_validatorCallbackCalled = CY_FALSE;
bool_t g_memoryDebugCallbackCalled = CY_FALSE;
bool_t g_assertHandlerCalled = CY_FALSE;
u32 g_assertHandlerCallCount = 0u;
assert_info_t g_assertInfo{};
u32 g_profileEventCount = 0u;

void TestLogCallback( const log_record_t &record, void *pUserData ) noexcept
{
    auto *pCalled = static_cast<bool_t *>( pUserData );
    *pCalled = record.pMessage != nullptr;
}

void CYPHER_CALL TestProfileSink(
    const profile_event_t &,
    void *pUserData ) noexcept
{
    auto *pCount = static_cast<u32 *>( pUserData );
    ++( *pCount );
}

void TestValidatorCallback( const validation_record_t &record, void * ) noexcept
{
    g_validatorCallbackCalled = record.pMessage != nullptr;
}

void TestMemoryDebugCallback( const memory_debug_record_t &, void * ) noexcept
{
    g_memoryDebugCallbackCalled = CY_TRUE;
}

assert_action_t TestAssertHandler( const assert_info_t &info ) noexcept
{
    g_assertHandlerCalled = CY_TRUE;
    ++g_assertHandlerCallCount;
    g_assertInfo = info;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Tier0 error helpers pack domains and local codes", "[CypherCommon][Tier0][Services]" )
{
    const error_code_t errorCode = Cy_ErrorMake( error_domain_t::COM_DOMAIN_FILESYSTEM, 77u );

    REQUIRE( Cy_ErrorDomain( errorCode ) == error_domain_t::COM_DOMAIN_FILESYSTEM );
    REQUIRE( Cy_ErrorLocalCode( errorCode ) == 77u );
    REQUIRE( Cy_ErrorSucceeded( common_error_t::OK ) );
    REQUIRE( Cy_ErrorFailed( common_error_t::ERR_FAILED ) );
    REQUIRE( Cy_ErrorName( common_error_t::ERR_TIMEOUT )[0] != '\0' );
    REQUIRE( Cy_ErrorDomainName( error_domain_t::COM_DOMAIN_TOOLS )[0] != '\0' );
}

TEST_CASE( "Tier0 debug helpers expose debugger state and build gating", "[CypherCommon][Tier0][Services]" )
{
    const bool_t debuggerAttached = Cy_DebuggerIsAttached();
    REQUIRE( ( debuggerAttached == CY_TRUE || debuggerAttached == CY_FALSE ) );

    i32 debugCount = 0;
    i32 releaseCount = 0;
    CY_DEBUG_ONLY( ++debugCount );
    CY_RELEASE_ONLY( ++releaseCount );

    REQUIRE( debugCount == CYPHER_CONFIG_DEBUG );
    REQUIRE( releaseCount == CYPHER_CONFIG_RELEASE );
}

TEST_CASE( "Tier0 assertions dispatch records and preserve build semantics", "[CypherCommon][Tier0][Services]" )
{
    g_assertHandlerCalled = CY_FALSE;
    g_assertHandlerCallCount = 0u;
    g_assertInfo = {};
    Cy_AssertSetHandler( TestAssertHandler );

    REQUIRE( Cy_AssertGetHandler() == TestAssertHandler );

    const source_location_t location{ "assert_test.cpp", "TestFunction", 42u };
    Cy_AssertHandleFailure( "value != 0", "value must be nonzero", location );

    REQUIRE( g_assertHandlerCalled );
    REQUIRE( std::string( g_assertInfo.pExpression ) == "value != 0" );
    REQUIRE( std::string( g_assertInfo.pMessage ) == "value must be nonzero" );
    REQUIRE( std::string( g_assertInfo.location.pFile ) == "assert_test.cpp" );
    REQUIRE( std::string( g_assertInfo.location.pFunction ) == "TestFunction" );
    REQUIRE( g_assertInfo.location.line == 42u );
    REQUIRE( g_assertHandlerCallCount == 1u );

    i32 assertEvaluationCount = 0;
    i32 assertMessageEvaluationCount = 0;
    i32 verifyEvaluationCount = 0;
    i32 verifyMessageEvaluationCount = 0;
    g_assertHandlerCalled = CY_FALSE;
    g_assertHandlerCallCount = 0u;

    CY_ASSERT( ++assertEvaluationCount == 0 );
    CY_ASSERT_MSG( ++assertMessageEvaluationCount == 0, "assert message" );
    CY_VERIFY( ++verifyEvaluationCount == 0 );
    CY_VERIFY_MSG( ++verifyMessageEvaluationCount == 0, "verify message" );

    #if CYPHER_ASSERTS_ENABLED
        REQUIRE( assertEvaluationCount == 1 );
        REQUIRE( assertMessageEvaluationCount == 1 );
        REQUIRE( verifyEvaluationCount == 1 );
        REQUIRE( verifyMessageEvaluationCount == 1 );
        REQUIRE( g_assertHandlerCalled );
        REQUIRE( g_assertHandlerCallCount == 4u );
    #else
        REQUIRE( assertEvaluationCount == 0 );
        REQUIRE( assertMessageEvaluationCount == 0 );
        REQUIRE( verifyEvaluationCount == 1 );
        REQUIRE( verifyMessageEvaluationCount == 1 );
        REQUIRE_FALSE( g_assertHandlerCalled );
        REQUIRE( g_assertHandlerCallCount == 0u );
    #endif

    Cy_AssertSetHandler( nullptr );
    REQUIRE( Cy_AssertGetHandler() == nullptr );
}

TEST_CASE( "Tier0 handle helpers pack index and generation", "[CypherCommon][Tier0][Services]" )
{
    const handle32_t handle = Cy_Handle32Make( 12u, 34u );
    const handle_parts32_t parts = Cy_Handle32Unpack( handle );

    REQUIRE( Cy_Handle32IsValid( handle ) );
    REQUIRE( parts.nIndex == 12u );
    REQUIRE( parts.nGeneration == 34u );
    REQUIRE( Cy_Handle32Index( handle ) == 12u );
    REQUIRE( Cy_Handle32Generation( handle ) == 34u );
    REQUIRE( Cy_Handle64IsValid( Cy_Handle64Make( 1u, 2u, 3u ) ) );
}

TEST_CASE( "Tier0 build id and module helpers produce stable names", "[CypherCommon][Tier0][Services]" )
{
    char szBuild[256] = {};
    const build_id_t *pBuild = Cy_BuildIdGetEngine();

    REQUIRE( pBuild != nullptr );
    REQUIRE( Cy_BuildIdFormat( pBuild, szBuild, sizeof( szBuild ) ) > 0u );
    REQUIRE( szBuild[0] != '\0' );

    REQUIRE( Cy_ModuleStateName( module_state_t::Initialized ) != nullptr );

    const module_version_t required{ 1u, 2u, 3u, 0u };
    const module_version_t provided{ 1u, 3u, 0u, 0u };
    REQUIRE( Cy_ModuleVersionCompatible( required, provided ) );
}

TEST_CASE( "Tier0 source location formats into caller buffer", "[CypherCommon][Tier0][Services]" )
{
    char szLocation[128] = {};
    source_location_t location{ "file.cpp", "Func", 42u };

    REQUIRE( Cy_SourceLocation_Format( location, szLocation, sizeof( szLocation ) ) == std::strlen( szLocation ) );
    REQUIRE( szLocation[0] != '\0' );
}

TEST_CASE( "Tier0 command line base handles flags and values", "[CypherCommon][Tier0][Services]" )
{
    const char *args[] = {
        "tool",
        "-game",
        "reap",
        "--map=arena01",
        "-verbose"
    };

    command_line_base_t commandLine{};
    REQUIRE( Cy_CommandLineBaseSet( &commandLine, 5, args ) );

    REQUIRE( Cy_CommandLineBaseHasSwitch( &commandLine, "game" ) );
    REQUIRE( Cy_CommandLineBaseFindValue( &commandLine, "game" ) == args[2] );
    REQUIRE( Cy_CommandLineBaseFindValue( &commandLine, "map" ) != nullptr );
    REQUIRE( Cy_CommandLineBaseFindValue( &commandLine, "verbose" ) != nullptr );
    REQUIRE( Cy_CommandLineBaseFindValue( &commandLine, "missing" ) == nullptr );
}

TEST_CASE( "Tier0 environment and process helpers return basic process state", "[CypherCommon][Tier0][Services]" )
{
    REQUIRE( Cy_ProcessGetCurrentId() != 0u );
    REQUIRE( Cy_ProcessGetExecutablePath() != nullptr );

    REQUIRE( Cy_EnvironmentSet( "CYPHER_TEST_ENV", "ok" ) );
    char szValue[16] = {};
    const cy_environment_get_result_t environment =
        Cy_EnvironmentGet( "CYPHER_TEST_ENV", szValue, sizeof( szValue ) );
    REQUIRE( environment.exists );
    REQUIRE( environment.cchRequired == 2u );
    REQUIRE( szValue[0] == 'o' );
    REQUIRE( Cy_EnvironmentHas( "CYPHER_TEST_ENV" ) );
    REQUIRE( Cy_EnvironmentUnset( "CYPHER_TEST_ENV" ) );
}

TEST_CASE( "Tier0 platform memory and page allocator reserve writable pages", "[CypherCommon][Tier0][Services]" )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    REQUIRE( info.nPageSize != 0u );

    page_allocator_t allocator{};
    REQUIRE( Cy_PageAllocatorInit( &allocator, info.nPageSize * 2u ) );
    void *pMemory = Cy_PageAllocatorCommit( &allocator, info.nPageSize );
    REQUIRE( pMemory != nullptr );

    static_cast<byte *>( pMemory )[0] = 0xABu;
    REQUIRE( static_cast<byte *>( pMemory )[0] == 0xABu );

    REQUIRE( Cy_PageAllocatorReset( &allocator ) );
    REQUIRE( allocator.nCommittedByteCount == 0u );
    REQUIRE( Cy_PageAllocatorShutdown( &allocator ) );
}

TEST_CASE( "Tier0 logging, validation, and memory diagnostics invoke callbacks", "[CypherCommon][Tier0][Services]" )
{
    g_logCallbackCalled = CY_FALSE;
    Cy_LogSetCallback( TestLogCallback, &g_logCallbackCalled );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, "hello" );
    REQUIRE( g_logCallbackCalled );
    Cy_LogSetCallback( nullptr, nullptr );

    g_validatorCallbackCalled = CY_FALSE;
    Cy_ValidatorSetCallback( TestValidatorCallback, nullptr );
    Cy_ValidatorReport( validator_severity_t::Info, "ok" );
    REQUIRE( g_validatorCallbackCalled );
    Cy_ValidatorSetCallback( nullptr, nullptr );

    g_memoryDebugCallbackCalled = CY_FALSE;
    Cy_MemoryDebugSetCallback( TestMemoryDebugCallback );
    i32 value = 0;
    memory_allocation_record_t record{};
    record.pMemory = &value;
    record.nByteCount = sizeof( value );
    record.nAlignment = alignof( i32 );
    record.pszTag = "test";

    REQUIRE( Cy_MemoryTrackerRecordAlloc( record ) );
    const memory_tracker_stats_t memoryStats = Cy_MemoryTrackerGetStats();
    REQUIRE( memoryStats.nLiveAllocationCount >= 1u );
    REQUIRE( memoryStats.nLiveByteCount >= sizeof( value ) );
    REQUIRE( Cy_MemoryTrackerRecordFree( &value ) );
    REQUIRE( g_memoryDebugCallbackCalled );
    Cy_MemoryDebugSetCallback( nullptr );
}

TEST_CASE( "Tier0 stats and profile counters can be updated", "[CypherCommon][Tier0][Services]" )
{
    stat_desc_t desc{};
    desc.pszName = "test.stat";
    desc.pszCategory = "Test";
    desc.pszDescription = "Test stat";
    desc.type = stat_value_type_t::U64;

    stat_id_t statId = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister( desc, &statId ) );
    REQUIRE( Cy_StatsSetU64( statId, 123u ) );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( statId, &value ) );
    REQUIRE( value.type == stat_value_type_t::U64 );
    REQUIRE( value.u64Value == 123u );

    profile_zone_desc_t zone{};
    zone.pszName = "Zone";
    zone.pszCategory = "Test";
    zone.location = CY_SOURCE_LOCATION;
    zone.flags = PROFILE_FLAG_CPU;

    Cy_ProfileResetState();
    Cy_ProfileSetSink( TestProfileSink, &g_profileEventCount );
    Cy_ProfileSetEnabled( CY_TRUE );

    const profile_token_t token = Cy_ProfileBeginZone( &zone );
    REQUIRE( token != 0u );
    REQUIRE( Cy_ProfileEndZone( token ) );
    REQUIRE( Cy_ProfileCounterSet( "profile.counter", 7 ) );
    REQUIRE( Cy_ProfileCounterAdd( "profile.counter", 5 ) );

    stat_value_t profileValue{};
    REQUIRE( Cy_StatsGetByName( "profile.counter", &profileValue ) );
    REQUIRE( profileValue.type == stat_value_type_t::I64 );
    REQUIRE( profileValue.i64Value == 12 );

    REQUIRE( Cy_ProfileFrameBegin() == 1u );
    REQUIRE( Cy_ProfileFrameEnd() );
    REQUIRE( g_profileEventCount >= 6u );

    Cy_ProfileSetEnabled( CY_FALSE );
    Cy_ProfileSetSink( nullptr );
}

TEST_CASE( "Tier0 minidump writes portable diagnostic file", "[CypherCommon][Tier0][Services]" )
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "CypherEngine_Test_Minidump.txt";
    const std::string pathString = path.string();
    std::filesystem::remove( path );

    minidump_info_t info{};
    info.pApplicationName = "CypherTest";
    info.pVersion = "1";
    info.pOutputPath = pathString.c_str();

    REQUIRE( Cy_MinidumpWrite( info ) == minidump_result_t::Ok );
    REQUIRE( std::filesystem::exists( path ) );

    std::string contents;
    {
        std::ifstream file( path );
        REQUIRE( file.is_open() );
        contents.assign(
            std::istreambuf_iterator<char>( file ),
            std::istreambuf_iterator<char>() );
    }
    REQUIRE( contents.find( "format=cypher-text-dump" ) != std::string::npos );
    REQUIRE( contents.find( "stack_frame_count=" ) != std::string::npos );

    REQUIRE( std::filesystem::remove( path ) );
}

TEST_CASE( "Tier0 intrusive thread-safe list pushes and pops nodes", "[CypherCommon][Tier0][Services]" )
{
    tslist_t list{};
    tslist_node_t a{};
    tslist_node_t b{};

    REQUIRE( Cy_TsListInit( &list ) );
    REQUIRE( Cy_TsListPop( &list ) == nullptr );

    REQUIRE( Cy_TsListPush( &list, &a ) );
    REQUIRE( Cy_TsListPush( &list, &b ) );

    REQUIRE( Cy_TsListPop( &list ) == &b );
    REQUIRE( Cy_TsListPop( &list ) == &a );
    REQUIRE( Cy_TsListPop( &list ) == nullptr );
    REQUIRE( Cy_TsListShutdown( &list ) );
}

TEST_CASE( "Tier0 miscellaneous platform services return sane values", "[CypherCommon][Tier0][Services]" )
{
    i32 value = 0;
    Cy_CachePrefetchRead( &value );
    Cy_CachePrefetchWrite( &value );
    REQUIRE( Cy_CacheGetLineSize() != 0u );

    cy_cpu_monitor_t cpuMonitor{};
    REQUIRE( Cy_CPUMonitorInit( &cpuMonitor ) );

    cy_cpu_monitor_sample_t sample{};
    REQUIRE( Cy_CPUMonitorSample( &cpuMonitor, &sample ) );
    REQUIRE( sample.nLogicalThreadCount >= 1u );
    REQUIRE( sample.totalUsagePercent >= 0.0f );
    REQUIRE( sample.totalUsagePercent <= 100.0f );
    REQUIRE( sample.processUsagePercent >= 0.0f );
    REQUIRE( sample.processUsagePercent <= 100.0f );

    REQUIRE( Cy_PerformanceCounterFrequency() != 0u );
    REQUIRE( Cy_PerformanceCounterNow() != 0u );
    REQUIRE( Cy_PerformanceCounterToSeconds(
                 Cy_PerformanceCounterFrequency() ) > 0.0 );

    dynamic_library_t library{};
    REQUIRE( Cy_DynamicLibraryInit( &library ) );
    REQUIRE_FALSE( Cy_DynamicLibraryLoad( &library, "/path/that/does/not/exist" ) );
    REQUIRE( Cy_DynamicLibraryGetSymbol( &library, "missing" ) == nullptr );

    Cy_LogToggleEnable( 0x2u );
    REQUIRE( Cy_LogToggleAnyEnabled( 0x2u ) );
    Cy_LogToggleDisable( 0x2u );
    REQUIRE_FALSE( Cy_LogToggleAnyEnabled( 0x2u ) );
    Cy_LogToggleReset();
}
