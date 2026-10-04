//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ThemeEditor_Tests.cpp
//  Purpose: Contract tests for the theme editor: live preview, derived
//           colours pinned and set back to their formula, revert, saving a
//           complete theme, and restoring the previous theme on close.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_ThemeEditor.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QTemporaryDir>

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

TEST_CASE( "The theme editor lists and searches every token", "[editor][gui][theme_editor]" )
{
    fixture_t f;
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );
    CHECK( static_cast<usize>( EditorThemeEditor_VisibleTokens( pEditor ).size() ) == f.gui.themeTokens.tokens.nCount );
    CHECK_FALSE( EditorThemeEditor_HasChanges( pEditor ) );
    CHECK( ActiveId( f.gui ) == "charcoal" ); // Opening previews nothing yet.

    EditorThemeEditor_SetSearch( pEditor, QStringLiteral( "border" ) );
    const QStringList border = EditorThemeEditor_VisibleTokens( pEditor );
    CHECK( border.contains( QStringLiteral( "ui.border" ) ) );
    CHECK_FALSE( border.contains( QStringLiteral( "ui.accent" ) ) );

    // A colour value finds where it is used.
    EditorThemeEditor_SetSearch( pEditor, QStringLiteral( "#3c3c3c" ) );
    CHECK( EditorThemeEditor_VisibleTokens( pEditor ).contains( QStringLiteral( "ui.background" ) ) );
    delete pEditor;
}

TEST_CASE( "Theme edits preview live, derived colours pin and follow their formula", "[editor][gui][theme_editor]" )
{
    fixture_t f;
    const QColor originalBorder = EditorStyle_TokenColor( f.gui.style, "ui.border" );
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );

    // A base colour changes, and ui.border (a mix of it) follows at once.
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.background" ), QColor( 0x10, 0x10, 0x10 ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x101010FFu );
    CHECK( EditorStyle_TokenColor( f.gui.style, "ui.border" ) != originalBorder );
    CHECK( EditorThemeEditor_HasChanges( pEditor ) );
    CHECK( ActiveId( f.gui ) == "draft" );

    // Pinned, the border no longer moves with the background.
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.border" ), QColor( 0xFF, 0x00, 0x00 ) );
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.background" ), QColor( 0x20, 0x20, 0x20 ) );
    CHECK( EditorStyle_TokenColor( f.gui.style, "ui.border" ) == QColor( 0xFF, 0x00, 0x00 ) );

    // Following the formula again, it does.
    EditorThemeEditor_SetAuto( pEditor, QStringLiteral( "ui.border" ) );
    const QColor followed = EditorStyle_TokenColor( f.gui.style, "ui.border" );
    CHECK( followed != QColor( 0xFF, 0x00, 0x00 ) );
    CHECK( followed.red() < originalBorder.red() ); // Darker background, darker mix.

    // A base colour has no formula to follow.
    EditorThemeEditor_SetAuto( pEditor, QStringLiteral( "ui.background" ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x202020FFu );

    // Revert All returns to the opening theme, and the real theme is active.
    EditorThemeEditor_Revert( pEditor );
    CHECK_FALSE( EditorThemeEditor_HasChanges( pEditor ) );
    CHECK( ActiveId( f.gui ) == "charcoal" );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x3C3C3CFFu );
    CHECK( EditorStyle_TokenColor( f.gui.style, "ui.border" ) == originalBorder );
    delete pEditor;
}

TEST_CASE( "Closing the theme editor without saving restores the previous theme", "[editor][gui][theme_editor]" )
{
    fixture_t f;
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.accent" ), QColor( 0x00, 0xFF, 0x00 ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_ACCENT] == 0x00FF00FFu );
    pEditor->reject();
    CHECK( ActiveId( f.gui ) == "charcoal" );
    CHECK( f.gui.style.colors[STYLE_COLOR_ACCENT] == 0xE59A2FFFu );

    // Destroyed while previewing: the style must not keep the draft.
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.accent" ), QColor( 0x00, 0xFF, 0x00 ) );
    delete pEditor;
    CHECK( ActiveId( f.gui ) == "charcoal" );
    CHECK( f.gui.style.colors[STYLE_COLOR_ACCENT] == 0xE59A2FFFu );
}

