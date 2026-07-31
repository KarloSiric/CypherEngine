//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_String_Bench.cpp
//  Purpose: Benchmarks String Bench performance.
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

#include "CypherCommon_String.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstring>

using namespace cypher::common;

namespace
{

constexpr const char *kShortLower = "cypher";
constexpr const char *kShortUpper = "CYPHER";
constexpr const char *kMediumA = "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr const char *kMediumB = "textures/world/industrial/wall_panel_01_normal.dds";
constexpr const char *kMediumCase = "TEXTURES/WORLD/INDUSTRIAL/WALL_PANEL_01_ALBEDO.DDS";
constexpr const char *kLongA =
    "scripts/entities/weapons/projectiles/arena_survival/rocket_launcher/projectile_rocket_fast.cfg";
constexpr const char *kLongB =
    "scripts/entities/weapons/projectiles/arena_survival/rocket_launcher/projectile_rocket_slow.cfg";
constexpr usize kMatrixStringLength = 128u;

struct string_matrix_t {
    std::array<char, kMatrixStringLength + 1u> equalA;
    std::array<char, kMatrixStringLength + 1u> equalB;
    std::array<char, kMatrixStringLength + 1u> earlyA;
    std::array<char, kMatrixStringLength + 1u> earlyB;
    std::array<char, kMatrixStringLength + 1u> middleA;
    std::array<char, kMatrixStringLength + 1u> middleB;
    std::array<char, kMatrixStringLength + 1u> lateA;
    std::array<char, kMatrixStringLength + 1u> lateB;
};

string_matrix_t BuildStringMatrix()
{
    string_matrix_t matrix{};

    for ( usize i = 0u; i < kMatrixStringLength; ++i ) {
        const char chValue = static_cast<char>( 'a' + static_cast<char>( i % 26u ) );
        matrix.equalA[i] = chValue;
        matrix.equalB[i] = chValue;
        matrix.earlyA[i] = chValue;
        matrix.earlyB[i] = chValue;
        matrix.middleA[i] = chValue;
        matrix.middleB[i] = chValue;
        matrix.lateA[i] = chValue;
        matrix.lateB[i] = chValue;
    }

    matrix.earlyB[0] = 'z';
    matrix.middleB[kMatrixStringLength / 2u] = 'z';
    matrix.lateB[kMatrixStringLength - 1u] = 'z';

    return matrix;
}

const string_matrix_t &GetStringMatrix()
{
    static const string_matrix_t matrix = BuildStringMatrix();
    return matrix;
}

void BM_Cy_strlen_Short( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strlen( kShortLower ) );
    }
}

void BM_Std_strlen_Short( benchmark::State &state )
{
    const char *pString = kShortLower;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pString );
        benchmark::DoNotOptimize( std::strlen( pString ) );
    }
}

void BM_Cy_strlen_Medium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strlen( kMediumA ) );
    }
}

void BM_Std_strlen_Medium( benchmark::State &state )
{
    const char *pString = kMediumA;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pString );
        benchmark::DoNotOptimize( std::strlen( pString ) );
    }
}

void BM_Cy_strlen_Long( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strlen( kLongA ) );
    }
}

void BM_Std_strlen_Long( benchmark::State &state )
{
    const char *pString = kLongA;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pString );
        benchmark::DoNotOptimize( std::strlen( pString ) );
    }
}

void BM_Cy_strnlen_Capped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strnlen( kLongA, 32u ) );
    }
}

void BM_Cy_strisempty_NotEmpty( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strisempty( kShortLower ) );
    }
}

void BM_Cy_strisblank_NotBlank( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strisblank( kMediumA ) );
    }
}

void BM_Cy_strcmp_EqualShort( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strcmp( kShortLower, kShortLower ) );
    }
}

void BM_Std_strcmp_EqualShort( benchmark::State &state )
{
    const char *pStringA = kShortLower;
    const char *pStringB = kShortLower;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Cy_strcmp_DifferentMedium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strcmp( kMediumA, kMediumB ) );
    }
}

void BM_Cy_strcmp_Equal128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.equalA.data();
    const char *pStringB = matrix.equalB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( Cy_strcmp( pStringA, pStringB ) );
    }
}

void BM_Std_strcmp_Equal128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.equalA.data();
    const char *pStringB = matrix.equalB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Std_strcmp_DifferentMedium( benchmark::State &state )
{
    const char *pStringA = kMediumA;
    const char *pStringB = kMediumB;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Cy_strcmp_EarlyMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.earlyA.data();
    const char *pStringB = matrix.earlyB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( Cy_strcmp( pStringA, pStringB ) );
    }
}

void BM_Std_strcmp_EarlyMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.earlyA.data();
    const char *pStringB = matrix.earlyB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Cy_strcmp_MiddleMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.middleA.data();
    const char *pStringB = matrix.middleB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( Cy_strcmp( pStringA, pStringB ) );
    }
}

