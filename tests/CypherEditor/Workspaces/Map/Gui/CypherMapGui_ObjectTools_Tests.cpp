//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ObjectTools_Tests.cpp
//  Purpose: Tests the everyday object commands from the Hammer 5 and
//           TrenchBroom parity review: drop to floor, cycling through the
//           objects under the cursor, rotating by the angle snap, clearing
//           entity rotation, the bounding box, and keyboard material edits.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_ObjectTools.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_Modifiers.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_ToolPanels.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLineF>
#include <QPushButton>
#include <QTemporaryDir>

#include <memory>
#include <cmath>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    session_t()
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &ws ); gui::EditorGui_Shutdown( &gui ); }

    u64 Box( math::vec3d_t lo, math::vec3d_t hi )
    {
        map_bounds_t bounds{};
        MapBounds_AddPoint( bounds, lo );
        MapBounds_AddPoint( bounds, hi );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
        return EditorSelection_At( &ws.selection, 0 );
    }
    u64 Entity( const char *className, math::vec3d_t at )
    {
        u64 id = 0;
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( ws.pDocument->layers.pData[0].id ), StringView_FromCString( className ), at, &id ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        return id;
    }
    void Select( std::initializer_list<u64> ids )
    {
        const std::vector<u64> list( ids );
        MapWorkspace_SetSelection( &ws, list.data(), list.size() );
    }
    command_result_t Run( const char *command ) { return EditorCommands_Execute( &gui.commands, StringView_FromCString( command ), {} ); }
    bool Enabled( const char *command ) { return ( EditorCommands_State( &gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_ENABLED ) != 0; }
    const math::aabbd_t &Bounds( u64 id )
    {
        const map_wire_object_t *object = MapWireframe_FindObject( ws.wire, id );
        REQUIRE( object != nullptr );
        return object->bounds.box;
    }
    usize Steps() { return EditorHistory_AppliedStepCount( &ws.history ); }
    usize Count() { return ws.wire.objects.nCount; }
};
} // namespace

TEST_CASE( "Drop to Floor lands each object on the surface below it", "[map][gui][object-tools]" )
{
    session_t s;
    s.Box( { 0, 0, 0 }, { 512, 512, 16 } );     // Floor.
    s.Box( { 256, 0, 16 }, { 512, 512, 64 } );  // A step on its right half.
    const u64 crate = s.Box( { 64, 64, 200 }, { 128, 128, 264 } );
    const u64 wide = s.Box( { 224, 300, 300 }, { 288, 364, 332 } ); // Over the step's edge.
    const u64 lamp = s.Entity( "light", { 96, 400, 300 } );
    s.Select( { crate, wide, lamp } );
    REQUIRE( s.Enabled( "map.transform.drop_to_floor" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.transform.drop_to_floor" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );                               // One undo step for all of them.
    CHECK( s.Bounds( crate ).minimum.z == Catch::Approx( 16.0 ) );  // On the floor.
    CHECK( s.Bounds( wide ).minimum.z == Catch::Approx( 64.0 ) );   // Caught by the step.
    CHECK( s.Bounds( lamp ).minimum.z == Catch::Approx( 16.0 ) );   // The entity's box rests on the floor.
    CHECK( EditorSelection_Count( &s.ws.selection ) == 3u );        // The selection stays.

    // Resting already: nothing to do, and it says so.
    REQUIRE( s.Run( "map.transform.drop_to_floor" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );
    CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "already rests" ) ) );

    // Nothing below: it stays.
    const u64 outside = s.Box( { 1000, 1000, 100 }, { 1064, 1064, 164 } );
    s.Select( { outside } );
    REQUIRE( s.Run( "map.transform.drop_to_floor" ) == command_result_t::OK );
    CHECK( s.Bounds( outside ).minimum.z == 100.0 );
    CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "nothing below" ) ) );
}

TEST_CASE( "Selected objects do not hold each other up when dropped", "[map][gui][object-tools]" )
{
    session_t s;
    s.Box( { 0, 0, 0 }, { 256, 256, 16 } );
    const u64 lower = s.Box( { 64, 64, 100 }, { 128, 128, 132 } );
    const u64 upper = s.Box( { 64, 64, 200 }, { 128, 128, 232 } );
    f64 distance = 0.0;
    REQUIRE( MapObjectTools_DropDistance( &s.ws, upper, { upper }, &distance ) );
    CHECK( distance == Catch::Approx( 200.0 - 132.0 ) ); // Alone, it lands on the lower box.
    REQUIRE( MapObjectTools_DropDistance( &s.ws, upper, { upper, lower }, &distance ) );
    CHECK( distance == Catch::Approx( 200.0 - 16.0 ) );  // Moving together, both reach the floor.
}

