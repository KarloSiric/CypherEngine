//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Application_Tests.cpp
//  Purpose: Contract tests for the editor application context and the
//           built-in resources it loads.
//  Details: The built-in theme, keymap, and layouts are ordinary documents
//           read through the public decoders, so a typo in one is a build
//           defect these tests catch before anyone launches the editor.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Application.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

TEST_CASE( "The framework initialises from its built-in resources", "[editor][gui][application]" )
{
    QApplication *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
    REQUIRE( pApplication != nullptr );
    editor_gui_t gui{};
    REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
    CHECK( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::INVALID_ARGUMENT );
    CHECK( gui.nThemeChain == 1u );
    CHECK( gui.style.nInvalidSkipped == 0u );
    CHECK_FALSE( pApplication->styleSheet().isEmpty() );

    // The built-in theme names only registered tokens with valid values.
    theme_problem_t problems[4]{};
    CHECK( EditorTheme_Audit( &gui.themeTokens, gui.themeChain[0], problems, 4u ) == 0u );
    const theme_header_t header = EditorTheme_Header( gui.themeChain[0] );
    CHECK( std::string( header.id.pData, header.id.cchLength ) == "charcoal" );

    // The built-in keymap binds save to Ctrl+S and has no conflicts.
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBinding( gui.keymapChain, gui.nKeymapChain, StringView_FromCString( EDITOR_KEYMAP_CONTEXT_GLOBAL ),
                                       StringView_FromCString( "file.save" ), &binding ) == keymap_lookup_t::BOUND );
    char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    REQUIRE( binding.nChords == 1u );
    CHECK( std::string( text, EditorKeyChord_Format( binding.chords[0], text ) ) == "Ctrl+S" );
    keymap_conflict_t conflicts[4]{};
    CHECK( EditorKeymap_FindConflicts( gui.keymapChain, gui.nKeymapChain, StringView_FromCString( EDITOR_KEYMAP_CONTEXT_GLOBAL ), conflicts, 4u ) == 0u );
    EditorGui_Shutdown( &gui );
    CHECK_FALSE( gui.bInitialized );
}

TEST_CASE( "The built-in Mason layout decodes", "[editor][gui][application]" )
{
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    REQUIRE( EditorGui_LoadLayoutResource( ":/cypher/editor/layouts/mason_default.cylayout", &layout, Allocator_GetSystem() ) ==
             editor_gui_status_t::OK );
    CHECK( layout.bHasRoot );
    CHECK( Vector_Count( &layout.panels ) == 11u ); // Hammer 5's panels, Undo History, console, output, problems; assets open in a window.
    CHECK( Vector_Count( &layout.hidden ) == 0u );
    CHECK( EditorGui_LoadLayoutResource( ":/cypher/editor/layouts/missing.cylayout", &layout, Allocator_GetSystem() ) ==
           editor_gui_status_t::RESOURCE_MISSING );
    EditorLayout_Shutdown( &layout );
}

