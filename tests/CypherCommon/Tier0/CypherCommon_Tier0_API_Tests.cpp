//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_API_Tests.cpp
//  Purpose: Tests Tier0 binary-boundary macro contracts.
//  Details: These compile probes verify static Common linkage, generic symbol
//           visibility, C linkage, and the public calling convention.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_API.h"

#include <catch2/catch_test_macros.hpp>

#if !defined( CYPHER_COMMON_STATIC )
    #error "CypherCommonTier0 tests must consume the configured static Common target."
#endif

namespace
{

CYPHER_API_LOCAL int LocalFunction()
{
    return 7;
}

} // namespace

CYPHER_EXTERN_C CYPHER_COMMON_API int CYPHER_CALL CypherCommonApiTestFunction()
{
    return 11;
}

TEST_CASE( "API linkage macros compile and preserve calls", "[CypherCommon][Tier0][API]" )
{
    REQUIRE( LocalFunction() == 7 );
    REQUIRE( CypherCommonApiTestFunction() == 11 );
}

