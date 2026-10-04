//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Dialogs_Tests.cpp
//  Purpose: Contract tests for Map Info and Go To on the example map:
//           counts that agree with the wireframe, entities by class,
//           selecting a class, and going to IDs, names, and positions.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Dialogs.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QTreeWidget>

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
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
    }
    ~session_t()
    {
        MapWorkspace_Shutdown( &workspace );
        gui::EditorGui_Shutdown( &gui );
    }
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
};

const map_wire_entity_t *EntityNamed( const map_workspace_t &workspace, const char *pName )
{
    for ( usize i = 0u; i < workspace.wire.entities.nCount; ++i ) {
        if ( QByteArray( workspace.wire.entities.pData[i].name ) == pName ) { return &workspace.wire.entities.pData[i]; }
    }
    return nullptr;
}

} // namespace

TEST_CASE( "Map Info counts what the map holds", "[map][gui][dialogs]" )
{
    session_t s;
    const map_info_t info = MapInfo_Compute( &s.workspace );
    CHECK( info.nBrushes > 0 );
    CHECK( info.nMeshes >= 1 );
    CHECK( info.nPatches >= 1 );
    CHECK( info.nTerrains >= 1 );
    CHECK( info.nEntities == static_cast<int>( s.workspace.wire.entities.nCount ) );
    CHECK( info.nPointEntities + info.nBrushEntities == info.nEntities );
    int nClassTotal = 0;
    for ( const map_info_class_t &entry : info.classes ) { nClassTotal += entry.nCount; }
    CHECK( nClassTotal == info.nEntities );
    // Most common class first.
    for ( int i = 1; i < info.classes.size(); ++i ) { CHECK( info.classes[i - 1].nCount >= info.classes[i].nCount ); }
    CHECK( info.nLayers == static_cast<int>( s.workspace.pDocument->layers.nCount ) );
    CHECK( info.nMaterials == static_cast<int>( s.workspace.pDocument->materials.entries.nCount ) );
    CHECK( info.bounds.bHas );
    CHECK( MapInfo_Text( &s.workspace ).contains( QStringLiteral( "info_player_start" ) ) );

    std::unique_ptr<QDialog> pDialog( MapInfoDialog_Create( nullptr, &s.workspace ) );
    REQUIRE( pDialog != nullptr );
    auto *pClasses = pDialog->findChild<QTreeWidget *>( QStringLiteral( "MapInfoClasses" ) );
    REQUIRE( pClasses != nullptr );
    CHECK( pClasses->topLevelItemCount() == info.classes.size() );
}

TEST_CASE( "Map Info selects every entity of a class", "[map][gui][dialogs]" )
{
    session_t s;
    const int nSpawns = MapInfo_SelectClass( &s.workspace, QStringLiteral( "info_player_start" ) );
    CHECK( nSpawns >= 2 );
    CHECK( EditorSelection_Count( &s.workspace.selection ) == static_cast<usize>( nSpawns ) );
    CHECK( MapInfo_SelectClass( &s.workspace, QStringLiteral( "no_such_class" ) ) == 0 );
    CHECK( EditorSelection_Count( &s.workspace.selection ) == 0u );
}

TEST_CASE( "The selection's assets are its materials and the assets its records name", "[map][gui][dialogs]" )
{
    session_t s;
    CHECK( MapInfo_SelectionAssets( &s.workspace ).isEmpty() );
    // A world brush: its side materials.
    u64 brushId = 0u;
    for ( usize i = 0u; i < s.workspace.wire.objects.nCount && brushId == 0u; ++i ) {
        const map_wire_object_t &object = s.workspace.wire.objects.pData[i];
        if ( object.kind == map_wire_kind_t::BRUSH && object.owner == 0u ) { brushId = object.id; }
    }
    REQUIRE( brushId != 0u );
    MapWorkspace_Select( &s.workspace, brushId, MAP_SELECT_REPLACE );
    const QStringList brush = MapInfo_SelectionAssets( &s.workspace );
    REQUIRE_FALSE( brush.isEmpty() );
    for ( const QString &path : brush ) { CHECK( path.endsWith( QStringLiteral( ".cymat" ) ) ); }
    // Everything selected: at least what the brush alone uses, sorted, once each.
    std::vector<u64> all;
    for ( usize i = 0u; i < s.workspace.wire.objects.nCount; ++i ) { all.push_back( s.workspace.wire.objects.pData[i].id ); }
    MapWorkspace_SetSelection( &s.workspace, all.data(), all.size() );
    const QStringList everything = MapInfo_SelectionAssets( &s.workspace );
    for ( const QString &path : brush ) { CHECK( everything.contains( path ) ); }
    CHECK( everything.size() >= brush.size() );
    for ( int i = 1; i < everything.size(); ++i ) { CHECK( everything[i - 1] < everything[i] ); }
}

