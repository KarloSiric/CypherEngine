//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Files_Tests.cpp
//  Purpose: Contract tests for reading and writing maps on disk.
//  Details: Every test works in its own folder under the system temp
//           directory, removed afterwards, and never writes to the docs
//           example it copies from.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Files.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace fs = std::filesystem;

namespace
{

// A fresh folder per test, deleted on scope exit even when a REQUIRE fails.
struct scratch_dir_t {
    explicit scratch_dir_t( const char *pName )
        : path( fs::temp_directory_path() / ( std::string( "cypher_map_files_" ) + pName ) )
    {
        fs::remove_all( path );
        fs::create_directories( path );
    }
    ~scratch_dir_t() { fs::remove_all( path ); }
    fs::path path;
};

std::string ReadFile( const fs::path &path )
{
    std::ifstream stream( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
}

void WriteFile( const fs::path &path, const std::string &text )
{
    fs::create_directories( path.parent_path() );
    std::ofstream( path, std::ios::binary ) << text;
}

// Every regular file under a folder, keyed by its path relative to it.
std::map<std::string, std::string> Snapshot( const fs::path &directory )
{
    std::map<std::string, std::string> files;
    for ( const fs::directory_entry &entry : fs::recursive_directory_iterator( directory ) ) {
        if ( entry.is_regular_file() ) { files[fs::relative( entry.path(), directory ).generic_string()] = ReadFile( entry.path() ); }
    }
    return files;
}

bool AnyStagingFiles( const fs::path &directory )
{
    for ( const fs::directory_entry &entry : fs::recursive_directory_iterator( directory ) ) {
        if ( entry.path().extension() == ".tmp" ) { return true; }
    }
    return false;
}

fs::path ExampleDir() { return fs::path( CYPHER_MAP_EXAMPLE_DIR ); }

// Copies the docs example map into a scratch folder.
fs::path CopyExample( const scratch_dir_t &scratch )
{
    fs::copy( ExampleDir(), scratch.path, fs::copy_options::recursive );
    return scratch.path / "facility.cymap";
}

void CreateMap( map_document_t *pMap )
{
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "Disk Test" );
    desc.game = StringView_FromCString( "reap" );
    REQUIRE( MapDocument_Create( pMap, Allocator_GetSystem(), desc ) == map_status_t::OK );
}

u64 AddLight( map_document_t *pMap, f64 x )
{
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( pMap, StringView_FromCString( "default" ), StringView_FromCString( "light" ), { x, 0.0, 0.0 }, &id ) ==
             map_status_t::OK );
    return id;
}

usize CountProblems( const map_document_t &map, map_problem_code_t code )
{
    usize n = 0u;
    for ( usize i = 0u; i < map.problems.nCount; ++i ) { n += map.problems.pData[i].code == code ? 1u : 0u; }
    return n;
}

} // namespace

TEST_CASE( "Loading reports a missing file and refuses files that are not maps", "[map][files]" )
{
    scratch_dir_t scratch( "missing" );
    map_document_t map{};
    CHECK( MapFiles_Load( &map, Allocator_GetSystem(), ( scratch.path / "nothing.cymap" ).string().c_str() ).status ==
           map_files_status_t::ROOT_MISSING );
    CHECK( MapFiles_Load( nullptr, Allocator_GetSystem(), "a.cymap" ).status == map_files_status_t::INVALID_ARGUMENT );
    CHECK( MapFiles_Save( &map, nullptr ).status == map_files_status_t::INVALID_ARGUMENT );
    WriteFile( scratch.path / "notes.cymap", "just some notes" );
    const map_files_result_t notes = MapFiles_Load( &map, Allocator_GetSystem(), ( scratch.path / "notes.cymap" ).string().c_str() );
    CHECK( notes.status == map_files_status_t::DOCUMENT_FAILED );
    CHECK( notes.documentStatus == map_status_t::ROOT_UNREADABLE );
    CHECK( CountProblems( map, map_problem_code_t::TEXT_UNREADABLE ) == 1u );
}

TEST_CASE( "A file larger than the format allows is refused without being read", "[map][files]" )
{
    scratch_dir_t scratch( "huge" );
    const fs::path path = scratch.path / "huge.cymap";
    WriteFile( path, "@cykv 1\n" );
    fs::resize_file( path, MAP_TEXT_MAX + 1u ); // Sparse: no real disk use.
    map_document_t map{};
    CHECK( MapFiles_Load( &map, Allocator_GetSystem(), path.string().c_str() ).status == map_files_status_t::READ_FAILED );
    CHECK( map.pAllocator == nullptr );
}

TEST_CASE( "The example map loads from disk and saves back byte for byte", "[map][files]" )
{
    scratch_dir_t scratch( "roundtrip" );
    const fs::path root = CopyExample( scratch );
    const std::map<std::string, std::string> before = Snapshot( scratch.path );

    map_document_t map{};
    const map_files_result_t loaded = MapFiles_Load( &map, Allocator_GetSystem(), root.string().c_str() );
    REQUIRE( loaded.status == map_files_status_t::OK );
    CHECK( loaded.documentStatus == map_status_t::OK );
    CHECK( loaded.cbFile == fs::file_size( root ) );
    CHECK( map.problems.nCount == 0u );

    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    REQUIRE( saved.status == map_files_status_t::OK );
    CHECK( saved.cbFile == loaded.cbFile );
    CHECK( Snapshot( scratch.path ) == before );
}

