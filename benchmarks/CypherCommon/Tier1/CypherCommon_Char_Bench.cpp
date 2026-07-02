//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Char_Bench.cpp
//  Purpose: Benchmarks Char Bench performance.
//  Details: This benchmark measures runtime cost for the corresponding low-level
//           path. Results should be treated as signals and compared across build
//           modes and platforms.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Char.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr const char *kPathChars = "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr const char *kHexChars = "0123456789ABCDEFabcdef";
constexpr const char *kMixedHexChars = "0123456789ABCDEFabcdefxyzXYZ";
constexpr u32 kAsciiCharCount = 128u;

void BM_Char_IsAlphaNumeric_AllAscii( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kAsciiCharCount; ++i ) {
            nAccepted += Char_IsAlphaNumericAscii( static_cast<char>( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsWhitespace_AllAscii( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kAsciiCharCount; ++i ) {
            nAccepted += Char_IsWhitespaceAscii( static_cast<char>( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsHexDigit_AllAscii( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kAsciiCharCount; ++i ) {
            nAccepted += Char_IsHexDigitAscii( static_cast<char>( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsPathNameChar_TexturePath( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( const char *pCursor = kPathChars; *pCursor != '\0'; ++pCursor ) {
            nAccepted += Char_IsPathNameChar( *pCursor ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_ToLowerAscii_TexturePath( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( const char *pCursor = kPathChars; *pCursor != '\0'; ++pCursor ) {
            nAccum += static_cast<u8>( Char_ToLowerAscii( *pCursor ) );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_Char_HexValueAscii_AllHexDigits( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( const char *pCursor = kHexChars; *pCursor != '\0'; ++pCursor ) {
            nAccum += Char_HexValueAscii( *pCursor );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_Char_HexValueAscii_MixedInput( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( const char *pCursor = kMixedHexChars; *pCursor != '\0'; ++pCursor ) {
            nAccum += Char_HexValueAscii( *pCursor );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

} // namespace

BENCHMARK( BM_Char_IsAlphaNumeric_AllAscii );
BENCHMARK( BM_Char_IsWhitespace_AllAscii );
BENCHMARK( BM_Char_IsHexDigit_AllAscii );
BENCHMARK( BM_Char_IsPathNameChar_TexturePath );
BENCHMARK( BM_Char_ToLowerAscii_TexturePath );
BENCHMARK( BM_Char_HexValueAscii_AllHexDigits );
BENCHMARK( BM_Char_HexValueAscii_MixedInput );