TEST_CASE( "The objects under the cursor come nearest first and Cycle walks them", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 bottom = s.Box( { 0, 0, 0 }, { 128, 128, 32 } );
    const u64 middle = s.Box( { 16, 16, 64 }, { 112, 112, 96 } );
    const u64 top = s.Box( { 32, 32, 128 }, { 96, 96, 160 } );
    MapWorkspace_SetSelection( &s.ws, nullptr, 0 );
    // Looking straight down through the stack.
    const auto along = MapObjectTools_ObjectsAlongRay( &s.ws, { 64, 64, 1000 }, { 0, 0, -1 } );
    REQUIRE( along.size() == 3u );
    CHECK( along[0] == top );
    CHECK( along[1] == middle );
    CHECK( along[2] == bottom );
    // In the Top view, the smallest outline comes first.
    const auto under = MapObjectTools_ObjectsUnderPoint( &s.ws, { 64, 64, 0 }, 0, 1 );
    REQUIRE( under.size() == 3u );
    CHECK( under[0] == top );
    CHECK( under[2] == bottom );
    // Nothing selected: the first; then each in turn, wrapping round.
    REQUIRE( MapObjectTools_Cycle( &s.ws, along, 1 ) );
    CHECK( EditorSelection_At( &s.ws.selection, 0 ) == top );
    REQUIRE( MapObjectTools_Cycle( &s.ws, along, 1 ) );
    CHECK( EditorSelection_At( &s.ws.selection, 0 ) == middle );
    CHECK( MapNotice_Last( &s.ws ).startsWith( QStringLiteral( "2 of 3 under the cursor" ) ) );
    REQUIRE( MapObjectTools_Cycle( &s.ws, along, 1 ) );
    REQUIRE( MapObjectTools_Cycle( &s.ws, along, 1 ) );
    CHECK( EditorSelection_At( &s.ws.selection, 0 ) == top );
    REQUIRE( MapObjectTools_Cycle( &s.ws, along, -1 ) );
    CHECK( EditorSelection_At( &s.ws.selection, 0 ) == bottom );
    // The command uses where the pointer last was.
    MapObjectTools_SetPointerRay( &s.ws, { 64, 64, 1000 }, { 0, 0, -1 } );
    REQUIRE( s.Run( "map.select.cycle_next" ) == command_result_t::OK );
    CHECK( EditorSelection_At( &s.ws.selection, 0 ) == top );
    // Hidden objects are not under the cursor.
    MapWorkspace_SetSelection( &s.ws, &middle, 1 );
    REQUIRE( s.Run( "map.hide.selected" ) == command_result_t::OK );
    CHECK( MapObjectTools_ObjectsAlongRay( &s.ws, { 64, 64, 1000 }, { 0, 0, -1 } ).size() == 2u );
}

TEST_CASE( "Rotate by Angle Snap turns the selection in the active view", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 plank = s.Box( { 0, 0, 0 }, { 128, 32, 16 } );
    s.Select( { plank } );
    s.ws.angleSnap = 90.0;
    MapObjectTools_SetPointerPoint( &s.ws, { 0, 0, 0 }, 0, 1 ); // The Top view.
    REQUIRE( s.Run( "map.nudge_rotate.right" ) == command_result_t::OK );
    CHECK( s.Bounds( plank ).maximum.x - s.Bounds( plank ).minimum.x == Catch::Approx( 32.0 ) );
    CHECK( s.Bounds( plank ).maximum.y - s.Bounds( plank ).minimum.y == Catch::Approx( 128.0 ) );
    CHECK( s.Bounds( plank ).maximum.z - s.Bounds( plank ).minimum.z == Catch::Approx( 16.0 ) );
    // Tilting stands it on end.
    REQUIRE( s.Run( "map.nudge_rotate.up" ) == command_result_t::OK );
    CHECK( s.Bounds( plank ).maximum.z - s.Bounds( plank ).minimum.z == Catch::Approx( 128.0 ) );
}

TEST_CASE( "Clear Rotation and Scale removes an entity's angles and scale", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 start = s.Entity( "info_player_start", { 0, 0, 0 } );
    s.Select( { start } );
    CHECK( s.Run( "map.transform.clear_rotation" ) == command_result_t::OK ); // Nothing to clear yet.
    CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "not turned or scaled" ) ) );
    {
        map_layer_store_t *store = nullptr;
        key_value_t *record = MapDocument_FindObject( s.ws.pDocument, start, &store );
        REQUIRE( record != nullptr );
        key_value_t *angles = KeyValue_ObjectInsert( store->store.pDocument, record, StringView_FromCString( "angles" ), key_value_type_t::ARRAY );
        REQUIRE( angles != nullptr );
        for ( f64 value : { 0.0, 90.0, 0.0 } ) { REQUIRE( KeyValue_SetF64( store->store.pDocument, KeyValue_ArrayAppend( store->store.pDocument, angles, key_value_type_t::F64 ), value ) ); }
    }
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.transform.clear_rotation" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );
    CHECK( KeyValue_Find( MapEntityEdit_FindEntity( s.ws.pDocument, start ), StringView_FromCString( "angles" ) ) == nullptr );
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    CHECK( KeyValue_Find( MapEntityEdit_FindEntity( s.ws.pDocument, start ), StringView_FromCString( "angles" ) ) != nullptr );
}

TEST_CASE( "Create Bounding Box makes a box around the selection", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = s.Box( { 128, 32, 16 }, { 192, 96, 128 } );
    s.Select( { a, b } );
    const usize before = s.Count();
    REQUIRE( s.Run( "map.brush.bounding_box" ) == command_result_t::OK );
    CHECK( s.Count() == before + 1u );
    const u64 hull = EditorSelection_At( &s.ws.selection, 0 );
    CHECK( hull != a );
    CHECK( hull != b );
    CHECK( s.Bounds( hull ).minimum.x == 0.0 );
    CHECK( s.Bounds( hull ).maximum.x == 192.0 );
    CHECK( s.Bounds( hull ).maximum.y == 96.0 );
    CHECK( s.Bounds( hull ).maximum.z == 128.0 );
}

