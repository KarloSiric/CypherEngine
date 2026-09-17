//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// File: tests/CypherConfig/CypherConfig_Tests.cpp
// Purpose: Verify config input bounds, nested execution, and CVar preservation.
//////////////////////////////////////////////////////////////////////////

#include "CypherConfig.h"
#include "CypherCommand.h"
#include "CypherCVar.h"
#include "CypherFileSystem.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace cfg = cypher::engine::cfg;
namespace cmd = cypher::engine::cmd;
namespace cvar = cypher::engine::cvar;
namespace fs = cypher::engine::fs;

namespace
{

struct config_scope_t {
    std::filesystem::path directory;
    unsigned calls{ 0u };

    config_scope_t()
    {
        static std::atomic<unsigned> sequence{ 0u };
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
            ( "cypher_config_" + std::to_string( now ) + "_" + std::to_string( sequence++ ) );
    }

    ~config_scope_t()
    {
        cfg::Cfg_Shutdown();
        cvar::Cvar_Shutdown();
        cmd::Cmd_Shutdown();
        fs::FS_Shutdown();
        std::error_code ignored;
        std::filesystem::remove_all( directory, ignored );
    }

    void Init()
    {
        REQUIRE( std::filesystem::create_directory( directory ) );
        REQUIRE( fs::FS_Init() == fs::fs_error_t::OK );
        REQUIRE( fs::FS_MountDirectory( "", directory.string().c_str(),
            fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY, 0u ) == fs::fs_error_t::OK );
        REQUIRE( cmd::Cmd_Init() == cmd::cmd_error_t::OK );
        REQUIRE( cvar::Cvar_Init() == cvar::cvar_error_t::OK );
        REQUIRE( cfg::Cfg_Init() == cfg::cfg_error_t::OK );
        REQUIRE( cmd::Cmd_Register( "capture", []( void *context, auto, auto ) {
            ++*static_cast<unsigned *>( context );
        }, &calls, nullptr ) == cmd::cmd_error_t::OK );
        REQUIRE( cvar::Cvar_Register( "value", "original", cvar::CYPHER_CVAR_NONE ) == cvar::cvar_error_t::OK );
    }

    void Write( const std::string &name, const std::string &contents )
    {
        std::ofstream stream( directory / name, std::ios::binary );
        REQUIRE( stream.is_open() );
        stream.write( contents.data(), static_cast<std::streamsize>( contents.size() ) );
        stream.close();
        REQUIRE_FALSE( stream.fail() );
    }
};

} // namespace

TEST_CASE( "Runtime config preserves comments, quoted values, and line endings", "[Config]" )
{
    config_scope_t scope;
    scope.Init();
    scope.Write( "valid.cfg", "# comment\r\nset value \"hello // # world\" // comment\r\ncapture\rcapture\n" );
    bool loaded = false;
    REQUIRE( cfg::Cfg_LoadFile( "valid.cfg", true, &loaded ) == cfg::cfg_error_t::OK );
    REQUIRE( loaded );
    REQUIRE( std::string( cvar::Cvar_GetString( "value" ) ) == "hello // # world" );
    REQUIRE( scope.calls == 2u );
    loaded = true;
    REQUIRE( cfg::Cfg_LoadFile( "missing.cfg", false, &loaded ) == cfg::cfg_error_t::OK );
    REQUIRE_FALSE( loaded );
    REQUIRE( cfg::Cfg_LoadFile( "missing.cfg", true ) == cfg::cfg_error_t::ERR_FILE_OPEN_FAILED );
    loaded = true;
    REQUIRE( cfg::Cfg_LoadFile( "../invalid.cfg", false, &loaded ) == cfg::cfg_error_t::ERR_FILE_OPEN_FAILED );
    REQUIRE_FALSE( loaded );
}

TEST_CASE( "Runtime config rejects oversized lines and embedded NUL before dispatch", "[Config][Bounds]" )
{
    config_scope_t scope;
    scope.Init();
    SECTION( "Oversized command line" ) {
        std::string line = "capture ";
        line.append( cfg::CYPHER_CONFIG_MAX_LINE_LENGTH, 'x' );
        REQUIRE( cfg::Cfg_ExecuteLine( line.c_str() ) == cfg::cfg_error_t::ERR_INVALID_LINE );
    }
    SECTION( "Embedded NUL file" ) {
        std::string contents = "capture\n";
        contents += '\0';
        contents += "capture\n";
        scope.Write( "nul.cfg", contents );
        REQUIRE( cfg::Cfg_LoadFile( "nul.cfg", true ) == cfg::cfg_error_t::ERR_INVALID_LINE );
    }
    REQUIRE( scope.calls == 0u );
}

