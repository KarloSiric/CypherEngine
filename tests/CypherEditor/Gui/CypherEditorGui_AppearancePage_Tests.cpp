//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AppearancePage_Tests.cpp
//  Purpose: Contract tests for Settings > Appearance and the bundled preset
//           themes: choosing a theme applies and records it, colour and
//           size edits preview live as a modified theme, filters, revert,
//           save, delete, the close prompt's headless path, the settings
//           dialog's custom pages, and staying in step with the Theme
//           Editor.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AppearancePage.h"
#include "CypherEditorGui_SettingsDialog.h"
#include "CypherEditorGui_ThemeEditor.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QSpinBox>
#include <QTemporaryDir>

#include <memory>
#include <string>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

std::string ActiveId( const editor_gui_t &gui )
{
    const string_view_t id = EditorGui_ActiveThemeId( &gui );
    return std::string( id.pData, id.cchLength );
}

std::string ThemeSetting( const editor_gui_t &gui )
{
    const string_view_t id = EditorSettings_Text( &gui.settings, "editor.ui.theme", string_view_t{} );
    return std::string( id.pData != nullptr ? id.pData : "", id.cchLength );
}

struct fixture_t {
    QApplication *pApplication{ qobject_cast<QApplication *>( QCoreApplication::instance() ) };
    editor_gui_t gui{};
    settings_document_t user{};
    QTemporaryDir folder{};

    fixture_t()
    {
        REQUIRE( pApplication != nullptr );
        REQUIRE( folder.isValid() );
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &user );
    }
    ~fixture_t()
    {
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
        EditorGui_Shutdown( &gui );
        SettingsDocument_Shutdown( &user );
    }
};

} // namespace

TEST_CASE( "Preset themes ship with the editor and build on the default theme", "[editor][gui][appearance]" )
{
    fixture_t f;
    const QStringList ids = EditorGui_ThemeIds( &f.gui );
    for ( const char *pId : { "charcoal", "radiant_dark", "slate", "hammer_charcoal", "radiant_light", "midnight", "warm_workshop",
                              "blueprint_blue", "graphite", "high_contrast_dark", "coastal_dusk", "sandstone_light" } ) {
        CHECK( ids.contains( QString::fromLatin1( pId ) ) );
    }
    CHECK( ActiveId( f.gui ) == "charcoal" ); // Presets load; none is chosen.

    // A preset sets the base colours; everything else comes from Charcoal or
    // follows the formulas from the preset's colours.
    REQUIRE( EditorGui_SelectTheme( &f.gui, f.pApplication, StringView_FromCString( "slate" ) ) == editor_gui_status_t::OK );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x363636FFu );
    CHECK( f.gui.style.colors[STYLE_COLOR_VIEWPORT_2D] == 0x334B60FFu );
    CHECK( EditorStyle_TokenColor( f.gui.style, "viewport.entity.unknown" ) == QColor( 0xFF, 0x00, 0xFF ) ); // From Charcoal.
}

TEST_CASE( "Choosing a theme on the Appearance page applies and records it", "[editor][gui][appearance]" )
{
    fixture_t f;
    std::unique_ptr<QWidget> pPage( EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() ) );
    REQUIRE( pPage != nullptr );
    const QStringList themes = EditorAppearancePage_Themes( pPage.get() );
    REQUIRE( themes.size() == 12 );
    CHECK( themes.first() == QStringLiteral( "charcoal" ) ); // The default leads; presets follow by name.
    CHECK( EditorAppearancePage_CurrentTheme( pPage.get() ) == QStringLiteral( "charcoal" ) );

    EditorAppearancePage_SelectTheme( pPage.get(), QStringLiteral( "midnight" ) );
    CHECK( ActiveId( f.gui ) == "midnight" );
    CHECK( ThemeSetting( f.gui ) == "midnight" ); // The next session starts with it.
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x171B25FFu );
    CHECK( EditorAppearancePage_CurrentTheme( pPage.get() ) == QStringLiteral( "midnight" ) );
    CHECK_FALSE( EditorAppearancePage_HasChanges( pPage.get() ) );
}

