//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Console_Tests.cpp
//  Purpose: Contract tests for the log store and its panels: the console's
//           command lines and filters, the Problems list, the Output view,
//           and the process-wide sink.
//  Details: The log sink is process-wide; these tests check it is restored
//           when the store is uninstalled, so a destroyed store never
//           receives records.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Console.h"
#include "CypherEditorGui_LogPanels.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QWidget>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

std::string gLastArgument;

command_result_t Echo( void *, const command_args_t &args )
{
    if ( args.nArgs != 1u ) { return command_result_t::INVALID_ARGUMENTS; }
    gLastArgument.assign( args.pArgs[0].pData, args.pArgs[0].cchLength );
    return command_result_t::OK;
}

} // namespace

TEST_CASE( "The console runs command lines and reports failures", "[editor][gui][console]" )
{
    command_registry_t registry{};
    REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
    const command_desc_t echo{ "test.echo", "Echo", "Stores its argument.", nullptr, "test.echo <text>", COMMAND_FLAG_NONE, Echo, nullptr, nullptr };
    REQUIRE( EditorCommands_Register( &registry, &echo, 1u ) == command_registry_status_t::OK );
    theme_registry_t tokens{};
    REQUIRE( EditorThemeRegistry_Init( &tokens, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorStyle_RegisterTokens( &tokens ) == theme_status_t::OK );
    const editor_style_t style = EditorStyle_Resolve( &tokens, nullptr, 0u );
    EditorThemeRegistry_Shutdown( &tokens );
    editor_log_t log{};
    EditorLog_Init( &log );
    {
        QWidget *pConsole = EditorConsole_Create( nullptr, &registry, &style, &log );
        REQUIRE( EditorConsole_RegisterCommands( &registry, pConsole ) == command_registry_status_t::OK );
        EditorConsole_Submit( pConsole, QStringLiteral( "test.echo \"hello world\"" ) );
        CHECK( gLastArgument == "hello world" );
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "> test.echo \"hello world\"" ) ) );
        EditorConsole_Submit( pConsole, QStringLiteral( "nope.nothing" ) );
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "UNKNOWN_COMMAND" ) ) );
        EditorConsole_Submit( pConsole, QStringLiteral( "test.echo" ) );
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "INVALID_ARGUMENTS" ) ) );
        EditorConsole_Submit( pConsole, QStringLiteral( "console.help test." ) );
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "Stores its argument." ) ) );
        // Level filters: the failure lines are warnings.
        EditorConsole_SetLevels( pConsole, true, false, true );
        CHECK_FALSE( EditorConsole_Text( pConsole ).contains( QStringLiteral( "UNKNOWN_COMMAND" ) ) );
        EditorConsole_SetLevels( pConsole, true, true, true );
        EditorConsole_SetSearch( pConsole, QStringLiteral( "Stores" ) );
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "Stores its argument." ) ) );
        CHECK_FALSE( EditorConsole_Text( pConsole ).contains( QStringLiteral( "hello world" ) ) );
        EditorConsole_SetSearch( pConsole, QString() );
        EditorConsole_Submit( pConsole, QStringLiteral( "console.clear" ) );
        CHECK( EditorConsole_Text( pConsole ).isEmpty() );
        CHECK( log.entries.isEmpty() ); // Clearing the console clears the log.
        delete pConsole;
    }
    // The console's own commands leave with it.
    CHECK( EditorCommands_Find( &registry, StringView_FromCString( "console.clear" ) ) == nullptr );
    CHECK( EditorCommands_Find( &registry, StringView_FromCString( "test.echo" ) ) != nullptr );
    EditorCommands_Shutdown( &registry );
}

TEST_CASE( "The log store keeps start-up records, feeds the panels, and releases the sink", "[editor][gui][console]" )
{
    command_registry_t registry{};
    REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
    theme_registry_t tokens{};
    REQUIRE( EditorThemeRegistry_Init( &tokens, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorStyle_RegisterTokens( &tokens ) == theme_status_t::OK );
    const editor_style_t style = EditorStyle_Resolve( &tokens, nullptr, 0u );
    EditorThemeRegistry_Shutdown( &tokens );
    log_callback_t pfnBefore = nullptr;
    void *pBefore = nullptr;
    Cy_LogGetCallback( &pfnBefore, &pBefore );
    {
        editor_log_t log{};
        EditorLog_Init( &log );
        EditorLog_Install( &log );
        // Logged before any panel exists: the console still shows it.
        CY_LOG_WRITE( Info, Editor, "boot record before the console" );
        CY_LOG_WRITE( Warning, Tools, "a build warning" );
        CY_LOG_WRITE( Error, Render, "a render error" );
        CHECK( EditorLog_Count( &log, log_level_t::Warning ) == 1u );
        CHECK( EditorLog_ProblemCount( &log ) == 2u );
        QWidget *pConsole = EditorConsole_Create( nullptr, &registry, &style, &log );
        QWidget *pProblems = EditorProblems_Create( nullptr, &style, &log );
        QWidget *pOutput = EditorOutput_Create( nullptr, &style, &log );
        const QString text = EditorConsole_Text( pConsole );
        CHECK( text.contains( QStringLiteral( "boot record before the console" ) ) );
        CHECK( text.contains( QStringLiteral( "WARN" ) ) );
        CHECK( text.contains( QStringLiteral( "[Render]" ) ) );
        // Problems lists warnings and errors only, with their source.
        const QStringList rows = EditorProblems_Rows( pProblems );
        REQUIRE( rows.size() == 2 );
        CHECK( rows[0] == QStringLiteral( "WARN Tools a build warning" ) );
        CHECK( rows[1] == QStringLiteral( "ERROR Render a render error" ) );
        // Output is per source and reads like tool output.
        EditorOutput_SetSource( pOutput, QStringLiteral( "Build" ) );
        CHECK( EditorOutput_Text( pOutput ) == QStringLiteral( "warning: a build warning" ) );
        // Later records reach every panel.
        CY_LOG_WRITE( Warning, Editor, "console sink test record" );
        QCoreApplication::processEvents();
        CHECK( EditorConsole_Text( pConsole ).contains( QStringLiteral( "console sink test record" ) ) );
        CHECK( EditorProblems_Rows( pProblems ).size() == 3 );
        delete pOutput;
        delete pProblems;
        delete pConsole;
        EditorLog_Uninstall( &log );
    }
    log_callback_t pfnAfter = nullptr;
    void *pAfter = nullptr;
    Cy_LogGetCallback( &pfnAfter, &pAfter );
    CHECK( pfnAfter == pfnBefore );
    CHECK( pAfter == pBefore );
    CY_LOG_WRITE( Info, Editor, "logged after the store is gone" ); // Must not crash.
    QCoreApplication::processEvents();
    EditorCommands_Shutdown( &registry );
}
