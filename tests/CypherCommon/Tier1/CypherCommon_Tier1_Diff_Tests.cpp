//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Diff_Tests.cpp
//  Purpose: Tests deterministic binary delta generation and application.
//  Details: Covers prefix/literal/suffix reconstruction, identical and empty data,
//           source validation, output sizing, corruption, and no-mutation failures.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Diff.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Diff reconstructs changed binary data",
           "[CypherCommon][Tier1][Diff]" )
{
    const byte source[]{ 1u, 2u, 3u, 4u, 5u, 6u };
    const byte target[]{ 1u, 2u, 9u, 8u, 5u, 6u };
    binary_diff_t diff{};
    REQUIRE( Diff_Init( &diff, Allocator_GetSystem() ) );
    REQUIRE( Diff_Generate(
        { source, sizeof( source ) },
        { target, sizeof( target ) },
        &diff ) == diff_status_t::OK );
    REQUIRE( Diff_SerializedSize( diff ) > 0u );

    byte output[sizeof( target )]{};
    usize cbWritten = 0u;
    REQUIRE( Diff_Apply(
        { source, sizeof( source ) },
        diff,
        Span_FromArray( output ),
        &cbWritten ) == diff_status_t::OK );
    REQUIRE( cbWritten == sizeof( target ) );
    REQUIRE( Cy_MemCompare( output, target, sizeof( target ) ) == 0 );

    Diff_Clear( &diff );
    REQUIRE( diff.cbSource == 0u );
    REQUIRE( diff.cbTarget == 0u );
    REQUIRE( Diff_SerializedSize( diff ) == 0u );
    Diff_Shutdown( &diff );
}

TEST_CASE( "Diff handles identical and empty targets",
           "[CypherCommon][Tier1][Diff]" )
{
    const byte source[]{ 4u, 5u, 6u };
    binary_diff_t diff{};
    REQUIRE( Diff_Init( &diff, Allocator_GetSystem() ) );
    REQUIRE( Diff_Generate(
        { source, sizeof( source ) },
        { source, sizeof( source ) },
        &diff ) == diff_status_t::OK );
    byte output[sizeof( source )]{};
    REQUIRE( Diff_Apply(
        { source, sizeof( source ) },
        diff,
        Span_FromArray( output ),
        nullptr ) == diff_status_t::OK );
    REQUIRE( Cy_MemCompare( output, source, sizeof( source ) ) == 0 );

    REQUIRE( Diff_Generate(
        { source, sizeof( source ) },
        {},
        &diff ) == diff_status_t::OK );
    REQUIRE( Diff_Apply(
        { source, sizeof( source ) },
        diff,
        {},
        nullptr ) == diff_status_t::OK );
    Diff_Shutdown( &diff );
}

TEST_CASE( "Diff rejects mismatches and corruption before changing output",
           "[CypherCommon][Tier1][Diff]" )
{
    const byte source[]{ 1u, 2u, 3u };
    const byte target[]{ 1u, 8u, 3u, 4u };
    binary_diff_t diff{};
    REQUIRE( Diff_Init( &diff, Allocator_GetSystem() ) );
    REQUIRE( Diff_Generate(
        { source, sizeof( source ) },
        { target, sizeof( target ) },
        &diff ) == diff_status_t::OK );

    byte output[sizeof( target )]{ 0xAAu, 0xAAu, 0xAAu, 0xAAu };
    const byte wrongSource[]{ 1u, 2u, 9u };
    REQUIRE( Diff_Apply(
        { wrongSource, sizeof( wrongSource ) },
        diff,
        Span_FromArray( output ),
        nullptr ) == diff_status_t::SOURCE_MISMATCH );
    REQUIRE( output[0] == 0xAAu );

    byte tooSmall[2]{ 0xBBu, 0xBBu };
    REQUIRE( Diff_Apply(
        { source, sizeof( source ) },
        diff,
        Span_FromArray( tooSmall ),
        nullptr ) == diff_status_t::OUTPUT_TOO_SMALL );
    REQUIRE( tooSmall[0] == 0xBBu );

    diff.encodedOps.pData[0] ^= 0xFFu;
    REQUIRE( Diff_Apply(
        { source, sizeof( source ) },
        diff,
        Span_FromArray( output ),
        nullptr ) == diff_status_t::CORRUPT_DIFF );
    REQUIRE( output[0] == 0xAAu );
    Diff_Shutdown( &diff );
}
