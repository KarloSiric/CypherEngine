//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Keymap_Tests.cpp
//  Purpose: Contract tests for key chords and editor keymaps.
//  Details: Covers canonical chord text, lookups through base keymaps,
//           explicit unbinding, overrides in reverse lookup, conflicts, and
//           sparse editing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Keymap.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

std::string Canonical( const char *pText )
{
    key_chord_t chord{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( pText ), &chord ) );
    char buffer[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    const usize cch = EditorKeyChord_Format( chord, buffer );
    REQUIRE( cch != 0u );
    return std::string( buffer, cch );
}

key_chord_t Chord( const char *pText )
{
    key_chord_t chord{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( pText ), &chord ) );
    return chord;
}

struct keymap_doc_t {
    settings_document_t store{};
    explicit keymap_doc_t( const char *pText )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        if ( pText != nullptr ) {
            REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( pText ) ).status == settings_document_status_t::OK );
        }
    }
    const key_value_t *Root() const { return SettingsDocument_Root( &store ); }
};

string_view_t View( const char *pText )
{
    return StringView_FromCString( pText );
}

const char *kDefault = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 1
{
    id = "cypher_default"
    name = "Cypher Default"
    bindings = {
        global = {
            "file.save" = [ "Ctrl+S" ]
            "edit.duplicate" = [ "Ctrl+D" ]
            "edit.undo" = [ "Ctrl+Z" ]
            "view.console" = [ "Backquote" ]
        }
        "map.viewport" = { "map.tool.clip" = [ "Shift+X" ] "map.tool.vertex" = [ "Shift+V" ] }
    }
}
)cykv";

} // namespace

TEST_CASE( "Key chords parse flexibly and format canonically",
           "[CypherEditor][Core][Keymap]" )
{
    REQUIRE( Canonical( "ctrl+shift+s" ) == "Ctrl+Shift+S" );
    REQUIRE( Canonical( "Meta+Alt+Control+Shift+F12" ) == "Ctrl+Alt+Shift+Meta+F12" );
    REQUIRE( Canonical( "Cmd+Option+Del" ) == "Alt+Meta+Delete" );
    REQUIRE( Canonical( "Ctrl+K, Ctrl+C" ) == "Ctrl+K, Ctrl+C" );
    REQUIRE( Canonical( "Ctrl+," ) == "Ctrl+Comma" );
    REQUIRE( Canonical( "`" ) == "Backquote" );
    REQUIRE( Canonical( "num5" ) == "Num5" );
    REQUIRE( Canonical( "PgUp" ) == "PageUp" );
    REQUIRE( Canonical( "shift+space" ) == "Shift+Space" );
    REQUIRE( EditorKeyChord_Equals( Chord( "Ctrl+Shift+S" ), Chord( "shift+ctrl+s" ) ) );

    key_chord_t chord{};
    for ( const char *pBad : { "", "Ctrl", "Ctrl+", "+S", "Ctrl+Hyper", "A+B", "F25", "Num10",
                               "Ctrl+K, Ctrl+C, A, B, C" } ) {
        CAPTURE( pBad );
        REQUIRE_FALSE( EditorKeyChord_Parse( View( pBad ), &chord ) );
    }
}

TEST_CASE( "Editor command IDs are dotted stable identifiers",
           "[CypherEditor][Core][Keymap]" )
{
    REQUIRE( EditorCommand_IsValidId( View( "file.save" ) ) );
    REQUIRE( EditorCommand_IsValidId( View( "map.tool.clip" ) ) );
    for ( const char *pBad : { "save", "File.Save", "file..save", "file.", ".save", "file.save as" } ) {
        CAPTURE( pBad );
        REQUIRE_FALSE( EditorCommand_IsValidId( View( pBad ) ) );
    }
}