TEST_CASE( "Appearance edits preview live as a modified theme and revert", "[editor][gui][appearance]" )
{
    fixture_t f;
    std::unique_ptr<QWidget> pPage( EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() ) );
    REQUIRE( pPage != nullptr );

    EditorAppearancePage_SetColor( pPage.get(), QStringLiteral( "ui.accent" ), QColor( 0xFF, 0x00, 0x00 ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_ACCENT] == 0xFF0000FFu );
    CHECK( EditorAppearancePage_HasChanges( pPage.get() ) );
    CHECK( EditorAppearancePage_CurrentTheme( pPage.get() ).isEmpty() ); // "Charcoal (modified)".
    CHECK( ThemeSetting( f.gui ) != "draft" );                             // A preview is never recorded.

    // Interface sizes are theme values too, previewed the same way.
    auto *pIcons = pPage->findChild<QSpinBox *>( QStringLiteral( "AppearanceIconSize" ) );
    REQUIRE( pIcons != nullptr );
    pIcons->setValue( 30 );
    CHECK( f.gui.style.iconSize == 30.0 );

    // One colour reverts on its own; the size change stays.
    EditorAppearancePage_RevertColor( pPage.get(), QStringLiteral( "ui.accent" ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_ACCENT] == 0xE59A2FFFu );
    CHECK( EditorAppearancePage_HasChanges( pPage.get() ) );

    EditorAppearancePage_Revert( pPage.get() );
    CHECK_FALSE( EditorAppearancePage_HasChanges( pPage.get() ) );
    CHECK( ActiveId( f.gui ) == "charcoal" );
    CHECK( f.gui.style.iconSize == 28.0 );
    CHECK( pIcons->value() == 28 );
}

TEST_CASE( "Appearance colour rows filter by name, token, and value", "[editor][gui][appearance]" )
{
    fixture_t f;
    std::unique_ptr<QWidget> pPage( EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() ) );
    REQUIRE( pPage != nullptr );
    const QStringList all = EditorAppearancePage_ColorTokens( pPage.get() );
    CHECK( all.contains( QStringLiteral( "ui.background" ) ) );
    CHECK( all.contains( QStringLiteral( "console.error" ) ) );

    EditorAppearancePage_SetFilter( pPage.get(), QStringLiteral( "grid" ) );
    const QStringList grid = EditorAppearancePage_ColorTokens( pPage.get() );
    CHECK( grid.contains( QStringLiteral( "viewport.grid.minor" ) ) );
    CHECK_FALSE( grid.contains( QStringLiteral( "ui.accent" ) ) );

    EditorAppearancePage_SetFilter( pPage.get(), QStringLiteral( "#e59a2f" ) );
    CHECK( EditorAppearancePage_ColorTokens( pPage.get() ).contains( QStringLiteral( "ui.accent" ) ) );

    // Base colours only: the dozen a palette is really made of.
    EditorAppearancePage_SetFilter( pPage.get(), QString() );
    auto *pDerived = pPage->findChild<QCheckBox *>( QStringLiteral( "AppearanceShowDerived" ) );
    REQUIRE( pDerived != nullptr );
    pDerived->setChecked( false );
    const QStringList base = EditorAppearancePage_ColorTokens( pPage.get() );
    CHECK( base.contains( QStringLiteral( "ui.background" ) ) );
    CHECK_FALSE( base.contains( QStringLiteral( "ui.border" ) ) );
    CHECK( base.size() < all.size() );
}

TEST_CASE( "Appearance saves user themes, deletes them, and discards on a headless close", "[editor][gui][appearance]" )
{
    fixture_t f;
    std::unique_ptr<QWidget> pPage( EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() ) );
    REQUIRE( pPage != nullptr );

    EditorAppearancePage_SetColor( pPage.get(), QStringLiteral( "ui.background" ), QColor( 0x12, 0x14, 0x16 ) );
    const QString path = EditorAppearancePage_SaveAs( pPage.get(), QStringLiteral( "my_dark" ), QStringLiteral( "My Dark" ) );
    REQUIRE_FALSE( path.isEmpty() );
    CHECK( QFile::exists( path ) );
    CHECK( ActiveId( f.gui ) == "my_dark" );
    CHECK( ThemeSetting( f.gui ) == "my_dark" );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x121416FFu );
    CHECK_FALSE( EditorAppearancePage_HasChanges( pPage.get() ) );
    CHECK( EditorAppearancePage_Themes( pPage.get() ).contains( QStringLiteral( "my_dark" ) ) );

    // The default theme cannot be saved over, and presets cannot be deleted.
    EditorAppearancePage_SetColor( pPage.get(), QStringLiteral( "ui.text" ), QColor( 0xEE, 0xEE, 0xEE ) );
    CHECK( EditorAppearancePage_SaveAs( pPage.get(), QStringLiteral( "charcoal" ), QStringLiteral( "Charcoal" ) ).isEmpty() );
    CHECK_FALSE( EditorAppearancePage_DeleteTheme( pPage.get(), QStringLiteral( "slate" ) ) );

    // Closing hidden discards the unsaved colour.
    CHECK( EditorAppearancePage_CanClose( pPage.get() ) );
    CHECK_FALSE( EditorAppearancePage_HasChanges( pPage.get() ) );
    CHECK( ActiveId( f.gui ) == "my_dark" );

    // Deleting the active theme falls back to the default and records it.
    CHECK( EditorAppearancePage_DeleteTheme( pPage.get(), QStringLiteral( "my_dark" ) ) );
    CHECK_FALSE( QFile::exists( path ) );
    CHECK( ActiveId( f.gui ) == "charcoal" );
    CHECK( ThemeSetting( f.gui ) == "charcoal" );
    CHECK_FALSE( EditorAppearancePage_Themes( pPage.get() ).contains( QStringLiteral( "my_dark" ) ) );
}

