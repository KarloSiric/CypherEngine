//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsDialog_Tests.cpp
//  Purpose: Contract tests for the generated settings dialog: pages,
//           search, typed editors writing to the chosen scope, sources, and
//           resets.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_SettingsDialog.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QSpinBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPointer>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

struct fixture_t {
    settings_registry_t registry{};
    settings_document_t user{};
    settings_document_t project{};
    QDialog *pDialog{ nullptr };

    fixture_t()
    {
        REQUIRE( EditorSettings_Init( &registry, Allocator_GetSystem() ) == settings_registry_status_t::OK );
        usize nFramework = 0u;
        const setting_descriptor_t *pFramework = EditorSettings_FrameworkCatalogue( &nFramework );
        REQUIRE( EditorSettings_Register( &registry, pFramework, nFramework ) == settings_registry_status_t::OK );
        const settings_document_identity_t identity{ StringView_FromCString( "cypher.settings" ), 1u, 2u };
        REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), identity ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Init( &project, Allocator_GetSystem(), identity ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &project, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { size = 8 } } }\n" ) )
                     .status == settings_document_status_t::OK );
        EditorSettings_SetScope( &registry, settings_scope_t::USER, &user );
        EditorSettings_SetScope( &registry, settings_scope_t::PROJECT, &project );
        pDialog = EditorSettingsDialog_Create( nullptr, &registry );
    }
    ~fixture_t()
    {
        delete pDialog;
        EditorSettings_Shutdown( &registry );
    }
};

} // namespace

TEST_CASE( "The settings dialog lists a page's settings and searches across pages", "[editor][gui][settings]" )
{
    fixture_t f;
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    const QStringList grid = EditorSettingsDialog_VisibleSettings( f.pDialog );
    CHECK( grid.size() == 10 );
    CHECK( grid.front() == QStringLiteral( "editor.grid.size" ) );
    CHECK_FALSE( grid.contains( QStringLiteral( "editor.grid.bands" ) ) );

    EditorSettingsDialog_SetSearch( f.pDialog, QStringLiteral( "sensitivity" ) );
    const QStringList found = EditorSettingsDialog_VisibleSettings( f.pDialog );
    CHECK( found.contains( QStringLiteral( "editor.camera.look_sensitivity" ) ) );
    CHECK( found.contains( QStringLiteral( "editor.camera.zoom_sensitivity" ) ) );
    CHECK_FALSE( found.contains( QStringLiteral( "editor.grid.size" ) ) );
    EditorSettingsDialog_SetSearch( f.pDialog, QStringLiteral( "qqqqzzzz" ) );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).isEmpty() );
}

TEST_CASE( "Editors write into the chosen scope and rows show where values come from", "[editor][gui][settings]" )
{
    fixture_t f;
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    CHECK( EditorSettingsDialog_SourceFor( f.pDialog, QStringLiteral( "editor.grid.size" ) ) == QStringLiteral( "Project" ) );
    CHECK( EditorSettingsDialog_SourceFor( f.pDialog, QStringLiteral( "editor.grid.snap" ) ) == QStringLiteral( "Default" ) );
    auto *pSize = qobject_cast<QSpinBox *>( EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.grid.size" ) ) );
    REQUIRE( pSize != nullptr );
    CHECK( pSize->value() == 8 );

    // Writing to the project scope changes the effective value.
    EditorSettingsDialog_SetTargetScope( f.pDialog, settings_scope_t::PROJECT );
    pSize->setValue( 32 );
    CHECK( EditorSettings_Integer( &f.registry, "editor.grid.size", 0 ) == 32 );

    // A checkbox writes to the user scope; the row then says User.
    EditorSettingsDialog_SetTargetScope( f.pDialog, settings_scope_t::USER );
    auto *pSnap = qobject_cast<QCheckBox *>( EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.grid.snap" ) ) );
    REQUIRE( pSnap != nullptr );
    pSnap->setChecked( false );
    CHECK_FALSE( EditorSettings_Bool( &f.registry, "editor.grid.snap", CY_TRUE ) );
    CHECK( EditorSettingsDialog_SourceFor( f.pDialog, QStringLiteral( "editor.grid.snap" ) ) == QStringLiteral( "User" ) );

    // Reset removes the user value; the default shows again.
    EditorSettingsDialog_Reset( f.pDialog, QStringLiteral( "editor.grid.snap" ) );
    CHECK( EditorSettings_Bool( &f.registry, "editor.grid.snap", CY_FALSE ) );
    CHECK( EditorSettingsDialog_SourceFor( f.pDialog, QStringLiteral( "editor.grid.snap" ) ) == QStringLiteral( "Default" ) );
    CHECK( pSnap->isChecked() );

    // Enums use combo boxes with the listed values.
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports" ) );
    auto *pAntialiasing = qobject_cast<QComboBox *>( EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.viewport.antialiasing" ) ) );
    REQUIRE( pAntialiasing != nullptr );
    CHECK( pAntialiasing->count() == 4 );
    CHECK( pAntialiasing->currentText() == QStringLiteral( "4x" ) );
    pAntialiasing->setCurrentText( QStringLiteral( "8x" ) );
    CHECK( std::string( EditorSettings_Text( &f.registry, "editor.viewport.antialiasing", {} ).pData, 2u ) == "8x" );
}

TEST_CASE( "Settings overrides filter follows scopes without replacing an editor on ordinary edits", "[editor][gui][settings][settings-navigation]" )
{
    fixture_t f;
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    auto *filter = f.pDialog->findChild<QCheckBox *>( QStringLiteral( "SettingsOverridesOnly" ) );
    REQUIRE( filter != nullptr );
    filter->setChecked( true );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).isEmpty() );
    const auto *size = EditorSettings_Find( &f.registry, StringView_FromCString( "editor.grid.size" ) );
    REQUIRE( size != nullptr );
    setting_value_t value{}; value.type = setting_type_t::INTEGER; value.nValue = 64;
    REQUIRE( EditorSettings_Write( &f.registry, settings_scope_t::USER, *size, value ) == settings_registry_status_t::OK );
    QCoreApplication::processEvents();
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ) == QStringList{ QStringLiteral( "editor.grid.size" ) } );
    CHECK( EditorSettingsDialog_SourceFor( f.pDialog, QStringLiteral( "editor.grid.size" ) ) == QStringLiteral( "Project" ) );
    bool maskedNotice = false;
    for ( auto *notice : f.pDialog->findChildren<QLabel *>( QStringLiteral( "SettingsScopeNotice" ) ) ) {
        maskedNotice |= notice->text().contains( QStringLiteral( "Project overrides changes saved to User" ) );
    }
    CHECK( maskedNotice );
    // Even a masked override can be edited repeatedly without a widget rebuild.
    QPointer<QWidget> editor = EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.grid.size" ) );
    REQUIRE( !editor.isNull() );
    value.nValue = 128;
    REQUIRE( EditorSettings_Write( &f.registry, settings_scope_t::USER, *size, value ) == settings_registry_status_t::OK );
    QCoreApplication::processEvents();
    CHECK( !editor.isNull() );
    CHECK( EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.grid.size" ) ) == editor.data() );
    EditorSettingsDialog_SetTargetScope( f.pDialog, settings_scope_t::PROJECT );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ) == QStringList{ QStringLiteral( "editor.grid.size" ) } );
    editor = EditorSettingsDialog_EditorFor( f.pDialog, QStringLiteral( "editor.grid.size" ) );
    EditorSettingsDialog_Reset( f.pDialog, QStringLiteral( "editor.grid.size" ) );
    CHECK( EditorSettings_Integer( &f.registry, "editor.grid.size", 0 ) == 128 );
    QCoreApplication::processEvents();
    CHECK( editor.isNull() );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).isEmpty() );
    EditorSettingsDialog_SetTargetScope( f.pDialog, settings_scope_t::USER );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ) == QStringList{ QStringLiteral( "editor.grid.size" ) } );
}

