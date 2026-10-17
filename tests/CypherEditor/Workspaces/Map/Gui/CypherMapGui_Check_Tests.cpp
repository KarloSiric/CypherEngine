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
#include "CypherMap_EntityOutputs.h"
#include "CypherMap_ObjectCreate.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QSet>

#include <cmath>
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
    CHECK_FALSE( Has( plain, "mesh_problems" ) );   // Authored meshes are sound.
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

TEST_CASE( "Fix and Fix All delete stacked twins, drop dangling outputs and repair broken meshes in one undo step", "[map][gui][check][fix]" )
{
    session_t s;
    struct made_t { u64 first{}, twin{}, sender{}, mesh{}; } made;
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Setup", []( void *context, map_document_t *copy, vector_t<u64> * ) noexcept {
        auto &m = *static_cast<made_t *>( context );
        const auto light = StringView_FromCString( "light" );
        auto status = MapCreate_Entity( copy, light, { 64, 0, 0 }, {}, &m.first );
        if ( status == map_status_t::OK ) { status = MapCreate_Entity( copy, light, { 64, 0, 0 }, {}, &m.twin ); }
        if ( status == map_status_t::OK ) { status = MapCreate_Entity( copy, StringView_FromCString( "logic_relay" ), { 0, 128, 0 }, {}, &m.sender ); }
        if ( status == map_status_t::OK ) {
            std::vector<map_output_t> outputs( 2 );
            outputs[0] = { "OnTrigger", "nobody_is_called_this", "Enable", "", 0.0, -1 };
            outputs[1] = { "OnTrigger", "!self", "Disable", "", 0.0, -1 };
            bool changed = false;
            status = MapEntityOutputs_Write( copy, m.sender, outputs, &changed );
        }
        // A cube whose lid has its own corners, not joined to the walls: a crack.
        const cypher::math::vec3d_t square[]{ { 0, 0, 0 }, { 64, 0, 0 }, { 64, 64, 0 }, { 0, 64, 0 } };
        if ( status == map_status_t::OK ) { status = MapCreate_PolygonMesh( copy, { square, 4 }, { 0, 0, 64 }, StringView_FromCString( "materials/dev/dev_grid.cymat" ), 128.0, &m.mesh ); }
        if ( status == map_status_t::OK ) {
            const std::vector<cypher::math::vec3d_t> points{ { 0, 0, 0 }, { 64, 0, 0 }, { 64, 64, 0 }, { 0, 64, 0 }, { 0, 0, 64 }, { 64, 0, 64 }, { 64, 64, 64 }, { 0, 64, 64 },
                                                            { 0, 0, 64 }, { 64, 0, 64 }, { 64, 64, 64 }, { 0, 64, 64 } };
            const std::vector<std::vector<u32>> faces{ { 0, 3, 2, 1 }, { 8, 9, 10, 11 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 0, 4, 7, 3 } };
            status = MapCreate_ReplaceMeshPolygons( copy, m.mesh, points, faces, StringView_FromCString( "materials/dev/dev_grid.cymat" ), 128.0 );
        }
        return status;
    }, &made ) );
    const auto count = [&]( const QVector<map_issue_t> &issues, const char *kind ) {
        return std::count_if( issues.begin(), issues.end(), [&]( const map_issue_t &issue ) { return issue.kind == QLatin1String( kind ); } );
    };
    const QVector<map_issue_t> before = MapCheck_Run( &s.workspace );
    CHECK( count( before, "stacked_entity" ) == 1 );
    CHECK( count( before, "missing_target" ) == 1 );
    CHECK( count( before, "mesh_problems" ) == 1 );
    for ( const map_issue_t &issue : before ) {
        CAPTURE( issue.kind );
        CHECK( MapCheck_FixLabel( issue ).isEmpty() == ( issue.kind == QLatin1String( "no_player_start" ) ) );
    }
    std::unique_ptr<QDialog> pDialog( MapCheckDialog_Create( nullptr, &s.workspace ) );
    REQUIRE( pDialog != nullptr );
    const usize steps = EditorHistory_StepCount( &s.workspace.history );
    CHECK( MapCheckDialog_FixAll( pDialog.get() ) == 3 );
    CHECK( EditorHistory_StepCount( &s.workspace.history ) == steps + 1 );
    const QVector<map_issue_t> after = MapCheck_Run( &s.workspace );
    CHECK( count( after, "stacked_entity" ) == 0 ); CHECK( count( after, "missing_target" ) == 0 ); CHECK( count( after, "mesh_problems" ) == 0 );
    CHECK( count( after, "no_player_start" ) == 1 ); // No automatic fix: it stays listed.
    std::vector<map_output_t> outputs;
    REQUIRE( MapEntityOutputs_Read( s.workspace.pDocument, made.sender, outputs ) == map_status_t::OK );
    REQUIRE( outputs.size() == 1u ); CHECK( outputs[0].input == "Disable" ); // The good output stays.
    CHECK( MapWireframe_FindEntity( s.workspace.wire, made.first ) != nullptr );
    CHECK( MapWireframe_FindEntity( s.workspace.wire, made.twin ) == nullptr );
    CHECK( MapCheckDialog_FixAll( pDialog.get() ) == 0 ); // Nothing fixable left.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( count( MapCheck_Run( &s.workspace ), "mesh_problems" ) == 1 );
}

