//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Workspace_Tests.cpp
//  Purpose: Contract tests for workspaces and the settings scope stack.
//  Details: Workspace identity is strict, optional members are tolerant,
//           new workspaces get fresh identities, and settings resolve
//           workspace first, then project, then user.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Workspace.h"

#include "CypherCommon/Tier2/CypherCommon_Settings.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

key_value_document_t *Parse( const char *pText )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_ParseText( StringView_FromCString( pText ), {}, pDocument ).status == key_value_parse_status_t::OK );
    return pDocument;
}

bool_t Equals( string_view_t view, const char *pText )
{
    return StringView_Equals( view, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Workspaces decode identity strictly and optional members tolerantly",
           "[CypherEditor][Core][Workspace]" )
{
    key_value_document_t *pDocument = Parse( R"cykv(@cykv 1
@schema "cypher.workspace" 1
{
    id = "7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77"
    name = "main"
    owner = 42
    project = "../reap/reap.cyproject"
    content = "../outside"
    settings = { editor = { autosave_minutes = 5 } }
    state = { open_maps = [ "maps/facility.cymap" ] }
    sharing = { visibility = "team" }
}
)cykv" );
    workspace_view_t workspace{};
    u32 problems = 0u;
    REQUIRE( EditorWorkspace_Decode( pDocument, &workspace, &problems ) == workspace_status_t::OK );
    REQUIRE( Equals( workspace.name, "main" ) );
    REQUIRE( Equals( workspace.project, "../reap/reap.cyproject" ) );
    REQUIRE( workspace.owner.cchLength == 0u );
    REQUIRE( workspace.content.cchLength == 0u );
    REQUIRE( problems == ( WORKSPACE_PROBLEM_OWNER | WORKSPACE_PROBLEM_CONTENT ) );
    REQUIRE( workspace.pSettings != nullptr );
    REQUIRE( workspace.pState != nullptr );
    KeyValue_DestroyDocument( pDocument );

    struct case_t {
        const char *pBody;
        workspace_status_t expected;
    };
    const case_t cases[]{
        { "{ name = \"a\" project = \"p.cyproject\" }", workspace_status_t::INVALID_ID },
        { "{ id = \"00000000-0000-0000-0000-000000000000\" name = \"a\" project = \"p\" }", workspace_status_t::INVALID_ID },
        { "{ id = \"7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77\" project = \"p\" }", workspace_status_t::INVALID_NAME },
        { "{ id = \"7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77\" name = \"a\" }", workspace_status_t::INVALID_PROJECT },
    };
    for ( const case_t &c : cases ) {
        const std::string source = std::string( "@cykv 1\n@schema \"cypher.workspace\" 1\n" ) + c.pBody;
        key_value_document_t *pBad = Parse( source.c_str() );
        workspace_view_t view{};
        CAPTURE( c.pBody );
        REQUIRE( EditorWorkspace_Decode( pBad, &view, nullptr ) == c.expected );
        KeyValue_DestroyDocument( pBad );
    }
}

TEST_CASE( "New workspaces get a fresh identity and decode back",
           "[CypherEditor][Core][Workspace]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorWorkspace_Identity() ) == settings_document_status_t::OK );
    REQUIRE( EditorWorkspace_Create( &store, StringView_FromCString( "main" ), StringView_FromCString( "Karlo" ),
                                     StringView_FromCString( "../reap/reap.cyproject" ), StringView_FromCString( "wip/maps" ) ) ==
             workspace_status_t::OK );
    workspace_view_t workspace{};
    u32 problems = 1u;
    REQUIRE( EditorWorkspace_Decode( store.pDocument, &workspace, &problems ) == workspace_status_t::OK );
    REQUIRE( problems == WORKSPACE_PROBLEM_NONE );
    REQUIRE( UniqueId_IsValid( workspace.id ) );
    REQUIRE( Equals( workspace.owner, "Karlo" ) );
    REQUIRE( Equals( workspace.content, "wip/maps" ) );

    // Creating into a non-empty store is refused.
    REQUIRE( EditorWorkspace_Create( &store, StringView_FromCString( "again" ), {}, StringView_FromCString( "p" ), {} ) ==
             workspace_status_t::INVALID_ARGUMENT );

    for ( const char *pBad : { "/abs", "C:/abs", "a/../b", "a//b", ".." } ) {
        CAPTURE( pBad );
        REQUIRE_FALSE( EditorWorkspace_IsContentPath( StringView_FromCString( pBad ) ) );
    }
    REQUIRE( EditorWorkspace_IsContentPath( StringView_FromCString( "wip" ) ) );
}

TEST_CASE( "Settings resolve workspace first, then project, then user",
           "[CypherEditor][Core][Workspace]" )
{
    key_value_document_t *pWorkspaceDoc = Parse(
        "@cykv 1\n@schema \"cypher.workspace\" 1\n"
        "{ id = \"7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77\" name = \"w\" project = \"p\" settings = { editor = { grid = 4 } } }" );
    key_value_document_t *pProjectDoc = Parse(
        "@cykv 1\n@schema \"cypher.project\" 2\n"
        "{ id = \"reap\" name = \"REAP\" settings = { editor = { grid = 8 snap = false } } }" );
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), CypherSettings_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &user, StringView_FromCString(
                 "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = 16 snap = true autosave = true } }" ) ).status ==
             settings_document_status_t::OK );

    workspace_view_t workspace{};
    project_manifest_view_t project{};
    REQUIRE( EditorWorkspace_Decode( pWorkspaceDoc, &workspace, nullptr ) == workspace_status_t::OK );
    REQUIRE( ProjectManifest_DecodeSucceeded( ProjectManifest_Decode( pProjectDoc, {}, nullptr, 0u, &project ) ) );
    const editor_scopes_t scopes = EditorScopes_Make( &workspace, &project, &user );

    setting_descriptor_t grid{};
    grid.pPath = "editor.grid";
    grid.type = setting_type_t::INTEGER;
    grid.nDefault = 32;
    grid.nMin = 1;
    grid.nMax = 1024;
    setting_descriptor_t snap{};
    snap.pPath = "editor.snap";
    snap.type = setting_type_t::BOOL;
    setting_descriptor_t autosave = snap;
    autosave.pPath = "editor.autosave";
    setting_descriptor_t fov{};
    fov.pPath = "editor.fov";
    fov.type = setting_type_t::REAL;
    fov.flDefault = 90.0;

    setting_resolution_t resolved = Setting_Resolve( scopes.roots, EDITOR_SCOPE_COUNT, grid, nullptr, 0u );
    REQUIRE( resolved.value.nValue == 4 );
    REQUIRE( resolved.iScope == EDITOR_SCOPE_WORKSPACE );
    resolved = Setting_Resolve( scopes.roots, EDITOR_SCOPE_COUNT, snap, nullptr, 0u );
    REQUIRE( resolved.value.bValue == CY_FALSE );
    REQUIRE( resolved.iScope == EDITOR_SCOPE_PROJECT );
    resolved = Setting_Resolve( scopes.roots, EDITOR_SCOPE_COUNT, autosave, nullptr, 0u );
    REQUIRE( resolved.iScope == EDITOR_SCOPE_USER );
    resolved = Setting_Resolve( scopes.roots, EDITOR_SCOPE_COUNT, fov, nullptr, 0u );
    REQUIRE( resolved.value.flValue == 90.0 );
    REQUIRE( std::string( EditorScope_Name( resolved.iScope ) ) == "Default" );

    KeyValue_DestroyDocument( pWorkspaceDoc );
    KeyValue_DestroyDocument( pProjectDoc );
}
