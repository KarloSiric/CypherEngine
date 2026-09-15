//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherSystem/CypherSystem_Runtime_Tests.cpp
//  Purpose: Verifies the engine-facing lifecycle and operating-system facade.
//  Details: Tests call CypherSystem rather than Tier0 so missing delegation,
//           ownership, and initialization behavior fails at the public boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Public.h"
#include "CypherCommon_Platform.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

using namespace cypher::engine::sys;

namespace
{

struct system_scope_t {
    std::filesystem::path root{};
    std::filesystem::path base{};
    std::filesystem::path user{};
    std::string baseString{};
    std::string userString{};
    const char *arguments[5]{};
    init_info_t initInfo{};
    sys_error_t result{ sys_error_t::ERR_INTERNAL_ERROR };

    system_scope_t()
    {
        root = std::filesystem::temp_directory_path() /
            ( "cypher_system_" + std::to_string( Sys_GetCurrentProcessId() ) + "_" +
              std::to_string( Sys_TimeNowNanoseconds() ) );
        base = root / "base";
        user = root / "user";

        std::error_code error{};
        std::filesystem::create_directories( base, error );
        if ( error ) {
            return;
        }

        baseString = base.string();
        userString = user.string();
        arguments[0] = "cypher_system_tests";
        arguments[1] = "-basedir";
        arguments[2] = baseString.c_str();
        arguments[3] = "-userpath";
        arguments[4] = userString.c_str();
        initInfo = { 5, arguments, "CypherSystemTests", "CypherTests" };
        result = Sys_Init( initInfo );
    }

    ~system_scope_t()
    {
        if ( Sys_IsInitialized() ) {
            (void)Sys_Shutdown();
        }
        std::error_code error{};
        std::filesystem::remove_all( root, error );
    }
};

} // namespace

