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

TEST_CASE( "The chunk folder is the root path without its extension", "[map][files]" )
{
    char buffer[64]{};
    CHECK( MapFiles_ChunkDirectory( "maps/facility.cymap", buffer, sizeof( buffer ) ) == 13u );
    CHECK( std::string( buffer ) == "maps/facility" );
    CHECK( MapFiles_ChunkDirectory( "maps/facility.txt", buffer, sizeof( buffer ) ) == 0u );
    CHECK( MapFiles_ChunkDirectory( ".cymap", buffer, sizeof( buffer ) ) == 0u );
    CHECK( MapFiles_ChunkDirectory( "maps/.cymap", buffer, sizeof( buffer ) ) == 0u );
    char tiny[8]{};
    CHECK( MapFiles_ChunkDirectory( "maps/facility.cymap", tiny, sizeof( tiny ) ) == 0u );
}

TEST_CASE( "Loading reports a missing root and rejects paths that are not maps", "[map][files]" )
{
    scratch_dir_t scratch( "missing" );
    map_document_t map{};
    CHECK( MapFiles_Load( &map, Allocator_GetSystem(), ( scratch.path / "nothing.cymap" ).string().c_str() ).status ==
           map_files_status_t::ROOT_MISSING );
    CHECK( MapFiles_Load( &map, Allocator_GetSystem(), ( scratch.path / "nothing.txt" ).string().c_str() ).status ==
           map_files_status_t::INVALID_ARGUMENT );
    CHECK( MapFiles_Load( nullptr, Allocator_GetSystem(), "a.cymap" ).status == map_files_status_t::INVALID_ARGUMENT );
    CHECK( MapFiles_Save( &map, nullptr ).status == map_files_status_t::INVALID_ARGUMENT );
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
    CHECK( loaded.nChunkFiles == 13u );
    CHECK( map.problems.nCount == 0u );

    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    REQUIRE( saved.status == map_files_status_t::OK );
    CHECK( saved.nChunkFiles == 13u );
    CHECK( saved.nRemoved == 0u );
    CHECK( Snapshot( scratch.path ) == before );
}

TEST_CASE( "Save As writes a complete copy under the new name", "[map][files]" )
{
    scratch_dir_t scratch( "saveas" );
    map_document_t map{};
    REQUIRE( MapFiles_Load( &map, Allocator_GetSystem(), ( ExampleDir() / "facility.cymap" ).string().c_str() ).status ==
             map_files_status_t::OK );
    const fs::path copy = scratch.path / "facility_copy.cymap";
    REQUIRE( MapFiles_Save( &map, copy.string().c_str() ).status == map_files_status_t::OK );

    CHECK( ReadFile( copy ) == ReadFile( ExampleDir() / "facility.cymap" ) );
    CHECK( Snapshot( scratch.path / "facility_copy" ) == Snapshot( ExampleDir() / "facility" ) );

    // The copy is a map in its own right.
    map_document_t reopened{};
    const map_files_result_t loaded = MapFiles_Load( &reopened, Allocator_GetSystem(), copy.string().c_str() );
    CHECK( loaded.status == map_files_status_t::OK );
    CHECK( loaded.nChunkFiles == 13u );
}

TEST_CASE( "A map with no objects has no chunk folder and still reopens", "[map][files]" )
{
    scratch_dir_t scratch( "empty" );
    map_document_t map{};
    CreateMap( &map );
    const fs::path root = scratch.path / "empty.cymap";
    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    REQUIRE( saved.status == map_files_status_t::OK );
    CHECK( saved.nChunkFiles == 0u );
    CHECK_FALSE( fs::exists( scratch.path / "empty" ) );

    map_document_t reopened{};
    const map_files_result_t loaded = MapFiles_Load( &reopened, Allocator_GetSystem(), root.string().c_str() );
    CHECK( loaded.status == map_files_status_t::OK );
    CHECK( loaded.nChunkFiles == 0u );
}

