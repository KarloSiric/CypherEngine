//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_FileIo_Tests.cpp
//  Purpose: Tests native-file bootstrap I/O and stream adaptation.
//  Details: These tests protect flag contracts, bounded UTF-8 paths, transactional
//           whole-file reads, append behavior, stream capabilities, and file offsets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FileIo.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

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

struct temporary_file_t {
    explicit temporary_file_t( const char *pStem )
    {
        const auto nUnique = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        const std::string fileName =
            std::string( pStem ) + "_" + std::to_string( nUnique ) + ".tmp";
        const std::u8string utf8FileName(
            reinterpret_cast<const char8_t *>( fileName.data() ),
            fileName.size() );
        path = std::filesystem::temp_directory_path() /
            std::filesystem::path( utf8FileName );
        std::filesystem::remove( path );
        utf8 = PathToUtf8( path );
    }

    ~temporary_file_t()
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

struct temporary_directory_t {
    explicit temporary_directory_t( const char *pStem )
    {
        const auto nUnique = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        path = std::filesystem::temp_directory_path() /
            ( std::string( pStem ) + "_" + std::to_string( nUnique ) );
        std::filesystem::remove_all( path );
    }

    ~temporary_directory_t()
    {
        std::error_code error;
        std::filesystem::remove_all( path, error );
    }

    std::string Utf8( const std::filesystem::path &value ) const
    {
        return PathToUtf8( value );
    }

    std::filesystem::path path{};
};

struct native_file_guard_t {
    ~native_file_guard_t()
    {
        FileIo_CloseNative( pFile );
    }

    native_file_t *pFile{ nullptr };
};

} // namespace

TEST_CASE( "FileIo validates open flag contracts",
           "[CypherCommon][Tier1][FileIo]" )
{
    temporary_file_t file( "CypherCommon_FileIo_Flags" );
    const allocator_t *pAllocator = Allocator_GetSystem();

    REQUIRE( FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_NONE,
        pAllocator ) == nullptr );
    REQUIRE( FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_READ | FILE_OPEN_FLAG_CREATE,
        pAllocator ) == nullptr );
    REQUIRE( FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_WRITE | FILE_OPEN_FLAG_EXCLUSIVE,
        pAllocator ) == nullptr );
    REQUIRE( FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_WRITE | FILE_OPEN_FLAG_APPEND | FILE_OPEN_FLAG_TRUNCATE,
        pAllocator ) == nullptr );
    REQUIRE( FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_READ | CYPHER_BIT32( 31 ),
        pAllocator ) == nullptr );

    const std::string embeddedNull = file.utf8 + std::string( "\0ignored", 8u );
    REQUIRE( FileIo_OpenNative(
        { embeddedNull.data(), embeddedNull.size() },
        FILE_OPEN_FLAG_READ,
        pAllocator ) == nullptr );
}

TEST_CASE( "FileIo stream supports write seek read size and flush",
           "[CypherCommon][Tier1][FileIo]" )
{
    temporary_file_t file( "CypherCommon_FileIo_Stream" );
    native_file_guard_t guard{};
    guard.pFile = FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_READ |
        FILE_OPEN_FLAG_WRITE |
        FILE_OPEN_FLAG_CREATE |
        FILE_OPEN_FLAG_TRUNCATE,
        Allocator_GetSystem() );
    REQUIRE( guard.pFile != nullptr );

    stream_t stream = FileIo_AsStream( guard.pFile );
    REQUIRE( Stream_HasCapabilities(
        &stream,
        STREAM_CAPABILITY_READ |
        STREAM_CAPABILITY_WRITE |
        STREAM_CAPABILITY_SEEK |
        STREAM_CAPABILITY_SIZE |
        STREAM_CAPABILITY_FLUSH ) );

    const byte source[]{ 0x10u, 0x20u, 0x30u, 0x40u, 0x50u };
    REQUIRE( Stream_WriteExact( &stream, source, sizeof( source ) ) == stream_status_t::OK );
    REQUIRE( Stream_Flush( &stream ) == stream_status_t::OK );

    u64 nPosition = 0u;
    u64 cbSize = 0u;
    REQUIRE( Stream_Tell( &stream, &nPosition ) == stream_status_t::OK );
    REQUIRE( nPosition == sizeof( source ) );
    REQUIRE( Stream_Size( &stream, &cbSize ) == stream_status_t::OK );
    REQUIRE( cbSize == sizeof( source ) );

    REQUIRE( Stream_Seek(
        &stream,
        0,
        stream_seek_origin_t::BEGIN ) == stream_status_t::OK );
    byte output[sizeof( source )]{};
    REQUIRE( Stream_ReadExact( &stream, output, sizeof( output ) ) == stream_status_t::OK );
    REQUIRE( Cy_MemCompare( output, source, sizeof( source ) ) == 0 );

    byte endByte = 0u;
    const stream_io_result_t end = Stream_Read( &stream, &endByte, 1u );
    REQUIRE( end.status == stream_status_t::END_OF_STREAM );
    REQUIRE( end.cbTransferred == 0u );
}