TEST_CASE( "System lifecycle validates startup and owns absolute paths", "[CypherSystem][Lifecycle]" )
{
    paths_t unavailablePaths{};
    std::memset( &unavailablePaths, 0xA5, sizeof( unavailablePaths ) );
    REQUIRE( Sys_GetPaths( unavailablePaths ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( unavailablePaths.basePath[0] == '\0' );

    init_info_t invalidInfo{};
    REQUIRE( Sys_Init( invalidInfo ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE_FALSE( Sys_IsInitialized() );

    const char *invalidArguments[] = { "cypher_system_runtime_tests", nullptr };
    const init_info_t nullArgumentInfo = {
        2,
        invalidArguments,
        "CypherSystemRuntimeTests",
        "CypherTests"
    };
    REQUIRE( Sys_Init( nullArgumentInfo ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE_FALSE( Sys_IsInitialized() );

    const std::string overlongName( SYS_MAX_NAME_LENGTH, 'x' );
    const init_info_t overlongNameInfo = {
        0,
        nullptr,
        overlongName.c_str(),
        "CypherTests"
    };
    REQUIRE( Sys_Init( overlongNameInfo ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE_FALSE( Sys_IsInitialized() );

    system_scope_t system{};
    REQUIRE( system.result == sys_error_t::OK );
    REQUIRE( Sys_IsInitialized() );
    REQUIRE( Sys_Init( system.initInfo ) == sys_error_t::ERR_IS_INIT );

    paths_t paths{};
    REQUIRE( Sys_GetPaths( paths ) == sys_error_t::OK );
    REQUIRE( std::strcmp( Sys_Paths().basePath, paths.basePath ) == 0 );
    REQUIRE( std::strcmp( Sys_Paths().userPath, paths.userPath ) == 0 );
    REQUIRE( std::filesystem::path( paths.basePath ).is_absolute() );
    REQUIRE( std::filesystem::path( paths.userPath ).is_absolute() );
    REQUIRE( std::filesystem::equivalent( paths.basePath, system.base ) );
    REQUIRE( std::filesystem::equivalent( paths.userPath, system.user ) );
    REQUIRE( std::strcmp( Sys_PathBasename( "/one/two/file.bin" ), "file.bin" ) == 0 );
    REQUIRE( std::strcmp( Sys_PathBasename( "C:\\one\\two\\file.bin" ), "file.bin" ) == 0 );
    REQUIRE( std::strcmp( Sys_PathBasename( nullptr ), "" ) == 0 );

    REQUIRE_FALSE( Sys_IsQuitRequested() );
    std::thread worker( []() noexcept { Sys_RequestQuit(); } );
    worker.join();
    REQUIRE( Sys_IsQuitRequested() );
}

TEST_CASE( "System error helpers expose stable names and common error codes", "[CypherSystem][Error]" )
{
    REQUIRE( std::strcmp( Sys_ErrorName( sys_error_t::ERR_RESOURCE_BUSY ), "ERR_RESOURCE_BUSY" ) == 0 );
    REQUIRE( std::strstr( Sys_ErrorDesc( sys_error_t::ERR_RESOURCE_BUSY ), "resource" ) != nullptr );
    REQUIRE( std::strcmp(
        Sys_ErrorName( sys_error_t::ERR_GRAPHICS_CONTEXT_FAILED ),
        "ERR_GRAPHICS_CONTEXT_FAILED" ) == 0 );
    REQUIRE( std::strstr(
        Sys_ErrorDesc( sys_error_t::ERR_GRAPHICS_CONFIG_UNSUPPORTED ),
        "graphics" ) != nullptr );
    REQUIRE( Sys_ErrorCode( sys_error_t::ERR_INVALID_ARGUMENT ) != 0u );
}

TEST_CASE( "System reports machine, process, disk, and live memory state", "[CypherSystem][Info]" )
{
    system_scope_t system{};
    REQUIRE( system.result == sys_error_t::OK );

    const system_info_t *info = Sys_GetSystemInfo();
    REQUIRE( info != nullptr );
    REQUIRE( info->process.processId == Sys_GetCurrentProcessId() );
    REQUIRE( info->platform.pointerSize == sizeof( void * ) );
    REQUIRE( info->cpu.logicalThreadCount > 0u );
    REQUIRE( info->memory.pageSize > 0u );

    const system_memory_status_t memory = Sys_QueryMemoryStatus();
    REQUIRE( memory.totalPhysicalBytes > 0u );

    const system_power_state_t power = Sys_QueryPowerState();
    REQUIRE( static_cast<cypher::common::u32>( power ) <=
        static_cast<cypher::common::u32>( ::cypher::common::CY_SYSTEM_POWER_NO_BATTERY ) );

    const system_disk_status_t disk = Sys_QueryDiskStatus( system.root.string().c_str() );
    REQUIRE( disk.isValid == ::cypher::common::CY_TRUE );
    REQUIRE( disk.totalBytes > 0u );

    char report[::cypher::common::CY_SYSTEMINFO_REPORT_MAX]{};
    const cypher::common::usize required = Sys_FormatSystemReport( report, sizeof( report ) );
    REQUIRE( required > 0u );
    REQUIRE( std::strstr( report, "Cypher System Info" ) != nullptr );
    REQUIRE( std::strstr( report, "cpu:" ) != nullptr );

    cpu_monitor_t monitor{};
    REQUIRE( Sys_InitCpuMonitor( monitor ) );
    Sys_SleepMilliseconds( 1u );
    cpu_monitor_sample_t sample{};
    REQUIRE( Sys_SampleCpuMonitor( monitor, sample ) );
    REQUIRE( sample.nLogicalThreadCount > 0u );
    REQUIRE( Sys_ResetCpuMonitor( monitor ) );
}

TEST_CASE( "System environment facade distinguishes values and missing names", "[CypherSystem][Environment]" )
{
    const std::string variableName =
        "CYPHER_SYSTEM_TEST_" + std::to_string( Sys_GetCurrentProcessId() );
    (void)Sys_UnsetEnvironment( variableName.c_str() );

    char value[64]{};
    environment_get_result_t result =
        Sys_GetEnvironment( variableName.c_str(), value, sizeof( value ) );
    REQUIRE( result.exists == ::cypher::common::CY_FALSE );

    REQUIRE( Sys_SetEnvironment( variableName.c_str(), "runtime-value" ) );
    REQUIRE( Sys_HasEnvironment( variableName.c_str() ) );
    result = Sys_GetEnvironment( variableName.c_str(), value, sizeof( value ) );
    REQUIRE( result.exists == ::cypher::common::CY_TRUE );
    REQUIRE( result.isTruncated == ::cypher::common::CY_FALSE );
    REQUIRE( std::strcmp( value, "runtime-value" ) == 0 );

    char shortValue[4]{};
    result = Sys_GetEnvironment( variableName.c_str(), shortValue, sizeof( shortValue ) );
    REQUIRE( result.exists == ::cypher::common::CY_TRUE );
    REQUIRE( result.isTruncated == ::cypher::common::CY_TRUE );
    REQUIRE( result.cchRequired > sizeof( shortValue ) );

    REQUIRE( Sys_UnsetEnvironment( variableName.c_str() ) );
    REQUIRE_FALSE( Sys_HasEnvironment( variableName.c_str() ) );
}

TEST_CASE( "System dynamic library facade loads and resolves a host symbol", "[CypherSystem][Library]" )
{
    dynamic_library_t library{};
    REQUIRE( Sys_InitLibrary( library ) );

#if CYPHER_PLATFORM_WINDOWS
    constexpr const char *LIBRARY_NAME = "kernel32.dll";
    constexpr const char *SYMBOL_NAME = "GetCurrentProcessId";
#elif CYPHER_PLATFORM_MACOS
    constexpr const char *LIBRARY_NAME = "/usr/lib/libSystem.B.dylib";
    constexpr const char *SYMBOL_NAME = "malloc";
#elif CYPHER_PLATFORM_LINUX
    constexpr const char *LIBRARY_NAME = "libc.so.6";
    constexpr const char *SYMBOL_NAME = "malloc";
#else
    constexpr const char *LIBRARY_NAME = "";
    constexpr const char *SYMBOL_NAME = "";
#endif

    REQUIRE( Sys_LoadLibrary( library, LIBRARY_NAME ) );
    REQUIRE( Sys_IsLibraryLoaded( library ) );
    REQUIRE( Sys_GetLibrarySymbol( library, SYMBOL_NAME ) != nullptr );
    REQUIRE( Sys_GetLibrarySymbol( library, "CypherSymbolThatMustNotExist" ) == nullptr );
    REQUIRE( Sys_GetLibraryError( library )[0] != '\0' );
    REQUIRE( Sys_UnloadLibrary( library ) );
    REQUIRE_FALSE( Sys_IsLibraryLoaded( library ) );
}

TEST_CASE( "System monotonic time and virtual memory facade are operational", "[CypherSystem][Time][Memory]" )
{
    const cypher::common::u64 start = Sys_TimeNowNanoseconds();
    Sys_SleepMilliseconds( 2u );
    const cypher::common::u64 end = Sys_TimeNowNanoseconds();
    REQUIRE( end >= start );
    REQUIRE( Sys_TimeNowSeconds() > 0.0 );

    const cypher::common::usize pageSize = Sys_VirtualPageSize();
    REQUIRE( pageSize > 0u );
    REQUIRE( Sys_VirtualReserve( 0u ) == nullptr );
    REQUIRE( Sys_VirtualReserve( pageSize - 1u ) == nullptr );
    REQUIRE( Sys_VirtualCommit( nullptr, pageSize ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE( Sys_VirtualDecommit( nullptr, pageSize ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE( Sys_VirtualRelease( nullptr, pageSize ) == sys_error_t::ERR_INVALID_ARGUMENT );

    void *memory = Sys_VirtualReserve( pageSize * 2u );
    REQUIRE( memory != nullptr );
    auto *unalignedMemory = static_cast<cypher::common::u8 *>( memory ) + 1u;
    REQUIRE( Sys_VirtualCommit( unalignedMemory, pageSize ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE( Sys_VirtualCommit( memory, pageSize - 1u ) == sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE( Sys_VirtualCommit( memory, pageSize * 2u ) == sys_error_t::OK );

    auto *bytes = static_cast<cypher::common::u8 *>( memory );
    bytes[0] = 0xA5u;
    bytes[pageSize * 2u - 1u] = 0x5Au;
    REQUIRE( bytes[0] == 0xA5u );
    REQUIRE( bytes[pageSize * 2u - 1u] == 0x5Au );

    REQUIRE( Sys_VirtualDecommit( memory, pageSize * 2u ) == sys_error_t::OK );
    REQUIRE( Sys_VirtualCommit( memory, pageSize * 2u ) == sys_error_t::OK );
    REQUIRE( Sys_VirtualRelease( memory, pageSize * 2u ) == sys_error_t::OK );

    std::tm localTime{};
    REQUIRE( Sys_LocalTime( std::time( nullptr ), localTime ) );
}