TEST_CASE( "Go To finds objects by ID or name and frames positions", "[map][gui][dialogs]" )
{
    session_t s;
    QString message;
    // By entity name.
    const map_wire_entity_t *pSpawn = EntityNamed( s.workspace, "spawn_a" );
    REQUIRE( pSpawn != nullptr );
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "spawn_a" ), &message ) == map_go_to_status_t::OBJECT );
    CHECK( MapWorkspace_IsSelected( &s.workspace, pSpawn->id ) );
    CHECK( EditorSelection_Count( &s.workspace.selection ) == 1u );
    CHECK( message.contains( QStringLiteral( "spawn_a" ) ) );
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "SPAWN_A" ) ) == map_go_to_status_t::OBJECT ); // Case does not matter.
    // By ID, with or without a hash.
    const u64 firstId = s.workspace.wire.objects.pData[0].id;
    CHECK( MapGoTo_Run( &s.workspace, QString::number( firstId ) ) == map_go_to_status_t::OBJECT );
    CHECK( MapWorkspace_IsSelected( &s.workspace, firstId ) );
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "#%1" ).arg( firstId ) ) == map_go_to_status_t::OBJECT );
    // A position frames the views around it.
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "256, -128, 64" ), &message ) == map_go_to_status_t::POSITION );
    REQUIRE( s.workspace.frameBounds.bHas );
    const math::vec3d_t centre = math::Vec3d_Make( ( s.workspace.frameBounds.box.minimum.x + s.workspace.frameBounds.box.maximum.x ) * 0.5,
                                                   ( s.workspace.frameBounds.box.minimum.y + s.workspace.frameBounds.box.maximum.y ) * 0.5,
                                                   ( s.workspace.frameBounds.box.minimum.z + s.workspace.frameBounds.box.maximum.z ) * 0.5 );
    CHECK( std::fabs( centre.x - 256.0 ) < 1e-9 );
    CHECK( std::fabs( centre.y + 128.0 ) < 1e-9 );
    CHECK( std::fabs( centre.z - 64.0 ) < 1e-9 );
    // What is not there says so.
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "nobody_here" ) ) == map_go_to_status_t::NOT_FOUND );
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "999999999" ) ) == map_go_to_status_t::NOT_FOUND );
    CHECK( MapGoTo_Run( &s.workspace, QStringLiteral( "   " ) ) == map_go_to_status_t::INVALID );

    std::unique_ptr<QDialog> pDialog( MapGoToDialog_Create( nullptr, &s.workspace ) );
    REQUIRE( pDialog != nullptr );
    CHECK( pDialog->findChild<QLineEdit *>( QStringLiteral( "MapGoToInput" ) ) != nullptr );
}

TEST_CASE( "The cordon hides what lies outside its box and can be toggled", "[map][gui][cordon]" )
{
    session_t s;
    const auto visibleCount = [&s]() {
        int n = 0;
        for ( usize i = 0u; i < s.workspace.wire.objects.nCount; ++i ) { n += MapWorkspace_IsVisible( &s.workspace, s.workspace.wire.objects.pData[i] ) ? 1 : 0; }
        return n;
    };
    const int nAll = visibleCount();
    REQUIRE( nAll > 2 );
    CHECK_FALSE( MapWorkspace_IsCordonActive( &s.workspace ) );
    // A small box around one entity: most of the map falls outside.
    const map_wire_entity_t *pSpawn = EntityNamed( s.workspace, "spawn_a" );
    REQUIRE( pSpawn != nullptr );
    map_bounds_t box{};
    MapBounds_AddPoint( box, math::Vec3d_Make( pSpawn->origin.x - 4.0, pSpawn->origin.y - 4.0, pSpawn->origin.z - 4.0 ) );
    MapBounds_AddPoint( box, math::Vec3d_Make( pSpawn->origin.x + 4.0, pSpawn->origin.y + 4.0, pSpawn->origin.z + 4.0 ) );
    MapWorkspace_SetCordon( &s.workspace, box );
    CHECK( MapWorkspace_IsCordonActive( &s.workspace ) );
    const int nInside = visibleCount();
    CHECK( nInside < nAll );
    CHECK( nInside >= 1 ); // The spawn itself.
    const map_wire_object_t *pSpawnObject = MapWireframe_FindObject( s.workspace.wire, pSpawn->id );
    REQUIRE( pSpawnObject != nullptr );
    CHECK( MapWorkspace_IsVisible( &s.workspace, *pSpawnObject ) );
    // Off keeps the box; on again uses it.
    MapWorkspace_SetCordonActive( &s.workspace, CY_FALSE );
    CHECK( visibleCount() == nAll );
    MapWorkspace_SetCordonActive( &s.workspace, CY_TRUE );
    CHECK( visibleCount() == nInside );
    // An empty box turns the cordon off.
    MapWorkspace_SetCordon( &s.workspace, map_bounds_t{} );
    CHECK_FALSE( MapWorkspace_IsCordonActive( &s.workspace ) );
    CHECK( visibleCount() == nAll );
}
