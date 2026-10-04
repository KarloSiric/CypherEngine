//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ShortcutsDialog_Tests.cpp
//  Purpose: Validates context-qualified shortcut declarations alongside
//           live window shortcuts, without implying an active viewport.
//
//  History:
//  - Created by Karlo Siric on 2026-10-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_ShortcutsDialog.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMainWindow>
#include <QTreeWidget>

#include <iterator>
#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

command_result_t Noop( void *, const command_args_t & ) { return command_result_t::OK; }
u32 Disabled( void * ) { return COMMAND_STATE_NONE; }

const char *HostName()
{
    switch ( EditorKeymap_HostPlatform() ) {
        case keymap_platform_t::MACOS: return "macos";
        case keymap_platform_t::WINDOWS: return "windows";
        case keymap_platform_t::LINUX: return "linux";
        default: return "none";
    }
}

const char *OtherPlatform()
{
    return EditorKeymap_HostPlatform() == keymap_platform_t::MACOS ? "windows" : "macos";
}

struct fixture_t {
    editor_gui_t gui{};
    QMainWindow window{};
    editor_actions_t actions{};
    std::unique_ptr<QDialog> dialog{};

    fixture_t()
    {
        auto *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( pApplication != nullptr );
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        const command_desc_t commands[]{
            { "file.save", "Save", "Save the map.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop, nullptr, nullptr },
            { "edit.undo", "Undo", "Undo the last change.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop, nullptr, nullptr },
            { "map.nudge.left", "Nudge Left", "Move the selection left.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop, nullptr, nullptr },
            { "map.mesh.flip_normals", "Flip Mesh Normals", "Reverse whole mesh normals.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop, Disabled, nullptr },
        };
        REQUIRE( EditorCommands_Register( &gui.commands, commands, std::size( commands ) ) == command_registry_status_t::OK );
        EditorActions_Init( &actions, &gui.commands, &gui.style, &window );
        const QString base = QString::fromUtf8( R"KEYMAP(@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "shortcuts_base"
    bindings = {
        global = { "file.save" = [ "Ctrl+S" ] "edit.undo" = [ "Ctrl+Z" ] }
        "map.viewport" = { "map.tool.confirm" = [ "Enter", "NumEnter" ] "map.tool.cancel" = [ "Escape" ] }
        "map.viewport.2d" = { "map.nudge.left" = [ "Left" ] }
        "map.selection.objects" = { "map.mesh.flip_normals" = [ "F" ] }
        "map.tool.vertex" = { "future.vertex.cut" = [ "C" ] }
    }
    held = { "map.viewport.3d" = { "map.camera.forward" = [ "W", "Up" ] } }
    mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] } }
}
)KEYMAP" );
        REQUIRE( EditorGui_AddKeymap( &gui, base ) == editor_gui_status_t::OK );
        const QString custom = QString::fromUtf8( R"KEYMAP(@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "shortcuts_custom"
    base = "shortcuts_base"
    bindings = {
        global = { "file.save" = [ "Ctrl+Shift+S" ] }
        "map.viewport" = { "map.tool.cancel" = [] }
        "map.viewport.2d" = { "map.nudge.left" = [ "A" ] }
    }
    held = { "map.viewport.3d" = { "map.camera.forward" = [] } }
    platforms = {
        %1 = {
            bindings = { "platform.only" = { "map.nudge.left" = [ "Ctrl+J" ] } }
            held = { "platform.only" = { "map.camera.up" = [ "Ctrl+Q" ] } }
            mouse = { "platform.only" = { "map.camera.orbit" = [ "Ctrl+Alt+LeftDrag" ] } }
        }
        %2 = {
            bindings = { "foreign.only" = { "map.nudge.left" = [ "F24" ] } }
            held = { "foreign.only" = { "map.camera.up" = [ "F23" ] } }
            mouse = { "foreign.only" = { "map.camera.orbit" = [ "Alt+MiddleDrag" ] } }
        }
    }
}
)KEYMAP" ).arg( QString::fromLatin1( HostName() ), QString::fromLatin1( OtherPlatform() ) );
        REQUIRE( EditorGui_AddKeymap( &gui, custom ) == editor_gui_status_t::OK );
        REQUIRE( EditorGui_SelectKeymap( &gui, StringView_FromCString( "shortcuts_custom" ) ) == editor_gui_status_t::OK );
        const string_view_t contexts[]{ StringView_FromCString( "map" ), StringView_FromCString( "global" ) };
        EditorActions_ApplyKeymapStack( &actions, gui.keymapChain, gui.nKeymapChain, EditorKeymap_HostPlatform(), contexts, std::size( contexts ) );
        dialog.reset( EditorShortcutsDialog_Create( &window, &gui, &actions ) );
        REQUIRE( dialog != nullptr );
    }

    ~fixture_t()
    {
        dialog.reset();
        EditorGui_Shutdown( &gui );
    }

    QTreeWidgetItem *Find( const QString &id, const QString &context ) const
    {
        auto *pTree = dialog->findChild<QTreeWidget *>( QStringLiteral( "EditorShortcutsList" ) );
        REQUIRE( pTree != nullptr );
        for ( int group = 0; group < pTree->topLevelItemCount(); ++group ) {
            for ( int child = 0; child < pTree->topLevelItem( group )->childCount(); ++child ) {
                QTreeWidgetItem *pItem = pTree->topLevelItem( group )->child( child );
                if ( pItem->text( 3 ) == id && pItem->text( 2 ) == context ) { return pItem; }
            }
        }
        return nullptr;
    }
};