TEST_CASE( "The built-in keymap parses completely and has no conflicts within a context", "[editor][gui][application]" )
{
    EditorGui_RegisterResources();
    settings_document_t keymap{};
    REQUIRE( SettingsDocument_Init( &keymap, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( EditorGui_LoadResource( EDITOR_BUILTIN_KEYMAP_RESOURCE, &keymap ) == editor_gui_status_t::OK );
    const key_value_t *pRoot = SettingsDocument_Root( &keymap );
    const key_value_t *chain[]{ pRoot };
    usize nCommands = 0u;
    const struct { const char *pSection; keymap_section_t section; } sections[]{
        { "bindings", keymap_section_t::BINDINGS }, { "held", keymap_section_t::HELD }, { "mouse", keymap_section_t::MOUSE } };
    for ( const auto &entry : sections ) {
        const key_value_t *pSection = KeyValue_Find( pRoot, StringView_FromCString( entry.pSection ) );
        REQUIRE( pSection != nullptr );
        for ( usize iContext = 0u; iContext < KeyValue_ChildCount( pSection ); ++iContext ) {
            const key_value_t *pContext = KeyValue_ChildAt( pSection, iContext );
            const string_view_t context = KeyValue_Name( pContext );
            for ( usize iId = 0u; iId < KeyValue_ChildCount( pContext ); ++iId ) {
                const string_view_t id = KeyValue_Name( KeyValue_ChildAt( pContext, iId ) );
                INFO( std::string( id.pData, id.cchLength ) );
                keymap_triggers_t triggers{};
                REQUIRE( EditorKeymap_FindTriggers( chain, 1u, entry.section, keymap_platform_t::NONE, context, id, &triggers ) !=
                         keymap_lookup_t::NOT_DEFINED );
                for ( usize i = 0u; i < triggers.nTexts; ++i ) {
                    INFO( std::string( triggers.texts[i].pData, triggers.texts[i].cchLength ) );
                    key_chord_t chord{};
                    key_stroke_t stroke{};
                    mouse_gesture_t gesture{};
                    const bool bParsed = entry.section == keymap_section_t::BINDINGS ? EditorKeyChord_Parse( triggers.texts[i], &chord )
                                         : entry.section == keymap_section_t::HELD ? EditorHeldKey_Parse( triggers.texts[i], &stroke )
                                                                                    : EditorMouseGesture_Parse( triggers.texts[i], &gesture );
                    CHECK( bParsed );
                }
                ++nCommands;
            }
            if ( entry.section == keymap_section_t::BINDINGS ) {
                keymap_conflict_t conflicts[4]{};
                const usize nConflicts = EditorKeymap_FindConflicts( chain, 1u, context, conflicts, 4u );
                INFO( std::string( context.pData, context.cchLength ) );
                CHECK( nConflicts == 0u );
            }
        }
    }
    // The unavailable Z mouselook toggle is omitted so it cannot reserve Z.
    CHECK( nCommands == 192u ); // 171 bindings (including camera speed/framing), 8 held, 13 mouse actions.
}

namespace
{

void CountStyle( void *pContext ) noexcept
{
    ++*static_cast<int *>( pContext );
}

struct allocation_audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *context, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<allocation_audit_t *>( context );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *pMemory = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( pMemory != nullptr ) { audit.bytes += size; }
            return pMemory;
        };
        allocator.pfnFree = []( void *context, void *pMemory, usize size, usize alignment ) noexcept {
            if ( pMemory != nullptr ) { static_cast<allocation_audit_t *>( context )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), pMemory, size, alignment );
        };
    }
};

struct gui_fixture_t {
    explicit gui_fixture_t( const allocator_t *pAllocator = Allocator_GetSystem() )
    {
        auto *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( EditorGui_Init( &gui, pApplication, pAllocator ) == editor_gui_status_t::OK );
    }
    editor_gui_t gui{};
};

QString ViewText( string_view_t text )
{
    return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
}

QString GlobalChord( const editor_gui_t &gui, const char *command )
{
    keymap_binding_t binding{};
    REQUIRE( EditorKeymap_FindBinding( gui.keymapChain, gui.nKeymapChain, StringView_FromCString( "global" ),
                                     StringView_FromCString( command ), &binding ) == keymap_lookup_t::BOUND );
    REQUIRE( binding.nChords == 1u );
    char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    return QString::fromLatin1( text, static_cast<qsizetype>( EditorKeyChord_Format( binding.chords[0], text ) ) );
}

struct keymap_snapshot_t {
    explicit keymap_snapshot_t( const editor_gui_t &gui )
        : activeId( ViewText( EditorGui_ActiveKeymapId( &gui ) ) ), saveChord( GlobalChord( gui, "file.save" ) ),
          openChord( GlobalChord( gui, "file.open" ) ), ids( EditorGui_KeymapIds( &gui ) ),
          pOwnedStorage( gui.keymaps.pData ), pLibraryStorage( gui.keymapLibrary.pData ), nListeners( gui.nKeymapListeners )
    {
        for ( usize i = 0u; i < gui.keymaps.nCount; ++i ) {
            owners.push_back( gui.keymaps.pData[i] );
            ownedRoots.push_back( SettingsDocument_Root( gui.keymaps.pData[i] ) );
        }
        for ( usize i = 0u; i < gui.keymapLibrary.nCount; ++i ) { library.push_back( gui.keymapLibrary.pData[i] ); }
        for ( usize i = 0u; i < gui.nKeymapChain; ++i ) { chain.push_back( gui.keymapChain[i] ); }
    }
    void CheckPreserved( const editor_gui_t &gui ) const
    {
        CHECK( ViewText( EditorGui_ActiveKeymapId( &gui ) ) == activeId );
        CHECK( EditorGui_KeymapIds( &gui ) == ids );
        CHECK( GlobalChord( gui, "file.save" ) == saveChord );
        CHECK( GlobalChord( gui, "file.open" ) == openChord );
        CHECK( gui.keymaps.pData == pOwnedStorage );
        CHECK( gui.keymapLibrary.pData == pLibraryStorage );
        REQUIRE( gui.keymaps.nCount == owners.size() );
        REQUIRE( gui.keymapLibrary.nCount == library.size() );
        REQUIRE( gui.nKeymapChain == chain.size() );
        for ( usize i = 0u; i < owners.size(); ++i ) {
            CHECK( gui.keymaps.pData[i] == owners[i] );
            // The library must still point into its live document owners.
            CHECK( SettingsDocument_Root( gui.keymaps.pData[i] ) == ownedRoots[i] );
        }
        for ( usize i = 0u; i < library.size(); ++i ) { CHECK( gui.keymapLibrary.pData[i] == library[i] ); }
        for ( usize i = 0u; i < chain.size(); ++i ) { CHECK( gui.keymapChain[i] == chain[i] ); }
        CHECK( gui.nKeymapListeners == nListeners );
    }
    QString activeId, saveChord, openChord;
    QStringList ids;
    settings_document_t *const *pOwnedStorage{};
    const key_value_t *const *pLibraryStorage{};
    usize nListeners{};
    std::vector<settings_document_t *> owners;
    std::vector<const key_value_t *> ownedRoots, library, chain;
};

