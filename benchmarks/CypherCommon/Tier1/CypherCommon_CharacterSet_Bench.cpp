//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_CharacterSet_Bench.cpp
//  Purpose: Benchmarks Tier1 CharacterSet performance.
//  Details: These benchmarks measure constant-time membership operations,
//           bounded byte scans, population counts, range construction, and
//           fixed four-word set algebra.
//
//  History:
//  - Created by Karlo Siric on 2026-08-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CharacterSet.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr char kScanText[] =
    "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr char kLateHitText[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaz";

char ByteValue( u32 nValue ) noexcept
{
    return static_cast<char>( static_cast<u8>( nValue ) );
}

character_set_t MakeAlternatingSet( u32 nParity )
{
    character_set_t set{};
    for ( u32 nValue = nParity;
          nValue < CY_CHARACTER_SET_VALUE_COUNT;
          nValue += 2u ) {
        CharacterSet_Add( &set, ByteValue( nValue ) );
    }
    return set;
}

void BM_CharacterSet_Contains_Hit( benchmark::State &state )
{
    character_set_t set{};
    CharacterSet_Add( &set, ByteValue( 200u ) );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            CharacterSet_Contains( &set, ByteValue( 200u ) ) );
    }
}

void BM_CharacterSet_Contains_Miss( benchmark::State &state )
{
    character_set_t set{};
    CharacterSet_Add( &set, ByteValue( 200u ) );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            CharacterSet_Contains( &set, ByteValue( 201u ) ) );
    }
}

void BM_CharacterSet_DirectContainsBaseline( benchmark::State &state )
{
    character_set_t set{};
    CharacterSet_Add( &set, ByteValue( 200u ) );
    constexpr usize kWordIndex = 200u / CY_CHARACTER_SET_WORD_BITS;
    constexpr u32 kBitIndex = 200u % CY_CHARACTER_SET_WORD_BITS;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            ( set.bitWords[kWordIndex] & ( 1ull << kBitIndex ) ) != 0u );
    }
}

void BM_CharacterSet_AddRemove( benchmark::State &state )
{
    character_set_t set{};

    for ( auto _ : state ) {
        CharacterSet_Add( &set, ByteValue( 200u ) );
        CharacterSet_Remove( &set, ByteValue( 200u ) );
        benchmark::DoNotOptimize( set.bitWords );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) * 2 );
}

void BM_CharacterSet_Count_Sparse( benchmark::State &state )
{
    const character_set_t set = MakeAlternatingSet( 0u );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CharacterSet_Count( &set ) );
    }
}

void BM_CharacterSet_Count_Full( benchmark::State &state )
{
    character_set_t set{};
    CharacterSet_Fill( &set );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CharacterSet_Count( &set ) );
    }
}

void BM_CharacterSet_AddRange_Full( benchmark::State &state )
{
    character_set_t set{};

    for ( auto _ : state ) {
        CharacterSet_Clear( &set );
        CharacterSet_AddRange( &set, ByteValue( 0u ), ByteValue( 255u ) );
        benchmark::DoNotOptimize( set.bitWords );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( CY_CHARACTER_SET_VALUE_COUNT ) );
}

void BM_CharacterSet_AddView( benchmark::State &state )
{
    character_set_t set{};
    const string_view_t view{ kScanText, sizeof( kScanText ) - 1u };

    for ( auto _ : state ) {
        CharacterSet_Clear( &set );
        CharacterSet_AddView( &set, view );
        benchmark::DoNotOptimize( set.bitWords );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( view.cchLength ) );
}

void BM_CharacterSet_ContainsAny_LateHit( benchmark::State &state )
{
    character_set_t set{};
    CharacterSet_Add( &set, 'z' );
    const string_view_t view{ kLateHitText, sizeof( kLateHitText ) - 1u };

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CharacterSet_ContainsAny( &set, view ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( view.cchLength ) );
}

void BM_CharacterSet_ContainsAll_Success( benchmark::State &state )
{
    character_set_t set{};
    const string_view_t view{ kScanText, sizeof( kScanText ) - 1u };
    CharacterSet_AddView( &set, view );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CharacterSet_ContainsAll( &set, view ) );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( view.cchLength ) );
}

void BM_CharacterSet_Union( benchmark::State &state )
{
    const character_set_t setA = MakeAlternatingSet( 0u );
    const character_set_t setB = MakeAlternatingSet( 1u );
    character_set_t result{};

    for ( auto _ : state ) {
        CharacterSet_Union( &result, &setA, &setB );
        benchmark::DoNotOptimize( result.bitWords );
        benchmark::ClobberMemory();
    }
}

void BM_CharacterSet_Intersection( benchmark::State &state )
{
    const character_set_t setA = MakeAlternatingSet( 0u );
    character_set_t setB{};
    CharacterSet_AddRange( &setB, ByteValue( 64u ), ByteValue( 191u ) );
    character_set_t result{};

    for ( auto _ : state ) {
        CharacterSet_Intersection( &result, &setA, &setB );
        benchmark::DoNotOptimize( result.bitWords );
        benchmark::ClobberMemory();
    }
}

void BM_CharacterSet_Difference( benchmark::State &state )
{
    const character_set_t setA = MakeAlternatingSet( 0u );
    const character_set_t setB = MakeAlternatingSet( 1u );
    character_set_t result{};

    for ( auto _ : state ) {
        CharacterSet_Difference( &result, &setA, &setB );
        benchmark::DoNotOptimize( result.bitWords );
        benchmark::ClobberMemory();
    }
}

void BM_CharacterSet_Invert( benchmark::State &state )
{
    const character_set_t input = MakeAlternatingSet( 0u );
    character_set_t result{};

    for ( auto _ : state ) {
        CharacterSet_Invert( &result, &input );
        benchmark::DoNotOptimize( result.bitWords );
        benchmark::ClobberMemory();
    }
}

void BM_CharacterSet_Intersects_LateHit( benchmark::State &state )
{
    character_set_t setA{};
    character_set_t setB{};
    CharacterSet_Add( &setA, ByteValue( 255u ) );
    CharacterSet_Add( &setB, ByteValue( 255u ) );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CharacterSet_Intersects( &setA, &setB ) );
    }
}

void BM_CharacterSet_IsSubset_Success( benchmark::State &state )
{
    const character_set_t subset = MakeAlternatingSet( 0u );
    character_set_t superset{};
    CharacterSet_Fill( &superset );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            CharacterSet_IsSubset( &subset, &superset ) );
    }
}

} // namespace

BENCHMARK( BM_CharacterSet_Contains_Hit );
BENCHMARK( BM_CharacterSet_Contains_Miss );
BENCHMARK( BM_CharacterSet_DirectContainsBaseline );
BENCHMARK( BM_CharacterSet_AddRemove );
BENCHMARK( BM_CharacterSet_Count_Sparse );
BENCHMARK( BM_CharacterSet_Count_Full );
BENCHMARK( BM_CharacterSet_AddRange_Full );
BENCHMARK( BM_CharacterSet_AddView );
BENCHMARK( BM_CharacterSet_ContainsAny_LateHit );
BENCHMARK( BM_CharacterSet_ContainsAll_Success );
BENCHMARK( BM_CharacterSet_Union );
BENCHMARK( BM_CharacterSet_Intersection );
BENCHMARK( BM_CharacterSet_Difference );
BENCHMARK( BM_CharacterSet_Invert );
BENCHMARK( BM_CharacterSet_Intersects_LateHit );
BENCHMARK( BM_CharacterSet_IsSubset_Success );
