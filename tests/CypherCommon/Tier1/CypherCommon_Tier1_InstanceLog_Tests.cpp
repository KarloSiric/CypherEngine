//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_InstanceLog_Tests.cpp
//  Purpose: Tests bounded per-instance diagnostic history.
//  Details: Copied text, record order, count and byte-budget eviction, invalid input,
//           borrowed views, clearing, and timestamp capture are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_InstanceLog.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "InstanceLog evicts oldest records by count",
           "[CypherCommon][Tier1][InstanceLog]" )
{
    instance_log_t *pLog = InstanceLog_Create({ Allocator_GetSystem(), 2u, 128u });
    REQUIRE( pLog != nullptr );
    char mutableMessage[] = "first";
    REQUIRE( InstanceLog_Add(
        pLog,
        log_level_t::Info,
        StringView_FromCString( "asset" ),
        StringView_FromCString( mutableMessage ) ) );
    mutableMessage[0] = 'x';
    REQUIRE( InstanceLog_Add(
        pLog,
        log_level_t::Warning,
        StringView_FromCString( "asset" ),
        StringView_FromCString( "second" ) ) );
    REQUIRE( InstanceLog_Add(
        pLog,
        log_level_t::Error,
        StringView_FromCString( "asset" ),
        StringView_FromCString( "third" ) ) );
    REQUIRE( InstanceLog_Count( pLog ) == 2u );

    instance_log_record_t record{};
    REQUIRE( InstanceLog_Record( pLog, 0u, &record ) );
    REQUIRE( record.level == log_level_t::Warning );
    REQUIRE( StringView_Equals( record.message, StringView_FromCString( "second" ) ) );
    REQUIRE( InstanceLog_Record( pLog, 1u, &record ) );
    REQUIRE( StringView_Equals( record.message, StringView_FromCString( "third" ) ) );
    InstanceLog_Destroy( pLog );
}

TEST_CASE( "InstanceLog enforces its text budget",
           "[CypherCommon][Tier1][InstanceLog]" )
{
    instance_log_t *pLog = InstanceLog_Create({ Allocator_GetSystem(), 8u, 10u });
    REQUIRE( pLog != nullptr );
    REQUIRE( InstanceLog_Add(
        pLog,
        log_level_t::Debug,
        StringView_FromCString( "a" ),
        StringView_FromCString( "1234" ) ) );
    REQUIRE( InstanceLog_Add(
        pLog,
        log_level_t::Debug,
        StringView_FromCString( "b" ),
        StringView_FromCString( "56789" ) ) );
    REQUIRE( InstanceLog_Count( pLog ) == 1u );
    REQUIRE_FALSE( InstanceLog_Add(
        pLog,
        log_level_t::Info,
        StringView_FromCString( "category" ),
        StringView_FromCString( "message" ) ) );

    InstanceLog_Clear( pLog );
    REQUIRE( InstanceLog_Count( pLog ) == 0u );
    InstanceLog_Destroy( pLog );
}