TEST_CASE( "Keymaps inherit, override, and explicitly unbind through their base",
           "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t base( kDefault );
    keymap_doc_t user( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 1
{
    id = "karlo"
    base = "cypher_default"
    bindings = {
        global = {
            "edit.duplicate" = [ "Ctrl+Shift+D" ]
            "edit.undo" = [ "Ctrl+Hyper+Z" ]
            "view.console" = []
        }
    }
}
)cykv" );
    const key_value_t *chain[]{ user.Root(), base.Root() };
    keymap_binding_t binding{};

    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "global" ), View( "file.save" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.iSource == 1u );
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "global" ), View( "edit.duplicate" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.iSource == 0u );
    REQUIRE( EditorKeyChord_Equals( binding.chords[0], Chord( "Ctrl+Shift+D" ) ) );
    // An unparseable override falls back to the base instead of losing undo.
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "global" ), View( "edit.undo" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.iSource == 1u );
    REQUIRE( binding.nInvalidChords == 1u );
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "global" ), View( "view.console" ), &binding ) == keymap_lookup_t::UNBOUND );
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "global" ), View( "map.tool.clip" ), &binding ) == keymap_lookup_t::NOT_DEFINED );

    // Reverse lookup honours the override: Ctrl+D no longer duplicates.
    REQUIRE( EditorKeymap_FindCommand( chain, 2u, View( "global" ), Chord( "Ctrl+D" ) ).cchLength == 0u );
    REQUIRE( StringView_Equals( EditorKeymap_FindCommand( chain, 2u, View( "global" ), Chord( "ctrl+shift+d" ) ), View( "edit.duplicate" ) ) );
    REQUIRE( StringView_Equals( EditorKeymap_FindCommand( chain, 2u, View( "map.viewport" ), Chord( "Shift+X" ) ), View( "map.tool.clip" ) ) );
    REQUIRE( EditorKeymap_FindCommand( chain, 2u, View( "global" ), Chord( "Backquote" ) ).cchLength == 0u );
}

TEST_CASE( "Keymap conflicts are reported once per clashing pair",
           "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t base( kDefault );
    keymap_doc_t user( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 1
{
    id = "clash"
    bindings = { global = { "view.grid" = [ "Ctrl+S" ] "edit.duplicate" = [ "Ctrl+Z" ] } }
}
)cykv" );
    const key_value_t *chain[]{ user.Root(), base.Root() };
    keymap_conflict_t conflicts[4]{};
    const usize nConflicts = EditorKeymap_FindConflicts( chain, 2u, View( "global" ), conflicts, 4u );
    REQUIRE( nConflicts == 2u );
    REQUIRE( StringView_Equals( conflicts[0].first, View( "view.grid" ) ) );
    REQUIRE( StringView_Equals( conflicts[0].second, View( "file.save" ) ) );
    REQUIRE( StringView_Equals( conflicts[1].first, View( "edit.duplicate" ) ) );
    REQUIRE( StringView_Equals( conflicts[1].second, View( "edit.undo" ) ) );
    const key_value_t *baseOnly[]{ base.Root() };
    REQUIRE( EditorKeymap_FindConflicts( baseOnly, 1u, View( "global" ), nullptr, 0u ) == 0u );
}

TEST_CASE( "Keymap edits write canonical chords and reset to the base",
           "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t base( kDefault );
    keymap_doc_t user( nullptr );
    const key_chord_t chords[]{ Chord( "shift+ctrl+e" ), Chord( "F5" ) };
    REQUIRE( EditorKeymap_SetBinding( &user.store, View( "map.viewport" ), View( "map.tool.clip" ), chords, 2u ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_SetBinding( &user.store, View( "global" ), View( "view.console" ), nullptr, 0u ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_SetBinding( &user.store, View( "global" ), View( "not_a_command" ), chords, 1u ) == keymap_status_t::INVALID_ARGUMENT );

    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &user.store, &text ) == settings_document_status_t::OK );
    const std::string written( TextBuffer_Data( &text ), TextBuffer_Length( &text ) );
    REQUIRE( written.find( "\"Ctrl+Shift+E\"" ) != std::string::npos );
    REQUIRE( written.find( "@schema \"cypher.editor_keymap\" 1" ) != std::string::npos );

    const key_value_t *chain[]{ user.Root(), base.Root() };
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "map.viewport" ), View( "map.tool.clip" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.nChords == 2u );
    REQUIRE( EditorKeymap_ResetBinding( &user.store, View( "map.viewport" ), View( "map.tool.clip" ) ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "map.viewport" ), View( "map.tool.clip" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.iSource == 1u );
}
