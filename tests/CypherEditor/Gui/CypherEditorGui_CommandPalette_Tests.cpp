//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandPalette_Tests.cpp
//  Purpose: Contract tests for the command palette: ranking, running,
//           disabled commands, console-only commands, and recent order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Application.h"
#include "CypherEditorGui_CommandPalette.h"
#include "CypherEditor_SettingsRegistry.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QImage>
#include <QListWidget>
#include <QMainWindow>
#include <QPixmap>

#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

struct counts_t {
    int grid{ 0 };
    int save{ 0 };
};

command_result_t CountGrid( void *pContext, const command_args_t & ) noexcept
{
    ++static_cast<counts_t *>( pContext )->grid;
    return command_result_t::OK;
}

command_result_t CountSave( void *pContext, const command_args_t & ) noexcept
{
    ++static_cast<counts_t *>( pContext )->save;
    return command_result_t::OK;
}

command_result_t Nothing( void *, const command_args_t & ) noexcept { return command_result_t::OK; }
u32 Disabled( void * ) noexcept { return COMMAND_STATE_NONE; }

struct fixture_t {
    counts_t counts{};
    command_registry_t registry{};
    settings_registry_t settings{};
    settings_document_t user{};
    editor_style_t style{};
    QMainWindow window{};
    editor_actions_t actions{};
    QWidget *pPalette{ nullptr };

    explicit fixture_t( bool preferences = false )
    {
        EditorGui_RegisterResources();
        REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
        const command_desc_t commands[]{
            { "map.grid.show", "Show Grid", "Show the grid.", nullptr, nullptr, COMMAND_FLAG_NONE, CountGrid, nullptr, &counts },
            { "file.save", "Save", "Save the map.", nullptr, nullptr, COMMAND_FLAG_NONE, CountSave, nullptr, &counts },
            { "map.run", "Run Map", "Not available yet.", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, Disabled, nullptr },
            { "map.grid", "Set Grid Size", "Console only.", nullptr, "map.grid <size>", COMMAND_FLAG_CONSOLE_ONLY, Nothing, nullptr, nullptr },
            { "paging.sets", "Paging Sets", "A decoy for ranking.", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        };
        REQUIRE( EditorCommands_Register( &registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
        window.resize( 1000, 700 );
        window.show();
        EditorActions_Init( &actions, &registry, &style, &window );
        if ( preferences ) {
            REQUIRE( EditorSettings_Init( &settings, Allocator_GetSystem() ) == settings_registry_status_t::OK );
            usize count = 0u;
            const setting_descriptor_t *pCatalogue = EditorSettings_FrameworkCatalogue( &count );
            REQUIRE( EditorSettings_Register( &settings, pCatalogue, count ) == settings_registry_status_t::OK );
            REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
            EditorSettings_SetScope( &settings, settings_scope_t::USER, &user );
        }
        pPalette = EditorCommandPalette_Create( &window, &actions, preferences ? &settings : nullptr );
        EditorCommandPalette_Open( pPalette );
        QCoreApplication::processEvents();
    }
    ~fixture_t()
    {
        delete pPalette;
        EditorCommands_Shutdown( &registry );
        EditorSettings_Shutdown( &settings );
    }

    void IntegerPreference( const char *pPath, i64 value )
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( &settings, StringView_FromCString( pPath ) );
        REQUIRE( pDescriptor != nullptr );
        setting_value_t setting{};
        setting.type = setting_type_t::INTEGER;
        setting.nValue = value;
        REQUIRE( EditorSettings_Write( &settings, settings_scope_t::USER, *pDescriptor, setting ) == settings_registry_status_t::OK );
    }

    QListWidget *List() const
    {
        QListWidget *pList = pPalette->findChild<QListWidget *>( QStringLiteral( "EditorCommandPaletteList" ) );
        REQUIRE( pList != nullptr );
        return pList;
    }
};

} // namespace

TEST_CASE( "The palette ranks commands by fuzzy match and hides console-only ones", "[editor][gui][palette]" )
{
    fixture_t f;
    const QStringList all = EditorCommandPalette_Results( f.pPalette );
    CHECK( all.size() == 4 ); // Every command except the console-only one.
    CHECK_FALSE( all.contains( QStringLiteral( "map.grid" ) ) );

    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "sg" ) );
    const QStringList ranked = EditorCommandPalette_Results( f.pPalette );
    REQUIRE_FALSE( ranked.isEmpty() );
    CHECK( ranked.front() == QStringLiteral( "map.grid.show" ) );
    // IDs match too.
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "file.sa" ) );
    REQUIRE_FALSE( EditorCommandPalette_Results( f.pPalette ).isEmpty() );
    CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QStringLiteral( "file.save" ) );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "zzz" ) );
    CHECK( EditorCommandPalette_Results( f.pPalette ).isEmpty() );
    CHECK_FALSE( EditorCommandPalette_Accept( f.pPalette ) );
}