TEST_CASE( "FileIo whole-file helpers preserve bounded paths and destination state",
           "[CypherCommon][Tier1][FileIo]" )
{
    temporary_file_t file( "CypherCommon_FileIo_Whole_\xC5\xBD" );
    const byte source[]{ 1u, 3u, 5u, 7u, 9u };

    std::string pathWithSuffix = file.utf8 + ".not-part-of-view";
    const string_view_t boundedPath{
        pathWithSuffix.data(),
        file.utf8.size()
    };
    REQUIRE( FileIo_WriteAllNative(
        boundedPath,
        { source, sizeof( source ) } ) );
    REQUIRE( FileIo_NativeExists( file.View() ) );

    blob_t output{};
    REQUIRE( Blob_Init( &output, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( file.View(), &output ) );
    REQUIRE( Blob_Size( &output ) == sizeof( source ) );
    REQUIRE( Cy_MemCompare( Blob_Data( &output ), source, sizeof( source ) ) == 0 );

    const byte previous[]{ 0xAAu, 0xBBu };
    REQUIRE( Blob_Assign( &output, { previous, sizeof( previous ) } ) );
    temporary_file_t missing( "CypherCommon_FileIo_Missing" );
    REQUIRE_FALSE( FileIo_ReadAllNative( missing.View(), &output ) );
    REQUIRE( Blob_Size( &output ) == sizeof( previous ) );
    REQUIRE( Cy_MemCompare( Blob_Data( &output ), previous, sizeof( previous ) ) == 0 );
}

TEST_CASE( "FileIo append and exclusive creation have deterministic semantics",
           "[CypherCommon][Tier1][FileIo]" )
{
    temporary_file_t file( "CypherCommon_FileIo_Append" );
    const byte prefix[]{ 'A', 'B' };
    REQUIRE( FileIo_WriteAllNative( file.View(), { prefix, sizeof( prefix ) } ) );

    native_file_guard_t appendGuard{};
    appendGuard.pFile = FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_WRITE | FILE_OPEN_FLAG_APPEND,
        Allocator_GetSystem() );
    REQUIRE( appendGuard.pFile != nullptr );

    stream_t appendStream = FileIo_AsStream( appendGuard.pFile );
    u64 nPosition = 0u;
    REQUIRE( Stream_Tell( &appendStream, &nPosition ) == stream_status_t::OK );
    REQUIRE( nPosition == sizeof( prefix ) );
    const byte suffix[]{ 'C', 'D' };
    REQUIRE( Stream_WriteExact(
        &appendStream,
        suffix,
        sizeof( suffix ) ) == stream_status_t::OK );
    REQUIRE( Stream_Flush( &appendStream ) == stream_status_t::OK );
    FileIo_CloseNative( appendGuard.pFile );
    appendGuard.pFile = nullptr;

    blob_t contents{};
    REQUIRE( Blob_Init( &contents, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative( file.View(), &contents ) );
    const byte expected[]{ 'A', 'B', 'C', 'D' };
    REQUIRE( Blob_Size( &contents ) == sizeof( expected ) );
    REQUIRE( Cy_MemCompare(
        Blob_Data( &contents ),
        expected,
        sizeof( expected ) ) == 0 );

    native_file_guard_t exclusiveGuard{};
    exclusiveGuard.pFile = FileIo_OpenNative(
        file.View(),
        FILE_OPEN_FLAG_WRITE |
        FILE_OPEN_FLAG_CREATE |
        FILE_OPEN_FLAG_EXCLUSIVE,
        Allocator_GetSystem() );
    REQUIRE( exclusiveGuard.pFile == nullptr );
}

TEST_CASE( "FileIo creates native directory trees and atomically replaces files",
           "[CypherCommon][Tier1][FileIo]" )
{
    temporary_directory_t temporary( "CypherCommon_FileIo_Publish" );
    const std::filesystem::path nested = temporary.path / "a" / "b" / "c";
    const std::string nestedText = temporary.Utf8( nested );
    REQUIRE( FileIo_CreateDirectoriesNative(
        { nestedText.data(), nestedText.size() } ) );
    REQUIRE( std::filesystem::is_directory( nested ) );
    REQUIRE( FileIo_CreateDirectoriesNative(
        { nestedText.data(), nestedText.size() } ) );

    const std::filesystem::path source = nested / "source.tmp";
    const std::filesystem::path destination = nested / "asset.bin";
    const std::string sourceText = temporary.Utf8( source );
    const std::string destinationText = temporary.Utf8( destination );
    const byte oldContents[]{ 'o', 'l', 'd' };
    const byte newContents[]{ 'n', 'e', 'w' };
    REQUIRE( FileIo_WriteAllNative(
        { destinationText.data(), destinationText.size() },
        { oldContents, sizeof( oldContents ) } ) );
    REQUIRE( FileIo_WriteAllNative(
        { sourceText.data(), sourceText.size() },
        { newContents, sizeof( newContents ) } ) );
    REQUIRE( FileIo_ReplaceNative(
        { sourceText.data(), sourceText.size() },
        { destinationText.data(), destinationText.size() } ) );
    REQUIRE_FALSE( std::filesystem::exists( source ) );

    blob_t contents{};
    REQUIRE( Blob_Init( &contents, Allocator_GetSystem() ) );
    REQUIRE( FileIo_ReadAllNative(
        { destinationText.data(), destinationText.size() },
        &contents ) );
    REQUIRE( Blob_Size( &contents ) == sizeof( newContents ) );
    REQUIRE( Cy_MemCompare(
        Blob_Data( &contents ),
        newContents,
        sizeof( newContents ) ) == 0 );
    REQUIRE( FileIo_RemoveNative(
        { destinationText.data(), destinationText.size() } ) );
    REQUIRE_FALSE( std::filesystem::exists( destination ) );
}