TEST_CASE( "Saving deletes emptied chunks and leaves other files alone", "[map][files]" )
{
    scratch_dir_t scratch( "moves" );
    map_document_t map{};
    CreateMap( &map );
    const u64 id = AddLight( &map, 16.0 );
    const fs::path root = scratch.path / "moves.cymap";
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    REQUIRE( fs::exists( scratch.path / "moves/default/x0_y0.cymapchunk" ) );

    // Files the writer never produces: a note, a stray backup, a deep copy.
    WriteFile( scratch.path / "moves/notes.txt", "keep me" );
    WriteFile( scratch.path / "moves/default/x0_y0.cymapchunk.bak", "old" );
    WriteFile( scratch.path / "moves/old/default/x0_y0.cymapchunk", ReadFile( scratch.path / "moves/default/x0_y0.cymapchunk" ) );

    REQUIRE( MapDocument_SetEntityOrigin( &map, id, { 9000.0, 0.0, 0.0 } ) == map_status_t::OK );
    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    REQUIRE( saved.status == map_files_status_t::OK );
    CHECK( saved.nRemoved == 1u );
    CHECK_FALSE( fs::exists( scratch.path / "moves/default/x0_y0.cymapchunk" ) );
    CHECK( fs::exists( scratch.path / "moves/default/x1_y0.cymapchunk" ) );
    CHECK( ReadFile( scratch.path / "moves/notes.txt" ) == "keep me" );
    CHECK( fs::exists( scratch.path / "moves/default/x0_y0.cymapchunk.bak" ) );
    CHECK_FALSE( AnyStagingFiles( scratch.path ) );

    // The deep copy is not a chunk path, so it does not load as a duplicate.
    map_document_t reopened{};
    const map_files_result_t loaded = MapFiles_Load( &reopened, Allocator_GetSystem(), root.string().c_str() );
    REQUIRE( loaded.status == map_files_status_t::OK );
    CHECK( loaded.documentStatus == map_status_t::OK );
    CHECK( loaded.nChunkFiles == 1u );
    CHECK( MapDocument_FindObject( &reopened, id, nullptr ) != nullptr );
}

TEST_CASE( "A chunk file that cannot be read is kept byte for byte", "[map][files]" )
{
    scratch_dir_t scratch( "damaged" );
    const fs::path root = CopyExample( scratch );
    const fs::path damaged = scratch.path / "facility/detail/x7_y7.cymapchunk";
    const std::string garbage = "@cykv 1\n{ this is not a chunk";
    WriteFile( damaged, garbage );

    map_document_t map{};
    const map_files_result_t loaded = MapFiles_Load( &map, Allocator_GetSystem(), root.string().c_str() );
    REQUIRE( loaded.status == map_files_status_t::OK );
    CHECK( loaded.nChunkFiles == 14u );
    CHECK( CountProblems( map, map_problem_code_t::CHUNK_UNREADABLE ) == 1u );

    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    CHECK( ReadFile( damaged ) == garbage );
}

TEST_CASE( "A failed save leaves every file on disk as it was", "[map][files]" )
{
    scratch_dir_t scratch( "failed" );
    map_document_t map{};
    CreateMap( &map );
    ( void )AddLight( &map, 16.0 );
    const fs::path root = scratch.path / "failed.cymap";
    REQUIRE( MapFiles_Save( &map, root.string().c_str() ).status == map_files_status_t::OK );
    const std::map<std::string, std::string> before = Snapshot( scratch.path );

    // A new layer whose folder cannot be created: a file already has its name.
    REQUIRE( MapDocument_AddLayer( &map, StringView_FromCString( "blocked" ), StringView_FromCString( "Blocked" ) ) == map_status_t::OK );
    WriteFile( scratch.path / "failed/blocked", "not a folder" );
    const std::map<std::string, std::string> withBlocker = Snapshot( scratch.path );
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "blocked" ), StringView_FromCString( "light" ), { 0.0, 0.0, 0.0 }, &id ) ==
             map_status_t::OK );
    ( void )AddLight( &map, 20000.0 ); // Another chunk that would be staged too.

    const map_files_result_t saved = MapFiles_Save( &map, root.string().c_str() );
    CHECK( saved.status == map_files_status_t::WRITE_FAILED );
    CHECK( saved.documentStatus == map_status_t::SINK_FAILED );
    CHECK( Snapshot( scratch.path ) == withBlocker );
    CHECK_FALSE( AnyStagingFiles( scratch.path ) );
    CHECK( before.size() + 1u == withBlocker.size() );
}
