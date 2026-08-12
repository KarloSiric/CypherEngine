//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_FileIo_Bench.cpp
//  Purpose: Benchmarks Tier1 native-file bootstrap operations.
//  Details: Measures warm existence, open/close, whole-file read, and whole-file
//           write paths. These are real-time OS benchmarks, not CPU-only kernels.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FileIo.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace cypher::common;

namespace
{

std::string PathToUtf8( const std::filesystem::path &path )
{
    const std::u8string bytes = path.u8string();
    return {
        reinterpret_cast<const char *>( bytes.data() ),
        bytes.size()
    };
}

struct benchmark_file_t {
    explicit benchmark_file_t( const char *pStem )
    {
        const auto nUnique = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        path = std::filesystem::temp_directory_path() /
            ( std::string( pStem ) + "_" + std::to_string( nUnique ) + ".tmp" );
        std::filesystem::remove( path );
        utf8 = PathToUtf8( path );
    }

    ~benchmark_file_t()
    {
        std::error_code error;
        std::filesystem::remove( path, error );
    }

    string_view_t View() const noexcept
    {
        return { utf8.data(), utf8.size() };
    }

    std::filesystem::path path{};
    std::string utf8{};
};

bool_t PrepareFile(
    benchmark_file_t &file,
    const std::vector<byte> &contents ) noexcept
{
    return FileIo_WriteAllNative(
        file.View(),
        { contents.data(), contents.size() } );
}

void BM_FileIo_NativeExists( benchmark::State &state )
{
    benchmark_file_t file( "CypherCommon_FileIo_Exists" );
    const std::vector<byte> contents( 4096u, 0xA5u );
    if ( !PrepareFile( file, contents ) ) {
        state.SkipWithError( "Temporary benchmark file creation failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t bExists = FileIo_NativeExists( file.View() );
        benchmark::DoNotOptimize( bExists );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_FileIo_OpenClose( benchmark::State &state )
{
    benchmark_file_t file( "CypherCommon_FileIo_Open" );
    const std::vector<byte> contents( 4096u, 0x5Au );
    if ( !PrepareFile( file, contents ) ) {
        state.SkipWithError( "Temporary benchmark file creation failed." );
        return;
    }

    for ( auto _ : state ) {
        native_file_t *pFile = FileIo_OpenNative(
            file.View(),
            FILE_OPEN_FLAG_READ,
            Allocator_GetSystem() );
        benchmark::DoNotOptimize( pFile );
        FileIo_CloseNative( pFile );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_FileIo_ReadAll64KiB( benchmark::State &state )
{
    constexpr usize cbContents = 64u * CY_KIB;
    benchmark_file_t file( "CypherCommon_FileIo_ReadAll" );
    std::vector<byte> contents( cbContents );
    for ( usize iByte = 0u; iByte < contents.size(); ++iByte ) {
        contents[iByte] = static_cast<byte>( ( iByte * 31u ) & 0xFFu );
    }
    if ( !PrepareFile( file, contents ) ) {
        state.SkipWithError( "Temporary benchmark file creation failed." );
        return;
    }

    blob_t output{};
    if ( !Blob_Init( &output, Allocator_GetSystem() ) ) {
        state.SkipWithError( "Benchmark blob initialization failed." );
        return;
    }
    for ( auto _ : state ) {
        bool_t bRead = FileIo_ReadAllNative( file.View(), &output );
        benchmark::DoNotOptimize( bRead );
        benchmark::DoNotOptimize( Blob_Data( &output ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbContents ) );
    Blob_Shutdown( &output );
}

void BM_FileIo_WriteAll64KiB( benchmark::State &state )
{
    constexpr usize cbContents = 64u * CY_KIB;
    benchmark_file_t file( "CypherCommon_FileIo_WriteAll" );
    std::vector<byte> contents( cbContents );
    for ( usize iByte = 0u; iByte < contents.size(); ++iByte ) {
        contents[iByte] = static_cast<byte>( ( iByte * 17u ) & 0xFFu );
    }
    const binary_block_t source{ contents.data(), contents.size() };

    for ( auto _ : state ) {
        bool_t bWritten = FileIo_WriteAllNative( file.View(), source );
        benchmark::DoNotOptimize( bWritten );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbContents ) );
}

} // namespace

BENCHMARK( BM_FileIo_NativeExists )->UseRealTime();
BENCHMARK( BM_FileIo_OpenClose )->UseRealTime();
BENCHMARK( BM_FileIo_ReadAll64KiB )->UseRealTime();
BENCHMARK( BM_FileIo_WriteAll64KiB )->UseRealTime();