TEST_CASE( "Material flips, quarter turns and nudges each take one undo step", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    s.Select( { box } );
    for ( const char *command : { "map.texture.flip_horizontal", "map.texture.flip_vertical", "map.texture.rotate_cw", "map.texture.rotate_ccw",
                                  "map.texture.nudge_left", "map.texture.nudge_up" } ) {
        INFO( command );
        REQUIRE( s.Enabled( command ) );
        const usize steps = s.Steps();
        REQUIRE( s.Run( command ) == command_result_t::OK );
        CHECK( s.Steps() == steps + 1u );
    }
    // Nothing picked or selected: nothing to edit.
    MapWorkspace_SetSelection( &s.ws, nullptr, 0 );
    CHECK_FALSE( s.Enabled( "map.texture.flip_horizontal" ) );
}

TEST_CASE( "Show Helpers switches the helpers off and back on", "[map][gui][object-tools]" )
{
    session_t s;
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &s.gui.settings, settings_scope_t::USER, &user );
    struct detach_t { gui::editor_gui_t &gui; ~detach_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); } } detach{ s.gui };
    const auto checked = [&]() { return ( EditorCommands_State( &s.gui.commands, StringView_FromCString( "map.show.helpers" ) ) & COMMAND_STATE_CHECKED ) != 0; };
    CHECK( checked() );
    REQUIRE( s.Run( "map.show.helpers" ) == command_result_t::OK );
    CHECK_FALSE( checked() );
    const auto mode = EditorSettings_Text( &s.gui.settings, "editor.viewport.helpers", {} );
    CHECK( std::string( mode.pData, mode.cchLength ) == "never" );
    REQUIRE( s.Run( "map.show.helpers" ) == command_result_t::OK );
    CHECK( checked() );
}

TEST_CASE( "Pick Entity answers with the clicked entity's name, through its brush too", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 named = s.Entity( "light", { 0, 0, 64 } );
    const u64 unnamed = s.Entity( "light", { 128, 0, 64 } );
    s.Select( { named } );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &s.ws, StringView_FromCString( "name" ), StringView_FromCString( "lamp_a" ) ) );
    const u64 brush = s.Box( { 256, 0, 0 }, { 320, 64, 64 } );
    u64 door = 0;
    REQUIRE( MapCreate_TieToEntity( s.ws.pDocument, { &brush, 1 }, 0, StringView_FromCString( "func_door" ), &door ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.ws );
    s.Select( { door } );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &s.ws, StringView_FromCString( "name" ), StringView_FromCString( "door_a" ) ) );

    auto owner = std::make_unique<QObject>();
    QString got;
    const auto begin = [&]() { MapObjectTools_BeginEntityPick( &s.ws, owner.get(), [&]( u64, const QString &name ) { got = name; } ); };
    begin();
    REQUIRE( MapObjectTools_EntityPickPending( &s.ws ) );
    CHECK( MapObjectTools_ResolveEntityPick( &s.ws, named ) );
    CHECK( got == QStringLiteral( "lamp_a" ) );
    CHECK_FALSE( MapObjectTools_EntityPickPending( &s.ws ) ); // One click, one pick.
    begin();
    CHECK( MapObjectTools_ResolveEntityPick( &s.ws, brush ) ); // The door's brush names the door.
    CHECK( got == QStringLiteral( "door_a" ) );
    got.clear();
    begin();
    CHECK( MapObjectTools_ResolveEntityPick( &s.ws, unnamed ) );
    CHECK( got.isEmpty() );
    CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "no name" ) ) );
    begin();
    owner.reset(); // The editor that asked is gone: the pick is too.
    CHECK_FALSE( MapObjectTools_EntityPickPending( &s.ws ) );
    CHECK_FALSE( MapObjectTools_ResolveEntityPick( &s.ws, named ) );
}

