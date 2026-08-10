//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_SequenceNumber_Tests.cpp
//  Purpose: Tests modular packet sequence and ACK-window behavior.
//  Details: Wraparound, half-range ambiguity, duplicate records, late packets, and
//           window eviction are covered for future real-time networking.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SequenceNumber.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "SequenceNumber compares across modular wrap",
           "[CypherCommon][Tier1][SequenceNumber]" )
{
    REQUIRE( Sequence16_IsNewer( 1u, 0xFFFFu ) );
    REQUIRE( Sequence32_IsNewer( 2u, 0xFFFFFFFEu ) );
    REQUIRE( Sequence16_Distance( 0xFFFFu, 1u ) == 2 );
    REQUIRE( Sequence32_Distance( 0xFFFFFFFEu, 2u ) == 4 );
    REQUIRE( Sequence16_Distance( 0u, 0x8000u ) == -32768 );
    REQUIRE_FALSE( Sequence16_IsNewer( 0x8000u, 0u ) );
    REQUIRE_FALSE( Sequence16_IsNewer( 0u, 0x8000u ) );
}

TEST_CASE( "SequenceNumber tracks a 32-packet acknowledgement history",
           "[CypherCommon][Tier1][SequenceNumber]" )
{
    sequence_ack32_t state{};
    REQUIRE( SequenceAck32_Record( &state, 100u ) );
    REQUIRE( SequenceAck32_Record( &state, 102u ) );
    REQUIRE( SequenceAck32_Contains( &state, 100u ) );
    REQUIRE_FALSE( SequenceAck32_Contains( &state, 101u ) );
    REQUIRE( SequenceAck32_Record( &state, 101u ) );
    REQUIRE( SequenceAck32_Contains( &state, 101u ) );
    REQUIRE_FALSE( SequenceAck32_Record( &state, 101u ) );

    REQUIRE( SequenceAck32_Record( &state, 135u ) );
    REQUIRE_FALSE( SequenceAck32_Contains( &state, 102u ) );
    REQUIRE( SequenceAck32_Contains( &state, 135u ) );
}

TEST_CASE( "SequenceNumber ACK history survives wraparound",
           "[CypherCommon][Tier1][SequenceNumber]" )
{
    sequence_ack32_t state{};
    REQUIRE( SequenceAck32_Record( &state, 0xFFFFFFFFu ) );
    REQUIRE( SequenceAck32_Record( &state, 0u ) );
    REQUIRE( SequenceAck32_Contains( &state, 0xFFFFFFFFu ) );
    REQUIRE_FALSE( SequenceAck32_Record( &state, 0xFFFFFFFFu ) );
}
