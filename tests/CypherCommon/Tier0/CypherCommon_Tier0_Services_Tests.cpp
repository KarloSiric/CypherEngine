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

#include "CypherCommon/Tier0/CypherCommon_BuildId.h"
#include "CypherCommon/Tier0/CypherCommon_CacheHints.h"
#include "CypherCommon/Tier0/CypherCommon_CommandLineBase.h"
#include "CypherCommon/Tier0/CypherCommon_CPUMonitoring.h"
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
#include "CypherCommon/Tier0/CypherCommon_ProgressBar.h"
#include "CypherCommon/Tier0/CypherCommon_SourceLocation.h"
#include "CypherCommon/Tier0/CypherCommon_Stats.h"
#include "CypherCommon/Tier0/CypherCommon_TestThread.h"
#include "CypherCommon/Tier0/CypherCommon_TsList.h"
#include "CypherCommon/Tier0/CypherCommon_Validator.h"
#include "CypherCommon/Tier0/CypherCommon_WideChar.h"

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

void TestLogCallback( const log_record_t &record, void *pUserData )
{
    auto *pCalled = static_cast<bool_t *>( pUserData );
    *pCalled = record.pMessage != nullptr;
}

void TestValidatorCallback( validator_severity_t, const char *pMessage )
{
    g_validatorCallbackCalled = pMessage != nullptr;
}

void TestMemoryDebugCallback( memory_debug_event_t, void *, usize, const char * )
{
    g_memoryDebugCallbackCalled = CY_TRUE;
}

i32 TestThreadProc( void *pUserData )
{
    auto *pValue = static_cast<i32 *>( pUserData );
    ++( *pValue );
    return *pValue;
}

} // namespace

TEST_CASE( "Tier0 error helpers pack domains and local codes", "[CypherCommon][Tier0][Services]" )
{
    const error_code_t errorCode = Cy_ErrorMake( error_domain_t::COM_DOMAIN_FILESYSTEM, 77u );

    REQUIRE( Cy_ErrorDomain( errorCode ) == error_domain_t::COM_DOMAIN_FILESYSTEM );
    REQUIRE( Cy_ErrorLocalCode( errorCode ) == 77u );
    REQUIRE( Cy_ErrorSucceeded( common_error_t::OK ) );
    REQUIRE( Cy_ErrorFailed( common_error_t::ERR_FAILED ) );
    REQUIRE( Cy_ErrorName( common_error_t::ERR_TIMEOUT ) != nullptr );
    REQUIRE( Cy_ErrorDomainName( error_domain_t::COM_DOMAIN_TOOLS ) != nullptr );
}

TEST_CASE( "Tier0 handle helpers pack index and generation", "[CypherCommon][Tier0][Services]" )
{
    const handle32_t handle = Cy_Handle32_Make( 12u, 34u );
    const handle_parts32_t parts = Cy_Handle32_Unpack( handle );

    REQUIRE( Cy_Handle32_IsValid( handle ) );
    REQUIRE( parts.index == 12u );
    REQUIRE( parts.generation == 34u );
    REQUIRE( Cy_Handle32_Index( handle ) == 12u );
    REQUIRE( Cy_Handle32_Generation( handle ) == 34u );
    REQUIRE( Cy_Handle64_IsValid( Cy_Handle64_Make( 1u, 2u, 3u ) ) );
}

TEST_CASE( "Tier0 build id and module helpers produce stable names", "[CypherCommon][Tier0][Services]" )
{
    char szBuild[256] = {};
    const build_id_t *pBuild = Cy_BuildId_GetEngine();

    REQUIRE( pBuild != nullptr );
    Cy_BuildId_Format( *pBuild, szBuild, sizeof( szBuild ) );
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

    REQUIRE( Cy_SourceLocation_Format( location, szLocation, sizeof( szLocation ) ) == szLocation );
    REQUIRE( szLocation[0] != '\0' );
}