TEST_CASE( "Hiding an entity class hides its entities and the brushes they own", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 lamp = s.Entity( "light", { 0, 0, 64 } );
    const u64 start = s.Entity( "info_player_start", { 128, 0, 0 } );
    const u64 brush = s.Box( { 256, 0, 0 }, { 320, 64, 64 } );
    u64 detail = 0;
    REQUIRE( MapCreate_TieToEntity( s.ws.pDocument, { &brush, 1 }, 0, StringView_FromCString( "func_detail" ), &detail ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.ws );
    s.Select( { lamp, start } );
    MapEntityLook_SetClassHidden( &s.ws, QStringLiteral( "light" ), true );
    CHECK_FALSE( MapWorkspace_IsVisible( &s.ws, *MapWireframe_FindObject( s.ws.wire, lamp ) ) );
    CHECK( MapWorkspace_IsVisible( &s.ws, *MapWireframe_FindObject( s.ws.wire, start ) ) );
    CHECK( EditorSelection_Count( &s.ws.selection ) == 1u ); // The hidden light left the selection.
    MapEntityLook_SetClassHidden( &s.ws, QStringLiteral( "func_detail" ), true );
    CHECK_FALSE( MapWorkspace_IsVisible( &s.ws, *MapWireframe_FindObject( s.ws.wire, brush ) ) );
    MapEntityLook_SetClassHidden( &s.ws, QStringLiteral( "light" ), false );
    MapEntityLook_SetClassHidden( &s.ws, QStringLiteral( "func_detail" ), false );
    CHECK( MapWorkspace_IsVisible( &s.ws, *MapWireframe_FindObject( s.ws.wire, lamp ) ) );
    CHECK( MapWorkspace_IsVisible( &s.ws, *MapWireframe_FindObject( s.ws.wire, brush ) ) );
    CHECK_FALSE( MapEntityLook_AnyClassHidden( &s.ws ) );
}

TEST_CASE( "Snap to Vertices lands a moving corner on a nearby corner", "[map][gui][object-tools]" )
{
    session_t s;
    const u64 moving = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    s.Box( { 100, 0, 0 }, { 164, 64, 64 } );
    s.Select( { moving } );
    // A Top view at one pixel per unit.
    const map_project_fn top = []( math::vec3d_t p, QPointF &screen ) { screen = QPointF( p.x, -p.y ); return true; };
    const QRectF viewport( -1000, -1000, 2000, 2000 );
    const int drag = 0;
    math::vec3d_t delta{ 33, 2, 0 };
    math::vec3d_t to{};
    REQUIRE( MapObjectTools_SnapToVertices( &s.ws, &drag, QPointF( 1, 1 ), top, viewport, 3u, 8.0, &delta, &to ) );
    CHECK( delta.x == 36.0 ); // Its corner (64, 0) meets the other box's (100, 0).
    CHECK( delta.y == 0.0 );
    CHECK( delta.z == 0.0 );  // The view's depth axis is left alone.
    CHECK( to.x == 100.0 );
    math::vec3d_t marker{};
    CHECK( MapObjectTools_VertexSnapTarget( &s.ws, &marker ) );
    // Too far on screen: the move stays as it was.
    delta = { 10, 0, 0 };
    CHECK_FALSE( MapObjectTools_SnapToVertices( &s.ws, &drag, QPointF( 1, 1 ), top, viewport, 3u, 8.0, &delta ) );
    CHECK( delta.x == 10.0 );
    CHECK_FALSE( MapObjectTools_VertexSnapTarget( &s.ws, &marker ) );
    // Only the axes allowed change: a move along X keeps Y.
    delta = { 34, 3, 0 };
    REQUIRE( MapObjectTools_SnapToVertices( &s.ws, &drag, QPointF( 1, 1 ), top, viewport, 1u, 8.0, &delta ) );
    CHECK( delta.x == 36.0 );
    CHECK( delta.y == 3.0 );
}

TEST_CASE( "Rotating a selection with entities turns the entities too", "[map][gui][object-tools][entity-rotate]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 128, 32, 32 } );
    const u64 start = s.Entity( "info_player_start", { 100, 0, 0 } );
    s.Select( { box, start } );
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 0, 0, 0 } ) );
    const map_wire_entity_t *entity = MapWireframe_FindEntity( s.ws.wire, start );
    REQUIRE( entity != nullptr );
    CHECK( entity->origin.x == Catch::Approx( 0.0 ).margin( 1e-9 ) );
    CHECK( entity->origin.y == Catch::Approx( 100.0 ) );
    const key_value_t *angles = KeyValue_Find( MapEntityEdit_FindEntity( s.ws.pDocument, start ), StringView_FromCString( "angles" ) );
    REQUIRE( KeyValue_ChildCount( angles ) == 3u );
    f64 yaw = 0.0;
    REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( angles, 1 ), &yaw ) );
    CHECK( yaw == Catch::Approx( 90.0 ) );
    // A second quarter turn adds up; four bring it back with no angles written.
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 0, 0, 0 } ) );
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 0, 0, 0 } ) );
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 0, 0, 0 } ) );
    CHECK( KeyValue_Find( MapEntityEdit_FindEntity( s.ws.pDocument, start ), StringView_FromCString( "angles" ) ) == nullptr );
    // Scaling moves the entity's origin and leaves its size alone.
    REQUIRE( MapWorkspace_ScaleSelection( &s.ws, { 2, 2, 2 }, { 0, 0, 0 } ) );
    entity = MapWireframe_FindEntity( s.ws.wire, start );
    REQUIRE( entity != nullptr );
    CHECK( entity->origin.x == Catch::Approx( 200.0 ) );
}

namespace
{
struct object_modifier_surface_t {
    geometry::modifier_result_t value{};
    object_modifier_surface_t() { REQUIRE( geometry::ModifierResult_Init( &value, Allocator_GetSystem() ) == geometry::geometry_status_t::OK ); }
    ~object_modifier_surface_t() { geometry::ModifierResult_Shutdown( &value ); }
    void Read( const map_document_t *document, u64 mesh ) { REQUIRE( MapModifiers_Evaluate( document, mesh, &value ) == map_status_t::OK ); }
};
std::string ModifierMapText( map_document_t *document )
{
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_Save( document, &text ) == map_status_t::OK );
    std::string result( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); TextBuffer_Shutdown( &text ); return result;
}
void ModifierMouse( QWidget *widget, QEvent::Type type, QPointF position )
{
    QMouseEvent event( type, position, widget->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( widget, &event );
}
void CheckObjectModifierTurn( const geometry::modifier_result_t &before, const geometry::modifier_result_t &after,
    math::vec3d_t pivot, bool turned )
{
    REQUIRE( after.mesh.vertices.nCount == before.mesh.vertices.nCount ); REQUIRE( after.mesh.faces.nCount == before.mesh.faces.nCount );
    for ( usize i = 0; i < before.mesh.vertices.nCount; ++i ) {
        auto expected = before.mesh.vertices.pData[i].position;
        if ( turned ) { expected = { pivot.x - ( expected.y - pivot.y ), pivot.y + ( expected.x - pivot.x ), expected.z }; }
        const auto actual = after.mesh.vertices.pData[i].position;
        CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-8 ) ); CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-8 ) );
        CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-8 ) );
        CHECK( after.vertexCopies.pData[i] == before.vertexCopies.pData[i] ); CHECK( after.vertexSources.pData[i] == before.vertexSources.pData[i] );
    }
}
}