TEST_CASE( "Saving writes a complete theme, loads it, and makes it the active setting", "[editor][gui][theme_editor]" )
{
    fixture_t f;
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.background" ), QColor( 0xF0, 0xF0, 0xF0 ) );
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.header" ), QColor( 0x12, 0x34, 0x56 ) );

    // The built-in theme and bad IDs are refused; nothing is written.
    CHECK( EditorThemeEditor_Save( pEditor, QStringLiteral( "charcoal" ), QStringLiteral( "Charcoal" ) ).isEmpty() );
    CHECK( EditorThemeEditor_Save( pEditor, QStringLiteral( "Bad ID" ), QStringLiteral( "Bad" ) ).isEmpty() );
    CHECK( EditorThemeEditor_Save( pEditor, QStringLiteral( "paper" ), QString() ).isEmpty() );
    CHECK( ActiveId( f.gui ) == "draft" );

    const QString path = EditorThemeEditor_Save( pEditor, QStringLiteral( "paper" ), QStringLiteral( "Paper" ) );
    REQUIRE_FALSE( path.isEmpty() );
    CHECK( ActiveId( f.gui ) == "paper" );
    CHECK_FALSE( EditorThemeEditor_HasChanges( pEditor ) );
    CHECK( EditorGui_ThemeIds( &f.gui ).contains( QStringLiteral( "paper" ) ) );
    CHECK( f.gui.style.colors[STYLE_COLOR_BACKGROUND] == 0xF0F0F0FFu );
    CHECK( f.gui.style.bLight );
    const string_view_t setting = EditorSettings_Text( &f.gui.settings, "editor.ui.theme", string_view_t{} );
    CHECK( std::string( setting.pData, setting.cchLength ) == "paper" );

    // The file is a complete root theme: every token, the pinned derived
    // colour kept, unpinned derived colours on "auto", no problems.
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QString text = QString::fromUtf8( file.readAll() );
    CHECK( text.contains( QStringLiteral( "\"ui.header\" = \"#123456\"" ) ) );
    CHECK( text.contains( QStringLiteral( "\"ui.border\" = \"auto\"" ) ) );
    CHECK( text.contains( QStringLiteral( "\n    appearance = \"light\"" ) ) ); // Bare keys, as the docs write them.
    CHECK_FALSE( text.contains( QStringLiteral( "base =" ) ) );
    settings_document_t saved{};
    REQUIRE( SettingsDocument_Init( &saved, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
    const QByteArray utf8 = text.toUtf8();
    REQUIRE( SettingsDocument_Load( &saved, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ).status == settings_document_status_t::OK );
    theme_problem_t problems[4]{};
    CHECK( EditorTheme_Audit( &f.gui.themeTokens, SettingsDocument_Root( &saved ), problems, 4u ) == 0u );
    SettingsDocument_Shutdown( &saved );

    // Closing now keeps the saved theme.
    pEditor->reject();
    CHECK( ActiveId( f.gui ) == "paper" );
    delete pEditor;

    // A new session finds the saved theme in the folder.
    editor_gui_t second{};
    REQUIRE( EditorGui_Init( &second, f.pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
    CHECK( EditorGui_LoadThemeFolder( &second, f.folder.path() ) == 1u );
    REQUIRE( EditorGui_SelectTheme( &second, f.pApplication, StringView_FromCString( "paper" ) ) == editor_gui_status_t::OK );
    CHECK( second.style.colors[STYLE_COLOR_BACKGROUND] == 0xF0F0F0FFu );
    CHECK( EditorStyle_TokenColor( second.style, "ui.header" ) == QColor( 0x12, 0x34, 0x56 ) );
    EditorGui_Shutdown( &second );
}

TEST_CASE( "A plugin's theme tokens survive an edit made while it is not loaded", "[editor][gui][theme_editor]" )
{
    fixture_t f;
    REQUIRE( EditorGui_AddTheme( &f.gui, QStringLiteral( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"studio\" name = \"Studio\" base = \"charcoal\"\n"
                                                         "  colors = { \"plugin.glow\" = \"#ff00ff\" } }\n" ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorGui_SelectTheme( &f.gui, f.pApplication, StringView_FromCString( "studio" ) ) == editor_gui_status_t::OK );
    QDialog *pEditor = EditorThemeEditor_Create( nullptr, &f.gui, f.pApplication, f.folder.path() );
    REQUIRE( pEditor != nullptr );
    CHECK_FALSE( EditorThemeEditor_VisibleTokens( pEditor ).contains( QStringLiteral( "plugin.glow" ) ) ); // Nothing to edit it with.
    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.accent" ), QColor( 0x11, 0x22, 0x33 ) );
    const QString path = EditorThemeEditor_Save( pEditor, QStringLiteral( "studio" ), QStringLiteral( "Studio" ) );
    REQUIRE_FALSE( path.isEmpty() );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QString text = QString::fromUtf8( file.readAll() );
    CHECK( text.contains( QStringLiteral( "\"plugin.glow\" = \"#ff00ff\"" ) ) );
    CHECK( text.contains( QStringLiteral( "\"ui.accent\" = \"#112233\"" ) ) );
    delete pEditor;
}
