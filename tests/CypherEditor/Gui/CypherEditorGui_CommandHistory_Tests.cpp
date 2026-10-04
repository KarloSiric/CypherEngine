//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandHistory_Tests.cpp
//  Purpose: Contract tests for the Command History panel: recording through
//           the registry observer, the repeatable filter, repeating the last
//           command or a selection in order with its arguments, and clear.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_CommandHistory.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QWidget>

#include <memory>
#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

struct calls_t {
    std::vector<std::string> log;
};

command_result_t Step( void *pContext, const command_args_t &args ) noexcept
{
    std::string line = "step";
    for ( usize i = 0u; i < args.nArgs; ++i ) { line += " " + std::string( args.pArgs[i].pData, args.pArgs[i].cchLength ); }
    static_cast<calls_t *>( pContext )->log.push_back( line );
    return command_result_t::OK;
}

command_result_t Turn( void *pContext, const command_args_t & ) noexcept
{
    static_cast<calls_t *>( pContext )->log.push_back( "turn" );
    return command_result_t::OK;
}

command_result_t Look( void *pContext, const command_args_t & ) noexcept
{
    static_cast<calls_t *>( pContext )->log.push_back( "look" );
    return command_result_t::OK;
}

void Forward( void *pContext, const command_desc_t &command, const command_args_t &args, command_result_t result )
{
    EditorCommandHistory_Record( static_cast<QWidget *>( pContext ), command, args, result );
}

struct fixture_t {
    QApplication *pApplication{ qobject_cast<QApplication *>( QCoreApplication::instance() ) };
    editor_gui_t gui{};
    calls_t calls{};
    std::unique_ptr<QWidget> panel{};

    fixture_t()
    {
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        const command_desc_t commands[]{
            { "map.test.step", "Step", nullptr, nullptr, "map.test.step <units>", COMMAND_FLAG_NONE, Step, nullptr, &calls },
            { "map.test.turn", "Turn", nullptr, nullptr, nullptr, COMMAND_FLAG_NONE, Turn, nullptr, &calls },
            { "view.test.look", "Look", nullptr, nullptr, nullptr, COMMAND_FLAG_NONE, Look, nullptr, &calls },
        };
        REQUIRE( EditorCommands_Register( &gui.commands, commands, std::size( commands ) ) == command_registry_status_t::OK );
        panel.reset( EditorCommandHistory_Create( nullptr, &gui ) );
        REQUIRE( panel != nullptr );
        EditorCommands_SetObserver( &gui.commands, &Forward, panel.get() );
    }
    ~fixture_t()
    {
        EditorCommands_SetObserver( &gui.commands, nullptr, nullptr );
        panel.reset();
        EditorGui_Shutdown( &gui );
    }
    command_result_t Line( const char *pLine ) { return EditorCommands_ExecuteLine( &gui.commands, StringView_FromCString( pLine ) ); }
};

} // namespace

TEST_CASE( "Commands that edit are repeatable; looking around, files, and undo are not", "[editor][gui][command_history]" )
{
    CHECK( EditorCommandHistory_IsRepeatable( StringView_FromCString( "map.grid.larger" ) ) );
    CHECK( EditorCommandHistory_IsRepeatable( StringView_FromCString( "edit.delete" ) ) );
    CHECK_FALSE( EditorCommandHistory_IsRepeatable( StringView_FromCString( "view.console" ) ) );
    CHECK_FALSE( EditorCommandHistory_IsRepeatable( StringView_FromCString( "file.save" ) ) );
    CHECK_FALSE( EditorCommandHistory_IsRepeatable( StringView_FromCString( "edit.undo" ) ) );
    CHECK_FALSE( EditorCommandHistory_IsRepeatable( StringView_FromCString( "edit.repeat_command" ) ) );
    CHECK_FALSE( EditorCommandHistory_IsRepeatable( string_view_t{} ) );
}

TEST_CASE( "The command history records every run and lists the repeatable ones", "[editor][gui][command_history]" )
{
    fixture_t f;
    REQUIRE( f.Line( "map.test.step 16" ) == command_result_t::OK );
    REQUIRE( f.Line( "view.test.look" ) == command_result_t::OK );
    REQUIRE( f.Line( "map.test.turn" ) == command_result_t::OK );
    CHECK( EditorCommandHistory_Rows( f.panel.get() ) == QStringList{ QStringLiteral( "map.test.step 16" ), QStringLiteral( "map.test.turn" ) } );
    EditorCommandHistory_SetShowAll( f.panel.get(), true );
    CHECK( EditorCommandHistory_Rows( f.panel.get() ).size() == 3 );
    EditorCommandHistory_Clear( f.panel.get() );
    CHECK( EditorCommandHistory_Rows( f.panel.get() ).isEmpty() );
    CHECK( EditorCommandHistory_Repeat( f.panel.get() ) == command_result_t::DISABLED );
}

TEST_CASE( "Repeat runs the last repeatable command, or the selection in order with its arguments", "[editor][gui][command_history]" )
{
    fixture_t f;
    REQUIRE( f.Line( "map.test.step 16" ) == command_result_t::OK );
    REQUIRE( f.Line( "map.test.turn" ) == command_result_t::OK );
    REQUIRE( f.Line( "view.test.look" ) == command_result_t::OK ); // Not repeatable: skipped.
    f.calls.log.clear();

    // Nothing selected: the newest repeatable entry.
    CHECK( EditorCommandHistory_Repeat( f.panel.get() ) == command_result_t::OK );
    CHECK( f.calls.log == std::vector<std::string>{ "turn" } );

    // A selection repeats in recorded order, arguments included: the spiral
    // stair's step-and-turn.
    f.calls.log.clear();
    EditorCommandHistory_SelectRows( f.panel.get(), { 1, 0 } );
    CHECK( EditorCommandHistory_Repeat( f.panel.get() ) == command_result_t::OK );
    CHECK( f.calls.log == std::vector<std::string>{ "step 16", "turn" } );
    // Repeated commands are recorded again, as in Hammer.
    CHECK( EditorCommandHistory_Rows( f.panel.get() ).size() == 5 );
}
