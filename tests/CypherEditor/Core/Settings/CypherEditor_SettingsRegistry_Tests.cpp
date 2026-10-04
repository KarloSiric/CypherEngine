//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsRegistry_Tests.cpp
//  Purpose: Contract tests for the settings registry: registration, scope
//           resolution, sparse writes, resets, and change notification.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_SettingsRegistry.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

struct store_t {
    settings_document_t store{};
    explicit store_t( const char *pText = "@cykv 1\n@schema \"cypher.settings\" 2\n{}\n" )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), { StringView_FromCString( "cypher.settings" ), 1u, 2u } ) ==
                 settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( pText ) ).status == settings_document_status_t::OK );
    }
};

struct registry_t {
    settings_registry_t registry{};
    registry_t()
    {
        REQUIRE( EditorSettings_Init( &registry, Allocator_GetSystem() ) == settings_registry_status_t::OK );
        usize nFramework = 0u;
        const setting_descriptor_t *pFramework = EditorSettings_FrameworkCatalogue( &nFramework );
        REQUIRE( EditorSettings_Register( &registry, pFramework, nFramework ) == settings_registry_status_t::OK );
    }
    const setting_descriptor_t &Find( const char *pPath ) const
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( &registry, StringView_FromCString( pPath ) );
        REQUIRE( pDescriptor != nullptr );
        return *pDescriptor;
    }
};

void Count( void *pContext, string_view_t ) noexcept
{
    ++*static_cast<int *>( pContext );
}

setting_value_t IntegerValue( i64 value )
{
    setting_value_t v{};
    v.type = setting_type_t::INTEGER;
    v.nValue = value;
    return v;
}

} // namespace

