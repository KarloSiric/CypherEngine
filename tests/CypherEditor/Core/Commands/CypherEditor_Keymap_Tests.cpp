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
    REQUIRE( Canonical( "plus" ) == "Plus" );
    REQUIRE( Canonical( "Shift+Plus" ) == "Shift+Plus" );
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
    REQUIRE( written.find( "@schema \"cypher.editor_keymap\" 2" ) != std::string::npos ); // Saving writes V2.

    const key_value_t *chain[]{ user.Root(), base.Root() };
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "map.viewport" ), View( "map.tool.clip" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.nChords == 2u );
    REQUIRE( EditorKeymap_ResetBinding( &user.store, View( "map.viewport" ), View( "map.tool.clip" ) ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_FindBinding( chain, 2u, View( "map.viewport" ), View( "map.tool.clip" ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.iSource == 1u );
}

namespace
{

std::string HeldText( const char *pText )
{
    key_stroke_t stroke{};
    if ( !EditorHeldKey_Parse( StringView_FromCString( pText ), &stroke ) ) { return "<invalid>"; }
    char buffer[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    return std::string( buffer, EditorHeldKey_Format( stroke, buffer ) );
}

std::string GestureText( const char *pText )
{
    mouse_gesture_t gesture{};
    if ( !EditorMouseGesture_Parse( StringView_FromCString( pText ), &gesture ) ) { return "<invalid>"; }
    char buffer[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
    return std::string( buffer, EditorMouseGesture_Format( gesture, buffer ) );
}

} // namespace

TEST_CASE( "Held keys and mouse gestures parse to canonical text", "[CypherEditor][Core][Keymap]" )
{
    CHECK( HeldText( "w" ) == "W" );
    CHECK( HeldText( "shift" ) == "Shift" );
    CHECK( HeldText( "Option" ) == "Alt" );
    CHECK( HeldText( "ctrl+W" ) == "Ctrl+W" );
    CHECK( HeldText( "Ctrl+" ) == "<invalid>" );

    CHECK( GestureText( "rightdrag" ) == "RightDrag" );
    CHECK( GestureText( "alt+LeftDrag" ) == "Alt+LeftDrag" );
    CHECK( GestureText( "Space+LeftDrag" ) == "Space+LeftDrag" );
    CHECK( GestureText( "Shift+Ctrl+LeftDoubleClick" ) == "Ctrl+Shift+LeftDoubleClick" );
    CHECK( GestureText( "Ctrl+Wheel" ) == "Ctrl+Wheel" );
    CHECK( GestureText( "WheelUp" ) == "WheelUp" );
    CHECK( GestureText( "MiddlePress" ) == "MiddlePress" );
    CHECK( GestureText( "LeftWiggle" ) == "<invalid>" );
    CHECK( GestureText( "Space+Z+LeftDrag" ) == "<invalid>" ); // One held key at most.
    mouse_gesture_t a{};
    mouse_gesture_t b{};
    REQUIRE( EditorMouseGesture_Parse( StringView_FromCString( "Alt+LeftDrag" ), &a ) );
    REQUIRE( EditorMouseGesture_Parse( StringView_FromCString( "option+leftdrag" ), &b ) );
    CHECK( EditorMouseGesture_Equals( a, b ) );
}

TEST_CASE( "V2 keymaps resolve held, mouse, platform overlays, and context stacks", "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t doc( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"mine\" name = \"Mine\" author = \"Karlo\" description = \"Test.\"\n"
                      "  bindings = { global = { \"edit.delete\" = [ \"Delete\" ] \"file.save\" = [ \"Ctrl+S\" ] }\n"
                      "               map = { \"map.tool.clip\" = [ \"C\" ] }\n"
                      "               \"map.tool.vertex\" = { \"map.vertex.merge\" = [ \"C\" ] } }\n"
                      "  held = { \"map.viewport.3d\" = { \"map.camera.forward\" = [ \"W\", \"Up\" ] \"map.camera.fast\" = [ \"Shift\" ] } }\n"
                      "  mouse = { \"map.viewport.2d\" = { \"map.view.pan\" = [ \"Space+LeftDrag\", \"MiddleDrag\" ] } }\n"
                      "  platforms = { macos = { bindings = { global = { \"edit.delete\" = [ \"Delete\", \"Backspace\" ] } }\n"
                      "                          mouse = { \"map.viewport.2d\" = { \"map.view.pan\" = [ \"RightDrag\" ] } } } } }\n" );
    const key_value_t *chain[]{ doc.Root() };
    const keymap_header_t header = EditorKeymap_Header( doc.Root() );
    CHECK( std::string( header.author.pData, header.author.cchLength ) == "Karlo" );

    keymap_triggers_t held{};
    REQUIRE( EditorKeymap_FindTriggers( chain, 1u, keymap_section_t::HELD, keymap_platform_t::NONE, StringView_FromCString( "map.viewport.3d" ),
                                        StringView_FromCString( "map.camera.forward" ), &held ) == keymap_lookup_t::BOUND );
    CHECK( held.nTexts == 2u );

    // The macOS overlay replaces the pan gesture there and only there.
    keymap_triggers_t pan{};
    REQUIRE( EditorKeymap_FindTriggers( chain, 1u, keymap_section_t::MOUSE, keymap_platform_t::MACOS, StringView_FromCString( "map.viewport.2d" ),
                                        StringView_FromCString( "map.view.pan" ), &pan ) == keymap_lookup_t::BOUND );
    REQUIRE( pan.nTexts == 1u );
    CHECK( std::string( pan.texts[0].pData, pan.texts[0].cchLength ) == "RightDrag" );
    REQUIRE( EditorKeymap_FindTriggers( chain, 1u, keymap_section_t::MOUSE, keymap_platform_t::WINDOWS, StringView_FromCString( "map.viewport.2d" ),
                                        StringView_FromCString( "map.view.pan" ), &pan ) == keymap_lookup_t::BOUND );
    CHECK( pan.nTexts == 2u );

    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBindingOn( chain, 1u, keymap_platform_t::MACOS, StringView_FromCString( "global" ),
                                         StringView_FromCString( "edit.delete" ), &binding ) == keymap_lookup_t::BOUND );
    CHECK( binding.nChords == 2u );
    REQUIRE( EditorKeymap_FindBindingOn( chain, 1u, keymap_platform_t::LINUX, StringView_FromCString( "global" ),
                                         StringView_FromCString( "edit.delete" ), &binding ) == keymap_lookup_t::BOUND );
    CHECK( binding.nChords == 1u );

    // The most specific context wins: C merges vertices in the vertex tool,
    // clips elsewhere; Ctrl+S falls through to global.
    const string_view_t vertexStack[]{ StringView_FromCString( "map.tool.vertex" ), StringView_FromCString( "map.viewport.2d" ),
                                       StringView_FromCString( "map" ), StringView_FromCString( "global" ) };
    const string_view_t plainStack[]{ StringView_FromCString( "map.viewport.2d" ), StringView_FromCString( "map" ), StringView_FromCString( "global" ) };
    const auto command = []( string_view_t view ) { return std::string( view.pData != nullptr ? view.pData : "", view.cchLength ); };
    CHECK( command( EditorKeymap_FindCommandInStack( chain, 1u, keymap_platform_t::NONE, vertexStack, 4u, Chord( "C" ) ) ) == "map.vertex.merge" );
    CHECK( command( EditorKeymap_FindCommandInStack( chain, 1u, keymap_platform_t::NONE, plainStack, 3u, Chord( "C" ) ) ) == "map.tool.clip" );
    CHECK( command( EditorKeymap_FindCommandInStack( chain, 1u, keymap_platform_t::NONE, vertexStack, 4u, Chord( "Ctrl+S" ) ) ) == "file.save" );
    CHECK( EditorKeymap_FindCommandInStack( chain, 1u, keymap_platform_t::NONE, plainStack, 3u, Chord( "Ctrl+J" ) ).cchLength == 0u );
}

TEST_CASE( "Held and mouse triggers are stored canonically and validated", "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t doc( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"mine\" name = \"Mine\" }\n" );
    const string_view_t gestures[]{ StringView_FromCString( "alt+leftdrag" ), StringView_FromCString( "middledrag" ) };
    REQUIRE( EditorKeymap_SetTriggers( &doc.store, keymap_section_t::MOUSE, StringView_FromCString( "map.viewport.3d" ),
                                       StringView_FromCString( "map.camera.orbit" ), gestures, 2u ) == keymap_status_t::OK );
    const key_value_t *chain[]{ doc.Root() };
    keymap_triggers_t triggers{};
    REQUIRE( EditorKeymap_FindTriggers( chain, 1u, keymap_section_t::MOUSE, keymap_platform_t::NONE, StringView_FromCString( "map.viewport.3d" ),
                                        StringView_FromCString( "map.camera.orbit" ), &triggers ) == keymap_lookup_t::BOUND );
    REQUIRE( triggers.nTexts == 2u );
    CHECK( std::string( triggers.texts[0].pData, triggers.texts[0].cchLength ) == "Alt+LeftDrag" );

    const string_view_t bad[]{ StringView_FromCString( "LeftWiggle" ) };
    CHECK( EditorKeymap_SetTriggers( &doc.store, keymap_section_t::MOUSE, StringView_FromCString( "map.viewport.3d" ),
                                     StringView_FromCString( "map.camera.orbit" ), bad, 1u ) == keymap_status_t::INVALID_ARGUMENT );
    const string_view_t held[]{ StringView_FromCString( "shift" ) };
    REQUIRE( EditorKeymap_SetTriggers( &doc.store, keymap_section_t::HELD, StringView_FromCString( "map.viewport.3d" ),
                                       StringView_FromCString( "map.camera.fast" ), held, 1u ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_SetTriggers( &doc.store, keymap_section_t::HELD, StringView_FromCString( "map.viewport.3d" ),
                                       StringView_FromCString( "map.camera.fast" ), nullptr, 0u ) == keymap_status_t::OK );
    CHECK( EditorKeymap_FindTriggers( chain, 1u, keymap_section_t::HELD, keymap_platform_t::NONE, StringView_FromCString( "map.viewport.3d" ),
                                      StringView_FromCString( "map.camera.fast" ), &triggers ) == keymap_lookup_t::UNBOUND );
    REQUIRE( EditorKeymap_SetDetails( &doc.store, StringView_FromCString( "Me" ), StringView_FromCString( "Mine." ) ) == keymap_status_t::OK );
    CHECK( EditorKeymap_Header( doc.Root() ).description.cchLength == 5u );
}

TEST_CASE( "Complete keymaps flatten a chain and resolve like it on every platform", "[CypherEditor][Core][Keymap]" )
{
    keymap_doc_t base( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"base\" name = \"Base\"\n"
                       "  bindings = { global = { \"file.save\" = [ \"Ctrl+S\" ] \"edit.delete\" = [ \"Delete\" ] \"edit.undo\" = [ \"Ctrl+Z\" ] }\n"
                       "               map = { \"map.tool.clip\" = [ \"Shift+X\" ] \"map.tool.vertex\" = [ \"Shift+V\" ] } }\n"
                       "  held = { \"map.viewport.3d\" = { \"map.camera.forward\" = [ \"W\", \"Up\" ] } }\n"
                       "  mouse = { \"map.viewport.2d\" = { \"map.view.pan\" = [ \"MiddleDrag\" ] } }\n"
                       "  platforms = { macos = { bindings = { global = { \"edit.delete\" = [ \"Delete\", \"Backspace\" ] } }\n"
                       "                          mouse = { \"map.viewport.2d\" = { \"map.view.pan\" = [ \"RightDrag\" ] } } } } }\n" );
    // The child overrides edit.delete in its main section, which must also
    // hide the base's macOS overlay; unbinds clip; carries a plugin command;
    // and has an unusable undo entry that falls through to the base.
    keymap_doc_t child( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"child\" name = \"Child\" base = \"base\"\n"
                        "  bindings = { global = { \"file.save\" = [ \"Ctrl+Alt+S\" ] \"edit.delete\" = [ \"Ctrl+Delete\" ] \"edit.undo\" = [ \"Wiggle\" ]\n"
                        "                          \"plugin.do_thing\" = [ \"Ctrl+Alt+P\" ] }\n"
                        "               map = { \"map.tool.clip\" = [] } }\n"
                        "  platforms = { linux = { bindings = { global = { \"file.save\" = [ \"Ctrl+S\" ] } } } } }\n" );
    const key_value_t *library[]{ child.Root(), base.Root() };
    const key_value_t *chain[EDITOR_KEYMAP_MAX_DEPTH]{};
    bool_t bComplete = CY_FALSE;
    REQUIRE( EditorKeymap_BuildChain( library, 2u, child.Root(), chain, EDITOR_KEYMAP_MAX_DEPTH, &bComplete ) == 2u );
    CHECK( bComplete );
    CHECK( chain[1] == base.Root() );

    keymap_doc_t complete( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"draft\" name = \"Draft\" }\n" );
    REQUIRE( EditorKeymap_SetHeader( &complete.store, View( "flat" ), View( "Flat" ), string_view_t{} ) == keymap_status_t::OK );
    REQUIRE( EditorKeymap_WriteComplete( &complete.store, chain, 2u ) == keymap_status_t::OK );
    CHECK( EditorKeymap_Header( complete.Root() ).base.cchLength == 0u );
    const key_value_t *flat[]{ complete.Root() };

    const struct { const char *pContext; const char *pCommand; } kBindings[]{
        { "global", "file.save" }, { "global", "edit.delete" }, { "global", "edit.undo" }, { "global", "plugin.do_thing" },
        { "map", "map.tool.clip" }, { "map", "map.tool.vertex" }, { "global", "not.mentioned" },
    };
    for ( const keymap_platform_t platform : { keymap_platform_t::NONE, keymap_platform_t::MACOS, keymap_platform_t::WINDOWS, keymap_platform_t::LINUX } ) {
        for ( const auto &entry : kBindings ) {
            CAPTURE( static_cast<int>( platform ), entry.pCommand );
            keymap_binding_t expected{};
            keymap_binding_t actual{};
            const keymap_lookup_t expectedLookup = EditorKeymap_FindBindingOn( chain, 2u, platform, View( entry.pContext ), View( entry.pCommand ), &expected );
            REQUIRE( EditorKeymap_FindBindingOn( flat, 1u, platform, View( entry.pContext ), View( entry.pCommand ), &actual ) == expectedLookup );
            REQUIRE( actual.nChords == expected.nChords );
            for ( usize i = 0u; i < actual.nChords; ++i ) { CHECK( EditorKeyChord_Equals( actual.chords[i], expected.chords[i] ) ); }
        }
        keymap_triggers_t expected{};
        keymap_triggers_t actual{};
        REQUIRE( EditorKeymap_FindTriggers( chain, 2u, keymap_section_t::MOUSE, platform, View( "map.viewport.2d" ), View( "map.view.pan" ), &expected ) ==
                 keymap_lookup_t::BOUND );
        REQUIRE( EditorKeymap_FindTriggers( flat, 1u, keymap_section_t::MOUSE, platform, View( "map.viewport.2d" ), View( "map.view.pan" ), &actual ) ==
                 keymap_lookup_t::BOUND );
        REQUIRE( actual.nTexts == expected.nTexts );
        CHECK( StringView_Equals( actual.texts[0], expected.texts[0] ) );
    }
    // The base's catalogue order leads: file.save is still the first global entry.
    const key_value_t *pGlobal = KeyValue_Find( KeyValue_Find( complete.Root(), View( "bindings" ) ), View( "global" ) );
    REQUIRE( pGlobal != nullptr );
    CHECK( StringView_Equals( KeyValue_Name( KeyValue_ChildAt( pGlobal, 0u ) ), View( "file.save" ) ) );
    // The hidden macOS overlay for edit.delete is gone.
    const key_value_t *pMacGlobal = KeyValue_Find(
        KeyValue_Find( KeyValue_Find( KeyValue_Find( complete.Root(), View( "platforms" ) ), View( "macos" ) ), View( "bindings" ) ), View( "global" ) );
    CHECK( KeyValue_Find( pMacGlobal, View( "edit.delete" ) ) == nullptr );

    // Overlay setters address platforms.<os>; reset removes the entry again.
    const string_view_t keys[]{ View( "Meta+Backspace" ) };
    REQUIRE( EditorKeymap_SetTriggersOn( &complete.store, keymap_platform_t::MACOS, keymap_section_t::BINDINGS, View( "global" ), View( "edit.delete" ), keys,
                                         1u ) == keymap_status_t::OK );
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBindingOn( flat, 1u, keymap_platform_t::MACOS, View( "global" ), View( "edit.delete" ), &binding ) == keymap_lookup_t::BOUND );
    CHECK( EditorKeyChord_Equals( binding.chords[0], Chord( "Meta+Backspace" ) ) );
    REQUIRE( EditorKeymap_ResetOn( &complete.store, keymap_platform_t::MACOS, keymap_section_t::BINDINGS, View( "global" ), View( "edit.delete" ) ) ==
             keymap_status_t::OK );
    REQUIRE( EditorKeymap_FindBindingOn( flat, 1u, keymap_platform_t::MACOS, View( "global" ), View( "edit.delete" ), &binding ) == keymap_lookup_t::BOUND );
    CHECK( EditorKeyChord_Equals( binding.chords[0], Chord( "Ctrl+Delete" ) ) );

    // Chains stop at a missing base and at cycles.
    CHECK( EditorKeymap_BuildChain( library, 1u, child.Root(), chain, EDITOR_KEYMAP_MAX_DEPTH, &bComplete ) == 1u );
    CHECK_FALSE( bComplete );
    keymap_doc_t loop( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n{ id = \"loop\" name = \"Loop\" base = \"loop\" }\n" );
    const key_value_t *loopLibrary[]{ loop.Root() };
    CHECK( EditorKeymap_BuildChain( loopLibrary, 1u, loop.Root(), chain, EDITOR_KEYMAP_MAX_DEPTH, &bComplete ) == 1u );
    CHECK_FALSE( bComplete );
    CHECK( EditorKeymap_SetHeader( &complete.store, View( "Bad ID" ), View( "Bad" ), string_view_t{} ) == keymap_status_t::INVALID_ARGUMENT );
}