TEST_CASE( "Accepting runs the command; disabled commands are listed but do not run", "[editor][gui][palette]" )
{
    fixture_t f;
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "show grid" ) );
    REQUIRE( EditorCommandPalette_Accept( f.pPalette ) );
    CHECK( f.counts.grid == 1 );
    CHECK_FALSE( f.pPalette->isVisible() );

    EditorCommandPalette_Open( f.pPalette );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "run map" ) );
    REQUIRE_FALSE( EditorCommandPalette_Results( f.pPalette ).isEmpty() );
    CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QStringLiteral( "map.run" ) );
    CHECK_FALSE( EditorCommandPalette_Accept( f.pPalette ) );

    // What the palette ran last comes first when the box is empty.
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "save" ) );
    REQUIRE( EditorCommandPalette_Accept( f.pPalette ) );
    CHECK( f.counts.save == 1 );
    EditorCommandPalette_Open( f.pPalette );
    const QStringList recent = EditorCommandPalette_Results( f.pPalette );
    REQUIRE( recent.size() >= 2 );
    CHECK( recent[0] == QStringLiteral( "file.save" ) );
    CHECK( recent[1] == QStringLiteral( "map.grid.show" ) );
}

TEST_CASE( "Palette-only commands resolve semantic icons without creating actions", "[editor][gui][palette][icons]" )
{
    fixture_t f;
    const command_desc_t commands[]{
        { "map.mesh.bevel", "Bevel", "Needs no menu action.", "mesh-bevel", nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "map.tool.rotate", "Rotate", "An obsolete icon name should not leave a blank.", "missing-tool-icon", nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "file.exit", "Exit", "An unadorned descriptor uses the close glyph.", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "help.about", "About", "An unadorned descriptor uses help.", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "map.view.center_selection_3d", "Center Selection", "An unadorned framing command.", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
    };
    REQUIRE( EditorCommands_Register( &f.registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
    EditorCommandPalette_Open( f.pPalette );
    struct expected_t { const char *pId; const char *pIcon; };
    constexpr expected_t expected[]{
        { "map.mesh.bevel", "mesh-bevel" }, { "map.tool.rotate", "tool-rotate" },
        { "file.exit", "file-close" }, { "help.about", "help" }, { "map.view.center_selection_3d", "view-frame" },
        { "map.run", "map-run" },
    };
    for ( const expected_t &entry : expected ) {
        INFO( entry.pId );
        EditorCommandPalette_SetQuery( f.pPalette, QString::fromLatin1( entry.pId ) );
        REQUIRE_FALSE( EditorCommandPalette_Results( f.pPalette ).isEmpty() );
        CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QString::fromLatin1( entry.pId ) );
        const QPixmap icon = f.List()->item( 0 )->icon().pixmap( 24, 24 );
        REQUIRE_FALSE( icon.isNull() );
        CHECK( icon.toImage() == EditorStyle_Icon( f.style, entry.pIcon ).pixmap( 24, 24 ).toImage() );
        CHECK_FALSE( f.actions.actions.contains( QString::fromLatin1( entry.pId ) ) );
    }
    // The fallback also decorates an unknown plug-in category.
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "paging.sets" ) );
    REQUIRE_FALSE( f.List()->item( 0 )->icon().pixmap( 24, 24 ).isNull() );
    // A caller's explicitly assigned icon wins over inferred semantics.
    QAction *pAction = EditorActions_Get( &f.actions, "file.save" );
    REQUIRE( pAction != nullptr );
    pAction->setIcon( EditorStyle_Icon( f.style, "asset-prefab" ) );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "file.save" ) );
    CHECK( f.List()->item( 0 )->icon().cacheKey() == pAction->icon().cacheKey() );
    // A nonrenderable action icon still recovers the descriptor/semantic SVG.
    pAction->setIcon( QIcon( QStringLiteral( ":/no-such-icon.svg" ) ) );
    EditorCommandPalette_Open( f.pPalette );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "file.save" ) );
    CHECK( f.List()->item( 0 )->icon().pixmap( 24, 24 ).toImage() == EditorStyle_Icon( f.style, "file-save" ).pixmap( 24, 24 ).toImage() );
}