TEST_CASE( "The framework catalogue registers once with valid defaults", "[editor][core][settings]" )
{
    registry_t r;
    usize nFramework = 0u;
    const setting_descriptor_t *pFramework = EditorSettings_FrameworkCatalogue( &nFramework );
    CHECK( EditorSettings_Count( &r.registry ) == nFramework );
    CHECK( nFramework == 99u ); // Includes palette rows/filter and working gizmo/framing preferences.
    CHECK( EditorSettings_Register( &r.registry, pFramework, 1u ) == settings_registry_status_t::DUPLICATE );
    // With no scopes everything is its default.
    CHECK( EditorSettings_Integer( &r.registry, "editor.grid.size", 0 ) == 16 );
    CHECK( EditorSettings_Real( &r.registry, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    CHECK( EditorSettings_Bool( &r.registry, "editor.grid.show", CY_FALSE ) );
    CHECK( EditorSettings_Bool( &r.registry, "editor.grid.show_surface_3d", CY_FALSE ) );
    CHECK( EditorSettings_Bool( &r.registry, "editor.viewport.perspective.show_axes", CY_FALSE ) );
    CHECK( EditorSettings_Bool( &r.registry, "editor.viewport.activate_on_hover", CY_FALSE ) );
    CHECK( std::string( EditorSettings_Text( &r.registry, "editor.ui.theme", {} ).pData, 8u ) == "charcoal" );
    CHECK( EditorSettings_Integer( &r.registry, "no.such.setting", 42 ) == 42 );
    CHECK( EditorSettings_Integer( &r.registry, "editor.grid.show", 7 ) == 7 ); // Wrong type: fallback.

    setting_descriptor_t bad{};
    bad.pPath = "test.bad";
    bad.type = setting_type_t::INTEGER;
    bad.nDefault = 99;
    bad.nMax = 10; // Default outside its own limits.
    bad.pLabel = "Bad";
    bad.pPage = "Test";
    CHECK( EditorSettings_Register( &r.registry, &bad, 1u ) == settings_registry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Values resolve through workspace, project, and user scopes", "[editor][core][settings]" )
{
    registry_t r;
    store_t user( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { size = 32 } camera = { fov = 90.0 } } }\n" );
    store_t project( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { size = 8 } camera = { fov = 999.0 } } }\n" );
    EditorSettings_SetScope( &r.registry, settings_scope_t::USER, &user.store );
    EditorSettings_SetScope( &r.registry, settings_scope_t::PROJECT, &project.store );

    const settings_value_source_t grid = EditorSettings_Resolve( &r.registry, r.Find( "editor.grid.size" ) );
    CHECK( grid.value.nValue == 8 );
    CHECK( grid.source == settings_scope_t::PROJECT );
    // An invalid project value is passed over to the user scope.
    const settings_value_source_t fov = EditorSettings_Resolve( &r.registry, r.Find( "editor.camera.fov" ) );
    CHECK( fov.value.flValue == 90.0 );
    CHECK( fov.source == settings_scope_t::USER );
    CHECK( fov.nInvalid == 1u );
    // What the project would inherit without its own value.
    CHECK( EditorSettings_ResolveInherited( &r.registry, r.Find( "editor.grid.size" ), settings_scope_t::PROJECT ).value.nValue == 32 );
    CHECK( EditorSettings_Resolve( &r.registry, r.Find( "editor.grid.snap" ) ).source == settings_scope_t::DEFAULT );
}

TEST_CASE( "Surface grid preference persists independently of floor and orthographic grids", "[editor][core][settings][surface-grid]" )
{
    registry_t r;
    store_t user;
    EditorSettings_SetScope( &r.registry, settings_scope_t::USER, &user.store );
    setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = CY_FALSE;
    REQUIRE( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.show_surface_3d" ), value ) == settings_registry_status_t::OK );
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &user.store, &text ) == settings_document_status_t::OK );
    store_t restored;
    REQUIRE( SettingsDocument_Load( &restored.store, { TextBuffer_Data( &text ), TextBuffer_Length( &text ) } ).status == settings_document_status_t::OK );
    TextBuffer_Shutdown( &text );
    registry_t again;
    EditorSettings_SetScope( &again.registry, settings_scope_t::USER, &restored.store );
    CHECK_FALSE( EditorSettings_Bool( &again.registry, "editor.grid.show_surface_3d", CY_TRUE ) );
    CHECK( EditorSettings_Bool( &again.registry, "editor.grid.show_3d", CY_FALSE ) );
    CHECK( EditorSettings_Bool( &again.registry, "editor.grid.show", CY_FALSE ) );
    REQUIRE( EditorSettings_Reset( &again.registry, settings_scope_t::USER, again.Find( "editor.grid.show_surface_3d" ) ) == settings_registry_status_t::OK );
    CHECK( EditorSettings_Bool( &again.registry, "editor.grid.show_surface_3d", CY_FALSE ) );
}

TEST_CASE( "Writes stay sparse, resets inherit, and listeners hear both", "[editor][core][settings]" )
{
    registry_t r;
    store_t user;
    int nChanged = 0;
    REQUIRE( EditorSettings_AddListener( &r.registry, &Count, &nChanged ) );
    CHECK( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ), IntegerValue( 64 ) ) ==
           settings_registry_status_t::NO_SCOPE );
    EditorSettings_SetScope( &r.registry, settings_scope_t::USER, &user.store );
    CHECK( nChanged == 1 ); // Attaching a scope changes everything.

    REQUIRE( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ), IntegerValue( 64 ) ) ==
             settings_registry_status_t::OK );
    CHECK( EditorSettings_Integer( &r.registry, "editor.grid.size", 0 ) == 64 );
    CHECK( nChanged == 2 );
    // Writing the inherited value removes the scope's own entry.
    REQUIRE( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ), IntegerValue( 16 ) ) ==
             settings_registry_status_t::OK );
    CHECK( EditorSettings_Resolve( &r.registry, r.Find( "editor.grid.size" ) ).source == settings_scope_t::DEFAULT );
    // Out-of-range values are refused.
    CHECK( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ), IntegerValue( 99999 ) ) ==
           settings_registry_status_t::INVALID_ARGUMENT );

    REQUIRE( EditorSettings_Write( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ), IntegerValue( 4 ) ) ==
             settings_registry_status_t::OK );
    REQUIRE( EditorSettings_Reset( &r.registry, settings_scope_t::USER, r.Find( "editor.grid.size" ) ) == settings_registry_status_t::OK );
    CHECK( EditorSettings_Integer( &r.registry, "editor.grid.size", 0 ) == 16 );
    CHECK( nChanged == 5 );
    EditorSettings_RemoveListener( &r.registry, &Count, &nChanged );
    CHECK( std::string( EditorSettings_ScopeName( settings_scope_t::PROJECT ) ) == "Project" );
}

TEST_CASE( "Settings notification skips a listener removed by an earlier callback", "[editor][core][settings][lifetime]" )
{
    registry_t r;
    store_t user;
    int removedCalls = 0;
    struct removal_t { settings_registry_t *registry; int *calls; };
    removal_t context{ &r.registry, &removedCalls };
    const auto remove = []( void *p, string_view_t ) noexcept {
        auto *c = static_cast<removal_t *>( p );
        EditorSettings_RemoveListener( c->registry, &Count, c->calls );
    };
    REQUIRE( EditorSettings_AddListener( &r.registry, remove, &context ) );
    REQUIRE( EditorSettings_AddListener( &r.registry, &Count, &removedCalls ) );
    EditorSettings_SetScope( &r.registry, settings_scope_t::USER, &user.store );
    CHECK( removedCalls == 0 );
    CHECK( r.registry.nListeners == 1 );
}
