//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_KeymapSettings_Tests.cpp
//  Purpose: Contracts for staged contextual keymap editing, import/export,
//           inheritance, conflict checks and publication without data loss.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_KeymapSettings.h"
#include "CypherEditorGui_SettingsDialog.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

command_result_t Noop( void *, const command_args_t & ) { return command_result_t::OK; }
u32 Disabled( void * ) { return COMMAND_STATE_NONE; }
QString Text( string_view_t view ) { return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) ); }

QString CanonicalTriggerText( keymap_section_t section, string_view_t text )
{
    char bytes[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    if ( section == keymap_section_t::BINDINGS ) {
        key_chord_t value{}; REQUIRE( EditorKeyChord_Parse( text, &value ) );
        return QString::fromUtf8( bytes, static_cast<qsizetype>( EditorKeyChord_Format( value, bytes ) ) );
    }
    if ( section == keymap_section_t::HELD ) {
        key_stroke_t value{}; REQUIRE( EditorHeldKey_Parse( text, &value ) );
        return QString::fromUtf8( bytes, static_cast<qsizetype>( EditorHeldKey_Format( value, bytes ) ) );
    }
    mouse_gesture_t value{}; REQUIRE( EditorMouseGesture_Parse( text, &value ) );
    char mouseBytes[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
    return QString::fromUtf8( mouseBytes, static_cast<qsizetype>( EditorMouseGesture_Format( value, mouseBytes ) ) );
}

struct fixture_t {
    editor_gui_t gui{};
    settings_document_t user{};
    QTemporaryDir folder{};
    std::unique_ptr<QWidget> page{};

    explicit fixture_t( const allocator_t *pAllocator = Allocator_GetSystem() )
    {
        auto *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( pApplication != nullptr ); REQUIRE( folder.isValid() );
        REQUIRE( EditorGui_Init( &gui, pApplication, pAllocator ) == editor_gui_status_t::OK );
        REQUIRE( SettingsDocument_Init( &user, pAllocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &user );
        const command_desc_t commands[]{
            { "file.save", "Save", "Save the current map.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop },
            { "edit.undo", "Undo", "Undo the last edit.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop },
            { "map.mesh.flip_normals", "Flip normals", "Reverse authored mesh normals.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop, Disabled },
            { "test.no_binding", "Unassigned action", "Registered without a key declaration.", nullptr, nullptr, COMMAND_FLAG_NONE, Noop },
        };
        REQUIRE( EditorCommands_Register( &gui.commands, commands, std::size( commands ) ) == command_registry_status_t::OK );
        page.reset( EditorKeymapSettings_Create( nullptr, &gui, folder.path() ) ); REQUIRE( page != nullptr );
    }
    ~fixture_t()
    {
        page.reset(); EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
        EditorGui_Shutdown( &gui ); SettingsDocument_Shutdown( &user );
    }
};

QStringList Row( QWidget *pPage, const QString &id, const QString &context )
{
    for ( const QString &line : EditorKeymapSettings_Rows( pPage ) ) {
        const QStringList fields = line.split( QLatin1Char( '\t' ) );
        if ( fields[1] == id && fields[3] == context ) { return fields; }
    }
    return {};
}

void Write( const QString &path, const QByteArray &text )
{
    QFile file( path ); REQUIRE( file.open( QIODevice::WriteOnly ) ); REQUIRE( file.write( text ) == text.size() );
}

const QByteArray kImported = R"KEYMAP(@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "shared_keys"
    name = "Shared keys"
    base = "cypher_default"
    author = "Level team"
    description = "Keep unknown content"
    future_metadata = { version = 57 note = "must survive" }
    bindings = {
        global = { "file.save" = [ "Ctrl+Alt+F12" ] "future.plugin.action" = [ "F24" ] }
    }
    held = { "map.viewport.3d" = { "map.camera.forward" = [ "P" ] } }
    mouse = { "map.viewport.3d" = { "map.camera.look" = [ "Ctrl+RightDrag" ] } }
    platforms = {
        windows = { bindings = { "windows.only" = { "future.plugin.action" = [ "Ctrl+F23" ] } } }
        macos = { held = { "mac.only" = { "future.plugin.held" = [ "Meta+Q" ] } } }
        freebsd = { future_keys = [ "unrecognized but retained" ] }
    }
}
)KEYMAP";

} // namespace

TEST_CASE( "Keybindings Settings groups contextual keyboard, held and mouse declarations", "[editor][gui][keymapsettings]" )
{
    fixture_t f;
    CHECK( EditorKeymapSettings_CurrentKeymap( f.page.get() ) == QStringLiteral( "cypher_default" ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) );
    CHECK( EditorKeymapSettings_Conflicts( f.page.get() ).isEmpty() ); // Shared selection/transform drag is intentional.
    const auto forward = Row( f.page.get(), QStringLiteral( "map.camera.forward" ), QStringLiteral( "map.viewport.3d" ) );
    REQUIRE( forward.size() == 7 );
    CHECK( forward[0] == QStringLiteral( "Camera" ) ); CHECK( forward[2] == QStringLiteral( "Held key" ) );
    CHECK( forward[4] == QStringLiteral( "W; Up" ) ); CHECK( forward[5].startsWith( QStringLiteral( "Inherited:" ) ) );
    CHECK( forward[6] == QStringLiteral( "3D camera held declaration" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.camera.look" ), QStringLiteral( "map.viewport.3d" ) )[6] == QStringLiteral( "3D camera gesture declaration" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.select.pick" ), QStringLiteral( "map.viewport" ) )[6] == QStringLiteral( "Not remappable yet" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.transform.drag" ), QStringLiteral( "map.viewport" ) )[6] == QStringLiteral( "Not remappable yet" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.view.pan" ), QStringLiteral( "map.viewport.2d" ) )[6] == QStringLiteral( "Not remappable yet" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.mesh.flip_normals" ), QStringLiteral( "map.selection.objects" ) )[6] == QStringLiteral( "Disabled now" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.entity.tie" ), QStringLiteral( "map" ) )[6] == QStringLiteral( "Not registered" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "test.no_binding" ), QStringLiteral( "global" ) )[4].isEmpty() );
    EditorKeymapSettings_SetFilter( f.page.get(), QStringLiteral( "map.viewport.2d nudge" ) );
    REQUIRE_FALSE( EditorKeymapSettings_Rows( f.page.get() ).isEmpty() );
    for ( const auto &line : EditorKeymapSettings_Rows( f.page.get() ) ) { CHECK( line.contains( QStringLiteral( "map.viewport.2d" ) ) ); CHECK( line.contains( QStringLiteral( "nudge" ) ) ); }

    std::unique_ptr<QDialog> dialog( EditorSettingsDialog_Create( nullptr, &f.gui.settings ) );
    auto *pPage = f.page.release();
    EditorSettingsDialog_AddPage( dialog.get(), QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ), pPage,
                                 EditorKeymapSettings_Keywords(), EditorKeymapSettings_CanClose );
    EditorSettingsDialog_SetSearch( dialog.get(), QStringLiteral( "held" ) );
    CHECK( EditorSettingsDialog_PageResults( dialog.get() ).contains( QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ) ) );
    EditorSettingsDialog_ShowPage( dialog.get(), QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ) );
    CHECK( EditorSettingsDialog_CurrentPage( dialog.get() ) == QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ) );
}

