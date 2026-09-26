//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsFile_Tests.cpp
//  Purpose: Contract tests for the settings-file policy.
//  Details: Exercises real files in a temporary directory: saves keep one
//           backup, a damaged file is restored from its backup and kept as
//           .broken, and without a backup saving stays blocked until the
//           user accepts the overwrite.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_SettingsFile.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

constexpr settings_document_identity_t kIdentity{ { "cypher.settings", 15u }, 1u, 2u };

struct temp_dir_t {
    std::filesystem::path path;
    temp_dir_t()
    {
        std::random_device device;
        path = std::filesystem::temp_directory_path() / ( "cypher_settings_file_" + std::to_string( device() ) );
        std::filesystem::create_directories( path );
    }
    ~temp_dir_t() { std::error_code ignored; std::filesystem::remove_all( path, ignored ); }
};

std::string Read( const std::filesystem::path &path )
{
    std::ifstream input( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
}

void Write( const std::filesystem::path &path, const std::string &text )
{
    std::ofstream output( path, std::ios::binary | std::ios::trunc );
    output << text;
}

setting_descriptor_t GridSetting()
{
    setting_descriptor_t descriptor{};
    descriptor.pPath = "editor.grid";
    descriptor.type = setting_type_t::INTEGER;
    descriptor.nDefault = 16;
    descriptor.nMin = 1;
    descriptor.nMax = 4096;
    return descriptor;
}

void SetGrid( settings_file_t &file, i64 nValue )
{
    setting_value_t value{};
    value.type = setting_type_t::INTEGER;
    value.nValue = nValue;
    REQUIRE( Setting_Write( &file.store, GridSetting(), value ) == settings_document_status_t::OK );
}

i64 Grid( const settings_file_t &file )
{
    setting_value_t value{};
    REQUIRE( Setting_Read( SettingsDocument_Root( &file.store ), GridSetting(), &value, nullptr ) == setting_read_status_t::VALUE );
    return value.nValue;
}

} // namespace

TEST_CASE( "Settings files are created, saved with a backup, and reloaded",
           "[CypherEditor][Core][SettingsFile]" )
{
    temp_dir_t dir;
    const std::string path = ( dir.path / "editor.cysettings" ).string();
    settings_file_t file{};
    REQUIRE( EditorSettingsFile_Init( &file, Allocator_GetSystem(), kIdentity, StringView_FromCString( path.c_str() ) ) ==
             settings_file_status_t::OK );
    REQUIRE( EditorSettingsFile_Load( &file ) == settings_file_status_t::MISSING );

    SetGrid( file, 8 );
    REQUIRE( EditorSettingsFile_Save( &file ) == settings_file_status_t::OK );
    REQUIRE( Read( path ).starts_with( "@cykv 1\n@schema \"cypher.settings\" 2\n" ) );
    REQUIRE_FALSE( std::filesystem::exists( path + ".bak" ) ); // Nothing to back up yet.
    REQUIRE_FALSE( std::filesystem::exists( path + ".tmp" ) );

    SetGrid( file, 32 );
    REQUIRE( EditorSettingsFile_Save( &file ) == settings_file_status_t::OK );
    REQUIRE( Read( path + ".bak" ).find( "8" ) != std::string::npos );

    settings_file_t again{};
    REQUIRE( EditorSettingsFile_Init( &again, Allocator_GetSystem(), kIdentity, StringView_FromCString( path.c_str() ) ) ==
             settings_file_status_t::OK );
    REQUIRE( EditorSettingsFile_Load( &again ) == settings_file_status_t::OK );
    REQUIRE( Grid( again ) == 32 );
}

TEST_CASE( "A damaged settings file is restored from its backup and kept as .broken",
           "[CypherEditor][Core][SettingsFile]" )
{
    temp_dir_t dir;
    const std::string path = ( dir.path / "editor.cysettings" ).string();
    Write( path + ".bak", "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = 12 } }\n" );
    const std::string damaged = "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = ";
    Write( path, damaged );

    settings_file_t file{};
    REQUIRE( EditorSettingsFile_Init( &file, Allocator_GetSystem(), kIdentity, StringView_FromCString( path.c_str() ) ) ==
             settings_file_status_t::OK );
    REQUIRE( EditorSettingsFile_Load( &file ) == settings_file_status_t::RESTORED_FROM_BACKUP );
    REQUIRE( file.damage.status == settings_document_status_t::PARSE_FAILED );
    REQUIRE( Grid( file ) == 12 );
    REQUIRE( Read( path + ".broken" ) == damaged );
    REQUIRE( EditorSettingsFile_Save( &file ) == settings_file_status_t::OK );
}

TEST_CASE( "Without a backup an unreadable file blocks saving until the user accepts",
           "[CypherEditor][Core][SettingsFile]" )
{
    temp_dir_t dir;
    const std::string path = ( dir.path / "editor.cysettings" ).string();
    const std::string foreign = "@cykv 1\n@schema \"cypher.project\" 1\n{ id = \"x\" }\n";
    Write( path, foreign );

    settings_file_t file{};
    REQUIRE( EditorSettingsFile_Init( &file, Allocator_GetSystem(), kIdentity, StringView_FromCString( path.c_str() ) ) ==
             settings_file_status_t::OK );
    REQUIRE( EditorSettingsFile_Load( &file ) == settings_file_status_t::UNREADABLE );
    REQUIRE( file.damage.status == settings_document_status_t::SCHEMA_MISMATCH );
    REQUIRE( KeyValue_ChildCount( SettingsDocument_Root( &file.store ) ) == 0u );

    SetGrid( file, 64 );
    REQUIRE( EditorSettingsFile_Save( &file ) == settings_file_status_t::SAVE_BLOCKED );
    REQUIRE( Read( path ) == foreign ); // Never overwritten behind the user's back.

    REQUIRE( EditorSettingsFile_AcceptOverwrite( &file ) == settings_file_status_t::OK );
    REQUIRE( Read( path + ".broken" ) == foreign );
    REQUIRE( EditorSettingsFile_Save( &file ) == settings_file_status_t::OK );
    REQUIRE( Read( path ).find( "64" ) != std::string::npos );
}
