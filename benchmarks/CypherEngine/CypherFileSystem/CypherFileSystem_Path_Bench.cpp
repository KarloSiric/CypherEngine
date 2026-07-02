//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherEngine/CypherFileSystem/CypherFileSystem_Path_Bench.cpp
//  Purpose: Benchmarks FileSystem Path Bench performance.
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

#include "CypherFileSystem.h"

#include <benchmark/benchmark.h>

#include <cstring>

using namespace cypher::engine::fs;

namespace
{

constexpr const char *kMessyPath = "Textures\\World//Industrial/./Wall_Panel_01_ALBEDO.DDS";
constexpr const char *kNormalizedPath = "textures/world/industrial/wall_panel_01_albedo.dds";
constexpr const char *kRootPath = "textures/world";
constexpr const char *kRelativePath = "Industrial/Wall_Panel_01_ALBEDO.DDS";
constexpr const char *kPhysicalRoot = "/tmp/cypher/content";

void BM_FS_NormalizeVirtualPath( benchmark::State &state )
{
    char szOut[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_NormalizeVirtualPath( kMessyPath, szOut, sizeof( szOut ) ) );
        benchmark::DoNotOptimize( szOut );
        benchmark::ClobberMemory();
    }
}

void BM_FS_PathJoin( benchmark::State &state )
{
    char szOut[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_PathJoin( kRootPath, kRelativePath, szOut, sizeof( szOut ) ) );
        benchmark::DoNotOptimize( szOut );
        benchmark::ClobberMemory();
    }
}

void BM_FS_BuildPhysicalPath( benchmark::State &state )
{
    char szOut[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_BuildPhysicalPath( kPhysicalRoot, kNormalizedPath, szOut, sizeof( szOut ) ) );
        benchmark::DoNotOptimize( szOut );
        benchmark::ClobberMemory();
    }
}

void BM_FS_PathBasename( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_PathBasename( kNormalizedPath ) );
    }
}

void BM_FS_PathDirname( benchmark::State &state )
{
    char szOut[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_PathDirname( kNormalizedPath, szOut, sizeof( szOut ) ) );
        benchmark::DoNotOptimize( szOut );
        benchmark::ClobberMemory();
    }
}

void BM_FS_PathWithoutExtension( benchmark::State &state )
{
    char szOut[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_PathWithoutExtension( kNormalizedPath, szOut, sizeof( szOut ) ) );
        benchmark::DoNotOptimize( szOut );
        benchmark::ClobberMemory();
    }
}

void BM_FS_PathHasExtension( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_PathHasExtension( kNormalizedPath, ".dds" ) );
    }
}

void BM_FS_VirtualPathStartsWithRoot( benchmark::State &state )
{
    const char *pszRelative = nullptr;

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CypherFileSystem_VirtualPathStartsWithRoot( kNormalizedPath, kRootPath, &pszRelative ) );
        benchmark::DoNotOptimize( pszRelative );
    }
}

}       // namespace

BENCHMARK( BM_FS_NormalizeVirtualPath );
BENCHMARK( BM_FS_PathJoin );
BENCHMARK( BM_FS_BuildPhysicalPath );
BENCHMARK( BM_FS_PathBasename );
BENCHMARK( BM_FS_PathDirname );
BENCHMARK( BM_FS_PathWithoutExtension );
BENCHMARK( BM_FS_PathHasExtension );
BENCHMARK( BM_FS_VirtualPathStartsWithRoot );
