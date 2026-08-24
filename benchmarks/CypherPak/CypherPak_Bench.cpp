//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherPak/CypherPak_Bench.cpp
//  Purpose: Benchmarks Pak Bench performance.
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

#include "CypherPak.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace pak = cypher::engine;

namespace
{

constexpr cypher::engine::common::u32 kPakBenchFileCount = 4u;
constexpr cypher::engine::common::usize kSmallFileSize = 4u * 1024u;

bool WritePhysicalFile( const std::filesystem::path &path, const void *pData, const std::size_t nSize )
{
    std::error_code ec{};
    std::filesystem::create_directories( path.parent_path(), ec );
    if ( ec ) {
        return false;
    }

    std::FILE *file = std::fopen( path.string().c_str(), "wb" );
    if ( file == nullptr ) {
        return false;
    }

    const bool bWriteOk = nSize == 0u || std::fwrite( pData, 1u, nSize, file ) == nSize;
    const bool bCloseOk = std::fclose( file ) == 0;
    return bWriteOk && bCloseOk;
}

std::filesystem::path BenchRoot()
{
    return std::filesystem::temp_directory_path() / "CypherPak_Bench";
}

bool PrepareSourceFiles(
    std::array<std::filesystem::path, kPakBenchFileCount> &physicalPathsOut,
    std::array<std::string, kPakBenchFileCount> &physicalPathStringsOut )
{
    const std::filesystem::path root = BenchRoot() / "Source";
    std::array<unsigned char, kSmallFileSize> data{};

    for ( cypher::engine::common::usize i = 0u; i < data.size(); ++i ) {
        data[i] = static_cast<unsigned char>( ( i * 31u ) & 0xFFu );
    }

    physicalPathsOut[0] = root / "scripts" / "player.cfg";
    physicalPathsOut[1] = root / "textures" / "wall_albedo.dds";
    physicalPathsOut[2] = root / "textures" / "wall_normal.dds";
    physicalPathsOut[3] = root / "materials" / "wall.mat";

    for ( cypher::engine::common::u32 i = 0u; i < kPakBenchFileCount; ++i ) {
        if ( !WritePhysicalFile( physicalPathsOut[i], data.data(), data.size() ) ) {
            return false;
        }
        physicalPathStringsOut[i] = physicalPathsOut[i].string();
    }

    return true;
}

bool BuildBenchArchive( const std::filesystem::path &archivePath )
{
    std::array<std::filesystem::path, kPakBenchFileCount> physicalPaths{};
    std::array<std::string, kPakBenchFileCount> physicalPathStrings{};
    if ( !PrepareSourceFiles( physicalPaths, physicalPathStrings ) ) {
        return false;
    }

    const std::string archivePathString = archivePath.string();
    const pak::pak_source_file_t files[kPakBenchFileCount] = {
        { "scripts/player.cfg", physicalPathStrings[0].c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "textures/wall_albedo.dds", physicalPathStrings[1].c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "textures/wall_normal.dds", physicalPathStrings[2].c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "materials/wall.mat", physicalPathStrings[3].c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE }
    };

    pak::pak_writer_config_t config{};
    config.szArchivePath = archivePathString.c_str();
    return pak::Pak_CreateArchive( config, files, kPakBenchFileCount ) == pak::pak_error_t::OK;
}

void BM_Pak_CreateArchive_SmallFiles( benchmark::State &state )
{
    const std::filesystem::path archivePath = BenchRoot() / "bench_create.cypak";
    std::error_code ec{};

    for ( auto _ : state ) {
        std::filesystem::remove( archivePath, ec );
        const bool bOk = BuildBenchArchive( archivePath );
        if ( !bOk ) {
            state.SkipWithError( "Pak_CreateArchive failed." );
            break;
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kPakBenchFileCount ) );
}

void BM_Pak_OpenCloseReader( benchmark::State &state )
{
    const std::filesystem::path archivePath = BenchRoot() / "bench_read.cypak";
    if ( !BuildBenchArchive( archivePath ) ) {
        state.SkipWithError( "benchmark archive setup failed." );
        return;
    }

    const std::string archivePathString = archivePath.string();
    for ( auto _ : state ) {
        pak::pak_reader_t reader{};
        const pak::pak_error_t openResult = pak::Pak_OpenReader( archivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader );
        if ( openResult != pak::pak_error_t::OK ) {
            state.SkipWithError( "Pak_OpenReader failed." );
            break;
        }
        benchmark::DoNotOptimize( reader.nFileCount );
        benchmark::DoNotOptimize( pak::Pak_CloseReader( reader ) );
        benchmark::ClobberMemory();
    }
}

void BM_Pak_ReadFileByPath_4KiB( benchmark::State &state )
{
    const std::filesystem::path archivePath = BenchRoot() / "bench_read.cypak";
    if ( !BuildBenchArchive( archivePath ) ) {
        state.SkipWithError( "benchmark archive setup failed." );
        return;
    }

    const std::string archivePathString = archivePath.string();
    pak::pak_reader_t reader{};
    if ( pak::Pak_OpenReader( archivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ) != pak::pak_error_t::OK ) {
        state.SkipWithError( "Pak_OpenReader failed." );
        return;
    }

    std::array<unsigned char, kSmallFileSize> buffer{};
    for ( auto _ : state ) {
        cypher::engine::common::u64 nBytesRead = 0u;
        const pak::pak_error_t readResult = pak::Pak_ReadFile(
            reader,
            "textures/wall_albedo.dds",
            buffer.data(),
            buffer.size(),
            nBytesRead );
        if ( readResult != pak::pak_error_t::OK ) {
            state.SkipWithError( "Pak_ReadFile failed." );
            break;
        }
        benchmark::DoNotOptimize( nBytesRead );
        benchmark::DoNotOptimize( buffer.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kSmallFileSize ) );
    benchmark::DoNotOptimize( pak::Pak_CloseReader( reader ) );
}

}       // namespace

BENCHMARK( BM_Pak_CreateArchive_SmallFiles );
BENCHMARK( BM_Pak_OpenCloseReader );
BENCHMARK( BM_Pak_ReadFileByPath_4KiB );