constexpr const char *kBaseKeymap = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "atomic_base" base = "cypher_default" bindings = { global = { "file.save" = [ "F2" ] } } }
)cykv";
constexpr const char *kActiveKeymap = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "atomic_active" base = "atomic_base" bindings = { global = { "file.open" = [ "F1" ] } } }
)cykv";

void LoadAtomicKeymaps( editor_gui_t &gui )
{
    REQUIRE( EditorGui_AddKeymap( &gui, QString::fromUtf8( kBaseKeymap ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorGui_AddKeymap( &gui, QString::fromUtf8( kActiveKeymap ) ) == editor_gui_status_t::OK );
    REQUIRE( EditorGui_SelectKeymap( &gui, StringView_FromCString( "atomic_active" ) ) == editor_gui_status_t::OK );
}

void WriteFile( const QString &path, const QByteArray &bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    file.close();
}

QByteArray ReadFile( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

struct keymap_commit_probe_t {
    editor_gui_t *pGui{};
    QString path, expectedId;
    QByteArray expectedBytes;
    usize nChanged{};
    bool committedBeforeNotify{ false };
    static void Changed( void *context ) noexcept
    {
        auto &probe = *static_cast<keymap_commit_probe_t *>( context );
        ++probe.nChanged;
        QFile file( probe.path );
        probe.committedBeforeNotify = file.open( QIODevice::ReadOnly ) && file.readAll() == probe.expectedBytes &&
            ViewText( EditorGui_ActiveKeymapId( probe.pGui ) ) == probe.expectedId;
    }
};

} // namespace

TEST_CASE( "Themes are added, selected, replaced, and previewed", "[editor][gui][application]" )
{
    QApplication *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
    editor_gui_t gui{};
    REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
    int nChanged = 0;
    REQUIRE( EditorGui_AddStyleListener( &gui, &CountStyle, &nChanged ) );
    CHECK( std::string( EditorGui_ActiveThemeId( &gui ).pData, 8u ) == "charcoal" );

    QString id;
    REQUIRE( EditorGui_AddTheme( &gui, QStringLiteral( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"midnight\" name = \"Midnight\" base = \"charcoal\"\n"
                                                       "  colors = { \"ui.accent\" = \"#4aa3ff\" } }\n" ), &id ) == editor_gui_status_t::OK );
    CHECK( id == QStringLiteral( "midnight" ) );
    CHECK( EditorGui_ThemeIds( &gui ).contains( QStringLiteral( "midnight" ) ) );
    REQUIRE( EditorGui_SelectTheme( &gui, pApplication, StringView_FromCString( "midnight" ) ) == editor_gui_status_t::OK );
    CHECK( gui.style.colors[STYLE_COLOR_ACCENT] == 0x4AA3FFFFu );
    CHECK( gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x3C3C3CFFu ); // From the charcoal base.
    CHECK( nChanged == 1 );
    CHECK( EditorGui_SelectTheme( &gui, pApplication, StringView_FromCString( "nope" ) ) == editor_gui_status_t::RESOURCE_MISSING );

    // Adding a theme with the same ID replaces it, and the active chain moves
    // to the new document.
    REQUIRE( EditorGui_AddTheme( &gui, QStringLiteral( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"midnight\" name = \"Midnight 2\" base = \"charcoal\"\n"
                                                       "  colors = { \"ui.accent\" = \"#ff0000\" } }\n" ) ) == editor_gui_status_t::OK );
    CHECK( std::string( EditorGui_ActiveThemeId( &gui ).pData, 8u ) == "midnight" );
    REQUIRE( EditorGui_SelectTheme( &gui, pApplication, StringView_FromCString( "midnight" ) ) == editor_gui_status_t::OK );
    CHECK( gui.style.colors[STYLE_COLOR_ACCENT] == 0xFF0000FFu );
    CHECK( EditorGui_AddTheme( &gui, QStringLiteral( "not a theme" ) ) == editor_gui_status_t::INVALID_ARGUMENT );

    // A working copy previews without joining the library.
    settings_document_t working{};
    REQUIRE( SettingsDocument_Init( &working, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &working, StringView_FromCString( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"draft\" name = \"Draft\" base = \"midnight\"\n"
                                                                      "  colors = { \"ui.background\" = \"#101010\" } }\n" ) )
                 .status == settings_document_status_t::OK );
    REQUIRE( EditorGui_PreviewTheme( &gui, pApplication, SettingsDocument_Root( &working ) ) == editor_gui_status_t::OK );
    CHECK( gui.style.colors[STYLE_COLOR_BACKGROUND] == 0x101010FFu );
    CHECK( gui.style.colors[STYLE_COLOR_ACCENT] == 0xFF0000FFu ); // Through the midnight base.
    CHECK_FALSE( EditorGui_ThemeIds( &gui ).contains( QStringLiteral( "draft" ) ) );
    REQUIRE( EditorGui_SelectTheme( &gui, pApplication, StringView_FromCString( "charcoal" ) ) == editor_gui_status_t::OK );
    EditorGui_RemoveStyleListener( &gui, &CountStyle, &nChanged );
    SettingsDocument_Shutdown( &working );
}

TEST_CASE( "Replacing a preview keymap base preserves the actual borrowed draft", "[editor][gui][application][keymap][preview]" )
{
    QString draftId;
    SECTION( "The preview has an ID absent from the owned library" ) { draftId = QStringLiteral( "preview_draft" ); }
    SECTION( "The preview ID aliases a different owned keymap" ) { draftId = QStringLiteral( "atomic_active" ); }
    REQUIRE_FALSE( draftId.isEmpty() );
    allocation_audit_t audit;
    {
        // The borrowed working copy outlives the GUI's active preview chain.
        settings_document_t working{};
        gui_fixture_t session( &audit.allocator );
        auto &gui = session.gui;
        LoadAtomicKeymaps( gui );
        REQUIRE( SettingsDocument_Init( &working, &audit.allocator, EditorKeymap_Identity() ) == settings_document_status_t::OK );
        const QByteArray draft = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
            "{ id = \"%1\" base = \"atomic_base\" bindings = { global = { \"file.open\" = [ \"F9\" ] } } }\n" ).arg( draftId ).toUtf8();
        REQUIRE( SettingsDocument_Load( &working, { draft.constData(), static_cast<usize>( draft.size() ) } ).status == settings_document_status_t::OK );
        const key_value_t *pDraft = SettingsDocument_Root( &working );
        REQUIRE( EditorGui_PreviewKeymap( &gui, pDraft ) == editor_gui_status_t::OK );
        REQUIRE( gui.nKeymapChain == 3u );
        REQUIRE( gui.keymapChain[0] == pDraft );
        const key_value_t *pPreviousBase = gui.keymapChain[1];
        const auto *pOwnedChild = gui.keymaps.pData[1];
        CHECK( GlobalChord( gui, "file.open" ) == QStringLiteral( "F9" ) );
        CHECK( GlobalChord( gui, "file.save" ) == QStringLiteral( "F2" ) );
        int changed = 0;
        REQUIRE( EditorGui_AddKeymapListener( &gui, &CountStyle, &changed ) );
        const QString replacement = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
            "{ id = \"atomic_base\" base = \"cypher_default\" bindings = { global = { \"file.save\" = [ \"F3\" ] } } }\n" );
        REQUIRE( EditorGui_AddKeymap( &gui, replacement ) == editor_gui_status_t::OK );
        REQUIRE( gui.nKeymapChain == 3u );
        CHECK( gui.keymapChain[0] == pDraft );
        CHECK( gui.keymapChain[1] != pPreviousBase );
        CHECK( ViewText( EditorGui_ActiveKeymapId( &gui ) ) == draftId );
        CHECK( GlobalChord( gui, "file.open" ) == QStringLiteral( "F9" ) ); // The draft overrides the stored alias's F1.
        CHECK( GlobalChord( gui, "file.save" ) == QStringLiteral( "F3" ) );
        CHECK( changed == 1 );
        REQUIRE( gui.keymaps.nCount == 2u );
        REQUIRE( gui.keymapLibrary.nCount == 3u );
        CHECK( gui.keymaps.pData[0] == pOwnedChild );
        for ( usize i = 0u; i < gui.keymaps.nCount; ++i ) {
            CHECK( gui.keymaps.pData[i] != &working );
            CHECK( SettingsDocument_Root( gui.keymaps.pData[i] ) != pDraft );
        }
        for ( usize i = 0u; i < gui.keymapLibrary.nCount; ++i ) { CHECK( gui.keymapLibrary.pData[i] != pDraft ); }
        CHECK( EditorGui_KeymapIds( &gui ).contains( draftId ) == ( draftId == QStringLiteral( "atomic_active" ) ) );
        EditorGui_RemoveKeymapListener( &gui, &CountStyle, &changed );
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Replacing an active keymap or its base is atomic at every allocation", "[editor][gui][application][keymap][allocation]" )
{
    const char *pOldText = nullptr;
    QString replacement;
    SECTION( "The active document is replaced without changing its inherited base" ) {
        pOldText = kActiveKeymap;
        replacement = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
            "{ id = \"atomic_active\" base = \"atomic_base\" bindings = { global = { \"file.open\" = [ \"F3\" ] } } }\n" );
    }
    SECTION( "Replacing an active base retains the active child and its own shortcuts" ) {
        pOldText = kBaseKeymap;
        replacement = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
            "{ id = \"atomic_base\" base = \"cypher_default\" bindings = { global = { \"file.save\" = [ \"F3\" ] } } }\n" );
    }
    REQUIRE( pOldText != nullptr );
    allocation_audit_t audit;
    {
        gui_fixture_t session( &audit.allocator );
        auto &gui = session.gui;
        LoadAtomicKeymaps( gui );
        int changed = 0;
        REQUIRE( EditorGui_AddKeymapListener( &gui, &CountStyle, &changed ) );
        audit.calls = 0u;
        REQUIRE( EditorGui_AddKeymap( &gui, replacement ) == editor_gui_status_t::OK );
        const usize nAllocations = audit.calls;
        REQUIRE( nAllocations > 0u );
        CHECK( changed == 1 );
        CHECK( ViewText( EditorGui_ActiveKeymapId( &gui ) ) == QStringLiteral( "atomic_active" ) );
        CHECK( GlobalChord( gui, pOldText == kBaseKeymap ? "file.save" : "file.open" ) == QStringLiteral( "F3" ) );
        REQUIRE( EditorGui_AddKeymap( &gui, QString::fromUtf8( pOldText ) ) == editor_gui_status_t::OK );
        changed = 0;
        const keymap_snapshot_t snapshot( gui );
        const usize baselineBytes = audit.bytes;
        for ( usize failure = 0u; failure < nAllocations; ++failure ) {
            CAPTURE( failure, nAllocations );
            audit.calls = 0u;
            audit.failAt = failure;
            const auto status = EditorGui_AddKeymap( &gui, replacement );
            audit.failAt = CY_USIZE_MAX;
            CHECK( status == editor_gui_status_t::OUT_OF_MEMORY );
            snapshot.CheckPreserved( gui );
            CHECK( changed == 0 );
            CHECK( audit.bytes == baselineBytes );
        }
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Saving keymaps commits the file before publishing and allocation failures preserve both", "[editor][gui][application][keymap][save][allocation]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "authoring.cykeymap" ) );
    const QByteArray oldBytes( kActiveKeymap );
    const QString replacement = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
        "{ id = \"atomic_active\" base = \"atomic_base\" bindings = { global = { \"file.open\" = [ \"F5\" ] } } }\n" );
    allocation_audit_t audit;
    {
        gui_fixture_t session( &audit.allocator );
        auto &gui = session.gui;
        LoadAtomicKeymaps( gui );
        // Save must activate the saved document, even when another keymap is active.
        REQUIRE( EditorGui_SelectKeymap( &gui, StringView_FromCString( "cypher_default" ) ) == editor_gui_status_t::OK );
        WriteFile( path, oldBytes );
        keymap_commit_probe_t probe{ &gui, path, QStringLiteral( "atomic_active" ), replacement.toUtf8() };
        REQUIRE( EditorGui_AddKeymapListener( &gui, &keymap_commit_probe_t::Changed, &probe ) );
        QString error = QStringLiteral( "previous error" );
        audit.calls = 0u;
        REQUIRE( EditorGui_SaveKeymap( &gui, replacement, path, &error ) == editor_gui_status_t::OK );
        const usize nAllocations = audit.calls;
        REQUIRE( nAllocations > 0u );
        CHECK( error.isEmpty() );
        CHECK( ReadFile( path ) == replacement.toUtf8() );
        CHECK( ViewText( EditorGui_ActiveKeymapId( &gui ) ) == QStringLiteral( "atomic_active" ) );
        CHECK( GlobalChord( gui, "file.open" ) == QStringLiteral( "F5" ) );
        CHECK( GlobalChord( gui, "file.save" ) == QStringLiteral( "F2" ) );
        CHECK( probe.nChanged == 1u );
        CHECK( probe.committedBeforeNotify );
        REQUIRE( gui.nKeymapListeners == 1u );

        REQUIRE( EditorGui_AddKeymap( &gui, QString::fromUtf8( kActiveKeymap ) ) == editor_gui_status_t::OK );
        REQUIRE( EditorGui_SelectKeymap( &gui, StringView_FromCString( "cypher_default" ) ) == editor_gui_status_t::OK );
        WriteFile( path, oldBytes );
        probe.nChanged = 0u;
        const keymap_snapshot_t snapshot( gui );
        const usize baselineBytes = audit.bytes;
        for ( usize failure = 0u; failure < nAllocations; ++failure ) {
            CAPTURE( failure, nAllocations );
            audit.calls = 0u;
            audit.failAt = failure;
            const auto status = EditorGui_SaveKeymap( &gui, replacement, path, &error );
            audit.failAt = CY_USIZE_MAX;
            CHECK( status == editor_gui_status_t::OUT_OF_MEMORY );
            CHECK_FALSE( error.isEmpty() );
            snapshot.CheckPreserved( gui );
            CHECK( ReadFile( path ) == oldBytes );
            CHECK( probe.nChanged == 0u );
            CHECK( audit.bytes == baselineBytes );
        }
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Failed keymap saves leave existing files and active bindings untouched", "[editor][gui][application][keymap][save]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "authoring.cykeymap" ) );
    const QByteArray oldBytes( kActiveKeymap );
    WriteFile( path, oldBytes );
    gui_fixture_t session;
    auto &gui = session.gui;
    LoadAtomicKeymaps( gui );
    int changed = 0;
    REQUIRE( EditorGui_AddKeymapListener( &gui, &CountStyle, &changed ) );
    const keymap_snapshot_t snapshot( gui );
    QString error;
    SECTION( "The destination cannot be opened" ) {
        // A regular file cannot be the parent directory of the destination.
        // This fails deterministically regardless of account permissions.
        const QString blockedPath = path + QStringLiteral( "/replacement.cykeymap" );
        CHECK( EditorGui_SaveKeymap( &gui, QString::fromUtf8( kActiveKeymap ), blockedPath, &error ) == editor_gui_status_t::IO_ERROR );
        CHECK_FALSE( error.isEmpty() );
        CHECK( EditorGui_SaveKeymap( &gui, QString::fromUtf8( kActiveKeymap ), blockedPath ) == editor_gui_status_t::IO_ERROR );
    }
    SECTION( "Invalid text is rejected before touching the destination" ) {
        CHECK( EditorGui_SaveKeymap( &gui, QStringLiteral( "not a keymap" ), path, &error ) == editor_gui_status_t::INVALID_ARGUMENT );
        CHECK_FALSE( error.isEmpty() );
    }
    SECTION( "A save requires a complete base chain" ) {
        const QString incomplete = QStringLiteral( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n"
            "{ id = \"atomic_active\" base = \"missing_base\" bindings = { global = { \"file.open\" = [ \"F7\" ] } } }\n" );
        CHECK( EditorGui_SaveKeymap( &gui, incomplete, path, &error ) == editor_gui_status_t::INVALID_ARGUMENT );
        CHECK_FALSE( error.isEmpty() );
    }
    snapshot.CheckPreserved( gui );
    CHECK( ReadFile( path ) == oldBytes );
    CHECK( changed == 0 );
}
