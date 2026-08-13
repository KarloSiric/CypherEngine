//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/FileSystem/CypherCommon_Vfs_Tests.cpp
//  Purpose: Tests the shared VFS contract and loose-directory provider.
//  Details: Protects canonical virtual identities, bounded transactional reads,
//           deterministic enumeration, cancellation, diagnostics, and root
//           isolation for tools and development-time resource loading.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Vfs.h"
#include "CypherCommon_VfsDirectory.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cypher::common;

namespace
{

std::string PathToUtf8( const std::filesystem::path &path )
{
    const std::u8string text = path.u8string();
    return {
        reinterpret_cast<const char *>( text.data() ),
        text.size()
    };
}

string_view_t TestText( const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

struct temporary_vfs_root_t {
    temporary_vfs_root_t()
    {
        const auto nUnique = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        path = std::filesystem::temp_directory_path() /
            ( "CypherCommon_Vfs_" + std::to_string( nUnique ) );
        std::filesystem::create_directories( path );
        utf8 = PathToUtf8( path );
    }

    ~temporary_vfs_root_t()
    {
        std::error_code error{};
        std::filesystem::remove_all( path, error );
    }

    void Write( const std::filesystem::path &relative, const char *pText ) const
    {
        const std::filesystem::path destination = path / relative;
        std::filesystem::create_directories( destination.parent_path() );
        std::ofstream stream( destination, std::ios::binary | std::ios::trunc );
        REQUIRE( stream.is_open() );
        stream.write( pText, static_cast<std::streamsize>( std::char_traits<char>::length( pText ) ) );
        REQUIRE( stream.good() );
    }

    string_view_t RootView() const noexcept
    {
        return { utf8.data(), utf8.size() };
    }

    std::filesystem::path path{};
    std::string utf8{};
};

struct directory_vfs_fixture_t {
    directory_vfs_fixture_t()
    {
        root.Write( "shaders/world.cyshader", "world" );
        root.Write( "shaders/sub/debug.cyshader", "debug" );
        root.Write( "materials/test.cymat", "material" );
        REQUIRE( VfsDirectory_Init( &directory, root.RootView() ) ==
                 vfs_status_t::OK );
        vfs = VfsDirectory_Make( &directory );
        REQUIRE( Vfs_IsValid( &vfs ) );
    }

    ~directory_vfs_fixture_t()
    {
        VfsDirectory_Shutdown( &directory );
    }

    temporary_vfs_root_t root{};
    vfs_directory_t directory{};
    vfs_t vfs{};
};

struct enumeration_capture_t {
    std::vector<std::string> paths{};
    usize cVisitsBeforeStop{ CY_INVALID_SIZE };
};

bool_t CaptureEntry(
    string_view_t virtualPath,
    const vfs_file_info_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<enumeration_capture_t *>( pUserData );
    if ( pCapture == nullptr ) {
        return CY_FALSE;
    }
    pCapture->paths.emplace_back( virtualPath.pData, virtualPath.cchLength );
    return pCapture->paths.size() < pCapture->cVisitsBeforeStop;
}

} // namespace

TEST_CASE( "VFS canonical paths are portable resource identities",
           "[CypherCommon][FileSystem][VFS]" )
{
    CHECK( Vfs_IsCanonicalPath( TestText( "shaders/world.cyshader" ) ) );
    CHECK( Vfs_IsCanonicalPath( TestText( "ui/menu-1/main_file.cykv" ) ) );

    CHECK_FALSE( Vfs_IsCanonicalPath( {} ) );
    CHECK_FALSE( Vfs_IsCanonicalPath( TestText( "/shaders/world.cyshader" ) ) );
    CHECK_FALSE( Vfs_IsCanonicalPath( TestText( "../world.cyshader" ) ) );
    CHECK_FALSE( Vfs_IsCanonicalPath( TestText( "shaders//world.cyshader" ) ) );
    CHECK_FALSE( Vfs_IsCanonicalPath( TestText( "Shaders/world.cyshader" ) ) );
    CHECK_FALSE( Vfs_IsCanonicalPath( TestText( "shaders\\world.cyshader" ) ) );
}

TEST_CASE( "Directory VFS stats and reads files transactionally",
           "[CypherCommon][FileSystem][VFS]" )
{
    directory_vfs_fixture_t fixture{};

    vfs_file_info_t info{};
    REQUIRE( Vfs_Stat(
        &fixture.vfs,
        TestText( "shaders/world.cyshader" ),
        &info ) == vfs_status_t::OK );
    CHECK( info.type == vfs_entry_type_t::FILE );
    CHECK( info.cbSize == 5u );

    REQUIRE( Vfs_Stat(
        &fixture.vfs,
        TestText( "shaders" ),
        &info ) == vfs_status_t::OK );
    CHECK( info.type == vfs_entry_type_t::DIRECTORY );

    bool_t bExists = CY_FALSE;
    REQUIRE( Vfs_Exists(
        &fixture.vfs,
        TestText( "materials/test.cymat" ),
        &bExists ) == vfs_status_t::OK );
    CHECK( bExists );
    REQUIRE( Vfs_Exists(
        &fixture.vfs,
        TestText( "materials/missing.cymat" ),
        &bExists ) == vfs_status_t::OK );
    CHECK_FALSE( bExists );

    blob_t contents{};
    REQUIRE( Blob_Init( &contents, Allocator_GetSystem() ) );
    REQUIRE( Vfs_ReadAll(
        &fixture.vfs,
        TestText( "shaders/world.cyshader" ),
        64u,
        &contents ) == vfs_status_t::OK );
    REQUIRE( Blob_Size( &contents ) == 5u );
    CHECK( std::string(
        reinterpret_cast<const char *>( Blob_Data( &contents ) ),
        Blob_Size( &contents ) ) == "world" );

    const byte previous[]{ 0xA5u, 0x5Au };
    REQUIRE( Blob_Assign(
        &contents,
        BinaryBlock_FromData( previous, sizeof( previous ) ) ) );
    CHECK( Vfs_ReadAll(
        &fixture.vfs,
        TestText( "shaders/world.cyshader" ),
        4u,
        &contents ) == vfs_status_t::SIZE_LIMIT );
    CHECK( Blob_Size( &contents ) == sizeof( previous ) );
    CHECK( Cy_MemEqual(
        Blob_Data( &contents ),
        previous,
        sizeof( previous ) ) );

    CHECK( Vfs_ReadAll(
        &fixture.vfs,
        TestText( "shaders/missing.cyshader" ),
        64u,
        &contents ) == vfs_status_t::NOT_FOUND );
    CHECK( Blob_Size( &contents ) == sizeof( previous ) );
    Blob_Shutdown( &contents );
}

TEST_CASE( "Directory VFS enumeration is sorted bounded and cancellable",
           "[CypherCommon][FileSystem][VFS]" )
{
    directory_vfs_fixture_t fixture{};

    enumeration_capture_t direct{};
    REQUIRE( Vfs_Enumerate(
        &fixture.vfs,
        TestText( "shaders" ),
        CY_FALSE,
        &CaptureEntry,
        &direct ) == vfs_status_t::OK );
    REQUIRE( direct.paths.size() == 2u );
    CHECK( direct.paths[0] == "shaders/sub" );
    CHECK( direct.paths[1] == "shaders/world.cyshader" );

    enumeration_capture_t recursive{};
    REQUIRE( Vfs_Enumerate(
        &fixture.vfs,
        TestText( "shaders" ),
        CY_TRUE,
        &CaptureEntry,
        &recursive ) == vfs_status_t::OK );
    REQUIRE( recursive.paths.size() == 3u );
    CHECK( recursive.paths[0] == "shaders/sub" );
    CHECK( recursive.paths[1] == "shaders/sub/debug.cyshader" );
    CHECK( recursive.paths[2] == "shaders/world.cyshader" );

    enumeration_capture_t cancelled{};
    cancelled.cVisitsBeforeStop = 1u;
    CHECK( Vfs_Enumerate(
        &fixture.vfs,
        TestText( "shaders" ),
        CY_TRUE,
        &CaptureEntry,
        &cancelled ) == vfs_status_t::CANCELLED );
    CHECK( cancelled.paths.size() == 1u );
}

TEST_CASE( "Directory VFS isolates its root and resolves diagnostic paths",
           "[CypherCommon][FileSystem][VFS]" )
{
    directory_vfs_fixture_t fixture{};

    vfs_file_info_t info{};
    CHECK( Vfs_Stat(
        &fixture.vfs,
        TestText( "../outside.cyshader" ),
        &info ) == vfs_status_t::INVALID_ARGUMENT );
    CHECK( Vfs_Stat(
        &fixture.vfs,
        TestText( "Shaders/world.cyshader" ),
        &info ) == vfs_status_t::INVALID_ARGUMENT );

    text_buffer_t diagnosticPath{};
    REQUIRE( TextBuffer_Init(
        &diagnosticPath,
        Allocator_GetSystem() ) );
    REQUIRE( Vfs_ResolveDiagnosticPath(
        &fixture.vfs,
        TestText( "shaders/missing.cyshader" ),
        &diagnosticPath ) == vfs_status_t::OK );
    const std::filesystem::path expected = std::filesystem::weakly_canonical(
        fixture.root.path / "shaders/missing.cyshader" );
    CHECK( std::filesystem::path(
        std::string(
            diagnosticPath.pData,
            diagnosticPath.cchLength ) ) == expected );
    TextBuffer_Shutdown( &diagnosticPath );

    const std::filesystem::path outside = fixture.root.path.parent_path() /
        "CypherCommon_Vfs_Outside.txt";
    {
        std::ofstream stream( outside, std::ios::binary | std::ios::trunc );
        REQUIRE( stream.is_open() );
        stream << "outside";
    }
    std::error_code error{};
    std::filesystem::create_symlink(
        outside,
        fixture.root.path / "outside-link.txt",
        error );
    if ( !error ) {
        CHECK( Vfs_Stat(
            &fixture.vfs,
            TestText( "outside-link.txt" ),
            &info ) == vfs_status_t::INVALID_PATH );
    }
    std::filesystem::remove( outside, error );
}

TEST_CASE( "Directory VFS lifecycle rejects invalid roots",
           "[CypherCommon][FileSystem][VFS]" )
{
    temporary_vfs_root_t root{};
    vfs_directory_t directory{};

    const std::filesystem::path missing = root.path / "missing";
    const std::string missingText = PathToUtf8( missing );
    CHECK( VfsDirectory_Init(
        &directory,
        { missingText.data(), missingText.size() } ) ==
        vfs_status_t::NOT_FOUND );

    REQUIRE( VfsDirectory_Init( &directory, root.RootView() ) ==
             vfs_status_t::OK );
    CHECK( VfsDirectory_Init( &directory, root.RootView() ) ==
           vfs_status_t::INVALID_ARGUMENT );
    VfsDirectory_Shutdown( &directory );
    const vfs_t invalidVfs = VfsDirectory_Make( &directory );
    CHECK_FALSE( Vfs_IsValid( &invalidVfs ) );
}
