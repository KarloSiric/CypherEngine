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
    const system_info_t info = GetSystemInfo();

    REQUIRE( info.logical_thread_count >= 1u );
    REQUIRE( info.pointer_size == sizeof( void * ) );
    REQUIRE( info.cache_line_size == CY_CACHE_LINE_SIZE );
    REQUIRE( info.default_page_size == CYPHER_DEFAULT_PAGE_SIZE );
}

TEST_CASE( "SystemInfo exposes power-of-two cache and page sizing", "[CypherCommon][Tier0][SystemInfo]" )
{
    const system_info_t info = GetSystemInfo();

    REQUIRE( IsPowerOfTwoSize( info.cache_line_size ) );
    REQUIRE( IsPowerOfTwoSize( info.default_page_size ) );
    REQUIRE( info.default_page_size >= 4096u );
}

TEST_CASE( "SystemInfo mirrors compile-time endian and pointer width detection", "[CypherCommon][Tier0][SystemInfo]" )
{
    const system_info_t info = GetSystemInfo();

    REQUIRE( info.is_little_endian == ( CYPHER_ENDIAN_LITTLE != 0 ) );
    REQUIRE( info.is_64_bit == ( CYPHER_TARGET_64BIT != 0 ) );
    REQUIRE( info.is_64_bit == ( sizeof( void * ) == 8u ) );
}
