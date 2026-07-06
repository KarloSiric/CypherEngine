//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_SystemInfo_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 system information behavior.
//  Details: This test file validates the basic runtime and target properties exposed
//           before higher-level engine systems are initialized.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SystemInfo.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

bool IsPowerOfTwoSize( usize nValue )
{
    return nValue != 0u && ( nValue & ( nValue - 1u ) ) == 0u;
}

} // namespace

TEST_CASE( "SystemInfo reports sane runtime sizing values", "[CypherCommon][Tier0][SystemInfo]" )
{
    REQUIRE( Cy_SystemInfoInit() );

    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    REQUIRE( pInfo != nullptr );

    REQUIRE( pInfo->cpu.logicalThreadCount >= 1u );
    REQUIRE( pInfo->platform.pointerSize == sizeof( void * ) );
    REQUIRE( pInfo->cpu.cacheLineSize >= 1u );
    REQUIRE( pInfo->memory.pageSize >= 4096u );
    REQUIRE( pInfo->memory.allocationGranularity >= pInfo->memory.pageSize );
    REQUIRE( pInfo->build.pszEngineName != nullptr );
    REQUIRE( pInfo->build.pszCompilerName != nullptr );
    REQUIRE( pInfo->os.szName[0] != '\0' );
    REQUIRE( pInfo->process.processId != 0u );
}

TEST_CASE( "SystemInfo exposes power-of-two cache and page sizing", "[CypherCommon][Tier0][SystemInfo]" )
{
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    REQUIRE( pInfo != nullptr );

    REQUIRE( IsPowerOfTwoSize( pInfo->cpu.cacheLineSize ) );
    REQUIRE( IsPowerOfTwoSize( pInfo->memory.pageSize ) );
    REQUIRE( IsPowerOfTwoSize( pInfo->memory.allocationGranularity ) );
}

TEST_CASE( "SystemInfo mirrors compile-time endian and pointer width detection", "[CypherCommon][Tier0][SystemInfo]" )
{
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    REQUIRE( pInfo != nullptr );

    REQUIRE( pInfo->platform.isLittleEndian == ( CYPHER_ENDIAN_LITTLE != 0 ) );
    REQUIRE( pInfo->platform.is64Bit == ( CYPHER_TARGET_64BIT != 0 ) );
    REQUIRE( pInfo->platform.is64Bit == ( sizeof( void * ) == 8u ) );
}

TEST_CASE( "SystemInfo dynamic queries report sane memory and disk status", "[CypherCommon][Tier0][SystemInfo]" )
{
    const cy_system_memory_status_t memory = Cy_SystemInfoQueryMemoryStatus();
    REQUIRE( memory.totalPhysicalBytes >= memory.availablePhysicalBytes );
    REQUIRE( memory.totalPhysicalBytes != 0u );
    REQUIRE( memory.pressure != CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN );

    const cy_system_disk_status_t disk = Cy_SystemInfoQueryDiskStatus( "." );
    REQUIRE( disk.totalBytes >= disk.freeBytes );
    REQUIRE( disk.totalBytes >= disk.availableBytes );
    REQUIRE( disk.totalBytes != 0u );
}

TEST_CASE( "SystemInfo CPU feature helpers expose stable names", "[CypherCommon][Tier0][SystemInfo]" )
{
    REQUIRE( Cy_SystemInfoHasCpuFeature( CY_SYSTEM_CPU_FEATURE_SSE2, CY_SYSTEM_CPU_FEATURE_SSE2 ) );
    REQUIRE_FALSE( Cy_SystemInfoHasCpuFeature( CY_SYSTEM_CPU_FEATURE_NONE, CY_SYSTEM_CPU_FEATURE_SSE2 ) );
    REQUIRE( Cy_SystemInfoCpuFeatureName( CY_SYSTEM_CPU_FEATURE_AVX2 )[0] != '\0' );
    REQUIRE( Cy_SystemInfoCpuFeatureName( static_cast<cy_system_cpu_feature_flags_t>( CYPHER_BIT64( 63 ) ) )[0] != '\0' );
}

TEST_CASE( "SystemInfo report formatting is bounded and queryable", "[CypherCommon][Tier0][SystemInfo]" )
{
    char szReport[CY_SYSTEMINFO_REPORT_MAX] = {};

    const usize cchRequired = Cy_SystemInfoFormatReport( szReport, sizeof( szReport ) );
    REQUIRE( cchRequired > 0u );
    REQUIRE( szReport[0] != '\0' );
    REQUIRE( szReport[sizeof( szReport ) - 1u] == '\0' );

    const usize cchProbe = Cy_SystemInfoFormatReport( nullptr, 0u );
    REQUIRE( cchProbe == cchRequired );
}