TEST_CASE( "Keymap working copies stage remaps, explicit unbindings and inherited resets", "[editor][gui][keymapsettings]" )
{
    fixture_t f;
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "global" ), QStringLiteral( "file.save" ), { QStringLiteral( "Ctrl+Alt+F12" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "P" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::MOUSE, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.look" ), { QStringLiteral( "Ctrl+RightDrag" ) } ) );
    CHECK( EditorKeymapSettings_HasChanges( f.page.get() ) );
    keymap_binding_t active{};
    REQUIRE( EditorKeymap_FindBinding( f.gui.keymapChain, f.gui.nKeymapChain, StringView_FromCString( "global" ), StringView_FromCString( "file.save" ), &active ) == keymap_lookup_t::BOUND );
    CHECK( active.chords[0].strokes[0].key == 'S' ); // Live input stays unchanged.
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "global" ), QStringLiteral( "file.save" ), {} ) );
    CHECK( Row( f.page.get(), QStringLiteral( "file.save" ), QStringLiteral( "global" ) )[4].isEmpty() );
    REQUIRE( EditorKeymapSettings_Reset( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                       QStringLiteral( "global" ), QStringLiteral( "file.save" ) ) );
    CHECK( Row( f.page.get(), QStringLiteral( "file.save" ), QStringLiteral( "global" ) )[4] == QStringLiteral( "Ctrl+S" ) );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    CHECK_FALSE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                                 QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "W, W" ) } ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    EditorKeymapSettings_Revert( f.page.get() ); // Discard is explicit, even when another Settings page is visible.
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) );
}