void BM_Std_strcmp_MiddleMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.middleA.data();
    const char *pStringB = matrix.middleB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Cy_strcmp_LateMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.lateA.data();
    const char *pStringB = matrix.lateB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( Cy_strcmp( pStringA, pStringB ) );
    }
}

void BM_Std_strcmp_LateMismatch128_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.lateA.data();
    const char *pStringB = matrix.lateB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strcmp( pStringA, pStringB ) );
    }
}

void BM_Cy_strncmp_DifferentMediumCapped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncmp( kMediumA, kMediumB, 32u ) );
    }
}

void BM_Std_strncmp_DifferentMediumCapped( benchmark::State &state )
{
    const char *pStringA = kMediumA;
    const char *pStringB = kMediumB;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strncmp( pStringA, pStringB, 32u ) );
    }
}

void BM_Cy_strncmp_Equal128_Capped64_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.equalA.data();
    const char *pStringB = matrix.equalB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( Cy_strncmp( pStringA, pStringB, 64u ) );
    }
}

void BM_Std_strncmp_Equal128_Capped64_Buffer( benchmark::State &state )
{
    const string_matrix_t &matrix = GetStringMatrix();
    const char *pStringA = matrix.equalA.data();
    const char *pStringB = matrix.equalB.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( pStringA );
        benchmark::DoNotOptimize( pStringB );
        benchmark::DoNotOptimize( std::strncmp( pStringA, pStringB, 64u ) );
    }
}

void BM_Cy_stricmp_EqualShort( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_stricmp( kShortLower, kShortUpper ) );
    }
}

void BM_Cy_stricmp_EqualMediumCase( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_stricmp( kMediumA, kMediumCase ) );
    }
}

void BM_Cy_strnicmp_EqualMediumCaseCapped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strnicmp( kMediumA, kMediumCase, 32u ) );
    }
}

void BM_Cy_strequal_EqualShort( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strequal( kShortLower, kShortLower ) );
    }
}

void BM_Cy_striequal_EqualMediumCase( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_striequal( kMediumA, kMediumCase ) );
    }
}

void BM_Cy_strniequal_EqualMediumCaseCapped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strniequal( kMediumA, kMediumCase, 32u ) );
    }
}

void BM_Cy_strncpy_Fits( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumA, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strncpy_Truncates( benchmark::State &state )
{
    char szBuffer[16]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumA, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strncpy_max_Capped( benchmark::State &state )
{
    char szBuffer[64]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy_max( szBuffer, kLongA, sizeof( szBuffer ), 32u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strncat_Fits( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, "textures/", sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strncat( szBuffer, "world/wall.dds", sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strncat_Truncates( benchmark::State &state )
{
    char szBuffer[24]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, "textures/", sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strncat( szBuffer, "world/industrial/wall_panel_01_albedo.dds", sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strncat_max_Capped( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, "textures/", sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strncat_max( szBuffer, "world/industrial/wall_panel_01_albedo.dds", sizeof( szBuffer ), 16u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strstr_FoundMedium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strstr( kMediumA, "wall_panel" ) );
    }
}

void BM_Cy_stristr_FoundMediumCase( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_stristr( kMediumCase, "wall_panel" ) );
    }
}

void BM_Cy_strnstr_FoundMediumCapped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strnstr( kMediumA, "industrial", 40u ) );
    }
}

void BM_Cy_strnistr_FoundMediumCaseCapped( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strnistr( kMediumCase, "industrial", 40u ) );
    }
}

void BM_Cy_strstarts_Medium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strstarts( kMediumA, "textures/world" ) );
    }
}

void BM_Cy_stristarts_MediumCase( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_stristarts( kMediumCase, "textures/world" ) );
    }
}

void BM_Cy_strends_Medium( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strends( kMediumA, ".dds" ) );
    }
}

void BM_Cy_striends_MediumCase( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_striends( kMediumCase, ".dds" ) );
    }
}