TEST_CASE( "The settings dialog hosts the Appearance page and finds it by keyword", "[editor][gui][appearance][settings]" )
{
    fixture_t f;
    std::unique_ptr<QDialog> pDialog( EditorSettingsDialog_Create( nullptr, &f.gui.settings ) );
    REQUIRE( pDialog != nullptr );
    QWidget *pPage = EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pPage != nullptr );
    EditorSettingsDialog_AddPage( pDialog.get(), QStringLiteral( "Appearance" ), pPage, EditorAppearancePage_Keywords(), &EditorAppearancePage_CanClose );
    CHECK( EditorSettingsDialog_CurrentPage( pDialog.get() ) == QStringLiteral( "Appearance" ) ); // The first custom page leads.
    CHECK( EditorSettingsDialog_VisibleSettings( pDialog.get() ).isEmpty() );

    // Search lists the page as a link beside the matching settings.
    EditorSettingsDialog_SetSearch( pDialog.get(), QStringLiteral( "theme" ) );
    CHECK( EditorSettingsDialog_PageResults( pDialog.get() ).contains( QStringLiteral( "Appearance" ) ) );
    CHECK( EditorSettingsDialog_VisibleSettings( pDialog.get() ).contains( QStringLiteral( "editor.ui.theme" ) ) );

    // Generated pages still work, and back again.
    EditorSettingsDialog_ShowPage( pDialog.get(), QStringLiteral( "Viewports/Camera" ) );
    CHECK( EditorSettingsDialog_VisibleSettings( pDialog.get() ).contains( QStringLiteral( "editor.camera.fov" ) ) );
    EditorSettingsDialog_ShowPage( pDialog.get(), QStringLiteral( "Appearance" ) );
    CHECK( EditorSettingsDialog_VisibleSettings( pDialog.get() ).isEmpty() );

    // Closing asks the page; hidden, it discards and lets the dialog close.
    EditorAppearancePage_SetColor( pPage, QStringLiteral( "ui.accent" ), QColor( 0x00, 0xFF, 0x00 ) );
    pDialog->reject();
    CHECK_FALSE( EditorAppearancePage_HasChanges( pPage ) );
    CHECK( ActiveId( f.gui ) == "charcoal" );
}

TEST_CASE( "The Appearance page follows themes the Theme Editor saves", "[editor][gui][appearance]" )
{
    fixture_t f;
    std::unique_ptr<QWidget> pPage( EditorAppearancePage_Create( nullptr, &f.gui, f.pApplication, f.folder.path() ) );
    REQUIRE( pPage != nullptr );
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );

    // The editor's preview is its own working copy; the page ignores it.
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.background" ), QColor( 0x40, 0x30, 0x20 ) );
    CHECK( EditorAppearancePage_CurrentTheme( pPage.get() ) == QStringLiteral( "charcoal" ) );

    REQUIRE_FALSE( EditorThemeEditor_Save( pEditor, QStringLiteral( "umber" ), QStringLiteral( "Umber" ) ).isEmpty() );
    CHECK( EditorAppearancePage_CurrentTheme( pPage.get() ) == QStringLiteral( "umber" ) );
    CHECK( EditorAppearancePage_Themes( pPage.get() ).contains( QStringLiteral( "umber" ) ) );
    delete pEditor;
    CHECK( ActiveId( f.gui ) == "umber" ); // Saved themes stay when the editor closes.
}
