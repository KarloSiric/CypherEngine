//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Warnings_Tests.cpp
//  Purpose: Compile-tests Tier0 warning-state helpers.
//  Details: These probes ensure scoped suppression syntax remains valid and does
//           not leak warning state beyond the matching push/pop region.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Warnings.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

CYPHER_WARNING_PUSH()
CYPHER_WARNING_DISABLE_UNUSED_PARAMETER()

int SuppressedUnusedParameter( int nUnused )
{
    return 7;
}

CYPHER_WARNING_POP()

} // namespace

TEST_CASE( "Warnings scoped helpers preserve compiled behavior", "[CypherCommon][Tier0][Warnings]" )
{
    REQUIRE( SuppressedUnusedParameter( 0 ) == 7 );
}