TEST_CASE( "New keybinding profiles inherit an installed profile and publish only on Apply", "[editor][gui][keymapsettings][profiles]" )
{
    fixture_t f;
    REQUIRE( EditorGui_AddKeymap( &f.gui, QString::fromUtf8( kImported ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorKeymapSettings_NewProfile( f.page.get(), QStringLiteral( "arena_keys" ), QStringLiteral( "Arena keys" ), QStringLiteral( "shared_keys" ) ) );
    CHECK( EditorKeymapSettings_CurrentKeymap( f.page.get() ) == QStringLiteral( "arena_keys" ) );
    CHECK( EditorKeymapSettings_HasChanges( f.page.get() ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ).contains( QStringLiteral( "base = \"shared_keys\"" ) ) );
    CHECK( Row( f.page.get(), QStringLiteral( "file.save" ), QStringLiteral( "global" ) )[4] == QStringLiteral( "Ctrl+Alt+F12" ) );
    EditorKeymapSettings_SetPlatform( f.page.get(), keymap_platform_t::WINDOWS );
    CHECK( Row( f.page.get(), QStringLiteral( "future.plugin.action" ), QStringLiteral( "windows.only" ) )[4] == QStringLiteral( "Ctrl+F23" ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    CHECK_FALSE( QFile::exists( f.folder.filePath( QStringLiteral( "arena_keys.cykeymap" ) ) ) );
    REQUIRE( EditorKeymapSettings_Apply( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "arena_keys" ) );
    CHECK( QFile::exists( f.folder.filePath( QStringLiteral( "arena_keys.cykeymap" ) ) ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) );
    REQUIRE( EditorKeymapSettings_SelectKeymap( f.page.get(), QStringLiteral( "shared_keys" ) ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ).contains( QStringLiteral( "must survive" ) ) );
}

TEST_CASE( "Duplicate profiles retain staged declarations and metadata and reject existing identities", "[editor][gui][keymapsettings][profiles]" )
{
    fixture_t f;
    const QString source = f.folder.filePath( QStringLiteral( "source.cykeymap" ) ); Write( source, kImported );
    REQUIRE( EditorKeymapSettings_Import( f.page.get(), source ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::MACOS,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "O" ) } ) );
    REQUIRE( EditorKeymapSettings_DuplicateProfile( f.page.get(), QStringLiteral( "shared_keys_copy" ), QStringLiteral( "Shared keys copy" ) ) );
    const QString duplicate = EditorKeymapSettings_DraftText( f.page.get() );
    CHECK( duplicate.contains( QStringLiteral( "base = \"cypher_default\"" ) ) );
    CHECK( duplicate.contains( QStringLiteral( "must survive" ) ) ); CHECK( duplicate.contains( QStringLiteral( "freebsd" ) ) );
    CHECK( duplicate.contains( QStringLiteral( "Level team" ) ) );
    EditorKeymapSettings_SetPlatform( f.page.get(), keymap_platform_t::MACOS );
    CHECK( Row( f.page.get(), QStringLiteral( "map.camera.forward" ), QStringLiteral( "map.viewport.3d" ) )[4] == QStringLiteral( "O" ) );
    CHECK( Row( f.page.get(), QStringLiteral( "future.plugin.held" ), QStringLiteral( "mac.only" ) )[4] == QStringLiteral( "Meta+Q" ) );
    CHECK_FALSE( EditorKeymapSettings_DuplicateProfile( f.page.get(), QStringLiteral( "cypher_default" ), QStringLiteral( "Overwrite" ) ) );
    CHECK_FALSE( EditorKeymapSettings_NewProfile( f.page.get(), QStringLiteral( "invalid id" ), QStringLiteral( "Invalid" ), QStringLiteral( "cypher_default" ) ) );
    CHECK_FALSE( EditorKeymapSettings_NewProfile( f.page.get(), QStringLiteral( "valid_id" ), QStringLiteral( "Valid" ), QStringLiteral( "missing" ) ) );
    const QString occupied = f.folder.filePath( QStringLiteral( "occupied.cykeymap" ) ); Write( occupied, QByteArray( "existing file" ) );
    CHECK_FALSE( EditorKeymapSettings_DuplicateProfile( f.page.get(), QStringLiteral( "occupied" ), QStringLiteral( "Occupied" ) ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == duplicate );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    // Editing a new draft's identity fields must not bypass its no-overwrite policy.
    CHECK_FALSE( EditorKeymapSettings_Apply( f.page.get(), QStringLiteral( "occupied" ), QStringLiteral( "Occupied" ) ) );
    QFile unchanged( occupied ); REQUIRE( unchanged.open( QIODevice::ReadOnly ) ); CHECK( unchanged.readAll() == QByteArray( "existing file" ) );
    REQUIRE( EditorKeymapSettings_Apply( f.page.get(), QStringLiteral( "shared_keys_copy" ), QStringLiteral( "Shared keys copy" ) ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "shared_keys_copy" ) );
}

TEST_CASE( "Profile creation controls and shortcuts reference are discoverable in Settings", "[editor][gui][keymapsettings][profiles]" )
{
    fixture_t f;
    auto *pProfiles = f.page->findChild<QToolButton *>( QStringLiteral( "KeymapProfileActions" ) ); REQUIRE( pProfiles != nullptr );
    REQUIRE( pProfiles->menu() != nullptr ); CHECK( pProfiles->popupMode() == QToolButton::InstantPopup );
    auto *pNew = pProfiles->menu()->findChild<QAction *>( QStringLiteral( "KeymapNewProfile" ) ); REQUIRE( pNew != nullptr );
    auto *pDuplicate = pProfiles->menu()->findChild<QAction *>( QStringLiteral( "KeymapDuplicateProfile" ) ); REQUIRE( pDuplicate != nullptr );
    for ( const char *pName : { "KeymapImport", "KeymapExportPortable", "KeymapExportSource" } ) {
        const auto *pAction = pProfiles->menu()->findChild<QAction *>( QString::fromLatin1( pName ) ); REQUIRE( pAction != nullptr );
        CHECK( pProfiles->menu()->actions().contains( const_cast<QAction *>( pAction ) ) ); CHECK( pAction->isEnabled() );
    }
    auto *pReference = f.page->findChild<QPushButton *>( QStringLiteral( "KeymapShortcutsReference" ) ); REQUIRE( pReference != nullptr );
    CHECK_FALSE( pReference->isEnabled() );
    int referenceCalls = 0;
    EditorKeymapSettings_SetReferenceCallback( f.page.get(), []( void *pContext ) { ++*static_cast<int *>( pContext ); }, &referenceCalls );
    CHECK( pReference->isEnabled() ); pReference->click(); CHECK( referenceCalls == 1 );
    EditorKeymapSettings_SetReferenceCallback( f.page.get(), nullptr, nullptr ); CHECK_FALSE( pReference->isEnabled() );
    bool newFieldsFound = false, duplicateFieldsFound = false;
    const auto submit = [&]( const QString &id, const QString &name, bool isNew, bool &found ) {
        auto *pDialog = f.page->findChild<QDialog *>( QStringLiteral( "KeymapProfileDialog" ) );
        if ( pDialog == nullptr ) { return; }
        auto *pId = pDialog->findChild<QLineEdit *>( QStringLiteral( "KeymapProfileDialogId" ) );
        auto *pName = pDialog->findChild<QLineEdit *>( QStringLiteral( "KeymapProfileDialogName" ) );
        auto *pButtons = pDialog->findChild<QDialogButtonBox *>();
        found = pId != nullptr && pName != nullptr && pButtons != nullptr &&
                ( pDialog->findChild<QComboBox *>( QStringLiteral( "KeymapProfileDialogBase" ) ) != nullptr ) == isNew;
        if ( found ) {
            pId->setText( id ); pName->setText( name ); pButtons->button( QDialogButtonBox::Ok )->click();
            if ( pDialog->isVisible() ) { pDialog->reject(); } // A rejected profile must not strand the test's modal loop.
        }
        else { pDialog->reject(); }
    };
    QTimer::singleShot( 0, f.page.get(), [&]() { submit( QStringLiteral( "ui_profile" ), QStringLiteral( "UI profile" ), true, newFieldsFound ); } );
    pNew->trigger(); CHECK( newFieldsFound ); CHECK( EditorKeymapSettings_CurrentKeymap( f.page.get() ) == QStringLiteral( "ui_profile" ) );
    QTimer::singleShot( 0, f.page.get(), [&]() { submit( QStringLiteral( "ui_copy" ), QStringLiteral( "UI copy" ), false, duplicateFieldsFound ); } );
    pDuplicate->trigger(); CHECK( duplicateFieldsFound ); CHECK( EditorKeymapSettings_CurrentKeymap( f.page.get() ) == QStringLiteral( "ui_copy" ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
}

TEST_CASE( "Keymap conflicts include prefixes, platform overlays and held and mouse collisions", "[editor][gui][keymapsettings]" )
{
    fixture_t f;
    const QString context = QStringLiteral( "test.viewport" );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE, context,
                                             QStringLiteral( "test.first" ), { QStringLiteral( "Ctrl+K" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::WINDOWS, context,
                                             QStringLiteral( "test.second" ), { QStringLiteral( "Ctrl+K, Ctrl+C" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE, context,
                                             QStringLiteral( "test.held_first" ), { QStringLiteral( "W" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE, context,
                                             QStringLiteral( "test.held_second" ), { QStringLiteral( "W" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::MOUSE, keymap_platform_t::NONE, context,
                                             QStringLiteral( "test.mouse_first" ), { QStringLiteral( "Wheel" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::MOUSE, keymap_platform_t::NONE, context,
                                             QStringLiteral( "test.mouse_second" ), { QStringLiteral( "WheelUp" ) } ) );
    const QString conflicts = EditorKeymapSettings_Conflicts( f.page.get() ).join( QLatin1Char( '\n' ) );
    CHECK( conflicts.contains( QStringLiteral( "windows" ) ) ); CHECK( conflicts.contains( QStringLiteral( "Ctrl+K, Ctrl+C" ) ) );
    CHECK( conflicts.contains( QStringLiteral( "test.held_first" ) ) ); CHECK( conflicts.contains( QStringLiteral( "test.mouse_first" ) ) );
    CHECK_FALSE( EditorKeymapSettings_Apply( f.page.get(), QStringLiteral( "conflicting" ), QStringLiteral( "Conflicting" ) ) );
    CHECK_FALSE( QFile::exists( f.folder.filePath( QStringLiteral( "conflicting.cykeymap" ) ) ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    CHECK( EditorKeymapSettings_HasChanges( f.page.get() ) );
}

TEST_CASE( "Keymap import export and Apply preserve base, metadata and unknown platform entries", "[editor][gui][keymapsettings]" )
{
    fixture_t f;
    const QString source = f.folder.filePath( QStringLiteral( "source.cykeymap" ) ); Write( source, kImported );
    REQUIRE( EditorKeymapSettings_Import( f.page.get(), source ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    CHECK( EditorKeymapSettings_HasChanges( f.page.get() ) );
    EditorKeymapSettings_SetPlatform( f.page.get(), keymap_platform_t::WINDOWS );
    CHECK( Row( f.page.get(), QStringLiteral( "future.plugin.action" ), QStringLiteral( "windows.only" ) )[4] == QStringLiteral( "Ctrl+F23" ) );
    EditorKeymapSettings_SetPlatform( f.page.get(), keymap_platform_t::MACOS );
    CHECK( Row( f.page.get(), QStringLiteral( "future.plugin.held" ), QStringLiteral( "mac.only" ) )[4] == QStringLiteral( "Meta+Q" ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::MOUSE, keymap_platform_t::MACOS,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.orbit" ), { QStringLiteral( "Ctrl+Alt+LeftDrag" ) } ) );
    const QString exportPath = f.folder.filePath( QStringLiteral( "export.cykeymap" ) );
    REQUIRE( EditorKeymapSettings_Export( f.page.get(), exportPath ) );
    QFile exported( exportPath ); REQUIRE( exported.open( QIODevice::ReadOnly ) ); const QByteArray text = exported.readAll();
    CHECK( text.contains( "future_metadata" ) ); CHECK( text.contains( "must survive" ) ); CHECK( text.contains( "freebsd" ) );
    CHECK( text.contains( "base = \"cypher_default\"" ) ); CHECK( text.contains( "Level team" ) );
    REQUIRE( EditorKeymapSettings_Apply( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "shared_keys" ) );
    CHECK( Text( EditorSettings_Text( &f.gui.settings, "editor.ui.keymap", {} ) ) == QStringLiteral( "shared_keys" ) );
    CHECK( QFile::exists( f.folder.filePath( QStringLiteral( "shared_keys.cykeymap" ) ) ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) );
    const QString saved = EditorKeymapSettings_DraftText( f.page.get() ); CHECK( saved.contains( QStringLiteral( "future_metadata" ) ) );
    CHECK( saved.contains( QStringLiteral( "freebsd" ) ) ); CHECK( saved.contains( QStringLiteral( "Ctrl+Alt+LeftDrag" ) ) );

    // Selecting an existing preset is staged too; an unchanged one needs no
    // duplicate file and can be activated with Apply.
    REQUIRE( EditorKeymapSettings_SelectKeymap( f.page.get(), QStringLiteral( "cypher_default" ) ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "shared_keys" ) );
    REQUIRE( EditorKeymapSettings_Apply( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
}

TEST_CASE( "Portable profile export resolves custom dependencies and preserves all effective platform input", "[editor][gui][keymapsettings][profiles][export]" )
{
    fixture_t f;
    const QString customBase = QStringLiteral( R"KEYMAP(@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "custom_base" name = "Custom base" base = "cypher_default"
    bindings = { "plugin.context" = { "plugin.common" = [ "F23" ] "plugin.unbound" = [ "F22" ] } }
    platforms = { windows = { bindings = { "plugin.context" = { "plugin.common" = [ "Alt+F23" ] "plugin.windows_only" = [ "F21" ] } } } }
}
)KEYMAP" );
    REQUIRE( EditorGui_AddKeymap( &f.gui, customBase ) == editor_gui_status_t::OK );
    QByteArray sourceText = kImported;
    sourceText.replace( "base = \"cypher_default\"", "base = \"custom_base\"" );
    sourceText.replace( "windows = { bindings", "windows = { future_windows_metadata = \"keep platform metadata\" bindings" );
    const QString source = f.folder.filePath( QStringLiteral( "source.cykeymap" ) ); Write( source, sourceText );
    REQUIRE( EditorKeymapSettings_Import( f.page.get(), source ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "plugin.context" ), QStringLiteral( "plugin.common" ), { QStringLiteral( "F20" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "plugin.context" ), QStringLiteral( "plugin.unbound" ), {} ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::LINUX,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "O" ) } ) );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    const QString destination = f.folder.filePath( QStringLiteral( "portable.cykeymap" ) );
    REQUIRE( EditorKeymapSettings_ExportPortable( f.page.get(), destination ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "require no base profiles" ) ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before ); CHECK( EditorKeymapSettings_HasChanges( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    QFile file( destination ); REQUIRE( file.open( QIODevice::ReadOnly ) ); const QByteArray exported = file.readAll();
    CHECK( exported.contains( "must survive" ) ); CHECK( exported.contains( "freebsd" ) ); CHECK( exported.contains( "future_windows_metadata" ) );
    settings_document_t original{}, portable{};
    REQUIRE( SettingsDocument_Init( &original, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Init( &portable, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    const QByteArray originalText = before.toUtf8();
    REQUIRE( SettingsDocument_Load( &original, { originalText.constData(), static_cast<usize>( originalText.size() ) } ).status == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &portable, { exported.constData(), static_cast<usize>( exported.size() ) } ).status == settings_document_status_t::OK );
    CHECK( EditorKeymap_Header( SettingsDocument_Root( &portable ) ).base.cchLength == 0u );
    const key_value_t *originalChain[EDITOR_KEYMAP_MAX_DEPTH]{}, *portableChain[EDITOR_KEYMAP_MAX_DEPTH]{}; bool_t complete = CY_FALSE;
    const usize originalDepth = EditorKeymap_BuildChain( f.gui.keymapLibrary.pData, f.gui.keymapLibrary.nCount, SettingsDocument_Root( &original ), originalChain,
                                                       EDITOR_KEYMAP_MAX_DEPTH, &complete );
    REQUIRE( complete ); REQUIRE( originalDepth == 3u );
    const usize portableDepth = EditorKeymap_BuildChain( nullptr, 0u, SettingsDocument_Root( &portable ), portableChain, EDITOR_KEYMAP_MAX_DEPTH, &complete );
    REQUIRE( complete ); REQUIRE( portableDepth == 1u ); // No custom or bundled profiles are needed by the recipient.
    for ( const auto platform : { keymap_platform_t::NONE, keymap_platform_t::MACOS, keymap_platform_t::WINDOWS, keymap_platform_t::LINUX } ) {
        EditorKeymapSettings_SetPlatform( f.page.get(), platform );
        for ( const QString &line : EditorKeymapSettings_Rows( f.page.get() ) ) {
            const auto fields = line.split( QLatin1Char( '\t' ) ); REQUIRE( fields.size() == 7 );
            const auto section = fields[2] == QStringLiteral( "Held key" ) ? keymap_section_t::HELD :
                                 fields[2] == QStringLiteral( "Mouse" ) ? keymap_section_t::MOUSE : keymap_section_t::BINDINGS;
            const QByteArray context = fields[3].toUtf8(), id = fields[1].toUtf8(); keymap_triggers_t expected{}, actual{};
            const auto expectedStatus = EditorKeymap_FindTriggers( originalChain, originalDepth, section, platform,
                { context.constData(), static_cast<usize>( context.size() ) }, { id.constData(), static_cast<usize>( id.size() ) }, &expected );
            const auto actualStatus = EditorKeymap_FindTriggers( portableChain, portableDepth, section, platform,
                { context.constData(), static_cast<usize>( context.size() ) }, { id.constData(), static_cast<usize>( id.size() ) }, &actual );
            INFO( fields[1].toStdString() ); INFO( fields[3].toStdString() ); INFO( static_cast<int>( platform ) );
            CHECK( actualStatus == expectedStatus ); REQUIRE( actual.nTexts == expected.nTexts );
            for ( usize i = 0u; i < expected.nTexts; ++i ) { CHECK( CanonicalTriggerText( section, actual.texts[i] ) == CanonicalTriggerText( section, expected.texts[i] ) ); }
        }
    }
    // The sparse source export keeps its dependency and says which files are needed.
    const QString authored = f.folder.filePath( QStringLiteral( "authored.cykeymap" ) );
    REQUIRE( EditorKeymapSettings_Export( f.page.get(), authored ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "custom_base, cypher_default" ) ) );
}

TEST_CASE( "Portable profile export preserves the previous file when dependencies cannot resolve", "[editor][gui][keymapsettings][profiles][export]" )
{
    fixture_t f;
    QByteArray broken = kImported; broken.replace( "base = \"cypher_default\"", "base = \"missing_base\"" );
    const QString source = f.folder.filePath( QStringLiteral( "broken.cykeymap" ) ); Write( source, broken );
    REQUIRE( EditorKeymapSettings_Import( f.page.get(), source ) );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    const QString output = f.folder.filePath( QStringLiteral( "previous.cykeymap" ) ); Write( output, QByteArray( "previous export" ) );
    CHECK_FALSE( EditorKeymapSettings_ExportPortable( f.page.get(), output ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "base is missing" ) ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    QFile previous( output ); REQUIRE( previous.open( QIODevice::ReadOnly ) ); CHECK( previous.readAll() == QByteArray( "previous export" ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
}

TEST_CASE( "Portable export rejects malformed authored entries instead of losing their working base fallback", "[editor][gui][keymapsettings][profiles][export]" )
{
    fixture_t f;
    REQUIRE( EditorGui_AddKeymap( &f.gui, QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
        "{ id = \"fallback_keys\" name = \"Fallback keys\" base = \"cypher_default\" bindings = { global = { \"edit.copy\" = [42] } } }" ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorKeymapSettings_SelectKeymap( f.page.get(), QStringLiteral( "fallback_keys" ) ) );
    REQUIRE( EditorGui_SelectKeymap( &f.gui, StringView_FromCString( "fallback_keys" ) ) == editor_gui_status_t::OK );
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBinding( f.gui.keymapChain, f.gui.nKeymapChain, StringView_FromCString( "global" ), StringView_FromCString( "edit.copy" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.nChords == 1u ); CHECK( binding.iSource == 1u ); CHECK( binding.chords[0].strokes[0].key == 'C' );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    const QString destination = f.folder.filePath( QStringLiteral( "existing.cykeymap" ) ); Write( destination, QByteArray( "previous valid export" ) );
    CHECK_FALSE( EditorKeymapSettings_ExportPortable( f.page.get(), destination ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "invalid declarations" ) ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "Export source" ) ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    QFile previous( destination ); REQUIRE( previous.open( QIODevice::ReadOnly ) ); CHECK( previous.readAll() == QByteArray( "previous valid export" ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "fallback_keys" ) );
    // Source export remains a lossless way to share/repair the authored data.
    const QString source = f.folder.filePath( QStringLiteral( "source.cykeymap" ) ); REQUIRE( EditorKeymapSettings_Export( f.page.get(), source ) );
    QFile authored( source ); REQUIRE( authored.open( QIODevice::ReadOnly ) ); const QByteArray text = authored.readAll();
    CHECK( text.contains( "42" ) ); CHECK( text.contains( "base = \"cypher_default\"" ) );

    // A malformed inherited intermediate entry is safe: it is not retained
    // in the selected draft, and WriteComplete materializes the valid fallback.
    REQUIRE( EditorGui_AddKeymap( &f.gui, QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
        "{ id = \"fallback_child\" name = \"Fallback child\" base = \"fallback_keys\" }" ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorKeymapSettings_SelectKeymap( f.page.get(), QStringLiteral( "fallback_child" ) ) );
    const QString inheritedDestination = f.folder.filePath( QStringLiteral( "inherited.cykeymap" ) );
    REQUIRE( EditorKeymapSettings_ExportPortable( f.page.get(), inheritedDestination ) );
    QFile inheritedFile( inheritedDestination ); REQUIRE( inheritedFile.open( QIODevice::ReadOnly ) ); const QByteArray inheritedText = inheritedFile.readAll();
    settings_document_t resolved{};
    REQUIRE( SettingsDocument_Init( &resolved, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &resolved, { inheritedText.constData(), static_cast<usize>( inheritedText.size() ) } ).status == settings_document_status_t::OK );
    CHECK( EditorKeymap_Header( SettingsDocument_Root( &resolved ) ).base.cchLength == 0u );
    const key_value_t *resolvedChain[]{ SettingsDocument_Root( &resolved ) }; keymap_binding_t flattened{};
    REQUIRE( EditorKeymap_FindBinding( resolvedChain, 1u, StringView_FromCString( "global" ), StringView_FromCString( "edit.copy" ), &flattened ) == keymap_lookup_t::BOUND );
    REQUIRE( flattened.nChords == binding.nChords ); REQUIRE( flattened.chords[0].nStrokes == binding.chords[0].nStrokes );
    CHECK( flattened.chords[0].strokes[0].key == binding.chords[0].strokes[0].key );
    CHECK( flattened.chords[0].strokes[0].modifiers == binding.chords[0].strokes[0].modifiers );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "fallback_keys" ) );
}

TEST_CASE( "Keymap conflicts audit the resolved map and global window action stack", "[editor][gui][keymapsettings][actions]" )
{
    fixture_t f;
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map" ), QStringLiteral( "edit.undo" ), { QStringLiteral( "Ctrl+S" ) } ) );
    QString conflicts = EditorKeymapSettings_Conflicts( f.page.get() ).join( QLatin1Char( '\n' ) );
    CHECK( conflicts.contains( QStringLiteral( "Window (map → global)" ) ) );
    CHECK( conflicts.contains( QStringLiteral( "edit.undo" ) ) ); CHECK( conflicts.contains( QStringLiteral( "file.save" ) ) );
    CHECK_FALSE( EditorKeymapSettings_Apply( f.page.get(), QStringLiteral( "ambiguous" ), QStringLiteral( "Ambiguous" ) ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map" ), QStringLiteral( "edit.undo" ), { QStringLiteral( "Ctrl+S, Ctrl+C" ) } ) );
    CHECK( EditorKeymapSettings_Conflicts( f.page.get() ).join( QLatin1Char( '\n' ) ).contains( QStringLiteral( "Window (map → global)" ) ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map" ), QStringLiteral( "edit.undo" ), {} ) );
    CHECK( EditorKeymapSettings_Conflicts( f.page.get() ).isEmpty() ); // Explicit workspace unbinding shadows global.
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport" ), QStringLiteral( "edit.undo" ), { QStringLiteral( "Ctrl+S" ) } ) );
    CHECK( EditorKeymapSettings_Conflicts( f.page.get() ).isEmpty() ); // A local view route can shadow a window shortcut.
}

TEST_CASE( "Keymap Settings labels declarations whose view routing is unavailable", "[editor][gui][keymapsettings][routing]" )
{
    fixture_t f;
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.2d" ), QStringLiteral( "test.no_binding" ), { QStringLiteral( "Ctrl+K, Ctrl+C" ) } ) );
    CHECK( Row( f.page.get(), QStringLiteral( "test.no_binding" ), QStringLiteral( "map.viewport.2d" ) )[6] == QStringLiteral( "Sequence not routed by view" ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::MOUSE, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.look" ), { QStringLiteral( "LeftClick" ) } ) );
    CHECK( Row( f.page.get(), QStringLiteral( "map.camera.look" ), QStringLiteral( "map.viewport.3d" ) )[6] == QStringLiteral( "Contains unrouted camera trigger" ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "future.input.held" ), { QStringLiteral( "P" ) } ) );
    CHECK( Row( f.page.get(), QStringLiteral( "future.input.held" ), QStringLiteral( "map.viewport.3d" ) )[6] == QStringLiteral( "Declaration · no known route" ) );
}

TEST_CASE( "Invalid imports and failed exports preserve the existing keymap working copy", "[editor][gui][keymapsettings]" )
{
    fixture_t f;
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "P" ) } ) );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    const QString invalidPath = f.folder.filePath( QStringLiteral( "invalid.cykeymap" ) );
    Write( invalidPath, "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"bad\" name = \"Bad\" held = { viewport = { \"test.action\" = [ \"W, W\" ] } } }" );
    CHECK_FALSE( EditorKeymapSettings_Import( f.page.get(), invalidPath ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    Write( invalidPath, "foreign editor config" ); CHECK_FALSE( EditorKeymapSettings_Import( f.page.get(), invalidPath ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    CHECK_FALSE( EditorKeymapSettings_Export( f.page.get(), f.folder.path() ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    Write( invalidPath, "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"missing_base\" name = \"Missing\" base = \"not_loaded\" }" );
    REQUIRE( EditorKeymapSettings_Import( f.page.get(), invalidPath ) );
    CHECK_FALSE( EditorKeymapSettings_Apply( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "base" ) ) );
}

TEST_CASE( "Keymap Apply reports a higher settings scope without claiming it controls input", "[editor][gui][keymapsettings][settings]" )
{
    fixture_t f;
    REQUIRE( EditorGui_AddKeymap( &f.gui, QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"project_keys\" name = \"Project keys\" base = \"cypher_default\" }" ) ) == editor_gui_status_t::OK );
    settings_document_t project{};
    REQUIRE( SettingsDocument_Init( &project, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &f.gui.settings, settings_scope_t::PROJECT, &project );
    const auto *pSetting = EditorSettings_Find( &f.gui.settings, StringView_FromCString( "editor.ui.keymap" ) ); REQUIRE( pSetting != nullptr );
    setting_value_t value{}; value.type = setting_type_t::STRING; value.text = StringView_FromCString( "project_keys" );
    REQUIRE( EditorSettings_Write( &f.gui.settings, settings_scope_t::PROJECT, *pSetting, value ) == settings_registry_status_t::OK );
    const auto applyPreference = []( void *pContext, string_view_t ) noexcept {
        auto *pGui = static_cast<editor_gui_t *>( pContext );
        ( void )EditorGui_SelectKeymap( pGui, EditorSettings_Text( &pGui->settings, "editor.ui.keymap", {} ) );
    };
    REQUIRE( EditorSettings_AddListener( &f.gui.settings, applyPreference, &f.gui ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "P" ) } ) );
    REQUIRE( EditorKeymapSettings_Apply( f.page.get(), QStringLiteral( "my_keys" ), QStringLiteral( "My keys" ) ) );
    CHECK( QFile::exists( f.folder.filePath( QStringLiteral( "my_keys.cykeymap" ) ) ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "project_keys" ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "Project scope selects project_keys" ) ) );
    CHECK( EditorKeymapSettings_Status( f.page.get() ).contains( QStringLiteral( "current input uses project_keys" ) ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) ); // Saved and acknowledged; closing must not prompt again.
    EditorSettings_RemoveListener( &f.gui.settings, applyPreference, &f.gui );
    EditorSettings_SetScope( &f.gui.settings, settings_scope_t::PROJECT, nullptr );
}

TEST_CASE( "Keymap Settings retains the draft when its allocator fails", "[editor][gui][keymapsettings][allocation]" )
{
    struct failure_allocator_t {
        bool fail{ false };
        allocator_t allocator{};
        failure_allocator_t()
        {
            allocator.pUserData = this;
            allocator.pfnAllocate = []( void *pContext, usize size, usize alignment ) noexcept -> void * {
                if ( static_cast<failure_allocator_t *>( pContext )->fail ) { return nullptr; }
                return Allocator_GetSystem()->pfnAllocate( Allocator_GetSystem()->pUserData, size, alignment );
            };
            allocator.pfnFree = []( void *, void *pMemory, usize size, usize alignment ) noexcept {
                Allocator_GetSystem()->pfnFree( Allocator_GetSystem()->pUserData, pMemory, size, alignment );
            };
        }
    } failure;
    fixture_t f( &failure.allocator );
    const QString before = EditorKeymapSettings_DraftText( f.page.get() );
    const QString exportPath = f.folder.filePath( QStringLiteral( "previous.cykeymap" ) ); Write( exportPath, QByteArray( "previous file" ) );
    failure.fail = true;
    CHECK_FALSE( EditorKeymapSettings_SetTriggers( f.page.get(), keymap_section_t::HELD, keymap_platform_t::NONE,
                                                 QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.forward" ), { QStringLiteral( "P" ) } ) );
    CHECK_FALSE( EditorKeymapSettings_NewProfile( f.page.get(), QStringLiteral( "my_profile" ), QStringLiteral( "My profile" ), QStringLiteral( "cypher_default" ) ) );
    CHECK_FALSE( EditorKeymapSettings_DuplicateProfile( f.page.get(), QStringLiteral( "my_duplicate" ), QStringLiteral( "My duplicate" ) ) );
    CHECK_FALSE( EditorKeymapSettings_ExportPortable( f.page.get(), exportPath ) );
    failure.fail = false;
    QFile previous( exportPath ); REQUIRE( previous.open( QIODevice::ReadOnly ) ); CHECK( previous.readAll() == QByteArray( "previous file" ) );
    CHECK( EditorKeymapSettings_DraftText( f.page.get() ) == before );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) );
    CHECK( Text( EditorGui_ActiveKeymapId( &f.gui ) ) == QStringLiteral( "cypher_default" ) );
}

TEST_CASE( "Held input can be recorded again without reopening the recorder", "[editor][gui][keymapsettings][capture]" )
{
    fixture_t f;
    auto *pRows = f.page->findChild<QTreeWidget *>( QStringLiteral( "KeymapBindings" ) ); REQUIRE( pRows != nullptr );
    QTreeWidgetItem *pForward = nullptr;
    for ( int i = 0; i < pRows->topLevelItemCount(); ++i ) {
        if ( pRows->topLevelItem( i )->toolTip( 0 ).startsWith( QStringLiteral( "map.camera.forward\n" ) ) ) { pForward = pRows->topLevelItem( i ); break; }
    }
    REQUIRE( pForward != nullptr ); pRows->setCurrentItem( pForward );
    auto *pRecord = f.page->findChild<QPushButton *>( QStringLiteral( "KeymapRecord0" ) ); REQUIRE( pRecord != nullptr );
    QString first, replacement, combined;
    QTimer::singleShot( 0, f.page.get(), [&]() {
        auto *pDialog = f.page->findChild<QDialog *>( QStringLiteral( "KeymapRecordDialog" ) );
        if ( pDialog == nullptr ) { return; }
        auto *pEdit = pDialog->findChild<QLineEdit *>( QStringLiteral( "KeymapHeldCapture" ) );
        if ( pEdit != nullptr ) {
            const auto send = [pEdit]( QEvent::Type type, int key, Qt::KeyboardModifiers modifiers ) {
                QKeyEvent event( type, key, modifiers ); QApplication::sendEvent( pEdit, &event );
            };
            send( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); send( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); first = pEdit->text();
            send( QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier ); send( QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier ); replacement = pEdit->text();
            send( QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier ); send( QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier );
            send( QEvent::KeyRelease, Qt::Key_W, Qt::ControlModifier ); send( QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier ); combined = pEdit->text();
        }
        pDialog->accept();
    } );
    pRecord->click();
    CHECK( first == QStringLiteral( "W" ) ); CHECK( replacement == QStringLiteral( "Shift" ) ); CHECK( combined == QStringLiteral( "Ctrl+W" ) );
    auto *pTrigger = f.page->findChild<QLineEdit *>( QStringLiteral( "KeymapTrigger0" ) ); REQUIRE( pTrigger != nullptr );
    CHECK( pTrigger->text() == QStringLiteral( "Ctrl+W" ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( f.page.get() ) ); // Record still requires explicit Set binding.
}

TEST_CASE( "Keybindings reserves space for actions and records bindings without opening technical details", "[editor][gui][keymapsettings][keymap-layout]" )
{
    for ( const QSize size : { QSize( 1045, 753 ), QSize( 1260, 900 ) } ) {
        CAPTURE( size.width(), size.height() );
        fixture_t f;
        std::unique_ptr<QDialog> dialog( EditorSettingsDialog_Create( nullptr, &f.gui.settings, &f.gui.style ) );
        auto *page = f.page.release();
        EditorSettingsDialog_AddPage( dialog.get(), QString::fromLatin1( EDITOR_KEYBINDINGS_PAGE ), page,
                                     EditorKeymapSettings_Keywords(), EditorKeymapSettings_CanClose );
        dialog->resize( size ); dialog->show(); QCoreApplication::processEvents();
        auto *rows = page->findChild<QTreeWidget *>( QStringLiteral( "KeymapBindings" ) ); REQUIRE( rows != nullptr );
        auto *toggle = page->findChild<QToolButton *>( QStringLiteral( "KeymapAdvancedToggle" ) ); REQUIRE( toggle != nullptr );
        auto *advanced = page->findChild<QScrollArea *>( QStringLiteral( "KeymapAdvancedDetails" ) ); REQUIRE( advanced != nullptr );
        CHECK_FALSE( toggle->isChecked() ); CHECK_FALSE( advanced->isVisible() );
        CHECK_FALSE( page->findChild<QLineEdit *>( QStringLiteral( "KeymapId" ) )->isVisible() );
        CHECK_FALSE( page->findChild<QComboBox *>( QStringLiteral( "KeymapContext" ) )->isVisible() );
        REQUIRE( rows->topLevelItemCount() > 20 );
        const int rowHeight = rows->visualItemRect( rows->topLevelItem( 0 ) ).height(); REQUIRE( rowHeight > 0 );
        CHECK( rows->viewport()->height() >= 8 * rowHeight );
        CHECK_FALSE( rows->isColumnHidden( 0 ) ); CHECK_FALSE( rows->isColumnHidden( 3 ) );
        for ( const int column : { 1, 2, 4, 5 } ) { CHECK( rows->isColumnHidden( column ) ); }
        auto *apply = page->findChild<QPushButton *>( QStringLiteral( "KeymapApply" ) ); REQUIRE( apply != nullptr );
        CHECK( dialog->rect().contains( apply->mapTo( dialog.get(), apply->rect().bottomRight() ) ) );
        const int fullHeight = rows->height();
        toggle->click(); QCoreApplication::processEvents();
        CHECK( advanced->isVisible() ); CHECK( toggle->arrowType() == Qt::DownArrow );
        CHECK( rows->viewport()->height() >= 5 * rowHeight );
        CHECK_FALSE( EditorKeymapSettings_HasChanges( page ) );
        toggle->click(); QCoreApplication::processEvents();
        CHECK_FALSE( advanced->isVisible() ); CHECK( rows->height() >= fullHeight );

        EditorKeymapSettings_SetFilter( page, QStringLiteral( "file.save" ) );
        QTreeWidgetItem *save = nullptr;
        for ( int i = 0; i < rows->topLevelItemCount(); ++i ) {
            auto *item = rows->topLevelItem( i );
            if ( item->toolTip( 0 ).startsWith( QStringLiteral( "file.save\n" ) ) && item->text( 2 ) == QStringLiteral( "global" ) ) { save = item; break; }
        }
        REQUIRE( save != nullptr ); rows->setCurrentItem( save );
        auto *record = page->findChild<QPushButton *>( QStringLiteral( "KeymapRecord0" ) ); REQUIRE( record != nullptr ); CHECK( record->isVisible() );
        bool recorded = false;
        QTimer::singleShot( 0, page, [&]() {
            auto *recorder = page->findChild<QDialog *>( QStringLiteral( "KeymapRecordDialog" ) );
            if ( recorder == nullptr ) { return; }
            auto *keys = recorder->findChild<QKeySequenceEdit *>();
            if ( keys != nullptr ) { keys->setKeySequence( QKeySequence( QStringLiteral( "Ctrl+Alt+F12" ) ) ); recorded = true; }
            recorder->accept();
        } );
        record->click(); REQUIRE( recorded );
        CHECK_FALSE( EditorKeymapSettings_HasChanges( page ) );
        CHECK( page->findChild<QLineEdit *>( QStringLiteral( "KeymapTrigger0" ) )->text() == QStringLiteral( "Ctrl+Alt+F12" ) );
        auto *set = page->findChild<QPushButton *>( QStringLiteral( "KeymapSetBinding" ) ); REQUIRE( set != nullptr ); set->click();
        CHECK( EditorKeymapSettings_HasChanges( page ) );
        keymap_binding_t binding{};
        REQUIRE( EditorKeymap_FindBinding( f.gui.keymapChain, f.gui.nKeymapChain, StringView_FromCString( "global" ), StringView_FromCString( "file.save" ), &binding ) == keymap_lookup_t::BOUND );
        CHECK( binding.chords[0].strokes[0].key == 'S' );
        CHECK( apply->isEnabled() ); apply->click();
        CHECK_FALSE( EditorKeymapSettings_HasChanges( page ) );
        REQUIRE( EditorKeymap_FindBinding( f.gui.keymapChain, f.gui.nKeymapChain, StringView_FromCString( "global" ), StringView_FromCString( "file.save" ), &binding ) == keymap_lookup_t::BOUND );
        CHECK( CanonicalTriggerText( keymap_section_t::BINDINGS, StringView_FromCString( "Ctrl+Alt+F12" ) ) ==
               Row( page, QStringLiteral( "file.save" ), QStringLiteral( "global" ) )[4] );
        char formatted[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
        CHECK( QString::fromUtf8( formatted, static_cast<qsizetype>( EditorKeyChord_Format( binding.chords[0], formatted ) ) ) == QStringLiteral( "Ctrl+Alt+F12" ) );
        CHECK_FALSE( advanced->isVisible() );
        dialog->hide();
    }
}