TEST_CASE( "Tier0 command line base handles flags and values", "[CypherCommon][Tier0][Services]" )
{
    const char *args[] = {
        "tool",
        "-game",
        "reap",
        "--map=arena01",
        "/verbose"
    };

    command_line_base_t commandLine{};
    CommandLineBase_Set( &commandLine, 5, args );

    REQUIRE( CommandLineBase_Has( &commandLine, "game" ) );
    REQUIRE( CommandLineBase_Find( &commandLine, "game" ) == args[2] );
    REQUIRE( CommandLineBase_Find( &commandLine, "map" ) != nullptr );
    REQUIRE( CommandLineBase_Find( &commandLine, "verbose" ) != nullptr );
    REQUIRE( CommandLineBase_Find( &commandLine, "missing" ) == nullptr );
}

TEST_CASE( "Tier0 progress and wide char helpers are bounded", "[CypherCommon][Tier0][Services]" )
{
    progress_bar_t progress{};
    ProgressBar_Begin( &progress, "Cook", 100u );
    ProgressBar_Update( &progress, 150u );
    REQUIRE( progress.completed_work == 100u );
    ProgressBar_End( &progress );
    REQUIRE( progress.completed_work == 100u );

    wchar_engine_t buffer[8] = {};
    REQUIRE( WChar_Copy( buffer, L"Cypher", 8u ) == 6u );
    REQUIRE( WChar_Length( buffer ) == 6u );
    REQUIRE( WChar_Compare( buffer, L"Cypher" ) == 0 );
}

TEST_CASE( "Tier0 environment and process helpers return basic process state", "[CypherCommon][Tier0][Services]" )
{
    REQUIRE( Process_GetCurrentId() != 0u );
    REQUIRE( Process_GetExecutablePath() != nullptr );

    REQUIRE( Environment_Set( "CYPHER_TEST_ENV", "ok" ) );
    char szValue[16] = {};
    REQUIRE( Environment_Get( "CYPHER_TEST_ENV", szValue, sizeof( szValue ) ) == 2u );
    REQUIRE( szValue[0] == 'o' );
    REQUIRE( Environment_Has( "CYPHER_TEST_ENV" ) );
}

TEST_CASE( "Tier0 platform memory and page allocator reserve writable pages", "[CypherCommon][Tier0][Services]" )
{
    const platform_memory_info_t info = PlatformMemory_GetInfo();
    REQUIRE( info.page_size != 0u );

    page_allocator_t allocator{};
    REQUIRE( PageAllocator_Init( &allocator, info.page_size * 2u ) );
    void *pMemory = PageAllocator_Commit( &allocator, info.page_size );
    REQUIRE( pMemory != nullptr );

    static_cast<byte *>( pMemory )[0] = 0xABu;
    REQUIRE( static_cast<byte *>( pMemory )[0] == 0xABu );

    PageAllocator_Reset( &allocator );
    REQUIRE( allocator.cbCommitted == 0u );
    PageAllocator_Shutdown( &allocator );
}

TEST_CASE( "Tier0 logging, validation, and memory diagnostics invoke callbacks", "[CypherCommon][Tier0][Services]" )
{
    g_logCallbackCalled = CY_FALSE;
    Cy_LogSetCallback( TestLogCallback, &g_logCallbackCalled );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, "hello" );
    REQUIRE( g_logCallbackCalled );
    Cy_LogSetCallback( nullptr, nullptr );

    g_validatorCallbackCalled = CY_FALSE;
    Validator_SetCallback( TestValidatorCallback );
    Validator_Report( validator_severity_t::Info, "ok" );
    REQUIRE( g_validatorCallbackCalled );
    Validator_SetCallback( nullptr );

    g_memoryDebugCallbackCalled = CY_FALSE;
    MemoryDebug_SetCallback( TestMemoryDebugCallback );
    i32 value = 0;
    memory_allocation_record_t record{};
    record.pMemory = &value;
    record.cbSize = sizeof( value );
    record.alignment = alignof( i32 );
    record.pTag = "test";

    MemoryTracker_RecordAlloc( record );
    REQUIRE( MemoryTracker_GetLiveAllocationCount() >= 1u );
    REQUIRE( MemoryTracker_GetLiveByteCount() >= sizeof( value ) );
    MemoryTracker_RecordFree( &value );
    REQUIRE( g_memoryDebugCallbackCalled );
    MemoryDebug_SetCallback( nullptr );
}