TEST_CASE( "Save As writes a complete copy under the new name", "[map][files]" )
{
    scratch_dir_t scratch( "saveas" );
    map_document_t map{};
    REQUIRE( MapFiles_Load( &map, Allocator_GetSystem(), ( ExampleDir() / "facility.cymap" ).string().c_str() ).status ==
             map_files_status_t::OK );
    const fs::path copy = scratch.path / "nested" / "facility_copy.cymap";
    REQUIRE( MapFiles_Save( &map, copy.string().c_str() ).status == map_files_status_t::OK );
    CHECK( ReadFile( copy ) == ReadFile( ExampleDir() / "facility.cymap" ) );
    // The whole level is the one file: nothing else was written.
    CHECK( Snapshot( scratch.path ).size() == 1u );

    map_document_t reopened{};
    CHECK( MapFiles_Load( &reopened, Allocator_GetSystem(), copy.string().c_str() ).status == map_files_status_t::OK );
}

TEST_CASE( "A map with no objects saves and reopens", "[map][files]" )
{
    scratch_dir_t scratch( "empty" );
    map_document_t map{};
    CreateMap( &map );
    const fs::path root = scratch.path / "empty.cymap";
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    CHECK( Snapshot( scratch.path ).size() == 1u );
    map_document_t reopened{};
    CHECK( MapFiles_Load( &reopened, Allocator_GetSystem(), root.string().c_str() ).status == map_files_status_t::OK );
}

TEST_CASE( "Saving rewrites only the map file and leaves files beside it alone", "[map][files]" )
{
    scratch_dir_t scratch( "beside" );
    map_document_t map{};
    CreateMap( &map );
    const u64 id = AddLight( &map, 16.0 );
    const fs::path root = scratch.path / "beside.cymap";
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );

    // Files the writer never produces, including a folder left by the
    // retired split format: none of them is read, moved, or deleted.
    WriteFile( scratch.path / "notes.txt", "keep me" );
    WriteFile( scratch.path / "beside.cymap.bak", "old" );
    WriteFile( scratch.path / "beside/default/x0_y0.cymapchunk", "@cykv 1\n@schema \"cypher.map_chunk\" 10\n{ entities = [ { id = 1u } ] }" );
    const std::map<std::string, std::string> others = [&] {
        auto files = Snapshot( scratch.path );
        files.erase( "beside.cymap" );
        return files;
    }();

    REQUIRE( MapDocument_SetEntityOrigin( &map, id, { 9000.0, 0.0, 0.0 } ) == map_status_t::OK );
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    auto after = Snapshot( scratch.path );
    after.erase( "beside.cymap" );
    CHECK( after == others );
    CHECK_FALSE( AnyStagingFiles( scratch.path ) );

    map_document_t reopened{};
    const map_files_result_t loaded = MapFiles_Load( &reopened, Allocator_GetSystem(), root.string().c_str() );
    REQUIRE( loaded.status == map_files_status_t::OK );
    CHECK( reopened.problems.nCount == 0u );
    CHECK( MapDocument_FindObject( &reopened, id, nullptr ) != nullptr );
}

TEST_CASE( "A failed save leaves the map on disk as it was", "[map][files]" )
{
    scratch_dir_t scratch( "failed" );
    map_document_t map{};
    CreateMap( &map );
    ( void )AddLight( &map, 16.0 );
    const fs::path root = scratch.path / "failed.cymap";
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    // The staging file's name is taken by a folder, so it cannot be written.
    fs::create_directories( scratch.path / "failed.cymap.tmp" );
    WriteFile( scratch.path / "failed.cymap.tmp" / "keep", "x" );
    const std::map<std::string, std::string> before = Snapshot( scratch.path );
    ( void )AddLight( &map, 20000.0 );
    map.bIdsAssigned = CY_TRUE;
    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    CHECK( saved.status == map_files_status_t::WRITE_FAILED );
    CHECK( saved.documentStatus == map_status_t::OK );
    CHECK( Snapshot( scratch.path ) == before );
    CHECK( map.bIdsAssigned ); // The IDs loading assigned still need writing.
}

// A maintenance tool, not a contract: re-saves the maps listed in
// CYPHER_RESAVE_MAPS (separated by ';') in the current canonical layout,
// the way Mason would, after a change to the writer's layout.
TEST_CASE( "Re-save maps in the canonical layout", "[.resave-maps]" )
{
    const char *pList = std::getenv( "CYPHER_RESAVE_MAPS" );
    REQUIRE( pList != nullptr );
    const std::string list = pList;
    for ( usize start = 0u; start < list.size(); ) {
        usize end = list.find( ';', start );
        if ( end == std::string::npos ) { end = list.size(); }
        const std::string path = list.substr( start, end - start );
        start = end + 1u;
        INFO( path );
        map_document_t map{};
        REQUIRE( MapFiles_Load( &map, Allocator_GetSystem(), path.c_str() ).status == map_files_status_t::OK );
        CHECK( map.problems.nCount == 0u );
        REQUIRE( MapFiles_Save( &map, path.c_str() ).status == map_files_status_t::OK );
    }
}
