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

#include <bit>

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

constexpr const char *kMixedChars = "Textures/World/INDUSTRIAL/wall_panel_01.DDS";
constexpr const char *kDigitChars = "0123456789xyzXYZ";
constexpr const char *kHexChars = "0123456789ABCDEFabcdef";
constexpr const char *kMixedHexChars = "0123456789ABCDEFabcdefxyzXYZ";
constexpr u32 kByteValueCount = 256u;

constexpr char ByteToChar( u32 nValue ) noexcept
{
    return std::bit_cast<char>( static_cast<u8>( nValue ) );
}

void BM_Char_IsAlphaNumeric_AllBytes( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kByteValueCount; ++i ) {
            nAccepted += Char_IsAlphaNumericAscii( ByteToChar( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsWhitespace_AllBytes( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kByteValueCount; ++i ) {
            nAccepted += Char_IsWhitespaceAscii( ByteToChar( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsPunctuation_AllBytes( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kByteValueCount; ++i ) {
            nAccepted += Char_IsPunctuationAscii( ByteToChar( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_IsHexDigit_AllBytes( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccepted = 0u;
        for ( u32 i = 0u; i < kByteValueCount; ++i ) {
            nAccepted += Char_IsHexDigitAscii( ByteToChar( i ) ) ? 1u : 0u;
        }
        benchmark::DoNotOptimize( nAccepted );
    }
}

void BM_Char_ToLowerAscii_MixedInput( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( const char *pCursor = kMixedChars; *pCursor != '\0'; ++pCursor ) {
            nAccum += static_cast<u8>( Char_ToLowerAscii( *pCursor ) );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_Char_DigitValueAscii_MixedInput( benchmark::State &state )
{
    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( const char *pCursor = kDigitChars; *pCursor != '\0'; ++pCursor ) {
            nAccum += Char_DigitValueAscii( *pCursor );
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

BENCHMARK( BM_Char_IsAlphaNumeric_AllBytes );
BENCHMARK( BM_Char_IsWhitespace_AllBytes );
BENCHMARK( BM_Char_IsPunctuation_AllBytes );
BENCHMARK( BM_Char_IsHexDigit_AllBytes );
BENCHMARK( BM_Char_ToLowerAscii_MixedInput );
BENCHMARK( BM_Char_DigitValueAscii_MixedInput );
BENCHMARK( BM_Char_HexValueAscii_AllHexDigits );
BENCHMARK( BM_Char_HexValueAscii_MixedInput );