TEST_CASE( "Tier0 stats and profile counters can be updated", "[CypherCommon][Tier0][Services]" )
{
    stat_desc_t desc{};
    desc.pName = "test.stat";
    desc.pCategory = "Test";
    desc.pDescription = "Test stat";
    desc.type = stat_value_type_t::U64;

    Cy_StatsRegister( desc );
    Cy_StatsSetU64( "test.stat", 123u );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( "test.stat", &value ) );
    REQUIRE( value.type == stat_value_type_t::U64 );
    REQUIRE( value.u64Value == 123u );

    profile_zone_desc_t zone{};
    zone.pName = "Zone";
    zone.pCategory = "Test";
    zone.location = CY_SOURCE_LOCATION;
    zone.flags = PROFILE_FLAG_CPU;

    const profile_token_t token = Cy_ProfileBeginZone( zone );
    REQUIRE( token != 0u );
    Cy_ProfileEndZone( token );
    Cy_ProfileCounterSet( "profile.counter", 7 );
    Cy_ProfileCounterAdd( "profile.counter", 5 );

    stat_value_t profileValue{};
    REQUIRE( Cy_StatsGet( "profile.counter", &profileValue ) );
    REQUIRE( profileValue.type == stat_value_type_t::I64 );
    REQUIRE( profileValue.i64Value == 12 );

    Cy_ProfileFrameBegin();
    Cy_ProfileFrameEnd();
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

    REQUIRE( Minidump_Write( info ) );
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

    TsList_Init( &list );
    REQUIRE( TsList_Pop( &list ) == nullptr );

    TsList_Push( &list, &a );
    TsList_Push( &list, &b );

    REQUIRE( TsList_Pop( &list ) == &b );
    REQUIRE( TsList_Pop( &list ) == &a );
    REQUIRE( TsList_Pop( &list ) == nullptr );
}

TEST_CASE( "Tier0 test thread helper runs callback", "[CypherCommon][Tier0][Services]" )
{
    i32 value = 4;
    const test_thread_result_t result = TestThread_Run( TestThreadProc, &value );

    REQUIRE( result.completed );
    REQUIRE( result.exit_code == 5 );
    REQUIRE( value == 5 );
}

TEST_CASE( "Tier0 miscellaneous platform services return sane values", "[CypherCommon][Tier0][Services]" )
{
    i32 value = 0;
    Cache_PrefetchRead( &value );
    Cache_PrefetchWrite( &value );
    REQUIRE( Cache_GetLineSize() != 0u );

    cpu_monitor_sample_t sample{};
    REQUIRE( CPUMonitoring_Sample( &sample ) );
    REQUIRE( sample.logical_thread_count >= 1u );
    REQUIRE( sample.total_usage >= 0.0f );
    REQUIRE( sample.total_usage <= 100.0f );
    REQUIRE( sample.process_usage >= 0.0f );
    REQUIRE( sample.process_usage <= 100.0f );

    cpu_monitor_sample_t secondSample{};
    REQUIRE( CPUMonitoring_Sample( &secondSample ) );
    REQUIRE( secondSample.logical_thread_count == sample.logical_thread_count );
    REQUIRE( secondSample.total_usage >= 0.0f );
    REQUIRE( secondSample.total_usage <= 100.0f );
    REQUIRE( secondSample.process_usage >= 0.0f );
    REQUIRE( secondSample.process_usage <= 100.0f );

    REQUIRE( PerformanceCounter_Frequency() != 0u );
    REQUIRE( PerformanceCounter_Now() != 0u );
    REQUIRE( PerformanceCounter_ToSeconds( PerformanceCounter_Frequency() ) > 0.0 );

    dynamic_library_t library{};
    REQUIRE_FALSE( DynamicLibrary_Load( &library, "/path/that/does/not/exist" ) );
    REQUIRE( DynamicLibrary_GetSymbol( &library, "missing" ) == nullptr );

    LogToggle_Enable( 0x2u );
    REQUIRE( LogToggle_IsEnabled( 0x2u ) );
    LogToggle_Disable( 0x2u );
    REQUIRE_FALSE( LogToggle_IsEnabled( 0x2u ) );
}
