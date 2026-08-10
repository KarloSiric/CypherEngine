//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_RefCount_Tests.cpp
//  Purpose: Tests intrusive atomic reference counting.
//  Details: Initialization, acquisition, release, and final ownership transfer are
//           exercised through the public API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RefCount.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "RefCount transfers final destruction ownership",
           "[CypherCommon][Tier1][RefCount]" )
{
    ref_count_t count{};
    RefCount_Init( &count, 1u );
    REQUIRE( RefCount_Load( &count ) == 1u );
    REQUIRE( RefCount_AddRef( &count ) == 2u );
    REQUIRE( RefCount_Release( &count ) == 1u );
    REQUIRE( RefCount_Release( &count ) == 0u );
    REQUIRE( RefCount_Load( &count ) == 0u );
}