TEST_CASE( "Settings filter exposes invalid overrides for reset and keeps custom pages usable", "[editor][gui][settings][settings-navigation]" )
{
    fixture_t f;
    REQUIRE( SettingsDocument_Load( &f.user, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { snap = \"invalid\" } } }\n" ) ).status == settings_document_status_t::OK );
    EditorSettings_SetScope( &f.registry, settings_scope_t::USER, &f.user );
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    auto *filter = f.pDialog->findChild<QCheckBox *>( QStringLiteral( "SettingsOverridesOnly" ) );
    REQUIRE( filter != nullptr );
    filter->setChecked( true );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ) == QStringList{ QStringLiteral( "editor.grid.snap" ) } );
    CHECK( EditorSettings_Bool( &f.registry, "editor.grid.snap", CY_FALSE ) );
    EditorSettingsDialog_Reset( f.pDialog, QStringLiteral( "editor.grid.snap" ) );
    QCoreApplication::processEvents();
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).isEmpty() );
    auto *custom = new QWidget();
    EditorSettingsDialog_AddPage( f.pDialog, QStringLiteral( "Profiles" ), custom, { QStringLiteral( "shortcuts" ) }, []( QWidget * ) { return false; } );
    CHECK( EditorSettingsDialog_CurrentPage( f.pDialog ) == QStringLiteral( "Profiles" ) );
    CHECK_FALSE( filter->isEnabled() );
    EditorSettingsDialog_SetSearch( f.pDialog, QStringLiteral( "shortcuts" ) );
    CHECK( EditorSettingsDialog_PageResults( f.pDialog ).contains( QStringLiteral( "Profiles" ) ) );
    f.pDialog->reject(); // The unsaved custom page blocks close and restores its navigation.
    CHECK( EditorSettingsDialog_CurrentPage( f.pDialog ) == QStringLiteral( "Profiles" ) );
    auto *title = f.pDialog->findChild<QLabel *>( QStringLiteral( "SettingsPageTitle" ) );
    REQUIRE( title != nullptr );
    CHECK( title->text() == QStringLiteral( "Profiles" ) );
}

TEST_CASE( "Settings category navigation and search include descendant pages", "[editor][gui][settings][settings-navigation]" )
{
    fixture_t f;
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports" ) );
    const auto parent = EditorSettingsDialog_VisibleSettings( f.pDialog );
    CHECK( parent.contains( QStringLiteral( "editor.camera.fov" ) ) );
    CHECK( parent.contains( QStringLiteral( "editor.grid.size" ) ) );
    CHECK( parent.contains( QStringLiteral( "editor.viewport.gizmo_scale" ) ) );
    CHECK_FALSE( parent.contains( QStringLiteral( "editor.general.language" ) ) );
    EditorSettingsDialog_SetSearch( f.pDialog, QStringLiteral( "Selection and Gizmos" ) );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).contains( QStringLiteral( "editor.viewport.gizmo_scale" ) ) );
    EditorSettingsDialog_ShowPage( f.pDialog, QStringLiteral( "Viewports/Camera" ) );
    CHECK( EditorSettingsDialog_VisibleSettings( f.pDialog ).contains( QStringLiteral( "editor.camera.frame_margin" ) ) );
}