TEST_CASE( "Runtime config bounds nested includes and recovers for the next file", "[Config][Depth]" )
{
    config_scope_t scope;
    scope.Init();
    // Finite chain also fails safely against an implementation without a depth guard.
    for ( unsigned i = 0u; i < 12u; ++i ) {
        const auto name = "chain" + std::to_string( i ) + ".cfg";
        const auto contents = i == 11u ? "capture\n" :
            "exec chain" + std::to_string( i + 1u ) + ".cfg\n";
        scope.Write( name, contents );
    }
    REQUIRE( cfg::Cfg_LoadFile( "chain0.cfg", true ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    REQUIRE( scope.calls == 0u );
    scope.Write( "leaf.cfg", "capture\n" );
    scope.Write( "parent.cfg", "exec leaf.cfg\n" );
    REQUIRE( cfg::Cfg_LoadFile( "parent.cfg", true ) == cfg::cfg_error_t::OK );
    REQUIRE( scope.calls == 1u );
}

TEST_CASE( "Runtime config rejects truncated names, paths, and trailing values", "[Config][Bounds]" )
{
    config_scope_t scope;
    scope.Init();
    REQUIRE( cfg::Cfg_ExecuteLine( "set value \"new\" extra" ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    REQUIRE( cfg::Cfg_ExecuteLine( "set value new extra" ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    REQUIRE( std::string( cvar::Cvar_GetString( "value" ) ) == "original" );
    const std::string longName( 256u, 'n' );
    REQUIRE( cfg::Cfg_ExecuteLine( ( "set " + longName + " 1" ).c_str() ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    scope.Write( "leaf.cfg", "capture\n" );
    REQUIRE( cfg::Cfg_ExecuteLine( "exec \"leaf.cfg\" extra" ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    REQUIRE( cfg::Cfg_ExecuteLine( "exec leaf.cfg extra" ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    const std::string longPath( cfg::CYPHER_CONFIG_MAX_PATH_LENGTH, 'p' );
    REQUIRE( cfg::Cfg_ExecuteLine( ( "exec " + longPath ).c_str() ) == cfg::cfg_error_t::ERR_PARSE_FAILED );
    REQUIRE( scope.calls == 0u );
}

TEST_CASE( "Runtime CVar rejects unrepresentable strings without partial mutation", "[Config][CVar]" )
{
    config_scope_t scope;
    scope.Init();
    const std::string oversized( 256u, '9' );
    REQUIRE( cvar::Cvar_Register( "oversized", oversized.c_str(), cvar::CYPHER_CVAR_NONE ) == cvar::cvar_error_t::ERR_INVALID_DEFAULT_VALUE );
    REQUIRE( cvar::Cvar_Find( "oversized" ) == nullptr );
    REQUIRE( cvar::Cvar_Set( "value", oversized.c_str() ) == cvar::cvar_error_t::ERR_INVALID_CVAR );
    REQUIRE( std::string( cvar::Cvar_GetString( "value" ) ) == "original" );
    REQUIRE( cvar::Cvar_Find( "value" )->flags == cvar::CYPHER_CVAR_NONE );
    const std::string maximum( 255u, 'x' );
    REQUIRE( cvar::Cvar_Set( "value", maximum.c_str() ) == cvar::cvar_error_t::OK );
    REQUIRE( std::string( cvar::Cvar_GetString( "value" ) ) == maximum );
    REQUIRE( cvar::Cvar_Set( "value", cvar::Cvar_GetString( "value" ) + 1u ) == cvar::cvar_error_t::OK );
    REQUIRE( std::string( cvar::Cvar_GetString( "value" ) ) == maximum.substr( 1u ) );
}

TEST_CASE( "Runtime CVar numeric caches remain defined for large values", "[Config][CVar]" )
{
    config_scope_t scope;
    scope.Init();
    REQUIRE( cvar::Cvar_Set( "value", "4294967295" ) == cvar::cvar_error_t::OK );
    REQUIRE( cvar::Cvar_GetInt( "value" ) == std::numeric_limits<cypher::engine::common::u32>::max() );
    REQUIRE( cvar::Cvar_GetBool( "value" ) );
    REQUIRE( cvar::Cvar_Set( "value", "-1" ) == cvar::cvar_error_t::OK );
    REQUIRE( cvar::Cvar_GetInt( "value" ) == std::numeric_limits<cypher::engine::common::u32>::max() );
    REQUIRE( cvar::Cvar_Set( "value", "1e1000" ) == cvar::cvar_error_t::OK );
    REQUIRE( std::isinf( cvar::Cvar_GetFloat( "value" ) ) );
    const std::string largeInteger( 255u, '9' );
    REQUIRE( cvar::Cvar_Set( "value", largeInteger.c_str() ) == cvar::cvar_error_t::OK );
    REQUIRE( cvar::Cvar_GetInt( "value" ) == std::numeric_limits<cypher::engine::common::u32>::max() );
    REQUIRE( cvar::Cvar_GetBool( "value" ) );
    const std::string paddedOne = std::string( 100u, '0' ) + "1";
    REQUIRE( cvar::Cvar_Set( "value", paddedOne.c_str() ) == cvar::cvar_error_t::OK );
    REQUIRE( cvar::Cvar_GetInt( "value" ) == 1u );
    REQUIRE( cvar::Cvar_GetBool( "value" ) );
    REQUIRE( cvar::Cvar_Set( "value", "ON" ) == cvar::cvar_error_t::OK );
    REQUIRE( cvar::Cvar_GetBool( "value" ) );
    REQUIRE( cvar::Cvar_Set( "value", "false" ) == cvar::cvar_error_t::OK );
    REQUIRE_FALSE( cvar::Cvar_GetBool( "value" ) );
}
