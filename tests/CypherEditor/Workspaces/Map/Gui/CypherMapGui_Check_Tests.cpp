//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Check_Tests.cpp
//  Purpose: Contract tests for Check for Problems: ordering, the player
//           start rule, missing assets through the catalogue callback, and
//           the window's Go to Error.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Check.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QSet>

#include <filesystem>
#include <memory>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

struct session_t {
    session_t()
    {
        auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, pApp, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    }
    ~session_t()
    {
        MapWorkspace_Shutdown( &workspace );
        gui::EditorGui_Shutdown( &gui );
    }
    void OpenExample()
    {
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
    }
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
};

bool Has( const QVector<map_issue_t> &issues, const char *pKind )
{
    for ( const map_issue_t &issue : issues ) {
        if ( issue.kind == QLatin1String( pKind ) ) { return true; }
    }
    return false;
}

bool NothingExists( void *, const QString & ) { return false; }
bool EverythingExists( void *, const QString & ) { return true; }

} // namespace

TEST_CASE( "An empty map is missing its player start", "[map][gui][check]" )
{
    session_t s;
    const QVector<map_issue_t> issues = MapCheck_Run( &s.workspace );
    CHECK( Has( issues, "no_player_start" ) );
    for ( const map_issue_t &issue : issues ) {
        if ( issue.kind == QStringLiteral( "no_player_start" ) ) { CHECK( issue.id == 0u ); }
    }
}

TEST_CASE( "The example map checks in severity order and finds its missing assets", "[map][gui][check]" )
{
    session_t s;
    s.OpenExample();
    const QVector<map_issue_t> plain = MapCheck_Run( &s.workspace );
    CHECK_FALSE( Has( plain, "no_player_start" ) ); // spawn_a and spawn_b are player starts.
    CHECK_FALSE( Has( plain, "missing_asset" ) );   // No catalogue, no asset checks.
    for ( int i = 1; i < plain.size(); ++i ) { CHECK( plain[i - 1].severity >= plain[i].severity ); }

    // With a catalogue that has nothing, every named asset is reported once.
    const QVector<map_issue_t> bare = MapCheck_Run( &s.workspace, &NothingExists, nullptr );
    QSet<QString> messages;
    int nMissing = 0;
    for ( const map_issue_t &issue : bare ) {
        if ( issue.kind != QStringLiteral( "missing_asset" ) ) { continue; }
        ++nMissing;
        CHECK_FALSE( messages.contains( issue.message ) );
        messages.insert( issue.message );
    }
    CHECK( nMissing >= 2 );
    bool bSky = false;
    for ( const QString &message : messages ) { bSky = bSky || message.contains( QStringLiteral( "materials/sky/dusk.cymat" ) ); }
    CHECK( bSky ); // A map-level reference, not only face materials.
    CHECK_FALSE( Has( MapCheck_Run( &s.workspace, &EverythingExists, nullptr ), "missing_asset" ) );
}

TEST_CASE( "The check window lists issues and goes to their objects", "[map][gui][check]" )
{
    session_t s;
    s.OpenExample();
    std::unique_ptr<QDialog> pDialog( MapCheckDialog_Create( nullptr, &s.workspace, &NothingExists, nullptr ) );
    REQUIRE( pDialog != nullptr );
    const QVector<map_issue_t> issues = MapCheck_Run( &s.workspace, &NothingExists, nullptr );
    CHECK( MapCheckDialog_Refresh( pDialog.get() ) == issues.size() );
    for ( int i = 0; i < issues.size(); ++i ) {
        if ( issues[i].id == 0u ) {
            CHECK_FALSE( MapCheckDialog_GoTo( pDialog.get(), i ) ); // Map-wide: nothing to select.
        } else {
            CHECK( MapCheckDialog_GoTo( pDialog.get(), i ) );
            CHECK( MapWorkspace_IsSelected( &s.workspace, issues[i].id ) );
        }
    }
    CHECK_FALSE( MapCheckDialog_GoTo( pDialog.get(), -1 ) );
    CHECK( QString::fromLatin1( MapCheck_SeverityName( map_issue_severity_t::ERROR ) ) == QStringLiteral( "Error" ) );
}
