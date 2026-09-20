//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Random_Tests.cpp
//  Purpose: Verifies determinism, stream independence, and range contracts.
//  Details: The properties tested here are the ones the generator exists for:
//           a sequence must replay exactly from its seed, different streams
//           must not correlate, and every range contract must hold at its
//           boundaries. Distribution quality is the algorithm's concern, not
//           something a unit test can meaningfully assert.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::math;

TEST_CASE( "output matches the published PCG32 reference vector",
           "[CypherCommon][Mathlib][Random]" )
{
    // Known-answer test against the reference PCG32 demo for
    // srandom_r(state = 42, seq = 54). Self-consistency tests cannot catch a
    // subtly wrong permutation -- a broken shift or rotation would still be
    // perfectly deterministic while destroying the statistical quality that
    // is the entire reason for choosing this generator. This is the only test
    // here that would notice.
    constexpr u32 cExpected[]{
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
        0x83d2f293u, 0xbfa4784bu, 0xcbed606eu
    };
    rng_t rng = Rng_Seed( 42u, 54u );
    for ( const u32 expected : cExpected ) {
        REQUIRE( Rng_NextU32( &rng ) == expected );
    }
}

TEST_CASE( "the same seed and stream replay an identical sequence",
           "[CypherCommon][Mathlib][Random]" )
{
    rng_t first = Rng_Seed( 12345u, 7u );
    rng_t second = Rng_Seed( 12345u, 7u );
    for ( int i = 0; i < 256; ++i ) {
        REQUIRE( Rng_NextU32( &first ) == Rng_NextU32( &second ) );
    }
}

TEST_CASE( "different seeds and different streams diverge",
           "[CypherCommon][Mathlib][Random]" )
{
    rng_t baseline = Rng_Seed( 1u, 1u );
    rng_t otherSeed = Rng_Seed( 2u, 1u );
    rng_t otherStream = Rng_Seed( 1u, 2u );

    // Two generators could coincide on any single draw by chance, so compare
    // whole sequences: a shared prefix of this length would mean the seed or
    // stream is not actually reaching the state.
    bool bSeedDiverged = false;
    bool bStreamDiverged = false;
    for ( int i = 0; i < 64; ++i ) {
        const u32 reference = Rng_NextU32( &baseline );
        bSeedDiverged = bSeedDiverged || Rng_NextU32( &otherSeed ) != reference;
        bStreamDiverged = bStreamDiverged || Rng_NextU32( &otherStream ) != reference;
    }
    REQUIRE( bSeedDiverged );
    REQUIRE( bStreamDiverged );
}

TEST_CASE( "a copied generator forks rather than advancing independently",
           "[CypherCommon][Mathlib][Random]" )
{
    // Documented behavior: copying is how replay works, so it must be exact.
    rng_t original = Rng_Seed( 99u, 0u );
    ( void )Rng_NextU32( &original );
    rng_t fork = original;
    for ( int i = 0; i < 32; ++i ) {
        REQUIRE( Rng_NextU32( &original ) == Rng_NextU32( &fork ) );
    }
}

TEST_CASE( "bounded draws stay in range and cover it",
           "[CypherCommon][Mathlib][Random]" )
{
    rng_t rng = Rng_Seed( 4242u, 3u );

    constexpr u32 cBound = 6u;
    bool bSeen[cBound] = {};
    for ( int i = 0; i < 4096; ++i ) {
        const u32 value = Rng_NextBoundedU32( &rng, cBound );
        REQUIRE( value < cBound );
        bSeen[value] = true;
    }
    for ( u32 i = 0u; i < cBound; ++i ) {
        REQUIRE( bSeen[i] );
    }

    // A bound of one has exactly one legal answer, and a bound of zero has
    // none -- the documented result there is 0 rather than a division fault.
    REQUIRE( Rng_NextBoundedU32( &rng, 1u ) == 0u );
    REQUIRE( Rng_NextBoundedU32( &rng, 0u ) == 0u );
}

TEST_CASE( "unit draws stay within the half-open interval",
           "[CypherCommon][Mathlib][Random]" )
{
    rng_t rng = Rng_Seed( 777u, 1u );
    for ( int i = 0; i < 4096; ++i ) {
        const f32 unitF32 = Rng_NextUnitF32( &rng );
        REQUIRE( unitF32 >= 0.0f );
        REQUIRE( unitF32 < 1.0f );

        const f64 unitF64 = Rng_NextUnitF64( &rng );
        REQUIRE( unitF64 >= 0.0 );
        REQUIRE( unitF64 < 1.0 );
    }
}

TEST_CASE( "ranged draws respect their bounds and reject malformed intervals",
           "[CypherCommon][Mathlib][Random]" )
{
    rng_t rng = Rng_Seed( 31337u, 2u );
    for ( int i = 0; i < 2048; ++i ) {
        const f64 value = Rng_NextRangeF64( &rng, -3.5, 7.25 );
        REQUIRE( value >= -3.5 );
        REQUIRE( value < 7.25 );
    }

    // An inverted or empty interval has no value to draw, so the contract is
    // to return the minimum rather than a NaN that would spread silently.
    REQUIRE( Rng_NextRangeF64( &rng, 5.0, 5.0 ) == 5.0 );
    REQUIRE( Rng_NextRangeF64( &rng, 5.0, 1.0 ) == 5.0 );

    const f64 infinity = std::numeric_limits<f64>::infinity();
    REQUIRE( Rng_NextRangeF64( &rng, 0.0, infinity ) == 0.0 );
    REQUIRE( Rng_NextRangeF64( &rng, -infinity, 0.0 ) == -infinity );
}

TEST_CASE( "64-bit draws are a documented pairing of the 32-bit sequence",
           "[CypherCommon][Mathlib][Random]" )
{
    // Evaluation order must not decide which half is which, or the sequence
    // would differ between compilers and break replay.
    rng_t wide = Rng_Seed( 2024u, 5u );
    rng_t narrow = Rng_Seed( 2024u, 5u );

    const u64 combined = Rng_NextU64( &wide );
    const u64 high = Rng_NextU32( &narrow );
    const u64 low = Rng_NextU32( &narrow );
    REQUIRE( combined == ( ( high << 32u ) | low ) );
}