TEST_CASE( "The native root rotation gizmo refuses unsupported modifier turns and preserves supported custom-pivot shapes", "[map][gui][object-tools][modifier-transform-safety][native]" )
{
    for ( const auto kind : { geometry::modifier_kind_t::MIRROR, geometry::modifier_kind_t::BEND } ) {
        CAPTURE( static_cast<int>( kind ) ); session_t s;
        const u64 mesh = s.Box( { 8, 4, 0 }, { 40, 24, 48 } );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, s.ws.pDocument->pAllocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( s.ws.pDocument, { &mesh, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
        geometry::modifier_t stage{}; stage.kind = kind; stage.axis = kind == geometry::modifier_kind_t::MIRROR ? 0 : 2;
        stage.alongAxis = 0; stage.origin = { 2, -3, 5 }; stage.offset = -16; stage.angle = .6;
        bool changed = false; REQUIRE( MapModifiers_Write( s.ws.pDocument, mesh, { stage }, &changed ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &s.ws ); s.Select( { mesh } );
        MapWorkspace_SetTool( &s.ws, map_tool_t::ROTATE ); MapWorkspace_SetAngleSnap( &s.ws, 15 );
        const math::vec3d_t pivot{ 11, -7, 3 }; MapToolGestures_SetPivot( &s.ws, pivot );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &s.ws, map_ortho_axes_t::TOP ) );
        top->resize( 800, 600 ); top->show(); top->activateWindow(); QCoreApplication::processEvents();
        // Establish the Qt dispatch precondition independently of background
        // macOS foreground activation, as the application routing tests do.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow( top.get() );
        QT_WARNING_POP
        top->setFocus(); QCoreApplication::processEvents();
        REQUIRE( QApplication::focusWidget() == top.get() );
        const f64 radius = 64.0 / MapOrthoView_Zoom( top.get() );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { pivot.x + radius, pivot.y } );
        const QPointF rejected = MapOrthoView_WorldToView( top.get(), { pivot.x + radius / std::sqrt( 2.0 ), pivot.y + radius / std::sqrt( 2.0 ) } );
        const QPointF accepted = MapOrthoView_WorldToView( top.get(), { pivot.x, pivot.y + radius } );
        object_modifier_surface_t before; before.Read( s.ws.pDocument, mesh );
        const auto savedBefore = ModifierMapText( s.ws.pDocument ); const auto *document = s.ws.pDocument;
        const auto revision = document->geometry.revision, selectionRevision = s.ws.selection.revision, nextId = document->nextId;
        const usize steps = s.Steps();
        ModifierMouse( top.get(), QEvent::MouseButtonPress, start ); ModifierMouse( top.get(), QEvent::MouseMove, rejected );
        REQUIRE( s.ws.editPreview.bActive ); CHECK( s.ws.editPreview.transform.degrees.z == Catch::Approx( 45 ) );
        ModifierMouse( top.get(), QEvent::MouseButtonRelease, rejected );
        CHECK_FALSE( s.ws.editPreview.bActive ); CHECK( s.ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( s.ws.selection.revision == selectionRevision ); CHECK( document->nextId == nextId ); CHECK( s.Steps() == steps );
        CHECK( ModifierMapText( s.ws.pDocument ) == savedBefore );
        CHECK( s.ws.lastEdit.status == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
        CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "cannot preserve the live modifier" ) ) );
        math::vec3d_t actualPivot{}; REQUIRE( MapToolGestures_Pivot( &s.ws, &actualPivot ) );
        CHECK( actualPivot.x == pivot.x ); CHECK( actualPivot.y == pivot.y ); CHECK( actualPivot.z == pivot.z );
        REQUIRE( EditorSelection_Count( &s.ws.selection ) == 1 ); CHECK( EditorSelection_At( &s.ws.selection, 0 ) == mesh );
        // The refused gesture leaves no stale capture: a fresh signed quarter turn works.
        ModifierMouse( top.get(), QEvent::MouseButtonPress, start ); ModifierMouse( top.get(), QEvent::MouseMove, accepted );
        REQUIRE( s.ws.editPreview.bActive ); ModifierMouse( top.get(), QEvent::MouseButtonRelease, accepted );
        REQUIRE( s.Steps() == steps + 1 );
        object_modifier_surface_t after; after.Read( s.ws.pDocument, mesh ); CheckObjectModifierTurn( before.value, after.value, pivot, true );
        const auto savedAfter = ModifierMapText( s.ws.pDocument );
        REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
        object_modifier_surface_t undone; undone.Read( s.ws.pDocument, mesh ); CheckObjectModifierTurn( before.value, undone.value, pivot, false );
        REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK );
        object_modifier_surface_t redone; redone.Read( s.ws.pDocument, mesh ); CheckObjectModifierTurn( before.value, redone.value, pivot, true );
        CHECK( ModifierMapText( s.ws.pDocument ) == savedAfter );
        map_document_t loaded{}; REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { savedAfter.data(), savedAfter.size() } ) == map_status_t::OK );
        object_modifier_surface_t reopened; reopened.Read( &loaded, mesh ); CheckObjectModifierTurn( before.value, reopened.value, pivot, true );
    }
}