TEST_CASE( "Palette preferences filter unavailable commands and resize visible results on refill", "[editor][gui][palette][settings]" )
{
    fixture_t f( true );
    const setting_descriptor_t *pShow = EditorSettings_Find( &f.settings, StringView_FromCString( "editor.ui.command_palette_show_unavailable" ) );
    REQUIRE( pShow != nullptr );
    setting_value_t setting{};
    setting.type = setting_type_t::BOOL;
    setting.bValue = CY_FALSE;
    REQUIRE( EditorSettings_Write( &f.settings, settings_scope_t::USER, *pShow, setting ) == settings_registry_status_t::OK );
    EditorCommandPalette_Open( f.pPalette );
    CHECK_FALSE( EditorCommandPalette_Results( f.pPalette ).contains( QStringLiteral( "map.run" ) ) );
    setting.bValue = CY_TRUE;
    REQUIRE( EditorSettings_Write( &f.settings, settings_scope_t::USER, *pShow, setting ) == settings_registry_status_t::OK );
    EditorCommandPalette_Open( f.pPalette );
    CHECK( EditorCommandPalette_Results( f.pPalette ).contains( QStringLiteral( "map.run" ) ) );

    // Enough real rows to distinguish four visible results from ten without
    // changing how many commands can be searched or executed.
    const command_desc_t commands[]{
        { "extra.one", "Extra One", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "extra.two", "Extra Two", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "extra.three", "Extra Three", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "extra.four", "Extra Four", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "extra.five", "Extra Five", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
        { "extra.six", "Extra Six", "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr },
    };
    REQUIRE( EditorCommands_Register( &f.registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
    f.IntegerPreference( "editor.ui.command_palette_rows", 4 );
    EditorCommandPalette_Open( f.pPalette );
    const int compactHeight = f.List()->height();
    CHECK( f.List()->count() == 10 );
    f.IntegerPreference( "editor.ui.command_palette_rows", 10 );
    EditorCommandPalette_Open( f.pPalette );
    CHECK( f.List()->height() > compactHeight );
    CHECK( f.List()->count() == 10 );
}

TEST_CASE( "Palette recent preferences honor zero and capacities above the old eight-command limit", "[editor][gui][palette][settings]" )
{
    fixture_t f( true );
    f.IntegerPreference( "editor.ui.command_palette_recent", 1 );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "show grid" ) );
    REQUIRE( EditorCommandPalette_Accept( f.pPalette ) );
    EditorCommandPalette_Open( f.pPalette );
    CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QStringLiteral( "map.grid.show" ) );
    f.IntegerPreference( "editor.ui.command_palette_recent", 0 );
    EditorCommandPalette_Open( f.pPalette );
    CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QStringLiteral( "paging.sets" ) );
    EditorCommandPalette_SetQuery( f.pPalette, QStringLiteral( "save" ) );
    REQUIRE( EditorCommandPalette_Accept( f.pPalette ) );
    EditorCommandPalette_Open( f.pPalette );
    CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QStringLiteral( "paging.sets" ) );

    std::vector<std::string> ids;
    std::vector<std::string> labels;
    std::vector<command_desc_t> commands;
    for ( int i = 0; i < 10; ++i ) {
        ids.push_back( "recent.command_" + std::to_string( i ) );
        labels.push_back( "Recent " + std::to_string( i ) );
    }
    for ( int i = 0; i < 10; ++i ) {
        commands.push_back( { ids[i].c_str(), labels[i].c_str(), "", nullptr, nullptr, COMMAND_FLAG_NONE, Nothing, nullptr, nullptr } );
    }
    REQUIRE( EditorCommands_Register( &f.registry, commands.data(), commands.size() ) == command_registry_status_t::OK );
    f.IntegerPreference( "editor.ui.command_palette_recent", 10 );
    for ( const std::string &id : ids ) {
        EditorCommandPalette_Open( f.pPalette );
        EditorCommandPalette_SetQuery( f.pPalette, QString::fromStdString( id ) );
        CHECK( EditorCommandPalette_Results( f.pPalette ).front() == QString::fromStdString( id ) );
        REQUIRE( EditorCommandPalette_Accept( f.pPalette ) );
    }
    EditorCommandPalette_Open( f.pPalette );
    const QStringList results = EditorCommandPalette_Results( f.pPalette );
    for ( int i = 0; i < 10; ++i ) { CHECK( results[i] == QString::fromStdString( ids[9 - i] ) ); }
}