void BM_Cy_strlower_MediumBuffer( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumCase, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strlower( szBuffer ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strupper_MediumBuffer( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumA, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strupper( szBuffer ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strnlower_CappedMediumBuffer( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumCase, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strnlower( szBuffer, 32u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strnupper_CappedMediumBuffer( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strncpy( szBuffer, kMediumA, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( Cy_strnupper( szBuffer, 32u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strtrim_BothSides( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            Cy_strncpy(
                szBuffer,
                " \t textures/world/industrial/wall_panel_01_albedo.dds \r\n",
                sizeof( szBuffer ) ) );
        Cy_strtrim( szBuffer );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strstripquotes_Medium( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            Cy_strncpy(
                szBuffer,
                "\"textures/world/industrial/wall_panel_01_albedo.dds\"",
                sizeof( szBuffer ) ) );
        Cy_strstripquotes( szBuffer );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strleft_Medium( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strleft( kMediumA, szBuffer, sizeof( szBuffer ), 32u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strright_Medium( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strright( kMediumA, szBuffer, sizeof( szBuffer ), 16u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strslice_Medium( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strslice( kMediumA, szBuffer, sizeof( szBuffer ), 15u, 32u ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

void BM_Cy_strcountchar_MediumPath( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strcountchar( kMediumA, '/' ) );
    }
}

void BM_Cy_strcountstring_MediumPath( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strcountstring( kMediumA, "wall" ) );
    }
}

void BM_Cy_strsubst_OneReplacement( benchmark::State &state )
{
    char szBuffer[128]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_strsubst( kMediumA, "albedo", "normal", szBuffer, sizeof( szBuffer ) ) );
        benchmark::DoNotOptimize( szBuffer );
        benchmark::ClobberMemory();
    }
}

} // namespace

BENCHMARK( BM_Cy_strlen_Short );
BENCHMARK( BM_Std_strlen_Short );
BENCHMARK( BM_Cy_strlen_Medium );
BENCHMARK( BM_Std_strlen_Medium );
BENCHMARK( BM_Cy_strlen_Long );
BENCHMARK( BM_Std_strlen_Long );
BENCHMARK( BM_Cy_strnlen_Capped );
BENCHMARK( BM_Cy_strisempty_NotEmpty );
BENCHMARK( BM_Cy_strisblank_NotBlank );
BENCHMARK( BM_Cy_strcmp_EqualShort );
BENCHMARK( BM_Std_strcmp_EqualShort );
BENCHMARK( BM_Cy_strcmp_DifferentMedium );
BENCHMARK( BM_Std_strcmp_DifferentMedium );
BENCHMARK( BM_Cy_strcmp_Equal128_Buffer );
BENCHMARK( BM_Std_strcmp_Equal128_Buffer );
BENCHMARK( BM_Cy_strcmp_EarlyMismatch128_Buffer );
BENCHMARK( BM_Std_strcmp_EarlyMismatch128_Buffer );
BENCHMARK( BM_Cy_strcmp_MiddleMismatch128_Buffer );
BENCHMARK( BM_Std_strcmp_MiddleMismatch128_Buffer );
BENCHMARK( BM_Cy_strcmp_LateMismatch128_Buffer );
BENCHMARK( BM_Std_strcmp_LateMismatch128_Buffer );
BENCHMARK( BM_Cy_strncmp_DifferentMediumCapped );
BENCHMARK( BM_Std_strncmp_DifferentMediumCapped );
BENCHMARK( BM_Cy_strncmp_Equal128_Capped64_Buffer );
BENCHMARK( BM_Std_strncmp_Equal128_Capped64_Buffer );
BENCHMARK( BM_Cy_stricmp_EqualShort );
BENCHMARK( BM_Cy_stricmp_EqualMediumCase );
BENCHMARK( BM_Cy_strnicmp_EqualMediumCaseCapped );
BENCHMARK( BM_Cy_strequal_EqualShort );
BENCHMARK( BM_Cy_striequal_EqualMediumCase );
BENCHMARK( BM_Cy_strniequal_EqualMediumCaseCapped );
BENCHMARK( BM_Cy_strncpy_Fits );
BENCHMARK( BM_Cy_strncpy_Truncates );
BENCHMARK( BM_Cy_strncpy_max_Capped );
BENCHMARK( BM_Cy_strncat_Fits );
BENCHMARK( BM_Cy_strncat_Truncates );
BENCHMARK( BM_Cy_strncat_max_Capped );
BENCHMARK( BM_Cy_strstr_FoundMedium );
BENCHMARK( BM_Cy_stristr_FoundMediumCase );
BENCHMARK( BM_Cy_strnstr_FoundMediumCapped );
BENCHMARK( BM_Cy_strnistr_FoundMediumCaseCapped );
BENCHMARK( BM_Cy_strstarts_Medium );
BENCHMARK( BM_Cy_stristarts_MediumCase );
BENCHMARK( BM_Cy_strends_Medium );
BENCHMARK( BM_Cy_striends_MediumCase );
BENCHMARK( BM_Cy_strlower_MediumBuffer );
BENCHMARK( BM_Cy_strupper_MediumBuffer );
BENCHMARK( BM_Cy_strnlower_CappedMediumBuffer );
BENCHMARK( BM_Cy_strnupper_CappedMediumBuffer );
BENCHMARK( BM_Cy_strtrim_BothSides );
BENCHMARK( BM_Cy_strstripquotes_Medium );
BENCHMARK( BM_Cy_strleft_Medium );
BENCHMARK( BM_Cy_strright_Medium );
BENCHMARK( BM_Cy_strslice_Medium );
BENCHMARK( BM_Cy_strcountchar_MediumPath );
BENCHMARK( BM_Cy_strcountstring_MediumPath );
BENCHMARK( BM_Cy_strsubst_OneReplacement );
