//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_WideChar_Tests.cpp
//  Purpose: Verifies Tier1 wide-character helpers.
//  Details: Tests null handling, ordering, bounded copying, truncation, and
//           guaranteed destination termination.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_WideChar.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::common
{

TEST_CASE( "WideChar reports lengths and lexical ordering" )
{
    CHECK( WChar_Length( nullptr ) == 0u );
    CHECK( WChar_Length( L"" ) == 0u );
    CHECK( WChar_Length( L"Cypher" ) == 6u );

    CHECK( WChar_Compare( nullptr, L"" ) == 0 );
    CHECK( WChar_Compare( L"alpha", L"alpha" ) == 0 );
    CHECK( WChar_Compare( L"alpha", L"beta" ) < 0 );
    CHECK( WChar_Compare( L"beta", L"alpha" ) > 0 );
    CHECK( WChar_Compare( L"alpha", L"alphabet" ) < 0 );
}

TEST_CASE( "WideChar copy is bounded and always terminates valid destinations" )
{
    wchar_engine_t destination[4]{ L'x', L'x', L'x', L'x' };

    CHECK( WChar_Copy( destination, L"hello", 4u ) == 5u );
    CHECK( destination[0] == L'h' );
    CHECK( destination[1] == L'e' );
    CHECK( destination[2] == L'l' );
    CHECK( destination[3] == L'\0' );

    CHECK( WChar_Copy( destination, nullptr, 4u ) == 0u );
    CHECK( destination[0] == L'\0' );
    CHECK( WChar_Copy( nullptr, L"ignored", 4u ) == 0u );
    CHECK( WChar_Copy( destination, L"ignored", 0u ) == 0u );
}

} // namespace cypher::common