QString Native( const char *pText )
{
    key_chord_t chord{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( pText ), &chord ) );
    return EditorKeyChord_ToKeySequence( chord ).toString( QKeySequence::NativeText );
}

} // namespace

TEST_CASE( "Shortcut reference separates live window keys from inherited context declarations", "[editor][gui][shortcuts][contexts]" )
{
    fixture_t f;
    const QStringList rows = EditorShortcutsDialog_Rows( f.dialog.get() );
    CHECK( rows.contains( QStringLiteral( "File\tSave\tCtrl+Shift+S\tWindow" ) ) );
    CHECK( rows.contains( QStringLiteral( "File\tSave\tCtrl+Shift+S\tglobal" ) ) );
    CHECK( rows.contains( QStringLiteral( "Edit\tUndo\tCtrl+Z\tglobal" ) ) );
    CHECK( rows.contains( QStringLiteral( "Map\tNudge Left\t\tWindow" ) ) );
    CHECK( rows.contains( QStringLiteral( "Map\tNudge Left\tA\tmap.viewport.2d" ) ) );
    CHECK_FALSE( rows.contains( QStringLiteral( "Map\tNudge Left\tLeft\tmap.viewport.2d" ) ) );
    CHECK( rows.contains( QStringLiteral( "Map Tools\tConfirm Tool\tEnter, NumEnter\tmap.viewport" ) ) );
    CHECK( rows.contains( QStringLiteral( "Mesh\tFlip Mesh Normals\tF\tmap.selection.objects" ) ) );
    for ( const QString &row : rows ) { CHECK_FALSE( row.contains( QStringLiteral( "future.vertex.cut" ) ) ); }
    CHECK( f.Find( QStringLiteral( "future.vertex.cut" ), QStringLiteral( "map.tool.vertex" ) ) == nullptr );

    auto *pWindow = f.Find( QStringLiteral( "file.save" ), QStringLiteral( "Window" ) );
    auto *pDeclared = f.Find( QStringLiteral( "file.save" ), QStringLiteral( "global" ) );
    REQUIRE( pWindow != nullptr );
    REQUIRE( pDeclared != nullptr );
    CHECK( pWindow->text( 1 ) == Native( "Ctrl+Shift+S" ) );
    CHECK( pDeclared->text( 1 ) == Native( "Ctrl+Shift+S" ) );
    auto *pDisabled = f.Find( QStringLiteral( "map.mesh.flip_normals" ), QStringLiteral( "map.selection.objects" ) );
    REQUIRE( pDisabled != nullptr );
    CHECK( pDisabled->toolTip( 0 ).contains( QStringLiteral( "Currently unavailable" ) ) );
    auto *pScope = f.dialog->findChild<QLabel *>( QStringLiteral( "EditorShortcutsScope" ) );
    REQUIRE( pScope != nullptr );
    CHECK( pScope->text().contains( QStringLiteral( "keymap declarations" ) ) );
    CHECK( pScope->text().contains( QStringLiteral( "active tool" ) ) );
}