TEST_CASE( "Meshes the creation tools build raise no mesh problems", "[map][gui][check][fix]" )
{
    session_t s;
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Setup", []( void *, map_document_t *copy, vector_t<u64> * ) noexcept {
        const auto material = StringView_FromCString( "materials/dev/dev_grid.cymat" );
        u64 id = 0;
        const cypher::math::vec3d_t square[]{ { 0, 0, 0 }, { 64, 0, 0 }, { 64, 64, 0 }, { 0, 64, 0 } };
        auto status = MapCreate_PolygonMesh( copy, { square, 4 }, { 0, 0, 64 }, material, 128.0, &id );
        const std::vector<cypher::math::vec2d_t> profile{ { 0, 0 }, { 48, 16 }, { 32, 64 }, { 0, 128 } };
        if ( status == map_status_t::OK ) { status = MapCreate_Lathe( copy, { profile.data(), profile.size() }, { 512, 0, 0 }, { 0, 0, 1 }, 6.283185307179586, 16, material, 128.0, &id ); }
        const std::vector<cypher::math::vec3d_t> path{ { 0, 512, 0 }, { 256, 512, 0 }, { 256, 768, 0 } };
        if ( status == map_status_t::OK ) { status = MapCreate_Walls( copy, { path.data(), path.size() }, { 0, 0, 1 }, false, 16.0, 128.0, map_wall_join_t::MITER, material, 128.0, &id ); }
        std::vector<cypher::math::vec2d_t> round;
        for ( int i = 0; i < 8; ++i ) { const f64 a = 6.283185307179586 * i / 8; round.push_back( { 16 * std::cos( a ), 16 * std::sin( a ) } ); }
        if ( status == map_status_t::OK ) { status = MapCreate_Sweep( copy, { path.data(), path.size() }, false, { round.data(), round.size() }, true, material, 128.0, &id ); }
        map_displacement_desc_t desc{};
        desc.corners[0] = { 0, -512, 0 }; desc.corners[1] = { 256, -512, 0 }; desc.corners[2] = { 256, -256, 0 }; desc.corners[3] = { 0, -256, 0 };
        desc.power = 3;
        if ( status == map_status_t::OK ) { status = MapCreate_Displacement( copy, desc, material, 128.0, &id ); }
        return status;
    }, nullptr ) );
    REQUIRE( s.workspace.pDocument->geometry.meshes.nCount == 5u );
    for ( const map_issue_t &issue : MapCheck_Run( &s.workspace ) ) { CAPTURE( issue.message ); CHECK( issue.kind != QStringLiteral( "mesh_problems" ) ); }
}

