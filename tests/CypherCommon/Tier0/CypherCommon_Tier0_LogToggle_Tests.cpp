//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_LogToggle_Tests.cpp
//  Purpose: Tests Tier0 log category filtering.
//  Details: These checks protect channel masks, any/all semantics, mutation,
//           reset behavior, and integration with callback delivery.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_LogToggle.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

void CountLog( const log_record_t &, void *pUserData ) noexcept
{
    auto *pCount = static_cast<u32 *>( pUserData );
    ++( *pCount );
}

} // namespace

TEST_CASE( "LogToggle builds one stable bit per channel", "[CypherCommon][Tier0][LogToggle]" )
{
    STATIC_REQUIRE( Cy_LogChannelMask( log_channel_t::Common ) == 1ull );
    STATIC_REQUIRE( Cy_LogChannelMask( log_channel_t::Memory ) == 2ull );
    STATIC_REQUIRE( Cy_LogChannelMask( log_channel_t::Count ) == 0ull );
    STATIC_REQUIRE( static_cast<u32>( log_channel_t::Count ) < 64u );
}

TEST_CASE( "LogToggle distinguishes any and all enabled categories", "[CypherCommon][Tier0][LogToggle]" )
{
    const log_category_mask_t commonMask = Cy_LogChannelMask( log_channel_t::Common );
    const log_category_mask_t memoryMask = Cy_LogChannelMask( log_channel_t::Memory );

    Cy_LogToggleSetMask( commonMask );
    REQUIRE( Cy_LogToggleAnyEnabled( commonMask | memoryMask ) );
    REQUIRE_FALSE( Cy_LogToggleAllEnabled( commonMask | memoryMask ) );

    Cy_LogToggleEnable( memoryMask );
    REQUIRE( Cy_LogToggleAllEnabled( commonMask | memoryMask ) );

    Cy_LogToggleDisable( commonMask );
    REQUIRE_FALSE( Cy_LogToggleChannelEnabled( log_channel_t::Common ) );
    REQUIRE( Cy_LogToggleChannelEnabled( log_channel_t::Memory ) );

    Cy_LogToggleReset();
    REQUIRE( Cy_LogToggleGetMask() == CY_LOG_CATEGORY_ALL );
}

TEST_CASE( "LogToggle suppresses disabled callback records", "[CypherCommon][Tier0][LogToggle]" )
{
    u32 nCallCount = 0u;
    Cy_LogSetCallback( CountLog, &nCallCount );

    const log_category_mask_t commonMask = Cy_LogChannelMask( log_channel_t::Common );
    Cy_LogToggleDisable( commonMask );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, "suppressed" );
    REQUIRE( nCallCount == 0u );

    Cy_LogToggleEnable( commonMask );
    Cy_LogWrite( log_level_t::Info, log_channel_t::Common, "delivered" );
    REQUIRE( nCallCount == 1u );

    Cy_LogSetCallback( nullptr, nullptr );
    Cy_LogToggleReset();
}