TEST_CASE( "Shortcut reference includes host-platform-only command, held, and mouse contexts", "[editor][gui][shortcuts][platform]" )
{
    fixture_t f;
    const QStringList rows = EditorShortcutsDialog_Rows( f.dialog.get() );
    CHECK( rows.contains( QStringLiteral( "Map\tNudge Left\tCtrl+J\tplatform.only" ) ) );
    CHECK( rows.contains( QStringLiteral( "Camera and Mouse\tCamera Up\tCtrl+Q\tplatform.only" ) ) );
    CHECK( rows.contains( QStringLiteral( "Camera and Mouse\tCamera Orbit\tCtrl+Alt+LeftDrag\tplatform.only" ) ) );
    for ( const QString &row : rows ) { CHECK_FALSE( row.endsWith( QStringLiteral( "\tforeign.only" ) ) ); }
    auto *pPlatform = f.Find( QStringLiteral( "map.nudge.left" ), QStringLiteral( "platform.only" ) );
    REQUIRE( pPlatform != nullptr );
    CHECK( pPlatform->text( 1 ) == Native( "Ctrl+J" ) );

    EditorShortcutsDialog_SetSearch( f.dialog.get(), QStringLiteral( "platform.only" ) );
    const QStringList filtered = EditorShortcutsDialog_Rows( f.dialog.get() );
    REQUIRE( filtered.size() == 3 );
    for ( const QString &row : filtered ) { CHECK( row.endsWith( QStringLiteral( "\tplatform.only" ) ) ); }
    const QString text = EditorShortcutsDialog_Text( f.dialog.get() );
    CHECK( text.contains( QStringLiteral( "Ctrl+J  [platform.only]" ) ) );
    CHECK( text.contains( QStringLiteral( "Ctrl+Alt+LeftDrag  [platform.only]" ) ) );
}

TEST_CASE( "Shortcut reference retains explicit unbindings and excludes them from bound-only results", "[editor][gui][shortcuts][unbound]" )
{
    fixture_t f;
    auto *pCancel = f.Find( QStringLiteral( "map.tool.cancel" ), QStringLiteral( "map.viewport" ) );
    auto *pForward = f.Find( QStringLiteral( "map.camera.forward" ), QStringLiteral( "map.viewport.3d" ) );
    REQUIRE( pCancel != nullptr );
    REQUIRE( pForward != nullptr );
    CHECK( pCancel->text( 1 ) == QStringLiteral( "Unbound" ) );
    CHECK( pForward->text( 1 ) == QStringLiteral( "Unbound" ) );
    CHECK( pCancel->data( 1, Qt::UserRole ).toString().isEmpty() );
    CHECK( pForward->data( 1, Qt::UserRole ).toString().isEmpty() );
    CHECK( EditorShortcutsDialog_Text( f.dialog.get() ).contains( QStringLiteral( "Unbound  [map.viewport]" ) ) );
    EditorShortcutsDialog_SetBoundOnly( f.dialog.get(), true );
    CHECK( f.Find( QStringLiteral( "map.tool.cancel" ), QStringLiteral( "map.viewport" ) ) == nullptr );
    CHECK( f.Find( QStringLiteral( "map.camera.forward" ), QStringLiteral( "map.viewport.3d" ) ) == nullptr );
    CHECK( f.Find( QStringLiteral( "map.tool.confirm" ), QStringLiteral( "map.viewport" ) ) != nullptr );
    for ( const QString &row : EditorShortcutsDialog_Rows( f.dialog.get() ) ) { CHECK_FALSE( row.split( QLatin1Char( '\t' ) ).value( 2 ).isEmpty() ); }
}

TEST_CASE( "Open shortcut reference follows keymap changes without losing filters or subscriptions", "[editor][gui][shortcuts][refresh]" )
{
    fixture_t f;
    const usize listeners = f.gui.nKeymapListeners;
    EditorShortcutsDialog_SetSearch( f.dialog.get(), QStringLiteral( "camera forward" ) );
    REQUIRE( EditorShortcutsDialog_Rows( f.dialog.get() ).contains( QStringLiteral( "Camera and Mouse\tCamera Forward\t\tmap.viewport.3d" ) ) );
    REQUIRE( EditorGui_SelectKeymap( &f.gui, StringView_FromCString( "shortcuts_base" ) ) == editor_gui_status_t::OK );
    const QStringList rows = EditorShortcutsDialog_Rows( f.dialog.get() );
    REQUIRE( rows.size() == 1 );
    CHECK( rows.contains( QStringLiteral( "Camera and Mouse\tCamera Forward\tW, Up\tmap.viewport.3d" ) ) );
    CHECK( f.gui.nKeymapListeners == listeners );
    f.dialog.reset();
    CHECK( f.gui.nKeymapListeners + 1u == listeners );
    REQUIRE( EditorGui_SelectKeymap( &f.gui, StringView_FromCString( "shortcuts_custom" ) ) == editor_gui_status_t::OK );
}