TEST_CASE( "A refused modifier edit preserves mixed selection and publication allocation failure permits retry", "[map][gui][object-tools][modifier-transform-safety][allocation]" )
{
    session_t s; const u64 ordinary = s.Box( { -128, 0, 0 }, { -96, 32, 32 } );
    const u64 mesh = s.Box( { 8, 4, 0 }, { 40, 24, 48 } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, s.ws.pDocument->pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( s.ws.pDocument, { &mesh, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    geometry::modifier_t stage{}; stage.kind = geometry::modifier_kind_t::RADIAL_ARRAY; stage.cCopies = 3; stage.origin = { -32, -48, 0 }; stage.angle = 3.14159265358979323846;
    bool changed = false; REQUIRE( MapModifiers_Write( s.ws.pDocument, mesh, { stage }, &changed ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.ws ); s.Select( { ordinary, mesh } );
    const auto before = ModifierMapText( s.ws.pDocument ); const auto *document = s.ws.pDocument; const auto selectionRevision = s.ws.selection.revision;
    const auto steps = s.Steps();
    CHECK_FALSE( MapWorkspace_ScaleSelection( &s.ws, { 2, 1, 1 }, { 11, -7, 3 } ) );
    CHECK( s.ws.lastEdit.status == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
    CHECK( s.ws.pDocument == document ); CHECK( s.Steps() == steps ); CHECK( s.ws.selection.revision == selectionRevision ); CHECK( ModifierMapText( s.ws.pDocument ) == before );
    usize calls = 0; allocator_t allocator{}; allocator.pUserData = &calls;
    allocator.pfnAllocate = []( void *context, usize, usize ) noexcept -> void * { ++*static_cast<usize *>( context ); return nullptr; };
    allocator.pfnFree = []( void *, void *, usize, usize ) noexcept {};
    const auto *original = s.gui.pAllocator; s.gui.pAllocator = &allocator;
    const bool completed = MapWorkspace_RotateSelection( &s.ws, { 0, 0, 45 }, { 11, -7, 3 } ); s.gui.pAllocator = original;
    CHECK_FALSE( completed ); CHECK( calls > 0 ); CHECK( s.ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
    CHECK( s.ws.pDocument == document ); CHECK( s.Steps() == steps ); CHECK( s.ws.selection.revision == selectionRevision ); CHECK( ModifierMapText( s.ws.pDocument ) == before );
    REQUIRE( EditorSelection_Count( &s.ws.selection ) == 2 ); CHECK( EditorSelection_At( &s.ws.selection, 0 ) == ordinary ); CHECK( EditorSelection_At( &s.ws.selection, 1 ) == mesh );
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 45 }, { 11, -7, 3 } ) ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( s.ws.pDocument ) == before );
}

namespace
{
u64 CreateScalePrefab( session_t &session, const QTemporaryDir &content )
{
    REQUIRE( content.isValid() ); const QString root = content.path();
    MapAssets_SetRoots( &session.ws, { root } );
    MapHotspots_SetResolver( &session.ws, [root]( const QString &path ) {
        const auto file = QDir( root ).filePath( path ); return QFileInfo::exists( file ) ? file : QString();
    } );
    session.Box( { -64, -32, 0 }, { 64, 32, 96 } );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.prefab.create_instance prefabs/scale_test" ) ) == command_result_t::OK );
    const u64 instance = EditorSelection_At( &session.ws.selection, 0 );
    map_instance_info_t info{}; REQUIRE( MapInstances_Info( &session.ws, instance, &info ) ); REQUIRE( info.bLoaded ); return instance;
}
std::vector<math::vec3d_t> ScalePrefabPoints( const session_t &session, u64 instance )
{
    const auto *object = MapWireframe_FindObject( session.ws.wire, instance ); REQUIRE( object );
    REQUIRE( object->nPoints > 0 ); const auto *first = session.ws.wire.points.pData + object->iFirstPoint;
    return { first, first + object->nPoints };
}
void CheckScaledPrefabPoints( const session_t &session, u64 instance, const std::vector<math::vec3d_t> &before,
    math::vec3d_t factors, math::vec3d_t pivot )
{
    const auto after = ScalePrefabPoints( session, instance ); REQUIRE( after.size() == before.size() );
    for ( usize i = 0; i < before.size(); ++i ) {
        CHECK( after[i].x == Catch::Approx( pivot.x + factors.x * ( before[i].x - pivot.x ) ).margin( 1e-8 ) );
        CHECK( after[i].y == Catch::Approx( pivot.y + factors.y * ( before[i].y - pivot.y ) ).margin( 1e-8 ) );
        CHECK( after[i].z == Catch::Approx( pivot.z + factors.z * ( before[i].z - pivot.z ) ).margin( 1e-8 ) );
    }
}
void ShowPrefabScaleView( QWidget *view )
{
    view->resize( 800, 600 ); view->show(); view->activateWindow(); QCoreApplication::processEvents();
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow( view );
    QT_WARNING_POP
    view->setFocus(); QCoreApplication::processEvents(); REQUIRE( QApplication::focusWidget() == view );
}
}

TEST_CASE( "Linked prefab scale uses the native gizmo and numeric panel with exact placed points and history", "[map][gui][object-tools][prefab-scale][native]" )
{
    QTemporaryDir content; session_t session; const u64 instance = CreateScalePrefab( session, content );
    REQUIRE( MapWorkspace_RotateSelection( &session.ws, { 0, 0, 90 }, {} ) );
    const math::vec3d_t pivot{ 11, -7, 3 }; MapToolGestures_SetPivot( &session.ws, pivot );
    MapWorkspace_SetTool( &session.ws, map_tool_t::SCALE ); MapWorkspace_SetScaleSnap( &session.ws, .25 );
    math::vec3d_t actualPivot{}; REQUIRE( MapToolGestures_Pivot( &session.ws, &actualPivot ) );
    REQUIRE( math::Vec3d_EqualsExact( actualPivot, pivot ) ); REQUIRE( MapWorkspace_CanTurnSelection( &session.ws ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.ws, map_ortho_axes_t::TOP ) ); ShowPrefabScaleView( top.get() );
    const auto before = ScalePrefabPoints( session, instance ); const auto beforeText = ModifierMapText( session.ws.pDocument );
    // Root Scale draws/picks its full 64*gizmo_scale logical-pixel shaft;
    // unlike Select, it does not shorten the shaft for bounds resize caps.
    const auto steps = session.Steps();
    const f64 length = 64 * EditorSettings_Real( &session.gui.settings, "editor.viewport.gizmo_scale", 1.0 ) / MapOrthoView_Zoom( top.get() );
    const auto start = MapOrthoView_WorldToView( top.get(), { pivot.x + length * .7, pivot.y } );
    const auto end = MapOrthoView_WorldToView( top.get(), { pivot.x + length * .7 + 32, pivot.y } );
    REQUIRE( top->rect().contains( start.toPoint() ) ); REQUIRE( top->rect().contains( end.toPoint() ) );
    REQUIRE( QLineF( start, end ).length() > 3 );
    ModifierMouse( top.get(), QEvent::MouseButtonPress, start ); ModifierMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( session.ws.editPreview.bActive ); const auto factors = session.ws.editPreview.transform.factors;
    REQUIRE( factors.x > 1 ); CHECK( factors.y == 1 ); CHECK( factors.z == 1 );
    ModifierMouse( top.get(), QEvent::MouseButtonRelease, end ); REQUIRE( session.Steps() == steps + 1 );
    CheckScaledPrefabPoints( session, instance, before, factors, pivot );
    map_instance_info_t info{}; REQUIRE( MapInstances_Info( &session.ws, instance, &info ) );
    CHECK( info.angles.y == 90 ); CHECK( info.scale.x == Catch::Approx( 1 ) ); CHECK( info.scale.y == Catch::Approx( factors.x ) );
    const auto gizmoText = ModifierMapText( session.ws.pDocument );
    REQUIRE( MapWorkspace_Undo( &session.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( session.ws.pDocument ) == beforeText );
    REQUIRE( MapWorkspace_Redo( &session.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( session.ws.pDocument ) == gizmoText );
    CheckScaledPrefabPoints( session, instance, before, factors, pivot );
    REQUIRE( MapWorkspace_Undo( &session.ws ) == editor_history_status_t::OK );
    std::unique_ptr<QDialog> dialog( MapTransformDialog_Create( nullptr, &session.ws ) );
    auto *scale = dialog->findChild<QWidget *>( QStringLiteral( "MapScaleControls" ) ); REQUIRE( scale );
    auto *apply = scale->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ); REQUIRE( apply ); REQUIRE( apply->isEnabled() );
    auto *pivotMode = scale->findChild<QComboBox *>( QStringLiteral( "MapTransformPivot" ) ); REQUIRE( pivotMode ); pivotMode->setCurrentIndex( 2 );
    auto *x = scale->findChild<QDoubleSpinBox *>( QStringLiteral( "MapScaleX" ) );
    auto *y = scale->findChild<QDoubleSpinBox *>( QStringLiteral( "MapScaleY" ) );
    auto *z = scale->findChild<QDoubleSpinBox *>( QStringLiteral( "MapScaleZ" ) ); REQUIRE( x ); REQUIRE( y ); REQUIRE( z );
    x->setValue( 2 ); y->setValue( 3 ); z->setValue( 4 ); apply->click();
    REQUIRE( session.Steps() == steps + 1 ); CheckScaledPrefabPoints( session, instance, before, { 2, 3, 4 }, pivot );
    REQUIRE( MapInstances_Info( &session.ws, instance, &info ) ); CHECK( info.scale.x == Catch::Approx( 3 ) ); CHECK( info.scale.y == Catch::Approx( 2 ) ); CHECK( info.scale.z == Catch::Approx( 4 ) );
    const auto numericText = ModifierMapText( session.ws.pDocument );
    REQUIRE( MapWorkspace_Undo( &session.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( session.ws.pDocument ) == beforeText );
    REQUIRE( MapWorkspace_Redo( &session.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( session.ws.pDocument ) == numericText );
    dialog.reset(); const QString file = QDir( content.path() ).filePath( QStringLiteral( "scaled.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &session.ws, file ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( &session.ws, file ).status == map_files_status_t::OK );
    CheckScaledPrefabPoints( session, instance, before, { 2, 3, 4 }, pivot );
    REQUIRE( MapInstances_Info( &session.ws, instance, &info ) ); CHECK( info.bLoaded ); CHECK( info.source == QStringLiteral( "prefabs/scale_test.cyprefab" ) );
}

TEST_CASE( "A linked prefab shear refusal and publication failure preserve the live selection and permit retry", "[map][gui][object-tools][prefab-scale][allocation]" )
{
    QTemporaryDir content; session_t session; const u64 instance = CreateScalePrefab( session, content );
    REQUIRE( MapWorkspace_RotateSelection( &session.ws, { 0, 0, 45 }, {} ) );
    const u64 ordinary = session.Box( { -160, 0, 0 }, { -128, 32, 32 } ); session.Select( { ordinary, instance } );
    const std::vector<u64> selectedIds( session.ws.selection.ids.pData, session.ws.selection.ids.pData + session.ws.selection.ids.nCount );
    const auto before = ModifierMapText( session.ws.pDocument ); const auto *document = session.ws.pDocument;
    const auto revision = document->geometry.revision, selected = session.ws.selection.revision, next = document->nextId;
    const auto partNext = document->geometry.sourceIds.allocator.next.value;
    const auto steps = session.Steps();
    CHECK_FALSE( MapWorkspace_ScaleSelection( &session.ws, { 2, 1, 1 }, { 11, -7, 3 } ) );
    CHECK( session.ws.lastEdit.status == map_status_t::PREFAB_TRANSFORM_UNSUPPORTED );
    CHECK( MapNotice_Last( &session.ws ).contains( QStringLiteral( "would shear the rotated linked prefab" ) ) );
    CHECK( session.ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( document->nextId == next );
    CHECK( document->geometry.sourceIds.allocator.next.value == partNext ); CHECK( session.ws.selection.revision == selected );
    CHECK( session.Steps() == steps ); CHECK( ModifierMapText( session.ws.pDocument ) == before );
    usize calls = 0; allocator_t failing{}; failing.pUserData = &calls;
    failing.pfnAllocate = []( void *context, usize, usize ) noexcept -> void * { ++*static_cast<usize *>( context ); return nullptr; };
    failing.pfnFree = []( void *, void *, usize, usize ) noexcept {};
    const auto *allocator = session.gui.pAllocator; session.gui.pAllocator = &failing;
    const bool result = MapWorkspace_ScaleSelection( &session.ws, { 2, 2, 3 }, { 11, -7, 3 } ); session.gui.pAllocator = allocator;
    CHECK_FALSE( result ); CHECK( calls > 0 ); CHECK( session.ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
    CHECK( session.ws.pDocument == document ); CHECK( session.ws.selection.revision == selected ); CHECK( session.Steps() == steps );
    CHECK( ModifierMapText( session.ws.pDocument ) == before );
    const auto points = ScalePrefabPoints( session, instance );
    REQUIRE( MapWorkspace_ScaleSelection( &session.ws, { 2, 2, 3 }, { 11, -7, 3 } ) ); CHECK( session.Steps() == steps + 1 );
    CheckScaledPrefabPoints( session, instance, points, { 2, 2, 3 }, { 11, -7, 3 } );
    REQUIRE( MapWorkspace_Undo( &session.ws ) == editor_history_status_t::OK ); CHECK( ModifierMapText( session.ws.pDocument ) == before );
    REQUIRE( EditorSelection_Count( &session.ws.selection ) == selectedIds.size() );
    for ( usize i = 0; i < selectedIds.size(); ++i ) { CHECK( EditorSelection_At( &session.ws.selection, i ) == selectedIds[i] ); }
}

TEST_CASE( "Linked prefab placement retains exact quarters without snapping nearby real angles", "[map][gui][object-tools][prefab-scale]" )
{
    for ( const f64 yaw : { 90.0, -90.0, 450.0 } ) {
        CAPTURE( yaw );
        const auto transform = MapInstances_Transform( {}, { 0, yaw, 0 }, { 2, 3e16, 4 } );
        const auto placed = math::Affine3d_TransformPoint( transform, { 2, -3, 7 } );
        const f64 sign = yaw == -90 ? -1 : 1;
        CHECK( placed.x == sign * 9e16 ); CHECK( placed.y == sign * 4 ); CHECK( placed.z == 28 );
    }
    constexpr f64 rad = 3.14159265358979323846 / 180;
    for ( const f64 yaw : { 1e-12, 90.0 + 1e-12 } ) {
        CAPTURE( yaw );
        const auto transform = MapInstances_Transform( {}, { 0, yaw, 0 }, { 1e16, 1, 1 } );
        const auto placed = math::Affine3d_TransformPoint( transform, { 1, 0, 0 } );
        CHECK( placed.x == Catch::Approx( 1e16 * std::cos( yaw * rad ) ).margin( 1e-10 ) );
        CHECK( placed.y == Catch::Approx( 1e16 * std::sin( yaw * rad ) ).margin( 1e-10 ) );
        CHECK( std::fabs( yaw < 1 ? placed.y : placed.x ) > 100 );
    }
}
