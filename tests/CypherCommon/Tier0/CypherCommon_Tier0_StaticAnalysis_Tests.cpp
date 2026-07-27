//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_StaticAnalysis_Tests.cpp
//  Purpose: Compile-tests Tier0 optimizer and analyzer contracts.
//  Details: These probes verify valid assumptions, suppression syntax, and
//           unreachable markers across supported compilers and build modes.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StaticAnalysis.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

int DivideKnownPositive( int nValue )
{
    CY_ANALYSIS_ASSUME( nValue > 0 );
    return 100 / nValue;
}

int SelectKnownBranch( bool bFirst )
{
    if ( bFirst ) {
        return 1;
    }

    if ( !bFirst ) {
        return 2;
    }

    CY_ANALYSIS_UNREACHABLE();
}

} // namespace

TEST_CASE( "StaticAnalysis preserves valid control flow", "[CypherCommon][Tier0][StaticAnalysis]" )
{
    CY_ANALYSIS_SUPPRESS( 4127 )
    REQUIRE( DivideKnownPositive( 4 ) == 25 );
    REQUIRE( SelectKnownBranch( true ) == 1 );
    REQUIRE( SelectKnownBranch( false ) == 2 );
}

