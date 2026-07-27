//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_CacheHints_Tests.cpp
//  Purpose: Tests Tier0 cache-hint portability and contracts.
//  Details: Covers null no-op behavior, every locality level, and reported cache-
//           line geometry without asserting non-observable prefetch effects.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Align.h"
#include "CypherCommon_CacheHints.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "CacheHints accepts null and every locality level", "[CypherCommon][Tier0][CacheHints]" )
{
    i32 value = 0;
    const cache_prefetch_locality_t localities[] = {
        cache_prefetch_locality_t::NonTemporal,
        cache_prefetch_locality_t::Low,
        cache_prefetch_locality_t::Medium,
        cache_prefetch_locality_t::High
    };

    for ( cache_prefetch_locality_t locality : localities ) {
        Cy_CachePrefetchRead( nullptr, locality );
        Cy_CachePrefetchWrite( nullptr, locality );
        Cy_CachePrefetchRead( &value, locality );
        Cy_CachePrefetchWrite( &value, locality );
    }

    REQUIRE( value == 0 );
}

TEST_CASE( "CacheHints reports valid cache-line geometry", "[CypherCommon][Tier0][CacheHints]" )
{
    const usize nLineSize = Cy_CacheGetLineSize();
    REQUIRE( nLineSize != 0u );
    REQUIRE( Cy_AlignIsPowerOfTwo( nLineSize ) );
}
