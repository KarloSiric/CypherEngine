//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolGestures_Tests.cpp
//  Purpose: Drives the creation and modelling tools through real mouse and
//           key events on 2D panes and checks what each one authors.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_Notice.h"
#include "CypherMapGui_Layers.h"
#include "CypherMap_Edit.h"
#include "CypherMap_TextureEdit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentSurfaces.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentBrushAttributes.h"
#include "CypherEditor/Geometry/Mesh/CypherGeometry_MeshSource.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QImage>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    settings_document_t store{};
    std::unique_ptr<QWidget> top;
    session_t()
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
        // A user scope, so tools that write settings (Eyedropper) can.
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &store );
        top.reset( MapOrthoView_Create( nullptr, &workspace, map_ortho_axes_t::TOP ) );
        top->resize( 800, 600 ); top->show(); QCoreApplication::processEvents();
        Frame( { -512, -512, -512 }, { 512, 512, 512 } );
    }
    ~session_t()
    {
        top.reset();
        MapWorkspace_Shutdown( &workspace );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
        SettingsDocument_Shutdown( &store );
        gui::EditorGui_Shutdown( &gui );
    }
    // Frames a fixed region so world points map to known pixels.
    void Frame( math::vec3d_t low, math::vec3d_t high )
    {
        workspace.frameBounds.box = { low, high }; workspace.frameBounds.bHas = CY_TRUE;
        MapWorkspace_Notify( &workspace, MAP_CHANGE_FRAME ); QCoreApplication::processEvents();
    }
    QPointF At( f64 x, f64 y ) { return MapOrthoView_WorldToView( top.get(), { x, y } ); }
    void Mouse( QEvent::Type type, QPointF at, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event( type, at, top->mapToGlobal( at ), button, buttons, modifiers );
        QCoreApplication::sendEvent( top.get(), &event );
    }
    void Click( f64 x, f64 y, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        Mouse( QEvent::MouseMove, At( x, y ), Qt::NoButton, modifiers );
        Mouse( QEvent::MouseButtonPress, At( x, y ), Qt::LeftButton, modifiers );
        Mouse( QEvent::MouseButtonRelease, At( x, y ), Qt::NoButton, modifiers );
    }
    void Drag( f64 x0, f64 y0, f64 x1, f64 y1, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        Mouse( QEvent::MouseButtonPress, At( x0, y0 ), Qt::LeftButton, modifiers );
        Mouse( QEvent::MouseMove, At( ( x0 + x1 ) / 2, ( y0 + y1 ) / 2 ), Qt::LeftButton, modifiers );
        Mouse( QEvent::MouseMove, At( x1, y1 ), Qt::LeftButton, modifiers );
        Mouse( QEvent::MouseButtonRelease, At( x1, y1 ), Qt::NoButton, modifiers );
    }
    void Key( int key )
    {
        QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &press );
        QKeyEvent release( QEvent::KeyRelease, key, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &release );
    }
    void Tool( const char *command ) { REQUIRE( EditorCommands_Execute( &gui.commands, StringView_FromCString( command ), {} ) == command_result_t::OK ); }
    usize Steps() { return EditorHistory_StepCount( &workspace.history ); }
    u64 Selected() { return workspace.selection.ids.nCount == 1 ? workspace.selection.ids.pData[0] : 0; }
};

u64 AddBox( map_workspace_t &ws, math::vec3d_t low, math::vec3d_t high )
{
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, low ); MapBounds_AddPoint( bounds, high );
    REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
    return EditorSelection_At( &ws.selection, 0 );
}
usize MeshFaces( const map_workspace_t &ws, u64 id )
{
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
    const auto *mesh = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { id } ); REQUIRE( mesh );
    REQUIRE( geo::MeshSource_TryDescribe( mesh, &d ) == geo::geometry_status_t::OK );
    const usize n = d.faces.nCount;
    geo::MeshSourceDescription_Shutdown( &d );
    return n;
}
math::aabbd_t Bounds( const map_workspace_t &ws, u64 id )
{
    const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object );
    return object->bounds.box;
}
} // namespace

TEST_CASE( "Every gesture tool is available and selectable", "[map][gui][tool-gestures]" )
{
    session_t s;
    for ( const char *id : { "map.tool.entity", "map.tool.decal", "map.tool.overlay", "map.tool.path", "map.tool.polygon", "map.tool.patch",
                             "map.tool.curve", "map.tool.measure", "map.tool.eyedropper", "map.tool.mirror", "map.tool.paint", "map.tool.knife",
                             "map.tool.loop_cut", "map.tool.vertex", "map.tool.pivot", "map.tool.workplane" } ) {
        CAPTURE( id );
        CHECK( ( EditorCommands_State( &s.gui.commands, StringView_FromCString( id ) ) & COMMAND_STATE_ENABLED ) != 0 );
        s.Tool( id );
    }
}

TEST_CASE( "Entity and Path tools place entities where clicked", "[map][gui][tool-gestures][entity]" )
{
    session_t s;
    s.Tool( "map.tool.entity" );
    const usize steps = s.Steps();
    s.Click( 63, 129 ); // Snaps to the 16-unit grid.
    const u64 entity = s.Selected(); REQUIRE( entity != 0 );
    const auto *object = MapWireframe_FindEntity( s.workspace.wire, entity ); REQUIRE( object );
    CHECK( object->origin.x == Catch::Approx( 64 ) ); CHECK( object->origin.y == Catch::Approx( 128 ) );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( MapWireframe_FindEntity( s.workspace.wire, entity ) == nullptr );

    s.Tool( "map.tool.path" );
    s.Click( 0, 0 ); const u64 first = s.Selected();
    s.Click( 256, 0 ); const u64 second = s.Selected();
    REQUIRE( first != 0 ); REQUIRE( second != 0 ); REQUIRE( first != second );
    const auto *record = MapEntityEdit_FindEntity( s.workspace.pDocument, first ); REQUIRE( record );
    string_view_t target{};
    const auto *properties = KeyValue_Find( record, StringView_FromCString( "properties" ) );
    REQUIRE( KeyValue_GetString( KeyValue_Find( properties != nullptr ? properties : record, StringView_FromCString( "target" ) ), &target ) );
    CHECK( StringView_Equals( target, StringView_FromCString( ( "path_corner_" + std::to_string( second ) ).c_str() ) ) );
}

TEST_CASE( "Polygon and Curve tools build meshes from clicked points", "[map][gui][tool-gestures][polygon]" )
{
    session_t s;
    s.Tool( "map.tool.polygon" );
    s.Click( 0, 0 ); s.Click( 256, 0 ); s.Click( 256, 128 );
    s.Key( Qt::Key_Backspace ); // Removes the last point...
    s.Click( 256, 256 ); s.Click( 0, 256 );
    s.Click( 0, 0 ); // ...and clicking the first point closes the polygon.
    const u64 polygon = s.Selected(); REQUIRE( polygon != 0 );
    CHECK( MeshFaces( s.workspace, polygon ) == 6 );
    const auto box = Bounds( s.workspace, polygon );
    CHECK( box.maximum.x == Catch::Approx( 256 ) ); CHECK( box.maximum.z == Catch::Approx( 64 ) ); // editor.map.block_depth

    s.Tool( "map.tool.curve" );
    s.Click( -256, -256 ); s.Click( 0, -256 ); s.Click( 0, -128 );
    s.Key( Qt::Key_Return );
    const u64 curve = s.Selected(); REQUIRE( curve != 0 ); REQUIRE( curve != polygon );
    CHECK( MeshFaces( s.workspace, curve ) == 2 + 4 * 2 );
}

TEST_CASE( "Curve tool with Patch output draws one curved patch through the clicks", "[map][gui][tool-gestures][curve][patch]" )
{
    session_t s;
    const auto set = [&]( const char *path, const char *text ) {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };
    set( "editor.map.curve_output", "patch" );
    set( "editor.map.curve_shape", "smooth" );
    s.Tool( "map.tool.curve" );
    s.Click( -256, -256 ); s.Click( 0, -256 ); s.Click( 0, -128 ); s.Click( 128, 0 );
    s.Key( Qt::Key_Return );
    const u64 arch = s.Selected(); REQUIRE( arch != 0 );
    const auto *patch = geo::GeometryDocument_FindPatch( &s.workspace.pDocument->geometry, { arch } ); REQUIRE( patch != nullptr );
    CHECK( patch->cColumns == 5u ); // Four clicks: a two-span arch through the ends.
    CHECK( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { arch } ) == nullptr );
    set( "editor.map.curve_shape", "straight" );
    s.Click( -256, -256 ); s.Click( 0, -256 ); s.Click( 0, -128 );
    s.Key( Qt::Key_Return );
    const u64 strip = s.Selected(); REQUIRE( strip != 0 ); REQUIRE( strip != arch );
    patch = geo::GeometryDocument_FindPatch( &s.workspace.pDocument->geometry, { strip } ); REQUIRE( patch != nullptr );
    CHECK( patch->cColumns == 5u ); // Three clicks: two straight spans.
}

TEST_CASE( "Patch tool drags a primitive; Measure changes nothing", "[map][gui][tool-gestures][patch]" )
{
    session_t s;
    s.Tool( "map.tool.patch" );
    s.Drag( 0, 0, 128, 128 );
    const u64 patch = s.Selected(); REQUIRE( patch != 0 );
    CHECK( geo::GeometryDocument_FindPatch( &s.workspace.pDocument->geometry, { patch } ) != nullptr );
    const usize steps = s.Steps();
    s.Tool( "map.tool.measure" );
    s.Drag( 0, 0, 300, 400 );
    CHECK( s.Steps() == steps );
}

TEST_CASE( "Mirror tool reflects the selection across the drawn line, Shift keeps a copy", "[map][gui][tool-gestures][mirror]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { 64, 0, 0 }, { 128, 64, 64 } );
    s.Tool( "map.tool.mirror" );
    s.Drag( 0, -128, 0, 128 ); // The Y axis.
    CHECK( Bounds( s.workspace, box ).minimum.x == Catch::Approx( -128 ) );
    s.Drag( 0, -128, 0, 128, Qt::ShiftModifier );
    CHECK( Bounds( s.workspace, box ).minimum.x == Catch::Approx( -128 ) ); // The original stays...
    REQUIRE( s.Selected() != 0 ); CHECK( s.Selected() != box );
    CHECK( Bounds( s.workspace, s.Selected() ).minimum.x == Catch::Approx( 64 ) ); // ...and its mirror copy is selected.
}

TEST_CASE( "Clip tool cuts meshes as it cuts brushes; Knife cuts faces", "[map][gui][tool-gestures][topology]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 6 );
    s.Tool( "map.tool.clip" ); // Clip mode Both: two closed halves, both selected.
    s.Drag( 0, -256, 0, 256 );
    REQUIRE( s.workspace.editPreview.bClip ); REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( EditorSelection_Count( &s.workspace.selection ) == 2 );
    const u64 other = EditorSelection_At( &s.workspace.selection, 0 ) == mesh ? EditorSelection_At( &s.workspace.selection, 1 ) : EditorSelection_At( &s.workspace.selection, 0 );
    CHECK( MeshFaces( s.workspace, mesh ) == 6 ); CHECK( MeshFaces( s.workspace, other ) == 6 );
    CHECK( Bounds( s.workspace, mesh ).minimum.x == Catch::Approx( 0 ) ); CHECK( Bounds( s.workspace, other ).maximum.x == Catch::Approx( 0 ) );

    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.knife" );
    s.Click( 0, -128 ); s.Click( 128, 128 ); // Two corners of the top face...
    s.Key( Qt::Key_Escape );                 // ...cancelled: nothing changes.
    CHECK( MeshFaces( s.workspace, mesh ) == 6 );
    s.Click( 0, -128 ); s.Click( 128, 128 ); // Corner to corner: cuts one face in two.
    s.Key( Qt::Key_Return );
    CHECK( MeshFaces( s.workspace, mesh ) == 7 );
}

TEST_CASE( "Clip tool cuts a mixed brush and mesh selection in one step", "[map][gui][tool-gestures][clip]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -128, -128, 0 }, { -32, 128, 128 } );
    const u64 mesh = AddBox( s.workspace, { 32, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_Select( &s.workspace, brush, MAP_SELECT_REPLACE ); MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_ADD );
    REQUIRE( MapWorkspace_CanClipSelection( &s.workspace ) ); REQUIRE_FALSE( MapWorkspace_CanEditBrushSelection( &s.workspace ) );
    s.Tool( "map.tool.clip" );
    s.Drag( -256, 0, 256, 0 ); // Along X: the plane y = 0.
    REQUIRE( s.workspace.editPreview.bClip ); REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    // The preview shows all four halves (Both keeps them all).
    usize previewBrushes = 0, previewMeshes = 0;
    for ( usize i = 0; i < s.workspace.editPreviewWire.objects.nCount; ++i ) {
        const auto kind = s.workspace.editPreviewWire.objects.pData[i].kind;
        previewBrushes += kind == map_wire_kind_t::BRUSH; previewMeshes += kind == map_wire_kind_t::MESH;
    }
    CHECK( previewBrushes == 2 ); CHECK( previewMeshes == 2 ); // Both previews show the actual retained halves.
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( EditorSelection_Count( &s.workspace.selection ) == 4 );
    usize meshes = 0;
    for ( usize i = 0; i < 4; ++i ) {
        const u64 id = EditorSelection_At( &s.workspace.selection, i );
        const auto box = Bounds( s.workspace, id );
        CHECK( ( box.maximum.y == Catch::Approx( 0 ) || box.minimum.y == Catch::Approx( 0 ) ) );
        if ( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { id } ) != nullptr ) { ++meshes; CHECK( MeshFaces( s.workspace, id ) == 6 ); }
    }
    CHECK( meshes == 2 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( Bounds( s.workspace, mesh ).minimum.y == Catch::Approx( -128 ) ); CHECK( Bounds( s.workspace, mesh ).maximum.y == Catch::Approx( 128 ) );
}

TEST_CASE( "3D Clip on a surface: draw across a selected face, click two points, drag an endpoint", "[map][gui][tool-gestures][clip][surface-clip]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    s.Tool( "map.tool.clip" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
    const auto at = [&]( math::vec3d_t p ) { QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), p, &screen ) ); return screen; };
    const auto send = [&]( QEvent::Type type, QPointF where ) {
        QMouseEvent event( type, where, camera->mapToGlobal( where ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                           type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    const auto drag = [&]( math::vec3d_t a, math::vec3d_t b ) {
        send( QEvent::MouseButtonPress, at( a ) ); send( QEvent::MouseMove, ( at( a ) + at( b ) ) / 2 ); send( QEvent::MouseMove, at( b ) );
        send( QEvent::MouseButtonRelease, at( b ) );
    };
    const auto click = [&]( math::vec3d_t p ) { send( QEvent::MouseButtonPress, at( p ) ); send( QEvent::MouseButtonRelease, at( p ) ); };
    const auto &preview = s.workspace.editPreview;
    // A line across the top face: the cut is vertical, through the line.
    drag( { 0, -96, 128 }, { 0, 96, 128 } );
    REQUIRE( preview.bActive ); REQUIRE( preview.bClip ); REQUIRE( preview.status == map_status_t::OK );
    CHECK_FALSE( preview.clipGuide.bHas );
    CHECK( std::fabs( preview.clipPlane.normal.x ) == Catch::Approx( 1 ) ); CHECK( preview.clipPlane.d == Catch::Approx( 0 ).margin( 1e-9 ) );
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 ); CHECK( EditorSelection_Count( &s.workspace.selection ) == 2 ); // Both halves.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    MapWorkspace_Select( &s.workspace, box, MAP_SELECT_REPLACE );
    // Two clicks on the face: point 1, then point 2 stages the same kind of cut.
    click( { -64, -96, 128 } );
    CHECK_FALSE( preview.bActive );
    click( { -64, 96, 128 } );
    REQUIRE( preview.bActive ); CHECK( std::fabs( preview.clipPlane.normal.x ) == Catch::Approx( 1 ) );
    CHECK( std::fabs( preview.clipPlane.d ) == Catch::Approx( 64 ) );
    // Dragging point 2 swings the cut; point 1 stays.
    drag( { -64, 96, 128 }, { 64, 96, 128 } );
    REQUIRE( preview.bActive ); REQUIRE( preview.status == map_status_t::OK );
    const auto n = preview.clipPlane.normal;
    CHECK( std::fabs( n.x ) == Catch::Approx( std::sqrt( 9.0 / 13.0 ) ).margin( 1e-6 ) ); CHECK( std::fabs( n.y ) == Catch::Approx( std::sqrt( 4.0 / 13.0 ) ).margin( 1e-6 ) );
    CHECK( n.x * -64 + n.y * -96 + preview.clipPlane.d == Catch::Approx( 0 ).margin( 1e-6 ) );
    // A press off the selection draws on the construction plane instead.
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &escape );
    CHECK_FALSE( preview.bActive );
    drag( { 16, -400, 64 }, { -16, -300, 64 } );
    if ( preview.bActive ) { CHECK( preview.clipGuide.bHas ); }
    CHECK( s.Steps() == steps + 1 );
}

TEST_CASE( "3D Clip locks onto vertices, takes a third point, and picks what to cut when nothing is selected", "[map][gui][tool-gestures][clip][surface-clip][clip-vertices]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    MapWorkspace_SetSelection( &s.workspace, nullptr, 0 );
    s.Tool( "map.tool.clip" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &s.workspace, CY_FALSE ); QCoreApplication::processEvents();
    const auto at = [&]( math::vec3d_t p ) { QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), p, &screen ) ); return screen; };
    const auto send = [&]( QEvent::Type type, QPointF where, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        QMouseEvent event( type, where, camera->mapToGlobal( where ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                           type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    const auto click = [&]( QPointF p ) { send( QEvent::MouseButtonPress, p ); send( QEvent::MouseButtonRelease, p ); };
    const auto &preview = s.workspace.editPreview;
    // Nothing selected: the first click picks the box to cut.
    click( at( { 0, 0, 128 } ) );
    REQUIRE( MapWorkspace_IsSelected( &s.workspace, box ) ); CHECK_FALSE( preview.bActive );
    // A few pixels off two top corners: the cut runs exactly corner to corner.
    click( at( { -128, -128, 128 } ) + QPointF( 4, 3 ) );
    click( at( { 128, 128, 128 } ) + QPointF( -3, 4 ) );
    REQUIRE( preview.bActive ); REQUIRE( preview.bClip ); REQUIRE( preview.status == map_status_t::OK );
    const auto diagonal = preview.clipPlane;
    CHECK( std::fabs( diagonal.normal.x ) == Catch::Approx( std::sqrt( 0.5 ) ).margin( 1e-9 ) );
    CHECK( std::fabs( diagonal.normal.y ) == Catch::Approx( std::sqrt( 0.5 ) ).margin( 1e-9 ) );
    CHECK( diagonal.normal.z == Catch::Approx( 0 ).margin( 1e-9 ) ); CHECK( diagonal.d == Catch::Approx( 0 ).margin( 1e-9 ) );
    // A third click, on a side the camera sees, tilts the cut through it.
    const auto eye = MapCameraView_Position( camera.get() );
    const math::vec3d_t third = eye.x > 128 ? math::vec3d_t{ 128, 0, 32 } : eye.x < -128 ? math::vec3d_t{ -128, 0, 32 } :
                                eye.y > 128 ? math::vec3d_t{ 0, 128, 32 } : math::vec3d_t{ 0, -128, 32 };
    click( at( third ) );
    REQUIRE( preview.bActive ); REQUIRE( preview.status == map_status_t::OK );
    const auto &plane = preview.clipPlane;
    CHECK( std::fabs( plane.normal.z ) > 0.1 );
    for ( const math::vec3d_t p : { math::vec3d_t{ -128, -128, 128 }, math::vec3d_t{ 128, 128, 128 }, third } ) {
        CHECK( plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z + plane.d == Catch::Approx( 0 ).margin( 1e-6 ) );
    }
    CHECK( plane.normal.x * diagonal.normal.x + plane.normal.y * diagonal.normal.y > 0 ); // The same side stays in front.
    // Backspace (in the camera view) takes point 3 back.
    QKeyEvent backspace( QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &backspace );
    CHECK( preview.clipPlane.normal.z == Catch::Approx( 0 ).margin( 1e-9 ) );
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 ); CHECK( EditorSelection_Count( &s.workspace.selection ) == 2 );
}

TEST_CASE( "After cutting, the kept side can still change until Enter (2D line and 3D points)", "[map][gui][tool-gestures][clip][clip-keep]" )
{
    for ( const bool camera3d : { false, true } ) {
        CAPTURE( camera3d );
        session_t s;
        const u64 box = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
        s.Tool( "map.tool.clip" );
        const auto &preview = s.workspace.editPreview;
        std::unique_ptr<QWidget> camera;
        if ( camera3d ) {
            camera.reset( MapCameraView_Create( nullptr, &s.workspace ) );
            camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
            MapWorkspace_Frame( &s.workspace, CY_FALSE ); QCoreApplication::processEvents();
            const auto click = [&]( math::vec3d_t p ) {
                QPointF at; REQUIRE( MapCameraView_WorldToView( camera.get(), p, &at ) );
                QMouseEvent press( QEvent::MouseButtonPress, at, camera->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
                QCoreApplication::sendEvent( camera.get(), &press );
                QMouseEvent release( QEvent::MouseButtonRelease, at, camera->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
                QCoreApplication::sendEvent( camera.get(), &release );
            };
            click( { 0, -96, 128 } ); click( { 0, 96, 128 } ); // Down the middle of the top.
        } else {
            s.Drag( 0, -256, 0, 256 );
        }
        REQUIRE( preview.bActive ); REQUIRE( preview.bClip ); REQUIRE( preview.status == map_status_t::OK );
        const auto plane = preview.clipPlane;
        CHECK( preview.clipMode == map_brush_clip_mode_t::BOTH );
        // Shift+X again (the tool's own command) cycles Back, Front, Both: the
        // same cut stays staged, only what it keeps changes.
        for ( const auto expected : { map_brush_clip_mode_t::BACK, map_brush_clip_mode_t::FRONT } ) {
            s.Tool( "map.tool.clip" );
            REQUIRE( preview.bActive ); CHECK( preview.clipMode == expected );
            CHECK( preview.clipPlane.d == plane.d ); CHECK( preview.clipPlane.normal.x == plane.normal.x );
            CHECK( s.workspace.pDocument->geometry.brushes.nCount == 1u ); // Nothing applied yet.
        }
        const usize steps = s.Steps();
        if ( camera3d ) { QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &enter ); }
        else { s.Key( Qt::Key_Return ); }
        CHECK( s.Steps() == steps + 1 );
        REQUIRE( s.workspace.pDocument->geometry.brushes.nCount == 1u ); // Front only.
        const auto kept = Bounds( s.workspace, EditorSelection_At( &s.workspace.selection, 0 ) );
        CHECK( kept.maximum.x - kept.minimum.x == Catch::Approx( 128 ) );
        ( void )box;
    }
}

TEST_CASE( "Displacement tool raises the surface under the brush in one undo step; Escape puts a stroke back", "[map][gui][tool-gestures][displace]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -256, -256, 0 }, { 256, 256, 64 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    u64 top = 0;
    for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
        const auto &face = s.workspace.wire.faces.pData[f];
        if ( face.id == mesh && face.normal.z > 0.99 ) { top = face.faceId; }
    }
    REQUIRE( top != 0 );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &s.workspace, mesh, top );
    REQUIRE( MapWorkspace_QuadSliceMeshFace( &s.workspace, 8, 8 ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::OBJECTS );
    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.displace" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
    const auto send = [&]( QEvent::Type type, math::vec3d_t p ) {
        QPointF at; REQUIRE( MapCameraView_WorldToView( camera.get(), p, &at ) );
        QMouseEvent event( type, at, camera->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                           type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    const usize steps = s.Steps();
    send( QEvent::MouseButtonPress, { -64, 0, 64 } );
    for ( int i = 1; i <= 8; ++i ) { send( QEvent::MouseMove, { -64.0 + 16.0 * i, 0, 64 } ); }
    send( QEvent::MouseButtonRelease, { 64, 0, 64 } );
    CHECK( s.Steps() == steps + 1 );
    const auto raised = Bounds( s.workspace, mesh );
    CHECK( raised.maximum.z > 64.5 ); CHECK( raised.minimum.z == Catch::Approx( 0 ) );
    // A second stroke cancelled with Escape leaves the map and the views as they were.
    send( QEvent::MouseButtonPress, { 128, 128, 64 } );
    for ( int i = 1; i <= 4; ++i ) { send( QEvent::MouseMove, { 128.0 - 16.0 * i, 128, 64 } ); }
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &escape );
    send( QEvent::MouseButtonRelease, { 64, 128, 64 } );
    CHECK( s.Steps() == steps + 1 );
    CHECK( Bounds( s.workspace, mesh ).maximum.z == Catch::Approx( raised.maximum.z ) );
}

TEST_CASE( "Vertex tool moves a corner shared by selected brushes in all of them (vertex clumping)", "[map][gui][tool-gestures][vertex][clump]" )
{
    session_t s;
    const u64 left = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 128 } );
    const u64 right = AddBox( s.workspace, { 128, 0, 0 }, { 256, 128, 128 } );
    MapWorkspace_Select( &s.workspace, left, MAP_SELECT_REPLACE ); MapWorkspace_Select( &s.workspace, right, MAP_SELECT_ADD );
    s.Tool( "map.tool.vertex" );
    const usize steps = s.Steps();
    s.Drag( 128, 128, 160, 192 ); // The top-view corner both boxes share (both heights move together in 2D).
    CHECK( s.Steps() == steps + 1 );
    CHECK( Bounds( s.workspace, left ).maximum.y == Catch::Approx( 192 ) );
    CHECK( Bounds( s.workspace, right ).maximum.y == Catch::Approx( 192 ) );
}

TEST_CASE( "Vertex tool drags brush corners and selected mesh vertices", "[map][gui][tool-gestures][vertex]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 128 } );
    s.Tool( "map.tool.vertex" );
    s.Drag( 128, 128, 192, 192 );
    CHECK( Bounds( s.workspace, brush ).maximum.x == Catch::Approx( 192 ) );
    CHECK( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ) != nullptr );
}

TEST_CASE( "Eyedropper takes a face's material; Pivot centres Rotate 90", "[map][gui][tool-gestures][surface]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { 0, 0, 0 }, { 128, 64, 64 } );
    s.Tool( "map.tool.eyedropper" );
    s.Click( 32, 32 );
    const auto material = EditorSettings_Text( &s.gui.settings, "editor.map.default_material", {} );
    CHECK( material.cchLength != 0 );

    s.Tool( "map.tool.pivot" );
    s.Click( 0, 0 );
    REQUIRE( MapToolGestures_Pivot( &s.workspace, nullptr ) );
    CHECK( ( EditorCommands_State( &s.gui.commands, StringView_FromCString( "map.pivot.clear" ) ) & COMMAND_STATE_ENABLED ) != 0 );
    MapWorkspace_Select( &s.workspace, box, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.select" );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.transform.rotate_ccw" ), {} ) == command_result_t::OK );
    // About (0, 0): the box now spans negative X.
    CHECK( Bounds( s.workspace, box ).minimum.x == Catch::Approx( -64 ) );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.pivot.clear" ), {} ) == command_result_t::OK );
    CHECK_FALSE( MapToolGestures_Pivot( &s.workspace, nullptr ) );
}

TEST_CASE( "Decal and Paint tools work on surfaces", "[map][gui][tool-gestures][decal]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { 0, 0, 0 }, { 256, 256, 64 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    s.Tool( "map.tool.decal" );
    s.Click( 128, 128 );
    const u64 decal = s.Selected(); REQUIRE( decal != 0 ); REQUIRE( decal != mesh );
    const auto *entity = MapWireframe_FindEntity( s.workspace.wire, decal ); REQUIRE( entity );
    CHECK( std::string( entity->className ) == "infodecal" );
    CHECK( entity->origin.z == Catch::Approx( 64 ) ); // On the top face.

    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.paint" );
    const usize steps = s.Steps();
    s.Drag( 0, 0, 32, 0 );
    CHECK( s.Steps() == steps + 1 ); // Paints the corner's vertex: the mesh keeps its faces.
}

namespace
{
// The mesh vertex authored at a position, from the wire's source IDs.
geo::geometry_source_id_t VertexAt( const map_workspace_t &ws, u64 mesh, math::vec3d_t at )
{
    const auto *object = MapWireframe_FindObject( ws.wire, mesh ); REQUIRE( object );
    for ( u32 i = 0; i < object->nPoints; ++i ) {
        const auto p = ws.wire.points.pData[object->iFirstPoint + i];
        if ( std::fabs( p.x - at.x ) < 1e-6 && std::fabs( p.y - at.y ) < 1e-6 && std::fabs( p.z - at.z ) < 1e-6 ) {
            return { ws.wire.pointSourceIds.pData[object->iFirstPoint + i] };
        }
    }
    FAIL( "no vertex at the position" );
    return {};
}
math::vec3d_t Position( const map_workspace_t &ws, u64 mesh, geo::geometry_source_id_t id )
{
    const auto *source = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } ); REQUIRE( source );
    geo::geometry_mesh_vertex_handle_t handle{};
    REQUIRE( geo::MeshSource_TryFindVertex( source, id, &handle ) );
    return GenerationPool_Get( &source->mesh.vertices, handle )->position;
}
bool VertexSelectedIn( const map_workspace_t &ws, geo::geometry_source_id_t id )
{
    return geo::MeshSelection_HasVertex( &ws.meshSelection, id );
}
u64 FaceFacing( const map_workspace_t &ws, u64 mesh, math::vec3d_t normal )
{
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) {
        const auto &face = ws.wire.faces.pData[f];
        if ( face.id == mesh && face.normal.x * normal.x + face.normal.y * normal.y + face.normal.z * normal.z > 0.99 ) { return face.faceId; }
    }
    FAIL( "no face with that normal" );
    return 0;
}
// A point on the Top pane's +X gizmo handle of a pivot, in world units.
f64 OnXHandle( session_t &s, math::vec3d_t pivot )
{
    const QPointF centre = s.At( pivot.x, pivot.y ), beyond = s.At( pivot.x + 100, pivot.y );
    return pivot.x + 100.0 * 40.0 / ( beyond.x() - centre.x() ); // 40 px along the axis.
}
} // namespace

TEST_CASE( "Component gizmo moves selected vertices along an axis and keeps them selected", "[map][gui][tool-gestures][gizmo]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    const auto corner = VertexAt( s.workspace, mesh, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_SelectMeshVertex( &s.workspace, mesh, corner, MAP_SELECT_REPLACE ) );
    s.Tool( "map.tool.translate" );
    REQUIRE( MapToolGestures_Owns( &s.workspace, true ) );
    const usize steps = s.Steps();
    const f64 grab = OnXHandle( s, { 64, 64, 128 } );
    s.Drag( grab, 64, grab + 64, 64 + 40 ); // The X handle keeps Y.
    CHECK( s.Steps() == steps + 1 );
    const auto moved = Position( s.workspace, mesh, corner );
    CHECK( moved.x == Catch::Approx( 128 ) ); CHECK( moved.y == Catch::Approx( 64 ) ); CHECK( moved.z == Catch::Approx( 128 ) );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 1 ); // Ready for the next drag.
    CHECK( s.workspace.meshSelection.vertices.pData[0].value == corner.value );
    CHECK( Position( s.workspace, mesh, VertexAt( s.workspace, mesh, { 64, -64, 128 } ) ).x == Catch::Approx( 64 ) );

    // Away from the handles a press is left to picking: nothing moves.
    s.Drag( -300, -300, -200, -300 );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( Position( s.workspace, mesh, corner ).x == Catch::Approx( 64 ) );
}

TEST_CASE( "Component gizmo scales selected vertices about their centre", "[map][gui][tool-gestures][gizmo]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    bool first = true;
    for ( const math::vec3d_t at : { math::vec3d_t{ -64, -64, 128 }, { 64, -64, 128 }, { 64, 64, 128 }, { -64, 64, 128 } } ) {
        REQUIRE( MapWorkspace_SelectMeshVertex( &s.workspace, mesh, VertexAt( s.workspace, mesh, at ), first ? MAP_SELECT_REPLACE : MAP_SELECT_ADD ) );
        first = false;
    }
    s.Tool( "map.tool.scale" );
    const f64 ring = 60.0 / MapOrthoView_Zoom( s.top.get() ); // Grab the painted scale ring.
    s.Drag( ring, 0, ring * 2, 0 );                         // Double its radius: twice the size.
    CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 128 ) );
    CHECK( Bounds( s.workspace, mesh ).minimum.y == Catch::Approx( -128 ) );
    CHECK( Bounds( s.workspace, mesh ).maximum.z == Catch::Approx( 128 ) ); // The Top pane's depth keeps its scale.
    CHECK( s.workspace.meshSelection.vertices.nCount == 4 );
}

TEST_CASE( "Component gizmo: Shift extrudes a face, Rotate turns edges, a brush face moves", "[map][gui][tool-gestures][gizmo]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &s.workspace, mesh, FaceFacing( s.workspace, mesh, { 1, 0, 0 } ) );
    REQUIRE( MapWorkspace_HasMeshFace( &s.workspace ) );
    s.Tool( "map.tool.translate" );
    const f64 grab = OnXHandle( s, { 64, 0, 64 } );
    s.Drag( grab, 0, grab + 64, 0, Qt::ShiftModifier );
    CHECK( MeshFaces( s.workspace, mesh ) == 10 );
    CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 128 ) );

    // Edges: the top edge at +Y turns 90 degrees about its middle.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    s.Tool( "map.tool.select" );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { -64, 64, 128 } ), VertexAt( s.workspace, mesh, { 64, 64, 128 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
    s.Tool( "map.tool.rotate" );
    const f64 r = 60.0 / MapOrthoView_Zoom( s.top.get() ); // On the visible rotation ring.
    s.Drag( r, 64, 0, 64 + r );                               // A quarter turn counter-clockwise.
    const auto a = Position( s.workspace, mesh, edge.a ), b = Position( s.workspace, mesh, edge.b );
    CHECK( std::fabs( a.x ) == Catch::Approx( 0 ).margin( 1e-6 ) ); CHECK( std::fabs( b.x ) == Catch::Approx( 0 ).margin( 1e-6 ) );
    CHECK( std::fabs( a.y - b.y ) == Catch::Approx( 128 ) );
    CHECK( s.workspace.meshSelection.edges.nCount == 1 );

    // A brush face moves with the Translate tool.
    const u64 brush = AddBox( s.workspace, { 200, 0, 0 }, { 264, 64, 64 } );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    const auto *source = geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ); REQUIRE( source );
    u64 side = 0;
    for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
        const auto &face = s.workspace.wire.faces.pData[f];
        if ( face.id == brush && face.normal.x > 0.99 ) { side = face.sideId; }
    }
    REQUIRE( side != 0 );
    MapWorkspace_SelectBrushFace( &s.workspace, brush, side );
    s.Tool( "map.tool.translate" );
    const f64 grabBrush = OnXHandle( s, { 264, 32, 32 } );
    s.Drag( grabBrush, 32, grabBrush + 32, 32 );
    CHECK( Bounds( s.workspace, brush ).maximum.x == Catch::Approx( 296 ) );
    CHECK( s.workspace.selectedBrushFaceObject == brush );
}

TEST_CASE( "Numeric component transforms move, rotate and scale the selected vertices", "[map][gui][tool-gestures][gizmo]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    CHECK_FALSE( MapToolGestures_CanTransformComponents( &s.workspace, map_component_transform_t::TRANSLATE ) ); // Objects mode.
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    std::vector<geo::geometry_source_id_t> top;
    for ( const math::vec3d_t at : { math::vec3d_t{ -64, -64, 128 }, { 64, -64, 128 }, { 64, 64, 128 }, { -64, 64, 128 } } ) {
        top.push_back( VertexAt( s.workspace, mesh, at ) );
        REQUIRE( MapWorkspace_SelectMeshVertex( &s.workspace, mesh, top.back(), top.size() == 1 ? MAP_SELECT_REPLACE : MAP_SELECT_ADD ) );
    }
    REQUIRE( MapToolGestures_CanTransformComponents( &s.workspace, map_component_transform_t::TRANSLATE ) );
    const usize steps = s.Steps();
    REQUIRE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::TRANSLATE, { 0, 0, 32 }, map_component_pivot_t::CENTER ) );
    CHECK( Bounds( s.workspace, mesh ).maximum.z == Catch::Approx( 160 ) );
    CHECK( Bounds( s.workspace, mesh ).minimum.z == Catch::Approx( 0 ) );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 4 ); // Still selected for the next one.
    // Twice as wide about the world origin; Z keeps its height.
    REQUIRE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::SCALE, { 2, 1, 1 }, map_component_pivot_t::WORLD_ORIGIN ) );
    CHECK( Position( s.workspace, mesh, top[1] ).x == Catch::Approx( 128 ) );
    CHECK( Position( s.workspace, mesh, top[1] ).z == Catch::Approx( 160 ) );
    // A quarter turn about the top's centre swaps its extents.
    REQUIRE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::ROTATE, { 0, 0, 90 }, map_component_pivot_t::CENTER ) );
    CHECK( std::fabs( Position( s.workspace, mesh, top[1] ).x ) == Catch::Approx( 64 ) );
    CHECK( std::fabs( Position( s.workspace, mesh, top[1] ).y ) == Catch::Approx( 128 ) );
    // Zero or negative factors and non-finite values change nothing.
    CHECK_FALSE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::SCALE, { 0, 1, 1 }, map_component_pivot_t::CENTER ) );
    CHECK_FALSE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::TRANSLATE, { std::nan( "" ), 0, 0 }, map_component_pivot_t::CENTER ) );
    CHECK( s.Steps() == steps + 3 );
}

TEST_CASE( "Edge Cut cuts between clicked edge points; Edge Arc bends a selected chain", "[map][gui][tool-gestures][edge-tools]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    s.Tool( "map.tool.edge_cut" );
    s.Click( -128, 0 ); s.Click( 128, 0 ); // The middles of the top face's left and right edges.
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( MeshFaces( s.workspace, mesh ) == 7 );
    CHECK( s.Steps() == steps + 1 );
    s.Click( 0, 0 ); // A face interior is not an edge point: nothing staged.
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 6 );

    // Edge Arc: two top edges meeting at (128, -128) bend toward the outside.
    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    s.Tool( "map.tool.select" );
    const auto a = VertexAt( s.workspace, mesh, { -128, -128, 128 } ), b = VertexAt( s.workspace, mesh, { 128, -128, 128 } ),
               c = VertexAt( s.workspace, mesh, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, geo::MeshEdgeRef_Make( a, b ), MAP_SELECT_REPLACE ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, geo::MeshEdgeRef_Make( b, c ), MAP_SELECT_ADD ) );
    s.Tool( "map.tool.edge_arc" );
    s.Drag( 0, 0, 64, -64 ); // Across the chord a -> c: a bulge of ~90.5, snapped to 96.
    const auto moved = Position( s.workspace, mesh, b );
    CHECK( moved.x == Catch::Approx( 96.0 / std::sqrt( 2.0 ) ).margin( 1e-6 ) );
    CHECK( moved.y == Catch::Approx( -96.0 / std::sqrt( 2.0 ) ).margin( 1e-6 ) );
    CHECK( moved.z == Catch::Approx( 128 ) );
    CHECK( Position( s.workspace, mesh, a ).x == Catch::Approx( -128 ) ); // The ends stay.
    CHECK( s.workspace.meshSelection.edges.nCount == 2 );
}

TEST_CASE( "Shift-click picks several mesh faces in a view; the gizmo moves them all", "[map][gui][tool-gestures][face-set]" )
{
    session_t s;
    const u64 a = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const u64 b = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const u64 both[]{ a, b };
    MapWorkspace_SetSelection( &s.workspace, both, 2 );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.merge" ), {} ) == command_result_t::OK );
    const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    s.Tool( "map.tool.select" );
    s.Click( -192, 0 );                        // The left top.
    REQUIRE( MapWorkspace_HasMeshFace( &s.workspace ) );
    const u64 left = s.workspace.selectedMeshFaceId;
    s.Click( 192, 0, Qt::ShiftModifier );      // The right top joins it.
    REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 );
    const u64 right = s.workspace.selectedMeshFaceId;
    CHECK( right != left );
    s.Click( 192, 0, Qt::ControlModifier );    // Ctrl takes it out again...
    CHECK( MapWorkspace_MeshFaces( &s.workspace ).nCount == 1 );
    CHECK( s.workspace.selectedMeshFaceId == left );
    s.Click( 192, 0, Qt::ShiftModifier );      // ...and Shift puts it back.
    REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 );

    // The gizmo sits between the two tops and moves both by the same step.
    s.Tool( "map.tool.translate" );
    const f64 grab = OnXHandle( s, { 0, 0, 128 } );
    s.Drag( grab, 0, grab, 64 ); // The X handle: the drag across it moves nothing...
    CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 256 ) );
    s.Drag( grab, 0, grab + 32, 0 ); // ...along it, both tops move 32 in X.
    CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 288 ) );
    CHECK( Bounds( s.workspace, mesh ).minimum.x == Catch::Approx( -256 ) ); // The bottoms stay.
    CHECK( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 );
}

namespace
{
// Pixels in the preview's amber, whatever the antialiasing blend.
usize AmberPixels( QWidget *view )
{
    const QImage image = view->grab().toImage();
    usize count = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            const QColor c = image.pixelColor( x, y );
            if ( c.red() > 150 && c.green() > 100 && c.green() < c.red() && c.blue() < 90 ) { ++count; }
        }
    }
    return count;
}
} // namespace

TEST_CASE( "Patch and Polygon tools preview their result while drawing", "[map][gui][tool-gestures][preview]" )
{
    session_t s;
    const usize idle = AmberPixels( s.top.get() );
    s.Tool( "map.tool.patch" );
    {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.patch_shape" ) ); REQUIRE( descriptor );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( "sphere" );
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
    s.Mouse( QEvent::MouseButtonPress, s.At( -200, -200 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 200, 200 ), Qt::LeftButton );
    const usize sphere = AmberPixels( s.top.get() );
    if ( const QByteArray shot = qgetenv( "MASON_PREVIEW_SHOT" ); !shot.isEmpty() ) { s.top->grab().save( QString::fromUtf8( shot ) ); }
    CHECK( sphere > idle + 500 ); // Iso-lines across the whole sphere, not just a box outline.
    s.Key( Qt::Key_Escape );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 200, 200 ), Qt::NoButton );
    CHECK( s.workspace.wire.objects.nCount == 0 ); // Cancelled: nothing created.

    s.Tool( "map.tool.polygon" );
    s.Click( -200, -200 ); s.Click( 200, -200 ); s.Click( 0, 200 );
    s.Mouse( QEvent::MouseMove, s.At( -100, 100 ), Qt::NoButton );
    CHECK( AmberPixels( s.top.get() ) > idle );
}

TEST_CASE( "The pivot moves the Rotate tool's ring: a drag turns the selection about it", "[map][gui][tool-gestures][pivot]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { 0, -32, 0 }, { 128, 32, 64 } ); // Centre (64, 0).
    MapWorkspace_SetAngleSnap( &s.workspace, 15 );
    // Drag the Pivot tool to the origin.
    s.Tool( "map.tool.pivot" );
    s.Drag( 64, 64, 0, 0 );
    cypher::math::vec3d_t pivot{};
    REQUIRE( MapToolGestures_Pivot( &s.workspace, &pivot ) );
    CHECK( pivot.x == Catch::Approx( 0 ) ); CHECK( pivot.y == Catch::Approx( 0 ) );
    // The rotation ring now circles the pivot: a quarter turn about (0, 0).
    MapWorkspace_Select( &s.workspace, box, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.rotate" );
    const f64 length = 64.0 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( length, 0, 0, length );
    const auto rotated = Bounds( s.workspace, box );
    CHECK( rotated.minimum.x == Catch::Approx( -32 ).margin( 1e-6 ) ); CHECK( rotated.maximum.x == Catch::Approx( 32 ).margin( 1e-6 ) );
    CHECK( rotated.minimum.y == Catch::Approx( 0 ).margin( 1e-6 ) ); CHECK( rotated.maximum.y == Catch::Approx( 128 ).margin( 1e-6 ) );
    // Escape in the Pivot tool removes it; the ring returns to the selection centre.
    s.Tool( "map.tool.pivot" );
    s.Key( Qt::Key_Escape );
    CHECK_FALSE( MapToolGestures_Pivot( &s.workspace, nullptr ) );
}

TEST_CASE( "Paint tints the painted faces in the colour set, and Ctrl erases it", "[map][gui][tool-gestures][paint]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 64 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const auto topTint = [&]() {
        for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
            const auto &face = s.workspace.wire.faces.pData[f];
            if ( face.id == mesh && face.normal.z > 0.99 ) { return face.colorRgba; }
        }
        return 0u;
    };
    REQUIRE( topTint() == 0xFFFFFFFFu ); // Unpainted: white, no tint.
    s.Tool( "map.tool.paint" );
    s.Drag( -128, -128, 128, 128 ); // Across the top's corners.
    const u32 painted = topTint();
    CHECK( painted != 0xFFFFFFFFu );
    CHECK( ( ( painted >> 24u ) & 0xFFu ) > ( ( painted >> 8u ) & 0xFFu ) ); // The default orange: more red than blue.
    s.Drag( -128, -128, 128, 128, Qt::ControlModifier );
    CHECK( topTint() == 0xFFFFFFFFu ); // Erased back to white.
}

TEST_CASE( "Vertices mode: drag a vertex handle to move it, box-select vertices, drag a brush corner", "[map][gui][tool-gestures][vertex-drag]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    s.Tool( "map.tool.select" );
    REQUIRE( MapToolGestures_Owns( &s.workspace, true ) );
    // Grab the corner at (128, 128) and pull it, without selecting it first
    // (TrenchBroom): the 2D handle is the column of both stacked corners
    // (Hammer), which moves in one undo step and stays selected.
    const auto top = VertexAt( s.workspace, mesh, { 128, 128, 128 } ), bottom = VertexAt( s.workspace, mesh, { 128, 128, 0 } );
    const usize steps = s.Steps();
    s.Drag( 128, 128, 192, 176 );
    CHECK( s.Steps() == steps + 1 );
    CHECK( Position( s.workspace, mesh, top ).x == Catch::Approx( 192 ) ); CHECK( Position( s.workspace, mesh, top ).y == Catch::Approx( 176 ) );
    CHECK( Position( s.workspace, mesh, top ).z == Catch::Approx( 128 ) );
    CHECK( Position( s.workspace, mesh, bottom ).x == Catch::Approx( 192 ) ); CHECK( Position( s.workspace, mesh, bottom ).y == Catch::Approx( 176 ) );
    CHECK( Position( s.workspace, mesh, bottom ).z == Catch::Approx( 0 ) );
    CHECK( s.workspace.meshSelection.vertices.nCount == 2 ); // Still picked for the next pull.
    // A click without moving changes nothing.
    s.Click( 192, 176 );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( Position( s.workspace, mesh, top ).x == Catch::Approx( 128 ) );
    // Box-select the two corners at (-128, -128).
    s.Drag( -200, -200, -100, -100 );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 2 );
    CHECK( VertexSelectedIn( s.workspace, VertexAt( s.workspace, mesh, { -128, -128, 0 } ) ) );
    // Shift adds the next box.
    s.Drag( 100, -200, 200, -100, Qt::ShiftModifier );
    CHECK( s.workspace.meshSelection.vertices.nCount == 4 );
    // Dragging one handle of the selection moves all of it, in one step.
    const auto lowLeft = VertexAt( s.workspace, mesh, { -128, -128, 0 } ), lowRight = VertexAt( s.workspace, mesh, { 128, -128, 128 } );
    s.Drag( -128, -128, -128, -192 );
    CHECK( s.Steps() == steps + 1 ); // The undone pull was replaced.
    CHECK( Position( s.workspace, mesh, lowLeft ).y == Catch::Approx( -192 ) ); CHECK( Position( s.workspace, mesh, lowRight ).y == Catch::Approx( -192 ) );
    CHECK( Position( s.workspace, mesh, top ).y == Catch::Approx( 128 ) );
    CHECK( s.workspace.meshSelection.vertices.nCount == 4 );
    // A click on one handle of the selection picks just that handle's column.
    s.Click( -128, -192 );
    CHECK( s.workspace.meshSelection.vertices.nCount == 2 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );

    // A selected brush's corner drags too: the brush becomes a mesh.
    const u64 brush = AddBox( s.workspace, { 300, 300, 0 }, { 400, 400, 64 } );
    REQUIRE( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ) != nullptr );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    s.Drag( 400, 400, 432, 448 );
    REQUIRE( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { brush } ) != nullptr );
    CHECK( Bounds( s.workspace, brush ).maximum.x == Catch::Approx( 432 ) );
    CHECK( Bounds( s.workspace, brush ).maximum.y == Catch::Approx( 448 ) );
}

TEST_CASE( "Vertices mode: a click on an object's body makes it the object being edited", "[map][gui][tool-gestures][vertex-drag]" )
{
    session_t s;
    const u64 first = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
    const u64 second = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
    MapWorkspace_Select( &s.workspace, first, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_Select( &s.workspace, second, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_Select( &s.workspace, first, MAP_SELECT_REPLACE );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    s.Tool( "map.tool.select" );
    s.Click( 192, 0 ); // Inside the second box, away from its corners.
    CHECK( MapWorkspace_IsSelected( &s.workspace, second ) ); CHECK_FALSE( MapWorkspace_IsSelected( &s.workspace, first ) );
    CHECK( s.workspace.elementMode == map_element_mode_t::VERTICES );
    s.Click( -192, 0, Qt::ShiftModifier ); // Modified component clicks retain the current owner.
    CHECK_FALSE( MapWorkspace_IsSelected( &s.workspace, first ) ); CHECK( MapWorkspace_IsSelected( &s.workspace, second ) );
    s.Click( -192, 0 );
    CHECK( MapWorkspace_IsSelected( &s.workspace, first ) ); CHECK_FALSE( MapWorkspace_IsSelected( &s.workspace, second ) );
}

TEST_CASE( "Edges mode: drag an edge to move both its ends", "[map][gui][tool-gestures][vertex-drag]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    s.Tool( "map.tool.select" );
    const usize steps = s.Steps();
    s.Drag( 0, 128, 0, 192 ); // The edge along y = 128.
    CHECK( s.Steps() == steps + 1 );
    CHECK( Bounds( s.workspace, mesh ).maximum.y == Catch::Approx( 192 ) );
    CHECK( s.workspace.meshSelection.edges.nCount == 1 );
}

TEST_CASE( "3D view: drag a vertex across the floor plane; hold Alt during the drag to raise it", "[map][gui][tool-gestures][vertex-drag]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    s.Tool( "map.tool.select" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
    const auto corner = VertexAt( s.workspace, mesh, { 128, -128, 128 } );
    const auto drag = [&]( math::vec3d_t from, math::vec3d_t to, Qt::KeyboardModifiers modifiers ) {
        QPointF a, b;
        REQUIRE( MapCameraView_WorldToView( camera.get(), from, &a ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), to, &b ) );
        const auto send = [&]( QEvent::Type type, QPointF at, Qt::MouseButtons buttons ) {
            // The press is plain (Alt at the press orbits); modifiers apply during the drag.
            QMouseEvent event( type, at, camera->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons,
                               type == QEvent::MouseButtonPress ? Qt::NoModifier : modifiers );
            QCoreApplication::sendEvent( camera.get(), &event );
        };
        send( QEvent::MouseButtonPress, a, Qt::LeftButton );
        send( QEvent::MouseMove, ( a + b ) / 2, Qt::LeftButton );
        send( QEvent::MouseMove, b, Qt::LeftButton );
        send( QEvent::MouseButtonRelease, b, Qt::NoButton );
    };
    drag( { 128, -128, 128 }, { 192, -128, 128 }, Qt::NoModifier );
    auto p = Position( s.workspace, mesh, corner );
    CHECK( p.x == Catch::Approx( 192 ).margin( 1e-6 ) ); CHECK( p.y == Catch::Approx( -128 ).margin( 1e-6 ) ); CHECK( p.z == Catch::Approx( 128 ) );
    drag( { 192, -128, 128 }, { 192, -128, 192 }, Qt::AltModifier );
    p = Position( s.workspace, mesh, corner );
    CHECK( p.x == Catch::Approx( 192 ).margin( 1e-6 ) ); CHECK( p.y == Catch::Approx( -128 ).margin( 1e-6 ) );
    CHECK( p.z == Catch::Approx( 192 ).margin( 1e-6 ) );
    // A grabbed handle never jumps: a press and release in place moves nothing.
    const usize steps = s.Steps();
    drag( { 192, -128, 192 }, { 192, -128, 192 }, Qt::NoModifier );
    CHECK( s.Steps() == steps );
}

namespace
{
// A camera over the scene and mouse input in it, by world point.
struct camera_t {
    session_t &s;
    std::unique_ptr<QWidget> view;
    explicit camera_t( session_t &session ) : s( session ), view( MapCameraView_Create( nullptr, &session.workspace ) )
    {
        view->resize( 800, 600 ); view->show(); QCoreApplication::processEvents();
        MapWorkspace_Frame( &s.workspace, CY_FALSE ); QCoreApplication::processEvents();
    }
    QPointF At( math::vec3d_t p ) { QPointF at; REQUIRE( MapCameraView_WorldToView( view.get(), p, &at ) ); return at; }
    void Send( QEvent::Type type, QPointF at, Qt::MouseButtons held )
    {
        QMouseEvent event( type, at, view->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &event );
    }
    void Click( math::vec3d_t p ) { const QPointF at = At( p ); Send( QEvent::MouseMove, at, Qt::NoButton ); Send( QEvent::MouseButtonPress, at, Qt::LeftButton ); Send( QEvent::MouseButtonRelease, at, Qt::NoButton ); QCoreApplication::processEvents(); }
    void Drag( math::vec3d_t a, math::vec3d_t b )
    {
        const QPointF from = At( a ), to = At( b );
        Send( QEvent::MouseMove, from, Qt::NoButton ); Send( QEvent::MouseButtonPress, from, Qt::LeftButton );
        for ( int i = 1; i <= 10; ++i ) { Send( QEvent::MouseMove, from + ( to - from ) * ( i / 10.0 ), Qt::LeftButton ); }
        Send( QEvent::MouseButtonRelease, to, Qt::NoButton ); QCoreApplication::processEvents();
    }
    void Key( int key ) { QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &press ); QCoreApplication::processEvents(); }
    // A drag whose moves carry `held` (Alt at the press would orbit the camera).
    void DragHolding( math::vec3d_t a, math::vec3d_t b, Qt::KeyboardModifiers held )
    {
        const QPointF from = At( a ), to = At( b );
        Send( QEvent::MouseMove, from, Qt::NoButton ); Send( QEvent::MouseButtonPress, from, Qt::LeftButton );
        for ( int i = 1; i <= 10; ++i ) {
            const QPointF at = from + ( to - from ) * ( i / 10.0 );
            QMouseEvent event( QEvent::MouseMove, at, view->mapToGlobal( at ), Qt::NoButton, Qt::LeftButton, held );
            QCoreApplication::sendEvent( view.get(), &event );
        }
        QMouseEvent release( QEvent::MouseButtonRelease, to, view->mapToGlobal( to ), Qt::LeftButton, Qt::NoButton, held );
        QCoreApplication::sendEvent( view.get(), &release ); QCoreApplication::processEvents();
    }
};
map_wire_kind_t KindOf( const map_workspace_t &ws, u64 id ) { const auto *o = MapWireframe_FindObject( ws.wire, id ); return o != nullptr ? o->kind : map_wire_kind_t::ENTITY; }
} // namespace

TEST_CASE( "Mesh tools used on a brush make it a mesh first: Knife, Loop Cut, Paint and Displace", "[map][gui][tool-gestures][brush-to-mesh]" )
{
    for ( const char *tool : { "map.tool.knife", "map.tool.loop_cut", "map.tool.paint", "map.tool.displace" } ) {
        CAPTURE( tool );
        session_t s;
        AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
        const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
        MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        camera_t camera( s );
        s.Tool( tool );
        const usize steps = s.Steps();
        const std::string id( tool );
        const usize objects = s.workspace.wire.objects.nCount;
        if ( id == "map.tool.knife" ) { camera.Click( { -64, 0, 128 } ); camera.Click( { 64, 0, 128 } ); camera.Key( Qt::Key_Return ); }
        else if ( id == "map.tool.loop_cut" ) { camera.Click( { 64, -64, 64 } ); }
        else if ( id == "map.tool.paint" ) { camera.Drag( { -60, -60, 128 }, { 60, 60, 128 } ); } // Vertex paint: past the corners.
        else { camera.Drag( { -40, -20, 128 }, { 40, 20, 128 } ); }
        CHECK( KindOf( s.workspace, brush ) == map_wire_kind_t::MESH );
        CHECK( s.Steps() == steps + 1 ); // Conversion and the useful edit publish together.
        if ( id == "map.tool.knife" || id == "map.tool.loop_cut" ) { CHECK( MeshFaces( s.workspace, brush ) > 6u ); }
        if ( id == "map.tool.displace" ) {
            // The top is sliced in place; ownership and the source root survive.
            CHECK( MeshFaces( s.workspace, brush ) > 6u );
            CHECK( s.workspace.wire.objects.nCount == objects );
            CHECK( Bounds( s.workspace, brush ).maximum.z > 128.5 );
        }
    }
    // Off: brushes stay brushes and the tools leave them alone.
    session_t s;
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.components_convert_brushes" ) );
    REQUIRE( descriptor != nullptr );
    setting_value_t off{}; off.type = setting_type_t::BOOL; off.bValue = CY_FALSE;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, off ) == settings_registry_status_t::OK );
    camera_t camera( s );
    s.Tool( "map.tool.paint" );
    const usize steps = s.Steps();
    camera.Drag( { -40, -20, 128 }, { 40, 20, 128 } );
    CHECK( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH ); CHECK( s.Steps() == steps );
}

TEST_CASE( "Extrude tool push-pulls mesh faces in 3D: out extrudes, in moves the face", "[map][gui][tool-gestures][extrude]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    camera_t camera( s );
    s.Tool( "map.tool.extrude" );
    const usize faces = MeshFaces( s.workspace, mesh ), steps = s.Steps();
    camera.Drag( { 0, 0, 128 }, { 0, 0, 192 } );
    CHECK( s.Steps() == steps + 1 );
    CHECK( MeshFaces( s.workspace, mesh ) == faces + 4 ); // Four side walls.
    CHECK( Bounds( s.workspace, mesh ).maximum.z > 150.0 );
    CHECK( MapWorkspace_HasMeshFace( &s.workspace ) ); // The cap stays picked for another drag.
    // Pushing the cap back in only moves it.
    const f64 height = Bounds( s.workspace, mesh ).maximum.z;
    camera.Drag( { 0, 0, height }, { 0, 0, height - 48 } );
    CHECK( Bounds( s.workspace, mesh ).maximum.z < height - 16.0 );
    CHECK( MeshFaces( s.workspace, mesh ) == faces + 4 );
    // A click without moving only picks the face.
    const usize before = s.Steps();
    camera.Click( { 0, 0, Bounds( s.workspace, mesh ).maximum.z } );
    CHECK( s.Steps() == before );
}

TEST_CASE( "Displace turns a face wider than the brush into a displacement grid, and paints with nothing selected", "[map][gui][tool-gestures][displace]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 32 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
    camera_t camera( s );
    s.Tool( "map.tool.displace" );
    const usize faces = MeshFaces( s.workspace, mesh ), steps = s.Steps(), objects = s.workspace.wire.objects.nCount;
    // The captured refinement must support the same 16 x 16 grid as a
    // direct source edit; failure here isolates preparation from mouse picking.
    u64 top = 0;
    for ( usize i = 0; i < s.workspace.wire.faces.nCount; ++i ) {
        const auto &face = s.workspace.wire.faces.pData[i];
        if ( face.id == mesh && face.normal.z > 0.99 ) { top = face.faceId; }
    }
    REQUIRE( top != 0 );
    {
        map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( s.workspace.pDocument, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
        const auto status = MapMeshEdit_QuadSliceFace( copy.get(), mesh, top, 16, 16 );
        INFO( "direct 16 x 16 preparation status = " << MapDocument_StatusName( status ) );
        REQUIRE( status == map_status_t::OK );
    }
    camera.Drag( { -40, 0, 32 }, { 40, 0, 32 } );
    CHECK( s.Steps() == steps + 1 ); // Slicing and the stroke publish together.
    const usize slicedFaces = MeshFaces( s.workspace, mesh ); CHECK( slicedFaces > faces );
    CHECK( s.workspace.wire.objects.nCount == objects );
    CHECK( Bounds( s.workspace, mesh ).maximum.z > 32.5 );
    // A second stroke on the grid sculpts it without making another.
    camera.Drag( { -40, 30, 32 }, { 40, 30, 32 } );
    CHECK( s.Steps() == steps + 2 ); CHECK( s.workspace.wire.objects.nCount == objects );
    CHECK( MeshFaces( s.workspace, mesh ) == slicedFaces );
    // Each undo removes the whole stroke, including any preparation.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( MeshFaces( s.workspace, mesh ) == faces ); CHECK( s.workspace.wire.objects.nCount == objects );
}

TEST_CASE( "Edges and Vertices mode clicks exactly on a brush edge in 3D convert that brush and pick the component", "[map][gui][views][brush-to-mesh]" )
{
    for ( const auto mode : { map_element_mode_t::EDGES, map_element_mode_t::VERTICES } ) {
        CAPTURE( static_cast<int>( mode ) );
        session_t s;
        AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } ); // The floor behind the edge.
        const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
        MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        camera_t camera( s );
        s.Tool( "map.tool.select" );
        MapWorkspace_SetElementMode( &s.workspace, mode );
        // The top edge facing away, and a corner: a ray through them grazes the brush.
        camera.Click( mode == map_element_mode_t::EDGES ? math::vec3d_t{ 0, 64, 128 } : math::vec3d_t{ 64, 64, 128 } );
        CHECK( KindOf( s.workspace, brush ) == map_wire_kind_t::MESH );
        CHECK( ( mode == map_element_mode_t::EDGES ? MapWorkspace_HasMeshEdges( &s.workspace ) : MapWorkspace_HasMeshVertices( &s.workspace ) ) );
    }
}

TEST_CASE( "Repro: displace a box, then cut it", "[.repro-displace]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    const u64 box = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
    camera_t camera( s );
    const auto report = [&]( const char *what ) {
        map_notice_t k{};
        std::printf( "REPRO %-28s steps %zu faces %zu notice: %s\n", what, s.Steps(), MeshFaces( s.workspace, box ), MapNotice_Last( &s.workspace, &k ).toUtf8().constData() );
    };
    s.Tool( "map.tool.displace" );
    camera.Drag( { -60, -20, 128 }, { 60, 20, 128 } ); report( "displace top" );
    camera.Drag( { -60, -128, 100 }, { 60, -128, 60 } ); report( "displace front side" );
    camera.Drag( { 128, -60, 100 }, { 128, 60, 60 } ); report( "displace right side" );
    std::printf( "REPRO camera at (%.0f %.0f %.0f)\n", MapCameraView_Position( camera.view.get() ).x, MapCameraView_Position( camera.view.get() ).y, MapCameraView_Position( camera.view.get() ).z );
    s.Tool( "map.tool.loop_cut" );
    camera.Click( { -128, 0, 64 } ); report( "loop cut on the box's left face" );
    camera.Click( { 128, -128, 64 } ); report( "loop cut vertical edge" );
    camera.Click( { -128, -128, 64 } ); report( "loop cut other edge" );
    s.Tool( "map.tool.knife" );
    camera.Click( { -128, 0, 128 } ); camera.Click( { 128, 0, 128 } ); camera.Key( Qt::Key_Return ); report( "knife across top" );
    camera.Click( { -128, -128, 32 } ); camera.Click( { 128, -128, 32 } ); camera.Key( Qt::Key_Return ); report( "knife across front" );
}

TEST_CASE( "Terrain tool sculpts a heightfield: raise, Ctrl lowers, smooth, flatten; one undo step per stroke; Escape cancels", "[map][gui][tool-gestures][terrain]" )
{
    session_t s;
    s.Tool( "map.tool.terrain" );
    {
        camera_t empty( s );
        empty.Drag( { -100, 0, 0 }, { 100, 0, 0 } );
        map_notice_t kind{};
        CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "no terrain yet" ) ) );
    }
    map_terrain_create_desc_t desc{}; desc.origin = { -1024, -1024, 0 }; desc.cellSize = 64.0; desc.cellsX = 32; desc.cellsY = 32;
    REQUIRE( MapWorkspace_CreateTerrain( &s.workspace, desc ) );
    MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
    camera_t camera( s );
    s.Tool( "map.tool.terrain" );
    const auto height = [&]( f64 x, f64 y ) {
        const auto *field = s.workspace.pDocument->geometry.heightFields.pData[0];
        f64 z = 0.0; REQUIRE( geo::HeightField_TryHeightAt( field, x, y, &z ) ); return z;
    };
    REQUIRE( height( 0, 0 ) == Catch::Approx( 0.0 ) );
    // Raise: a hill under the stroke, one undo step.
    usize steps = s.Steps();
    camera.Drag( { -200, 0, 0 }, { 200, 0, 0 } );
    CHECK( s.Steps() == steps + 1 );
    CHECK( height( 0, 0 ) > 1.0 ); CHECK( height( -900, -900 ) == Catch::Approx( 0.0 ) );
    // The views show the hill (the terrain's points moved).
    const auto *object = MapWireframe_FindObject( s.workspace.wire, s.workspace.pDocument->geometry.heightFields.pData[0]->sourceId.value );
    REQUIRE( object != nullptr ); CHECK( object->bounds.box.maximum.z > 1.0 );
    // Lower mode digs.
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.terrain.lower" ), {} ) == command_result_t::OK );
    CHECK( ( EditorCommands_State( &s.gui.commands, StringView_FromCString( "map.terrain.lower" ) ) & COMMAND_STATE_CHECKED ) != 0u );
    const f64 raised = height( 600, 600 );
    camera.Drag( { 500, 600, raised }, { 700, 600, raised } );
    CHECK( height( 600, 600 ) < raised - 1.0 );
    // Flatten levels to the height pressed; smooth evens it out.
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.terrain.flatten" ), {} ) == command_result_t::OK );
    const f64 hill = height( 0, 0 );
    camera.Drag( { -800, 0, height( -800, 0 ) }, { -50, 0, height( -50, 0 ) } );
    CHECK( height( 0, 0 ) < hill );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.terrain.smooth" ), {} ) == command_result_t::OK );
    steps = s.Steps();
    camera.Drag( { -100, 300, height( -100, 300 ) }, { 100, 300, height( 100, 300 ) } );
    CHECK( s.Steps() <= steps + 1 );
    // Undo puts the last stroke back.
    const f64 before = height( 600, 600 );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.terrain.raise" ), {} ) == command_result_t::OK );
    camera.Drag( { 500, 600, before }, { 700, 600, before } );
    REQUIRE( height( 600, 600 ) > before + 1.0 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( height( 600, 600 ) == Catch::Approx( before ) );
    // Mid-stroke the views already show the change; Escape puts them and the map back.
    steps = s.Steps();
    const f64 untouched = height( -600, -600 );
    const QPointF from = camera.At( { -700, -600, untouched } ), to = camera.At( { -500, -600, untouched } );
    camera.Send( QEvent::MouseMove, from, Qt::NoButton ); camera.Send( QEvent::MouseButtonPress, from, Qt::LeftButton );
    for ( int i = 1; i <= 10; ++i ) { camera.Send( QEvent::MouseMove, from + ( to - from ) * ( i / 10.0 ), Qt::LeftButton ); }
    const auto *live = MapWireframe_FindObject( s.workspace.wire, s.workspace.pDocument->geometry.heightFields.pData[0]->sourceId.value );
    REQUIRE( live != nullptr );
    const f64 liveTop = live->bounds.box.maximum.z;
    camera.Key( Qt::Key_Escape );
    camera.Send( QEvent::MouseButtonRelease, to, Qt::NoButton );
    CHECK( s.Steps() == steps ); CHECK( height( -600, -600 ) == Catch::Approx( untouched ) );
    CHECK( MapWireframe_FindObject( s.workspace.wire, s.workspace.pDocument->geometry.heightFields.pData[0]->sourceId.value )->bounds.box.maximum.z <= liveTop );
}

TEST_CASE( "Block tool: a drawn block is created when another tool is picked, or at once with Create blocks on release", "[map][gui][tool-gestures][block]" )
{
    session_t s;
    s.Tool( "map.tool.block" );
    usize objects = s.workspace.wire.objects.nCount;
    s.Drag( -128, -128, 0, 0 );
    CHECK( MapWorkspace_HasBlockPreview( &s.workspace ) ); CHECK( s.workspace.wire.objects.nCount == objects ); // Staged, with handles.
    s.Tool( "map.tool.select" );
    CHECK( s.workspace.wire.objects.nCount == objects + 1 );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "Created the block" ) ) );
    // A click without a drag says how the tool works.
    s.Tool( "map.tool.block" );
    s.Click( 200, 200 );
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "drag a rectangle" ) ) );
    // Created as the drag ends.
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.block_create_on_release" ) );
    REQUIRE( descriptor != nullptr );
    setting_value_t on{}; on.type = setting_type_t::BOOL; on.bValue = CY_TRUE;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, on ) == settings_registry_status_t::OK );
    objects = s.workspace.wire.objects.nCount;
    s.Drag( 64, 64, 192, 192 );
    CHECK( s.workspace.wire.objects.nCount == objects + 1 ); CHECK_FALSE( MapWorkspace_HasBlockPreview( &s.workspace ) );
}

TEST_CASE( "Edge Arc picks its chain inside the tool: click an edge, Shift+click the next, drag to bend", "[map][gui][tool-gestures][edge-arc]" )
{
    session_t s;
    // A flat sheet: its border bends within its plane, seen from above.
    map_primitive_desc_t desc = MapWorkspace_PrimitiveDefaults( &s.workspace, {} );
    desc.kind = map_primitive_kind_t::QUAD; desc.axis = 2; desc.bounds = { { -128, -128, 0 }, { 128, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, desc ) );
    const u64 sheet = s.Selected(); REQUIRE( sheet != 0 );
    s.Tool( "map.tool.edge_arc" );
    s.Click( 0, -128 );
    CHECK( MapWorkspace_HasMeshEdges( &s.workspace ) ); CHECK( s.workspace.meshSelection.edges.nCount == 1u );
    s.Click( -128, 0, Qt::ShiftModifier );
    REQUIRE( s.workspace.meshSelection.edges.nCount == 2u );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "drag to bend" ) ) );
    const usize steps = s.Steps();
    const auto before = Bounds( s.workspace, sheet );
    s.Drag( 0, 0, -48, -48 ); // Off the edges: the chain becomes an arc between its ends (the corner rounds in).
    CHECK( s.Steps() == steps + 1 );
    // The corner the chain turned at has moved (its ends stay, so the bounds do too).
    const auto *object = MapWireframe_FindObject( s.workspace.wire, sheet );
    REQUIRE( object != nullptr );
    bool corner = false;
    for ( u32 i = 0; i < object->nPoints; ++i ) {
        const auto p = s.workspace.wire.points.pData[object->iFirstPoint + i];
        corner |= std::fabs( p.x + 128.0 ) < 1e-6 && std::fabs( p.y + 128.0 ) < 1e-6;
    }
    CHECK_FALSE( corner );
    ( void )before;
    CHECK( s.workspace.meshSelection.edges.nCount == 2u ); // Still picked, for another bend.
}

TEST_CASE( "Knife clicks on faces apart cut every edge the straight line crosses", "[map][gui][tool-gestures][knife]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    // Split the top face along y = 0, so the next cut has to cross that edge.
    s.Tool( "map.tool.edge_cut" );
    s.Click( -128, 0 ); s.Click( 128, 0 ); s.Key( Qt::Key_Return );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 7 );

    s.Tool( "map.tool.knife" );
    const usize steps = s.Steps();
    s.Click( 0, -128 ); // The south edge's middle...
    s.Click( 0, 128 );  // ...to the north edge's: two faces apart.
    s.Key( Qt::Key_Return );
    // The crossing on y = 0 was filled in: both halves are cut, in one step.
    CHECK( MeshFaces( s.workspace, mesh ) == 9 );
    CHECK( s.Steps() == steps + 1 );
    CHECK( VertexAt( s.workspace, mesh, { 0, 0, 128 } ).value != 0u );
}

TEST_CASE( "Knife bridging follows the perspective cut plane in 3D", "[map][gui][tool-gestures][knife]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    camera_t camera( s );
    s.Tool( "map.tool.edge_cut" );
    camera.Click( { -64, 0, 128 } ); camera.Click( { 64, 0, 128 } ); camera.Key( Qt::Key_Return );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 7 );
    s.Tool( "map.tool.knife" );
    camera.Click( { 0, -64, 128 } ); camera.Click( { 0, 64, 128 } ); camera.Key( Qt::Key_Return );
    CHECK( MeshFaces( s.workspace, mesh ) == 9 );
    // Clicks land on whole pixels (both cuts' points too), so the filled-in
    // crossing sits within a unit or so of the top's centre.
    bool crossing = false;
    const auto &wire = s.workspace.wire;
    for ( usize i = 0; i < wire.points.nCount; ++i ) {
        const auto p = wire.points.pData[i];
        crossing |= wire.pointSourceIds.pData[i] != 0u && std::fabs( p.x ) < 2.0 && std::fabs( p.y ) < 2.0 && std::fabs( p.z - 128.0 ) < 1e-6;
    }
    CHECK( crossing );
}

TEST_CASE( "Knife refuses a point no straight cut can reach and keeps the chain", "[map][gui][tool-gestures][knife]" )
{
    session_t s;
    const u64 a = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
    const u64 b = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
    MapWorkspace_Select( &s.workspace, a, MAP_SELECT_REPLACE ); MapWorkspace_Select( &s.workspace, b, MAP_SELECT_ADD );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    s.Tool( "map.tool.knife" );
    s.Click( -256, 0 ); // On box a...
    s.Click( 256, 0 );  // ...then on box b: refused, the chain still holds one point.
    s.Click( -128, 0 ); // Across box a instead: accepted.
    s.Key( Qt::Key_Return );
    CHECK( MeshFaces( s.workspace, a ) == 7 );
    CHECK( MeshFaces( s.workspace, b ) == 6 );
}

namespace
{
// Pixels close to Mason's yellow selection colour.
usize YellowPixels( const QImage &image )
{
    usize n = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            const QColor c = image.pixelColor( x, y );
            n += c.red() > 200 && c.green() > 170 && c.blue() < 110 ? 1u : 0u;
        }
    }
    return n;
}
} // namespace

TEST_CASE( "Selected edges stay highlighted in 3D while a transform tool works on them", "[map][gui][tool-gestures][component-overlay]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    MapWorkspace_Select( &s.workspace, mesh, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    camera_t camera( s );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    s.Tool( "map.tool.select" );
    const auto a = VertexAt( s.workspace, mesh, { -64, -64, 128 } ), b = VertexAt( s.workspace, mesh, { 64, -64, 128 } );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, geo::MeshEdgeRef_Make( a, b ), MAP_SELECT_REPLACE ) );
    QCoreApplication::processEvents();
    const usize withSelect = YellowPixels( camera.view->grab().toImage() );
    REQUIRE( withSelect > 20u );
    // The RGB rotation rings must leave the yellow edge selection readable.
    s.Tool( "map.tool.rotate" );
    QCoreApplication::processEvents();
    CHECK( YellowPixels( camera.view->grab().toImage() ) * 2u > withSelect );
    // Navigation alone clears the editing overlays.
    MapWorkspace_SetTool( &s.workspace, map_tool_t::NONE );
    QCoreApplication::processEvents();
    CHECK( YellowPixels( camera.view->grab().toImage() ) * 4u < withSelect );
}

TEST_CASE( "Knife clicks inside a face cut straight across it to its edges", "[map][gui][tool-gestures][knife]" )
{
    session_t s;
    const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    s.Tool( "map.tool.knife" );
    s.Click( -32, 8 ); s.Click( 32, 8 ); // Both inside the top face, along y = 8.
    s.Key( Qt::Key_Return );
    CHECK( MeshFaces( s.workspace, mesh ) == 7 );
    // The cut ran on to the face's west and east edges, and no further.
    CHECK( VertexAt( s.workspace, mesh, { -128, 8, 128 } ).value != 0u );
    CHECK( VertexAt( s.workspace, mesh, { 128, 8, 128 } ).value != 0u );
}

TEST_CASE( "A free move in 3D slides on the floor, and Alt lifts it", "[map][gui][tool-gestures][free-move]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    camera_t camera( s );
    MapWorkspace_Select( &s.workspace, brush, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.translate" );
    // The gizmo centre sits on the pivot (the box's centre).
    camera.Drag( { 0, 0, 64 }, { 64, 0, 64 } );
    auto box = Bounds( s.workspace, brush );
    CHECK( box.minimum.x == Catch::Approx( 0 ) );
    CHECK( box.minimum.y == Catch::Approx( -64 ) ); // No sideways slide...
    CHECK( box.minimum.z == Catch::Approx( 0 ) );   // ...and it stays on the floor.
    // Alt held during the drag moves straight up.
    camera.DragHolding( { 64, 0, 64 }, { 64, 0, 128 }, Qt::AltModifier );
    box = Bounds( s.workspace, brush );
    CHECK( box.minimum.x == Catch::Approx( 0 ) );
    CHECK( box.minimum.y == Catch::Approx( -64 ) );
    CHECK( box.minimum.z == Catch::Approx( 64 ).margin( 16 ) );
    CHECK( box.minimum.z > 32 );
}

TEST_CASE( "A 2D vertex handle moves every vertex stacked under it", "[map][gui][tool-gestures][vertex-column]" )
{
    SECTION( "Vertex tool on a brush" )
    {
        session_t s;
        const u64 brush = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
        MapWorkspace_Select( &s.workspace, brush, MAP_SELECT_REPLACE );
        s.Tool( "map.tool.vertex" );
        s.Drag( 128, 128, 192, 192 );
        // Both the top and the bottom corner moved: the wall stays upright.
        const auto box = Bounds( s.workspace, brush );
        CHECK( box.maximum.x == Catch::Approx( 192 ) );
        CHECK( box.maximum.y == Catch::Approx( 192 ) );
        CHECK( box.minimum.z == Catch::Approx( 0 ) );
        CHECK( box.maximum.z == Catch::Approx( 128 ) );
        std::vector<math::vec3d_t> corners;
        const auto *object = MapWireframe_FindObject( s.workspace.wire, brush );
        REQUIRE( object != nullptr );
        for ( u32 k = 0; k < object->nPoints; ++k ) { corners.push_back( s.workspace.wire.points.pData[object->iFirstPoint + k] ); }
        const auto has = [&]( math::vec3d_t p ) { return std::any_of( corners.begin(), corners.end(), [&]( math::vec3d_t q ) { return std::abs( p.x - q.x ) + std::abs( p.y - q.y ) + std::abs( p.z - q.z ) < 1e-6; } ); };
        CHECK( has( { 192, 192, 0 } ) );
        CHECK( has( { 192, 192, 128 } ) );
    }
    SECTION( "Vertex tool on a mesh" )
    {
        session_t s;
        const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        s.Tool( "map.tool.vertex" );
        const auto top = VertexAt( s.workspace, mesh, { 128, 128, 128 } ), bottom = VertexAt( s.workspace, mesh, { 128, 128, 0 } );
        s.Drag( 128, 128, 160, 160 );
        CHECK( Position( s.workspace, mesh, top ).x == Catch::Approx( 160 ) );
        CHECK( Position( s.workspace, mesh, bottom ).x == Catch::Approx( 160 ) );
        CHECK( Position( s.workspace, mesh, bottom ).z == Catch::Approx( 0 ) );
    }
    SECTION( "Vertices mode: a click selects the column, a drag moves it" )
    {
        session_t s;
        const u64 mesh = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
        s.Tool( "map.tool.select" );
        s.Click( 128, 128 );
        CHECK( s.workspace.meshSelection.vertices.nCount == 2u );
        const auto bottom = VertexAt( s.workspace, mesh, { 128, 128, 0 } );
        s.Drag( 128, 128, 160, 128 );
        CHECK( Position( s.workspace, mesh, bottom ).x == Catch::Approx( 160 ) );
        CHECK( Position( s.workspace, mesh, bottom ).z == Catch::Approx( 0 ) );
    }
}

TEST_CASE( "A free rotate in 3D follows the cursor round the pivot", "[map][gui][tool-gestures][free-rotate]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    camera_t camera( s );
    MapWorkspace_Select( &s.workspace, brush, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.rotate" );
    // From east of the pivot to north-east: a 45 degree turn about Z.
    camera.Drag( { 200, 0, 64 }, { 141.42, 141.42, 64 } );
    const auto box = Bounds( s.workspace, brush );
    // The square turned 45 degrees: a diamond reaching 90.5 from the centre.
    CHECK( box.maximum.x == Catch::Approx( 90.51 ).margin( 0.05 ) );
    CHECK( box.maximum.y == Catch::Approx( 90.51 ).margin( 0.05 ) );
    CHECK( box.minimum.z == Catch::Approx( 0 ) );
}

TEST_CASE( "The Extrude tool pushes a brush side seen edge-on in 2D", "[map][gui][tool-gestures][extrude-2d]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    MapWorkspace_Select( &s.workspace, brush, MAP_SELECT_REPLACE );
    s.Tool( "map.tool.extrude" );
    const usize steps = s.Steps();
    s.Drag( 128, 0, 192, 0 ); // The east side, straight out.
    CHECK( s.Steps() == steps + 1 );
    const auto box = Bounds( s.workspace, brush );
    CHECK( box.maximum.x == Catch::Approx( 192 ) );
    CHECK( box.minimum.x == Catch::Approx( -128 ) );
    CHECK( box.maximum.y == Catch::Approx( 128 ) );
}

TEST_CASE( "Patch and Entity in 3D land exactly on the surface under the cursor", "[map][gui][tool-gestures][on-surface]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 64 } );
    MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
    camera_t camera( s );
    // An entity clicked on the top sits exactly at z 64.
    s.Tool( "map.tool.entity" );
    camera.Click( { 96, 96, 64 } );
    REQUIRE( s.workspace.wire.entities.nCount == 1 );
    const auto origin = s.workspace.wire.entities.pData[0].origin;
    CHECK( origin.z == 64 ); // Exactly: the ray's rounding noise would be saved in the map.
    CHECK( origin.x == Catch::Approx( 96 ) ); CHECK( origin.y == Catch::Approx( 96 ) );
    // A patch dragged on the box's top stands on it, not on the ground.
    s.Tool( "map.tool.patch" );
    camera.Drag( { -64, -64, 64 }, { 64, 64, 64 } );
    const u64 patch = s.Selected(); REQUIRE( patch != 0 ); REQUIRE( patch != brush );
    REQUIRE( geo::GeometryDocument_FindPatch( &s.workspace.pDocument->geometry, { patch } ) != nullptr );
    const auto box = Bounds( s.workspace, patch );
    CHECK( box.minimum.x == Catch::Approx( -64 ) ); CHECK( box.maximum.x == Catch::Approx( 64 ) );
    CHECK( box.minimum.z == 64 );
}

TEST_CASE( "3D Clip: a line begun on an edge runs on the face the cursor crosses", "[map][gui][tool-gestures][clip][surface-clip]" )
{
    session_t s;
    AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    camera_t camera( s );
    s.Tool( "map.tool.clip" );
    // From the top's front edge (shared with the front face) across the top.
    camera.Drag( { 0, -128, 128 }, { 0, 128, 128 } );
    const auto &preview = s.workspace.editPreview;
    REQUIRE( preview.bActive ); REQUIRE( preview.bClip );
    // Straight down through the line (x = 0), not tilted through the front face.
    CHECK( std::fabs( preview.clipPlane.normal.x ) == Catch::Approx( 1 ).margin( 1e-9 ) );
    CHECK( preview.clipPlane.d == Catch::Approx( 0 ).margin( 1e-9 ) );
}

TEST_CASE( "Vertex tool in 3D slides a corner across the floor plane; Alt lifts it", "[map][gui][tool-gestures][vertex]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    camera_t camera( s );
    s.Tool( "map.tool.vertex" );
    camera.Drag( { 64, -64, 128 }, { 96, -64, 128 } );
    const auto *object = MapWireframe_FindObject( s.workspace.wire, brush ); REQUIRE( object != nullptr );
    bool moved = false, drifted = false;
    for ( u32 i = 0; i < object->nPoints; ++i ) {
        const auto p = s.workspace.wire.points.pData[object->iFirstPoint + i];
        moved |= p.x == Catch::Approx( 96 ) && p.y == Catch::Approx( -64 ) && p.z == Catch::Approx( 128 );
        drifted |= p.z != Catch::Approx( 0 ) && p.z != Catch::Approx( 128 );
    }
    CHECK( moved );
    CHECK_FALSE( drifted ); // The corner kept its height.
}

TEST_CASE( "A new map drops the tools' staged state: an old workplane no longer catches clicks", "[map][gui][tool-gestures][surface]" )
{
    session_t s;
    AddBox( s.workspace, { -512, -512, -16 }, { 512, 512, 0 } );
    AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
    camera_t camera( s );
    s.Tool( "map.tool.workplane" );
    camera.Click( { 0, -64, 64 } ); // The box's front side: the plane y = -64.
    REQUIRE( MapWorkspace_New( &s.workspace ) == map_status_t::OK );
    QCoreApplication::processEvents();
    // In the new, empty map an entity lands on the ground, not on y = -64.
    s.Tool( "map.tool.entity" );
    camera.Click( { 0, -256, 0 } );
    REQUIRE( s.workspace.wire.entities.nCount == 1 );
    const auto origin = s.workspace.wire.entities.pData[0].origin;
    CHECK( origin.z == Catch::Approx( 0 ).margin( 1e-9 ) );
    CHECK( origin.y == Catch::Approx( -256 ) );
}

TEST_CASE( "Hiding an entity group hides its brush entities' volumes too", "[map][gui][tool-gestures][visgroups]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    u64 trigger = 0u;
    REQUIRE( MapCreate_TieToEntity( s.workspace.pDocument, { &brush, 1u }, 0u, StringView_FromCString( "trigger_once" ), &trigger ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.workspace );
    const auto visible = [&]() {
        const auto *object = MapWireframe_FindObject( s.workspace.wire, brush );
        REQUIRE( object != nullptr );
        return MapWorkspace_IsVisible( &s.workspace, *object ) != CY_FALSE;
    };
    CHECK( visible() );
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::TRIGGERS, CY_TRUE );
    CHECK_FALSE( visible() ); // The volume goes with its trigger.
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::TRIGGERS, CY_FALSE );
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::TIED, CY_TRUE );
    CHECK_FALSE( visible() ); // And with the tied-geometry group, as before.
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::TIED, CY_FALSE );
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::LIGHTS, CY_TRUE );
    CHECK( visible() );
}

TEST_CASE( "Shear slides a side of the selection's bounds and keeps brush textures on", "[map][gui][tool-gestures][shear]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 64 } );
    s.workspace.bTextureLock = CY_TRUE;
    // The top face's texels at its corners, to find again after the shear.
    map_uv_face_t before{};
    u64 top = 0;
    for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
        const auto &face = s.workspace.wire.faces.pData[f];
        if ( face.id == box && face.normal.z > 0.9 ) { top = face.sideId; }
    }
    REQUIRE( top != 0 );
    REQUIRE( MapTextureEdit_Describe( s.workspace.pDocument, { box, top }, &before ) == map_status_t::OK );
    s.Tool( "map.tool.shear" );
    REQUIRE( s.workspace.tool == map_tool_t::SHEAR );
    const usize steps = s.Steps();
    // Top view: grab the side at y = 128 and slide it 32 along x.
    s.Mouse( QEvent::MouseMove, s.At( 64, 128 ), Qt::NoButton );
    s.Drag( 64, 128, 96, 128 );
    CHECK( s.Steps() == steps + 1u );
    CHECK( s.Selected() == box );
    const auto bounds = Bounds( s.workspace, box );
    CHECK( bounds.minimum.x == Catch::Approx( 0.0 ) );
    CHECK( bounds.maximum.x == Catch::Approx( 160.0 ) );
    CHECK( bounds.maximum.y == Catch::Approx( 128.0 ) );
    CHECK( bounds.maximum.z == Catch::Approx( 64.0 ) );
    // The y = 0 side stayed; the y = 128 side moved: corners (0,0) and (160,128) exist, (128,128) does not.
    const auto *object = MapWireframe_FindObject( s.workspace.wire, box );
    REQUIRE( object != nullptr );
    bool fixed = false, slid = false, old = false;
    for ( u32 k = 0; k < object->nPoints; ++k ) {
        const auto &p = s.workspace.wire.points.pData[object->iFirstPoint + k];
        fixed |= std::fabs( p.x ) < 1e-6 && std::fabs( p.y ) < 1e-6;
        slid |= std::fabs( p.x - 160.0 ) < 1e-6 && std::fabs( p.y - 128.0 ) < 1e-6;
        old |= std::fabs( p.x - 128.0 ) < 1e-6 && std::fabs( p.y - 128.0 ) < 1e-6;
    }
    CHECK( fixed );
    CHECK( slid );
    CHECK_FALSE( old );
    // Texture lock: each corner shows the texel it showed before it moved.
    map_uv_face_t after{};
    REQUIRE( MapTextureEdit_Describe( s.workspace.pDocument, { box, top }, &after ) == map_status_t::OK );
    usize matched = 0;
    for ( usize i = 0; i < before.corners.size(); ++i ) {
        const math::vec3d_t moved{ before.corners[i].x + 32.0 * before.corners[i].y / 128.0, before.corners[i].y, before.corners[i].z };
        for ( usize k = 0; k < after.corners.size(); ++k ) {
            if ( std::fabs( after.corners[k].x - moved.x ) < 1e-6 && std::fabs( after.corners[k].y - moved.y ) < 1e-6 ) {
                CHECK( after.uvs[k].x == Catch::Approx( before.uvs[i].x ).margin( 1e-9 ) );
                CHECK( after.uvs[k].y == Catch::Approx( before.uvs[i].y ).margin( 1e-9 ) );
                ++matched;
            }
        }
    }
    CHECK( matched == 4u );

    // Away from the bounds a click selects as usual, and Escape drops a drag.
    s.Drag( 64, 128 + 32, 96, 128 + 32 );
    CHECK( s.Steps() == steps + 1u );
    const u64 other = AddBox( s.workspace, { 256, 0, 0 }, { 320, 64, 64 } );
    MapWorkspace_SetSelection( &s.workspace, &other, 1 );
    s.Mouse( QEvent::MouseButtonPress, s.At( 288, 64 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 320, 64 ), Qt::LeftButton );
    s.Key( Qt::Key_Escape );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 320, 64 ), Qt::NoButton );
    CHECK( Bounds( s.workspace, other ).maximum.x == Catch::Approx( 320.0 ) );
}

TEST_CASE( "MapEdit_Shear leans objects about the plane through the pivot", "[map][gui][shear]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 128 } );
    // Core: x gains a quarter of the height above z = 0.
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Shear", []( void *pBox, map_document_t *pDoc, vector_t<u64> * ) {
        const u64 id = *static_cast<const u64 *>( pBox );
        return MapEdit_Shear( pDoc, { &id, 1 }, 2u, { 0.25, 0.0, 0.0 }, { 0, 0, 0 }, true );
    }, const_cast<u64 *>( &box ) ) );
    const auto bounds = Bounds( s.workspace, box );
    CHECK( bounds.maximum.x == Catch::Approx( 160.0 ) );
    CHECK( bounds.minimum.x == Catch::Approx( 0.0 ) );
    // The slide must lie across the axis.
    CHECK( MapEdit_Shear( s.workspace.pDocument, { &box, 1 }, 2u, { 0.0, 0.0, 0.5 }, {}, true ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Default Clip splits a quad into open halves with one undoable edit", "[map][gui][tool-gestures][clip-regression]" )
{
    session_t s;
    map_primitive_desc_t desc = MapWorkspace_PrimitiveDefaults( &s.workspace, {} );
    desc.kind = map_primitive_kind_t::QUAD; desc.axis = 2; desc.bounds = { { -128, -128, 0 }, { 128, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, desc ) );
    const u64 mesh = s.Selected();
    s.Tool( "map.tool.clip" );
    s.Drag( 0, -256, 0, 256 );
    REQUIRE( s.workspace.editPreview.bClip );
    REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    REQUIRE( s.workspace.editPreviewWire.objects.nCount == 2 );
    const usize steps = s.Steps();
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( EditorSelection_Count( &s.workspace.selection ) == 2 );
    for ( usize i = 0; i < 2; ++i ) { CHECK( MeshFaces( s.workspace, EditorSelection_At( &s.workspace.selection, i ) ) == 1 ); }
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( geo::GeometryDocument_MeshCount( &s.workspace.pDocument->geometry ) == 1 );
    CHECK( Bounds( s.workspace, mesh ).minimum.x == Catch::Approx( -128 ) );
    CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 128 ) );
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( geo::GeometryDocument_MeshCount( &s.workspace.pDocument->geometry ) == 2 );
}

TEST_CASE( "Invalid surface clip endpoint drags cannot commit the preceding valid plane", "[map][gui][tool-gestures][clip-regression]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 128 } );
    s.Tool( "map.tool.clip" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
    const auto at = [&]( math::vec3d_t p ) { QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), p, &screen ) ); return screen; };
    const auto send = [&]( QEvent::Type type, math::vec3d_t p ) {
        const auto where = at( p );
        QMouseEvent event( type, where, camera->mapToGlobal( where ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    const auto click = [&]( math::vec3d_t p ) { send( QEvent::MouseButtonPress, p ); send( QEvent::MouseButtonRelease, p ); };
    const math::vec3d_t a{ -128, -128, 128 }, b{ 128, 128, 128 };
    click( a ); click( b );
    REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    const usize steps = s.Steps();
    const u64 revision = s.workspace.pDocument->geometry.revision;
    SECTION( "Coincident endpoints clear the old result and repair during the same drag" ) {
        send( QEvent::MouseButtonPress, b );
        send( QEvent::MouseMove, a );
        REQUIRE( s.workspace.editPreview.status != map_status_t::OK );
        CHECK( s.workspace.editPreviewWire.points.nCount == 0 );
        CHECK_FALSE( MapWorkspace_CommitClipPreview( &s.workspace ) );
        CHECK( s.workspace.pDocument->geometry.revision == revision ); CHECK( s.Steps() == steps );
        send( QEvent::MouseMove, b ); send( QEvent::MouseButtonRelease, b );
        REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    }
    SECTION( "A third point dragged onto the line cannot retain the old tilted cut" ) {
        const auto eye = MapCameraView_Position( camera.get() );
        const math::vec3d_t c = eye.x > 128 ? math::vec3d_t{ 128, 0, 32 } : eye.x < -128 ? math::vec3d_t{ -128, 0, 32 } :
                                eye.y > 128 ? math::vec3d_t{ 0, 128, 32 } : math::vec3d_t{ 0, -128, 32 };
        const math::vec3d_t collinear = a;
        click( c );
        REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
        send( QEvent::MouseButtonPress, c ); send( QEvent::MouseMove, collinear );
        REQUIRE( s.workspace.editPreview.status != map_status_t::OK );
        CHECK( s.workspace.editPreviewWire.points.nCount == 0 );
        CHECK_FALSE( MapWorkspace_CommitClipPreview( &s.workspace ) );
        CHECK( s.workspace.pDocument->geometry.revision == revision ); CHECK( s.Steps() == steps );
        send( QEvent::MouseMove, c ); send( QEvent::MouseButtonRelease, c );
        REQUIRE( s.workspace.editPreview.status == map_status_t::OK );
    }
    CHECK( Bounds( s.workspace, box ).minimum.x == Catch::Approx( -128 ) );
    s.Key( Qt::Key_Return );
    CHECK( s.Steps() == steps + 1 ); CHECK( EditorSelection_Count( &s.workspace.selection ) == 2 );
}

namespace
{
struct edge_workflow_mesh_t {
    geo::mesh_source_description_t value{};
    edge_workflow_mesh_t( const map_document_t &document, u64 id )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
        const auto *mesh = geo::GeometryDocument_FindMesh( &document.geometry, { id } ); REQUIRE( mesh );
        REQUIRE( geo::MeshSource_TryDescribe( mesh, &value ) == geo::geometry_status_t::OK );
    }
    ~edge_workflow_mesh_t() { geo::MeshSourceDescription_Shutdown( &value ); }
};
u64 EdgeWorkflowQuad( session_t &s, math::aabbd_t bounds = { { 0, -64, 0 }, { 128, 64, 0 } } )
{
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2; quad.bounds = bounds;
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) );
    const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    s.Tool( "map.tool.select" );
    return mesh;
}
void CheckPosition( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-6 ) );
    CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-6 ) );
    CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-6 ) );
}
void CheckOriginalVertices( const session_t &s, u64 mesh, const edge_workflow_mesh_t &before )
{
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i];
        CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), vertex.position );
    }
}
geo::mesh_edge_ref_t OnlySelectedEdge( const session_t &s )
{
    REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    return s.workspace.meshSelection.edges.pData[0];
}
} // namespace

TEST_CASE( "Shift Translate grows a quad from its selected outer edge repeatedly", "[map][gui][tool-gestures][gizmo][edge-workflow][persistence]" )
{
    session_t s;
    const u64 mesh = EdgeWorkflowQuad( s );
    const auto originalEdge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, originalEdge, MAP_SELECT_REPLACE ) );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    REQUIRE( original.value.vertices.nCount == 4 ); REQUIRE( original.value.faces.nCount == 1 );
    s.Tool( "map.tool.translate" );
    const usize steps = s.Steps();
    for ( u32 step = 0; step < 2; ++step ) {
        const auto previousEdge = OnlySelectedEdge( s );
        const f64 x = 128.0 + 64.0 * step, grab = OnXHandle( s, { x, 0, 0 } );
        s.Mouse( QEvent::MouseButtonPress, s.At( grab, 0 ), Qt::LeftButton, Qt::ShiftModifier );
        s.Mouse( QEvent::MouseMove, s.At( grab + 64, 0 ), Qt::LeftButton, Qt::ShiftModifier );
        if ( step == 0 ) {
            if ( const QByteArray shot = qgetenv( "MASON_EDGE_EXTEND_SHOT" ); !shot.isEmpty() ) { s.top->grab().save( QString::fromUtf8( shot ) ); }
        }
        // Extension is chosen at press; releasing Shift before the mouse must
        // not silently turn it into a move of the existing edge.
        s.Mouse( QEvent::MouseButtonRelease, s.At( grab + 64, 0 ), Qt::NoButton );
        CHECK( s.Steps() == steps + step + 1 );
        edge_workflow_mesh_t extended( *s.workspace.pDocument, mesh );
        CHECK( extended.value.vertices.nCount == 6u + step * 2u );
        CHECK( extended.value.faces.nCount == 2u + step );
        CHECK( extended.value.corners.nCount == 8u + step * 4u );
        const auto outer = OnlySelectedEdge( s );
        CHECK( outer.a.value != previousEdge.a.value ); CHECK( outer.a.value != previousEdge.b.value );
        CHECK( outer.b.value != previousEdge.a.value ); CHECK( outer.b.value != previousEdge.b.value );
        const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
        CHECK( a.x == Catch::Approx( x + 64 ) ); CHECK( b.x == Catch::Approx( x + 64 ) );
        CHECK( std::fabs( a.y - b.y ) == Catch::Approx( 128 ) ); CHECK( a.z == 0 ); CHECK( b.z == 0 );
        CheckOriginalVertices( s, mesh, original );
    }
    edge_workflow_mesh_t complete( *s.workspace.pDocument, mesh );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 ); CHECK( Bounds( s.workspace, mesh ).maximum.x == Catch::Approx( 192 ) );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &complete.value, &restored.value ) ); }
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "edge-strip.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &s.workspace, path ).status == map_files_status_t::OK );
    session_t loaded;
    REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
    edge_workflow_mesh_t reloaded( *loaded.workspace.pDocument, mesh );
    CHECK( geo::MeshSourceDescription_Equal( &complete.value, &reloaded.value ) );
}

TEST_CASE( "Shift Rotate extends an edge about a remote pivot without turning the original quad", "[map][gui][tool-gestures][gizmo][edge-workflow][pivot]" )
{
    session_t s;
    const u64 mesh = EdgeWorkflowQuad( s, { { 128, -64, 0 }, { 256, 64, 0 } } );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, 64, 0 } ), VertexAt( s.workspace, mesh, { 256, 64, 0 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    MapToolGestures_SetPivot( &s.workspace, { 0, 0, 0 } );
    MapWorkspace_SetAngleSnap( &s.workspace, 15 );
    s.Tool( "map.tool.rotate" );
    const usize steps = s.Steps();
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() );
    s.Mouse( QEvent::MouseButtonPress, s.At( radius, 0 ), Qt::LeftButton, Qt::ShiftModifier );
    s.Mouse( QEvent::MouseMove, s.At( 0, radius ), Qt::LeftButton, Qt::ShiftModifier );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 0, radius ), Qt::NoButton );
    CHECK( s.Steps() == steps + 1 );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, original );
    const auto outer = OnlySelectedEdge( s );
    CHECK( outer.a.value != edge.a.value ); CHECK( outer.a.value != edge.b.value );
    CHECK( outer.b.value != edge.a.value ); CHECK( outer.b.value != edge.b.value );
    const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
    CHECK( a.x == Catch::Approx( -64 ).margin( 1e-6 ) ); CHECK( b.x == Catch::Approx( -64 ).margin( 1e-6 ) );
    CHECK( std::min( a.y, b.y ) == Catch::Approx( 128 ) ); CHECK( std::max( a.y, b.y ) == Catch::Approx( 256 ) );
    CHECK( a.z == 0 ); CHECK( b.z == 0 );
    edge_workflow_mesh_t complete( *s.workspace.pDocument, mesh );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &complete.value, &restored.value ) ); }
}

TEST_CASE( "An ordinary edge move and rotation change only its endpoints", "[map][gui][tool-gestures][gizmo][edge-workflow]" )
{
    session_t s;
    const u64 mesh = EdgeWorkflowQuad( s );
    const auto fixedA = VertexAt( s.workspace, mesh, { 0, -64, 0 } ), fixedB = VertexAt( s.workspace, mesh, { 0, 64, 0 } );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
    s.Tool( "map.tool.translate" );
    const usize steps = s.Steps();
    const f64 grab = OnXHandle( s, { 128, 0, 0 } );
    s.Drag( grab, 0, grab + 32, 0 );
    CHECK( s.Steps() == steps + 1 );
    CHECK( Position( s.workspace, mesh, edge.a ).x == Catch::Approx( 160 ) );
    CHECK( Position( s.workspace, mesh, edge.b ).x == Catch::Approx( 160 ) );
    CheckPosition( Position( s.workspace, mesh, fixedA ), { 0, -64, 0 } );
    CheckPosition( Position( s.workspace, mesh, fixedB ), { 0, 64, 0 } );
    const auto selected = OnlySelectedEdge( s ); CHECK( selected.a.value == edge.a.value ); CHECK( selected.b.value == edge.b.value );
    s.Tool( "map.tool.rotate" );
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( 160 + radius, 0, 160, radius );
    CHECK( s.Steps() == steps + 2 );
    const auto a = Position( s.workspace, mesh, edge.a ), b = Position( s.workspace, mesh, edge.b );
    CHECK( a.y == Catch::Approx( 0 ).margin( 1e-6 ) ); CHECK( b.y == Catch::Approx( 0 ).margin( 1e-6 ) );
    CHECK( std::min( a.x, b.x ) == Catch::Approx( 96 ) ); CHECK( std::max( a.x, b.x ) == Catch::Approx( 224 ) );
    CheckPosition( Position( s.workspace, mesh, fixedA ), { 0, -64, 0 } );
    CheckPosition( Position( s.workspace, mesh, fixedB ), { 0, 64, 0 } );
    CHECK( MeshFaces( s.workspace, mesh ) == 1 );
}

TEST_CASE( "Cancelling and zero-distance edge extension leave source geometry and history untouched", "[map][gui][tool-gestures][gizmo][edge-workflow][cancel]" )
{
    for ( const char *tool : { "map.tool.translate", "map.tool.rotate" } ) {
        CAPTURE( tool );
        session_t s;
        const u64 mesh = EdgeWorkflowQuad( s );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
        edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
        const usize steps = s.Steps(); const u64 nextId = s.workspace.pDocument->nextId;
        s.Tool( tool );
        const f64 radius = ( s.workspace.tool == map_tool_t::ROTATE ? 60.0 : 40.0 ) / MapOrthoView_Zoom( s.top.get() );
        const QPointF begin = s.At( 128 + radius, 0 ), finish = s.At( 128, radius );
        s.Mouse( QEvent::MouseButtonPress, begin, Qt::LeftButton, Qt::ShiftModifier );
        s.Mouse( QEvent::MouseButtonRelease, begin, Qt::NoButton, Qt::ShiftModifier );
        CHECK( s.Steps() == steps ); CHECK( s.workspace.pDocument->nextId == nextId );
        { edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
        s.Mouse( QEvent::MouseButtonPress, begin, Qt::LeftButton, Qt::ShiftModifier );
        s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton, Qt::ShiftModifier );
        s.Key( Qt::Key_Escape );
        s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton, Qt::ShiftModifier );
        CHECK( s.Steps() == steps ); CHECK( s.workspace.pDocument->nextId == nextId );
        { edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
    }
}

TEST_CASE( "Perspective component rotation picks the visible X Y and Z rings instead of the whole disk", "[map][gui][tool-gestures][gizmo][edge-workflow][axis-rings]" )
{
    for ( u32 axis = 0; axis < 3; ++axis ) {
        CAPTURE( axis );
        session_t s;
        const u64 mesh = EdgeWorkflowQuad( s, { { 128, -64, 0 }, { 256, 64, 0 } } );
        const auto fixed = VertexAt( s.workspace, mesh, { 128, -64, 0 } );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, 64, 0 } ), VertexAt( s.workspace, mesh, { 256, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
        const auto a0 = Position( s.workspace, mesh, edge.a ), b0 = Position( s.workspace, mesh, edge.b );
        MapToolGestures_SetPivot( &s.workspace, {} );
        MapWorkspace_SetAngleSnap( &s.workspace, 15 );
        s.Tool( "map.tool.rotate" );
        const usize steps = s.Steps();
        // A deterministic perspective camera faces the origin along each axis.
        // The other two rings are edge-on, so a diagonal ring point is unique.
        struct projection_t { u32 axis; } projection{ axis };
        map_tool_view_t view{}; view.pView = &projection; view.rect = QRectF( 0, 0, 800, 600 );
        view.pfnProject = []( const void *context, math::vec3d_t world, QPointF *screen ) {
            const u32 axis = static_cast<const projection_t *>( context )->axis;
            const f64 c[]{ world.x, world.y, world.z }, depth = 800.0 - c[axis];
            if ( depth <= 0.0 ) { return false; }
            *screen = QPointF( 400.0 + c[( axis + 1 ) % 3] * 800.0 / depth, 300.0 - c[( axis + 2 ) % 3] * 800.0 / depth );
            return true;
        };
        view.eye = axis == 0 ? math::vec3d_t{ 800, 0, 0 } : axis == 1 ? math::vec3d_t{ 0, 800, 0 } : math::vec3d_t{ 0, 0, 800 };
        const auto eventAt = [&]( map_tool_event_kind_t kind, f64 u, f64 v ) {
            f64 c[3]{}; c[( axis + 1 ) % 3] = u; c[( axis + 2 ) % 3] = v;
            const math::vec3d_t point{ c[0], c[1], c[2] };
            map_tool_event_t event{}; event.kind = kind; event.view = view; event.rayOrigin = view.eye;
            REQUIRE( view.pfnProject( view.pView, point, &event.screen ) );
            event.rayDirection = { point.x - view.eye.x, point.y - view.eye.y, point.z - view.eye.z };
            const f64 length = std::sqrt( event.rayDirection.x * event.rayDirection.x + event.rayDirection.y * event.rayDirection.y + event.rayDirection.z * event.rayDirection.z );
            event.rayDirection = { event.rayDirection.x / length, event.rayDirection.y / length, event.rayDirection.z / length };
            event.bButtonDown = kind != map_tool_event_kind_t::RELEASE;
            return MapToolGestures_Event( &s.workspace, event );
        };
        // The old 90-pixel disk incorrectly swallowed this empty interior.
        CHECK_FALSE( eventAt( map_tool_event_kind_t::PRESS, 20, 20 ) );
        CHECK( s.Steps() == steps );
        const f64 diagonal = 100.0 / std::sqrt( 2.0 ); // distance 800 * 0.125 radius.
        REQUIRE( eventAt( map_tool_event_kind_t::PRESS, diagonal, diagonal ) );
        REQUIRE( eventAt( map_tool_event_kind_t::MOVE, -diagonal, diagonal ) );
        REQUIRE( eventAt( map_tool_event_kind_t::RELEASE, -diagonal, diagonal ) );
        CHECK( s.Steps() == steps + 1 );
        const auto rotated = [axis]( math::vec3d_t p ) {
            if ( axis == 0 ) { return math::vec3d_t{ p.x, -p.z, p.y }; }
            if ( axis == 1 ) { return math::vec3d_t{ p.z, p.y, -p.x }; }
            return math::vec3d_t{ -p.y, p.x, p.z };
        };
        CheckPosition( Position( s.workspace, mesh, edge.a ), rotated( a0 ) );
        CheckPosition( Position( s.workspace, mesh, edge.b ), rotated( b0 ) );
        CheckPosition( Position( s.workspace, mesh, fixed ), { 128, -64, 0 } );
        const auto selected = OnlySelectedEdge( s ); CHECK( selected.a.value == edge.a.value ); CHECK( selected.b.value == edge.b.value );
        CHECK( MeshFaces( s.workspace, mesh ) == 1 );
    }
}

TEST_CASE( "Numeric Extrude and Extend continue from each newly selected boundary edge", "[map][gui][tool-gestures][edge-workflow][commands]" )
{
    for ( const char *command : { "map.mesh.extrude_edges", "map.mesh.extend_edges" } ) {
        CAPTURE( command );
        session_t s;
        const u64 mesh = EdgeWorkflowQuad( s );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
        edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
        const usize steps = s.Steps();
        for ( u32 step = 0; step < 2; ++step ) {
            const auto previous = OnlySelectedEdge( s );
            REQUIRE( ( EditorCommands_State( &s.gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_ENABLED ) != 0 );
            s.Tool( command );
            const auto outer = OnlySelectedEdge( s );
            CHECK( outer.a.value != previous.a.value ); CHECK( outer.a.value != previous.b.value );
            CHECK( outer.b.value != previous.a.value ); CHECK( outer.b.value != previous.b.value );
            CHECK( MeshFaces( s.workspace, mesh ) == 2u + step );
            CHECK( s.Steps() == steps + step + 1 );
            CheckOriginalVertices( s, mesh, original );
            if ( std::string( command ) == "map.mesh.extend_edges" ) {
                CHECK( Position( s.workspace, mesh, outer.a ).x == Catch::Approx( 144.0 + 16.0 * step ) );
                CHECK( Position( s.workspace, mesh, outer.b ).x == Catch::Approx( 144.0 + 16.0 * step ) );
            }
        }
    }
}

TEST_CASE( "Shift Scale extends the selected edge around an explicit pivot", "[map][gui][tool-gestures][gizmo][edge-workflow][scale]" )
{
    session_t s;
    const u64 mesh = EdgeWorkflowQuad( s );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    MapToolGestures_SetPivot( &s.workspace, {} );
    s.Tool( "map.tool.scale" );
    const usize steps = s.Steps();
    const f64 grab = 60.0 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( grab, 0, grab * 2.0, 0, Qt::ShiftModifier );
    CHECK( s.Steps() == steps + 1 );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, original );
    const auto outer = OnlySelectedEdge( s );
    CHECK( outer.a.value != edge.a.value ); CHECK( outer.a.value != edge.b.value );
    CHECK( outer.b.value != edge.a.value ); CHECK( outer.b.value != edge.b.value );
    const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
    CHECK( a.x == Catch::Approx( 256 ) ); CHECK( b.x == Catch::Approx( 256 ) );
    CHECK( std::min( a.y, b.y ) == Catch::Approx( -128 ) ); CHECK( std::max( a.y, b.y ) == Catch::Approx( 128 ) );
    CHECK( a.z == 0 ); CHECK( b.z == 0 );
}

namespace
{
struct edge_gui_allocation_t { usize calls{}, failOn{}, live{}; };
void *EdgeGuiAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<edge_gui_allocation_t *>( context );
    if ( ++audit.calls == audit.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++audit.live; }
    return memory;
}
void EdgeGuiFree( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<edge_gui_allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
struct edge_gui_allocator_scope_t {
    gui::editor_gui_t &gui;
    const allocator_t *previous;
    edge_gui_allocator_scope_t( gui::editor_gui_t &target, const allocator_t *allocator ) : gui( target ), previous( target.pAllocator ) { gui.pAllocator = allocator; }
    ~edge_gui_allocator_scope_t() { gui.pAllocator = previous; }
};
} // namespace

TEST_CASE( "Edge extension GUI allocation failures preserve geometry selection and undo state for retry", "[map][gui][tool-gestures][gizmo][edge-workflow][allocation]" )
{
    // Inject only GUI allocations: authored geometry retains its normal
    // allocator, so this probes the next-edge storage and post-edit publication.
    for ( bool command : { false, true } ) {
        CAPTURE( command );
        const auto prepare = []( session_t &s ) {
            const u64 mesh = EdgeWorkflowQuad( s );
            const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
            REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
            s.Tool( "map.tool.translate" );
            return mesh;
        };
        const auto apply = [command]( session_t &s ) {
            if ( command ) { return EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.extend_edges" ), {} ) == command_result_t::OK; }
            const usize steps = s.Steps();
            const f64 grab = OnXHandle( s, { 128, 0, 0 } );
            s.Drag( grab, 0, grab + 64, 0, Qt::ShiftModifier );
            return s.Steps() != steps;
        };
        edge_gui_allocation_t audit{};
        const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
        usize successfulCalls = 0;
        {
            session_t s; prepare( s );
            { edge_gui_allocator_scope_t scope( s.gui, &allocator ); REQUIRE( apply( s ) ); }
            successfulCalls = audit.calls; REQUIRE( successfulCalls > 3 );
            REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
        }
        REQUIRE( audit.live == 0 );
        // The initial root vector and prepared edge vector allocate first;
        // later failures exercise wireframe and undo publication after the
        // private geometry edit has succeeded.
        for ( usize failure : { usize{ 1 }, usize{ 2 }, usize{ 3 }, successfulCalls } ) {
            CAPTURE( failure );
            {
                session_t s; const u64 mesh = prepare( s );
                edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
                const auto edge = OnlySelectedEdge( s );
                const auto *document = s.workspace.pDocument;
                const auto revision = document->geometry.revision;
                const auto nextId = document->nextId;
                const auto *points = s.workspace.wire.points.pData;
                const auto *edges = s.workspace.meshSelection.edges.pData;
                const auto selectionRevision = s.workspace.selection.revision;
                const auto token = UndoRedo_StateToken( s.workspace.history.pUndo );
                const usize steps = s.Steps();
                audit.calls = 0; audit.failOn = failure;
                { edge_gui_allocator_scope_t scope( s.gui, &allocator ); CHECK_FALSE( apply( s ) ); }
                CHECK( audit.calls >= failure ); CHECK( audit.live == 0 );
                CHECK( s.workspace.pDocument == document ); CHECK( s.workspace.pDocument->geometry.revision == revision );
                CHECK( s.workspace.pDocument->nextId == nextId ); CHECK( s.workspace.wire.points.pData == points );
                CHECK( s.workspace.meshSelection.edges.pData == edges ); CHECK( s.workspace.selection.revision == selectionRevision );
                CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
                const auto selected = OnlySelectedEdge( s ); CHECK( selected.a.value == edge.a.value ); CHECK( selected.b.value == edge.b.value );
                { edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
                audit.calls = 0; audit.failOn = 0;
                REQUIRE( apply( s ) ); CHECK( s.Steps() == steps + 1 );
                const auto outer = OnlySelectedEdge( s ); CHECK( outer.a.value != edge.a.value ); CHECK( outer.b.value != edge.b.value );
            }
            CHECK( audit.live == 0 );
        }
    }
}

TEST_CASE( "A component rotation released at its pivot cannot commit an earlier valid angle", "[map][gui][tool-gestures][gizmo][edge-workflow][invalid-sample]" )
{
    session_t s;
    const u64 mesh = EdgeWorkflowQuad( s );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    s.Tool( "map.tool.rotate" );
    const usize steps = s.Steps();
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() );
    const QPointF start = s.At( 128 + radius, 0 ), valid = s.At( 128, radius ), pivot = s.At( 128, 0 );
    s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, valid, Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, pivot, Qt::LeftButton );
    s.Mouse( QEvent::MouseButtonRelease, pivot, Qt::NoButton );
    CHECK( s.Steps() == steps );
    { edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
    // An undefined angle clears the previous preview, but a later usable
    // sample during a new drag can still produce the requested quarter turn.
    s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, pivot, Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, valid, Qt::LeftButton );
    s.Mouse( QEvent::MouseButtonRelease, valid, Qt::NoButton );
    CHECK( s.Steps() == steps + 1 );
    const auto a = Position( s.workspace, mesh, edge.a ), b = Position( s.workspace, mesh, edge.b );
    CHECK( a.y == Catch::Approx( 0 ).margin( 1e-6 ) ); CHECK( b.y == Catch::Approx( 0 ).margin( 1e-6 ) );
    CHECK( std::min( a.x, b.x ) == Catch::Approx( 64 ) ); CHECK( std::max( a.x, b.x ) == Catch::Approx( 192 ) );
}

TEST_CASE( "A component drag cannot apply to geometry or selection changed after press", "[map][gui][tool-gestures][gizmo][edge-workflow][gesture-context]" )
{
    for ( bool documentEdit : { false, true } ) {
        CAPTURE( documentEdit );
        session_t s;
        const u64 mesh = EdgeWorkflowQuad( s );
        const auto right = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 128, -64, 0 } ), VertexAt( s.workspace, mesh, { 128, 64, 0 } ) );
        const auto left = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 0, -64, 0 } ), VertexAt( s.workspace, mesh, { 0, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, right, MAP_SELECT_REPLACE ) );
        s.Tool( "map.tool.translate" );
        const f64 grab = OnXHandle( s, { 128, 0, 0 } );
        s.Mouse( QEvent::MouseButtonPress, s.At( grab, 0 ), Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, s.At( grab + 64, 0 ), Qt::LeftButton );
        if ( documentEdit ) {
            // The numeric panel changes the document and preserves the same
            // selected endpoint IDs while the old mouse gesture is active.
            REQUIRE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::TRANSLATE,
                { 16, 0, 0 }, map_component_pivot_t::CENTER ) );
        } else {
            // Component-set commands may change selection while Translate is
            // active; this change intentionally occurs during the captured gesture.
            REQUIRE( MapWorkspace_SetMeshComponents( &s.workspace, mesh, {}, { &left, 1 } ) );
        }
        edge_workflow_mesh_t beforeRelease( *s.workspace.pDocument, mesh );
        const usize steps = s.Steps(); const auto selected = OnlySelectedEdge( s );
        s.Mouse( QEvent::MouseButtonRelease, s.At( grab + 64, 0 ), Qt::NoButton );
        CHECK( s.Steps() == steps );
        edge_workflow_mesh_t afterRelease( *s.workspace.pDocument, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &beforeRelease.value, &afterRelease.value ) );
        const auto retained = OnlySelectedEdge( s );
        CHECK( retained.a.value == selected.a.value ); CHECK( retained.b.value == selected.b.value );
    }
}

namespace
{
// An eight-sided slab reproduces the overlapping top/bottom handles in the
// user's Top view while leaving enough faces for a valid adjacent weld.
u64 VertexWorkflowSlab( session_t &s )
{
    map_primitive_desc_t cylinder{}; cylinder.kind = map_primitive_kind_t::CYLINDER; cylinder.axis = 2; cylinder.nSides = 8;
    cylinder.bounds = { { -128, -128, 0 }, { 128, 128, 32 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, cylinder ) );
    const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
    if ( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { mesh } ) == nullptr ) {
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    }
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    MapWorkspace_SetSnapToGrid( &s.workspace, CY_FALSE );
    s.Tool( "map.tool.select" );
    return mesh;
}
std::vector<math::vec3d_t> VertexWorkflowTopRim( const edge_workflow_mesh_t &description )
{
    std::vector<math::vec3d_t> rim;
    for ( usize f = 0; f < description.value.faces.nCount; ++f ) {
        const auto &face = description.value.faces.pData[f];
        if ( face.cCorners != 8 ) { continue; }
        bool top = true;
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &p = description.value.vertices.pData[description.value.corners.pData[face.iFirstCorner + k].iVertex].position;
            top = top && std::fabs( p.z - 32.0 ) < 1e-6;
        }
        if ( !top ) { continue; }
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            rim.push_back( description.value.vertices.pData[description.value.corners.pData[face.iFirstCorner + k].iVertex].position );
        }
        return rim;
    }
    FAIL( "the slab must have an eight-sided top cap" ); return rim;
}
bool VertexWorkflowContains( const edge_workflow_mesh_t &description, geo::geometry_source_id_t id )
{
    for ( usize i = 0; i < description.value.vertices.nCount; ++i ) {
        if ( description.value.vertices.pData[i].sourceId.value == id.value ) { return true; }
    }
    return false;
}
} // namespace

TEST_CASE( "Two Top-view column clicks connect both cylinder caps", "[map][gui][tool-gestures][vertex-workflow][connect]" )
{
    session_t s; const u64 mesh = VertexWorkflowSlab( s );
    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
    const auto rim = VertexWorkflowTopRim( before ); REQUIRE( rim.size() == 8 );
    s.Click( rim[0].x, rim[0].y ); s.Click( rim[4].x, rim[4].y, Qt::ShiftModifier );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 4 );
    const usize steps = s.Steps();
    REQUIRE( ( EditorCommands_State( &s.gui.commands, StringView_FromCString( "map.mesh.connect_edges" ) ) & COMMAND_STATE_ENABLED ) != 0 );
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.connect_edges" ), {} ) == command_result_t::OK );
    edge_workflow_mesh_t connected( *s.workspace.pDocument, mesh );
    CHECK( connected.value.faces.nCount == before.value.faces.nCount + 2 );
    CHECK( connected.value.vertices.nCount == before.value.vertices.nCount );
    CheckOriginalVertices( s, mesh, before );
    CHECK( Bounds( s.workspace, mesh ).minimum.z == Catch::Approx( 0 ) );
    CHECK( Bounds( s.workspace, mesh ).maximum.z == Catch::Approx( 32 ) );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &connected.value, &restored.value ) ); }
}

TEST_CASE( "Merge after two Top-view column clicks preserves the slab's depth layers", "[map][gui][tool-gestures][vertex-workflow][merge]" )
{
    session_t s; const u64 mesh = VertexWorkflowSlab( s );
    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
    const auto rim = VertexWorkflowTopRim( before ); REQUIRE( rim.size() == 8 );
    s.Click( rim[0].x, rim[0].y ); s.Click( rim[1].x, rim[1].y, Qt::ShiftModifier );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 4 );
    const usize steps = s.Steps();
    REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.merge" ), {} ) == command_result_t::OK );
    edge_workflow_mesh_t merged( *s.workspace.pDocument, mesh );
    CHECK( merged.value.vertices.nCount == before.value.vertices.nCount - 2 );
    CHECK( s.workspace.meshSelection.vertices.nCount == 2 );
    CHECK( s.Steps() == steps + 1 );
    usize bottom = 0, top = 0;
    for ( usize i = 0; i < merged.value.vertices.nCount; ++i ) {
        const f64 z = merged.value.vertices.pData[i].position.z;
        bottom += std::fabs( z ) < 1e-6; top += std::fabs( z - 32 ) < 1e-6;
    }
    CHECK( bottom == 7 ); CHECK( top == 7 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &merged.value, &restored.value ) ); }
}

TEST_CASE( "Dragging one quad vertex onto its neighbour welds to the stationary target", "[map][gui][tool-gestures][vertex-workflow][drag-weld][persistence]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    s.Tool( "map.tool.select" );
    const auto moving = VertexAt( s.workspace, mesh, { 128, -64, 0 } );
    const auto target = VertexAt( s.workspace, mesh, { 128, 64, 0 } );
    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
    s.Drag( 128, -64, 128, 64 );
    REQUIRE( s.Steps() == steps + 1 );
    edge_workflow_mesh_t welded( *s.workspace.pDocument, mesh );
    REQUIRE( welded.value.vertices.nCount == 3 ); REQUIRE( welded.value.faces.nCount == 1 );
    CHECK_FALSE( VertexWorkflowContains( welded, moving ) ); CHECK( VertexWorkflowContains( welded, target ) );
    CheckPosition( Position( s.workspace, mesh, target ), { 128, 64, 0 } );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 1 ); CHECK( VertexSelectedIn( s.workspace, target ) );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i];
        if ( vertex.sourceId.value != moving.value ) { CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), vertex.position ); }
    }
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &welded.value, &restored.value ) ); }
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "welded-vertex.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &s.workspace, path ).status == map_files_status_t::OK );
    session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
    edge_workflow_mesh_t saved( *loaded.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &welded.value, &saved.value ) );
}

TEST_CASE( "Dragging a cylinder column onto its neighbour welds each depth pair", "[map][gui][tool-gestures][vertex-workflow][drag-weld]" )
{
    session_t s; const u64 mesh = VertexWorkflowSlab( s );
    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const auto rim = VertexWorkflowTopRim( before );
    const auto movingHigh = VertexAt( s.workspace, mesh, rim[0] );
    const auto movingLow = VertexAt( s.workspace, mesh, { rim[0].x, rim[0].y, 0 } );
    const auto targetHigh = VertexAt( s.workspace, mesh, rim[1] );
    const auto targetLow = VertexAt( s.workspace, mesh, { rim[1].x, rim[1].y, 0 } );
    const usize steps = s.Steps(); s.Drag( rim[0].x, rim[0].y, rim[1].x, rim[1].y );
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t welded( *s.workspace.pDocument, mesh );
    CHECK( welded.value.vertices.nCount == before.value.vertices.nCount - 2 );
    CHECK_FALSE( VertexWorkflowContains( welded, movingHigh ) ); CHECK_FALSE( VertexWorkflowContains( welded, movingLow ) );
    CheckPosition( Position( s.workspace, mesh, targetHigh ), rim[1] );
    CheckPosition( Position( s.workspace, mesh, targetLow ), { rim[1].x, rim[1].y, 0 } );
    REQUIRE( s.workspace.meshSelection.vertices.nCount == 2 );
    CHECK( VertexSelectedIn( s.workspace, targetHigh ) ); CHECK( VertexSelectedIn( s.workspace, targetLow ) );
    CHECK( Bounds( s.workspace, mesh ).minimum.z == Catch::Approx( 0 ) ); CHECK( Bounds( s.workspace, mesh ).maximum.z == Catch::Approx( 32 ) );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i];
        if ( vertex.sourceId.value != movingHigh.value && vertex.sourceId.value != movingLow.value ) {
            CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), vertex.position );
        }
    }
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &welded.value, &restored.value ) ); }
}

TEST_CASE( "Nearby Top vertex handles acquire an exact off-grid weld target", "[map][gui][tool-gestures][vertex-workflow][vertex-target-acquisition]" )
{
    bool snap = true, releaseOnly = false;
    SECTION( "Grid 16 does not prevent acquisition of an octagon neighbour" ) { snap = true; }
    SECTION( "Grid off still acquires a target from a nearby mouse pixel" ) { snap = false; }
    SECTION( "The release acquires its own target after a distant preview sample" ) { releaseOnly = true; }
    session_t s; const u64 mesh = VertexWorkflowSlab( s ); auto &ws = s.workspace;
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetSnapToGrid( &ws, snap ? CY_TRUE : CY_FALSE );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const auto rim = VertexWorkflowTopRim( before );
    const auto movingHigh = VertexAt( ws, mesh, rim[0] );
    const auto movingLow = VertexAt( ws, mesh, { rim[0].x, rim[0].y, 0 } );
    const auto targetHigh = VertexAt( ws, mesh, rim[1] );
    const auto targetLow = VertexAt( ws, mesh, { rim[1].x, rim[1].y, 0 } );
    const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto revision = ws.pDocument->geometry.revision; const auto nextId = ws.pDocument->nextId;
    const QPointF start = s.At( rim[0].x, rim[0].y );
    // Human input lands near a drawn handle, not at an exact irrational world
    // coordinate. Acquisition must precede grid rounding and retain both depths.
    const QPointF finish = s.At( rim[1].x, rim[1].y ) + QPointF( 2, -1 );
    s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, ( start + finish ) * 0.5, Qt::LeftButton );
    if ( !releaseOnly ) { s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton ); }
    REQUIRE( ws.meshSelection.vertices.nCount == 2 );
    CHECK( VertexSelectedIn( ws, movingHigh ) ); CHECK( VertexSelectedIn( ws, movingLow ) );
    CHECK( s.Steps() == steps ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    { edge_workflow_mesh_t previewSource( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &previewSource.value ) ); }
    s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t welded( *ws.pDocument, mesh );
    CHECK( welded.value.vertices.nCount == before.value.vertices.nCount - 2 );
    CHECK_FALSE( VertexWorkflowContains( welded, movingHigh ) ); CHECK_FALSE( VertexWorkflowContains( welded, movingLow ) );
    CHECK( VertexWorkflowContains( welded, targetHigh ) ); CHECK( VertexWorkflowContains( welded, targetLow ) );
    CheckPosition( Position( ws, mesh, targetHigh ), rim[1] );
    CheckPosition( Position( ws, mesh, targetLow ), { rim[1].x, rim[1].y, 0 } );
    CHECK( ws.meshSelection.vertices.nCount == 2 );
    CHECK( VertexSelectedIn( ws, targetHigh ) ); CHECK( VertexSelectedIn( ws, targetLow ) );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &v = before.value.vertices.pData[i];
        if ( v.sourceId.value != movingHigh.value && v.sourceId.value != movingLow.value ) {
            CheckPosition( Position( ws, mesh, v.sourceId ), v.position );
        }
    }
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    CHECK( VertexSelectedIn( ws, movingHigh ) ); CHECK( VertexSelectedIn( ws, movingLow ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &welded.value, &restored.value ) ); }
    CHECK( VertexSelectedIn( ws, targetHigh ) ); CHECK( VertexSelectedIn( ws, targetLow ) );
}

TEST_CASE( "Camera vertex drag acquires a nearby stationary handle on its movement plane", "[map][gui][tool-gestures][vertex-workflow][vertex-target-acquisition]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s ); auto &ws = s.workspace;
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); MapWorkspace_SetSnapToGrid( &ws, CY_FALSE );
    s.Tool( "map.tool.select" ); camera_t camera( s );
    MapCameraView_LookAt( camera.view.get(), { 320, -400, 500 }, { 64, 0, 0 } ); QCoreApplication::processEvents();
    const math::vec3d_t sourcePosition{ 128, -64, 0 }, targetPosition{ 128, 64, 0 };
    const auto moving = VertexAt( ws, mesh, sourcePosition ), target = VertexAt( ws, mesh, targetPosition );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
    const QPointF start = camera.At( sourcePosition );
    const QPointF finish = camera.At( targetPosition ) + QPointF( -2, 1 );
    camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton );
    camera.Send( QEvent::MouseMove, ( start + finish ) * 0.5, Qt::LeftButton );
    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton );
    REQUIRE( ws.meshSelection.vertices.nCount == 1 ); REQUIRE( VertexSelectedIn( ws, moving ) );
    CHECK( s.Steps() == steps );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t welded( *ws.pDocument, mesh );
    CHECK( welded.value.vertices.nCount == 3 ); CHECK( welded.value.faces.nCount == 1 );
    CHECK_FALSE( VertexWorkflowContains( welded, moving ) ); CHECK( VertexWorkflowContains( welded, target ) );
    CHECK( ws.meshSelection.vertices.nCount == 1 ); CHECK( VertexSelectedIn( ws, target ) );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &v = before.value.vertices.pData[i];
        if ( v.sourceId.value != moving.value ) { CheckPosition( Position( ws, mesh, v.sourceId ), v.position ); }
    }
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &welded.value, &restored.value ) ); }
}

TEST_CASE( "Camera vertex dragging keeps a fixed upright plane at shallow angles", "[map][gui][tool-gestures][vertex-workflow][vertex-target-acquisition]" )
{
    bool weld = false;
    SECTION( "An unsnapped drag stays on the plane through the grabbed corner" ) { weld = false; }
    SECTION( "A nearby stationary corner on that plane can be acquired" ) { weld = true; }
    session_t s; auto &ws = s.workspace;
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 1;
    quad.bounds = { { 0, 0, 0 }, { 128, 0, 128 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
    MapWorkspace_SetSnapToGrid( &ws, CY_FALSE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 0 }, { 64, 0, 64 } ); QCoreApplication::processEvents();
    const auto moving = VertexAt( ws, mesh, { 0, 0, 0 } ), target = VertexAt( ws, mesh, { 128, 0, 0 } );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
    if ( weld ) {
        const QPointF from = camera.At( { 0, 0, 0 } ), to = camera.At( { 128, 0, 0 } ) + QPointF( 2, -1 );
        camera.Send( QEvent::MouseButtonPress, from, Qt::LeftButton );
        camera.Send( QEvent::MouseMove, ( from + to ) * 0.5, Qt::LeftButton );
        camera.Send( QEvent::MouseButtonRelease, to, Qt::NoButton );
    } else {
        // The eye and grabbed corner define y=0. Rebuilding the plane from
        // each cursor ray incorrectly lets this same gesture drift in Y.
        camera.DragHolding( { 0, 0, 0 }, { 32, 0, 32 }, Qt::ControlModifier );
    }
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t after( *ws.pDocument, mesh );
    CHECK( after.value.vertices.nCount == ( weld ? 3u : 4u ) );
    if ( weld ) { CHECK_FALSE( VertexWorkflowContains( after, moving ) ); CHECK( VertexSelectedIn( ws, target ) ); }
    else { CheckPosition( Position( ws, mesh, moving ), { 32, 0, 32 } ); CHECK( VertexSelectedIn( ws, moving ) ); }
    CheckPosition( Position( ws, mesh, target ), { 128, 0, 0 } );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &v = before.value.vertices.pData[i];
        if ( v.sourceId.value != moving.value ) { CheckPosition( Position( ws, mesh, v.sourceId ), v.position ); }
    }
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}

TEST_CASE( "Shift absolute grid placement takes priority over a nearby vertex target", "[map][gui][tool-gestures][vertex-workflow][vertex-target-acquisition]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s, { { 0, -64, 0 }, { 139, 68, 0 } } ); auto &ws = s.workspace;
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetSnapToGrid( &ws, CY_TRUE );
    const auto moving = VertexAt( ws, mesh, { 139, -64, 0 } ), target = VertexAt( ws, mesh, { 139, 68, 0 } );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
    s.Mouse( QEvent::MouseButtonPress, s.At( 139, -64 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 139, 0 ), Qt::LeftButton, Qt::ShiftModifier );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 139, 68 ), Qt::NoButton, Qt::ShiftModifier );
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t after( *ws.pDocument, mesh );
    CHECK( after.value.vertices.nCount == 4 ); CHECK( VertexWorkflowContains( after, moving ) ); CHECK( VertexWorkflowContains( after, target ) );
    CheckPosition( Position( ws, mesh, moving ), { 144, 64, 0 } ); CheckPosition( Position( ws, mesh, target ), { 139, 68, 0 } );
    CHECK( VertexSelectedIn( ws, moving ) ); CHECK_FALSE( VertexSelectedIn( ws, target ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}

TEST_CASE( "Ctrl and Command during a vertex drag bypass grid and target acquisition", "[map][gui][tool-gestures][vertex-workflow][vertex-target-acquisition]" )
{
    for ( const auto modifier : { Qt::ControlModifier, Qt::MetaModifier } ) {
        CAPTURE( static_cast<int>( modifier ) );
        session_t s; const u64 mesh = EdgeWorkflowQuad( s ); auto &ws = s.workspace;
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
        MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetSnapToGrid( &ws, CY_TRUE );
        const math::vec3d_t sourcePosition{ 128, -64, 0 }, targetPosition{ 128, 64, 0 }, destination{ 124, 60, 0 };
        const auto moving = VertexAt( ws, mesh, sourcePosition ), target = VertexAt( ws, mesh, targetPosition );
        edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
        const QPointF finish = s.At( destination.x, destination.y ), targetScreen = s.At( targetPosition.x, targetPosition.y );
        REQUIRE( std::hypot( finish.x() - targetScreen.x(), finish.y() - targetScreen.y() ) < 9.0 );
        // A modified press toggles selection. Hold the modifier only once the
        // ordinary press has picked the source to exercise the drag contract.
        s.Mouse( QEvent::MouseButtonPress, s.At( sourcePosition.x, sourcePosition.y ), Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, s.At( 126, 0 ), Qt::LeftButton, modifier );
        s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton, modifier );
        CHECK( s.Steps() == steps );
        s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton, modifier );
        REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t moved( *ws.pDocument, mesh );
        CHECK( moved.value.vertices.nCount == 4 ); CHECK( moved.value.faces.nCount == 1 );
        CHECK( VertexWorkflowContains( moved, moving ) ); CHECK( VertexWorkflowContains( moved, target ) );
        CheckPosition( Position( ws, mesh, moving ), destination ); CheckPosition( Position( ws, mesh, target ), targetPosition );
        REQUIRE( ws.meshSelection.vertices.nCount == 1 ); CHECK( VertexSelectedIn( ws, moving ) );
        for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
            const auto &v = before.value.vertices.pData[i];
            if ( v.sourceId.value != moving.value ) { CheckPosition( Position( ws, mesh, v.sourceId ), v.position ); }
        }
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &moved.value, &restored.value ) ); }
    }
}

TEST_CASE( "Cancelled and invalid vertex welds leave authored geometry and history untouched", "[map][gui][tool-gestures][vertex-workflow][drag-weld][cancel]" )
{
    for ( bool cancel : { false, true } ) {
        CAPTURE( cancel ); session_t s; const u64 mesh = EdgeWorkflowQuad( s );
        MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
        const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( s.workspace.history.pUndo );
        s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
        const QPointF finish = cancel ? s.At( 128, 64 ) : s.At( 0, 64 );
        // Adjacent is valid but cancelled; the opposite corner would collapse
        // the entire quad to two zero-area loops and must not publish.
        s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton );
        if ( cancel ) { s.Key( Qt::Key_Escape ); }
        s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
        CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
        edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
    }
}

TEST_CASE( "Vertex weld GUI allocation failures preserve geometry and selection for retry", "[map][gui][tool-gestures][vertex-workflow][drag-weld][allocation][vertex-target-acquisition]" )
{
    const auto prepare = []( session_t &s ) {
        const u64 mesh = EdgeWorkflowQuad( s );
        MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
        s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, ( s.At( 128, 64 ) + QPointF( 2, -1 ) ), Qt::LeftButton );
        return mesh;
    };
    edge_gui_allocation_t audit{};
    const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    usize successfulCalls = 0;
    {
        session_t s; prepare( s ); const usize steps = s.Steps();
        { edge_gui_allocator_scope_t scope( s.gui, &allocator ); s.Mouse( QEvent::MouseButtonRelease, ( s.At( 128, 64 ) + QPointF( 2, -1 ) ), Qt::NoButton ); }
        REQUIRE( s.Steps() == steps + 1 ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
    }
    REQUIRE( audit.live == 0 );
    for ( usize failure : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
        CAPTURE( failure );
        {
            session_t s; const u64 mesh = prepare( s ); auto &ws = s.workspace;
            edge_workflow_mesh_t original( *ws.pDocument, mesh );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto nextId = document->nextId;
            const auto *points = ws.wire.points.pData; const auto *vertices = ws.meshSelection.vertices.pData;
            const auto selectionRevision = ws.selection.revision; const usize selected = ws.meshSelection.vertices.nCount;
            const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = s.Steps();
            audit.calls = 0; audit.failOn = failure;
            { edge_gui_allocator_scope_t scope( s.gui, &allocator ); s.Mouse( QEvent::MouseButtonRelease, ( s.At( 128, 64 ) + QPointF( 2, -1 ) ), Qt::NoButton ); }
            CHECK( audit.calls >= failure ); CHECK( audit.live == 0 );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
            CHECK( ws.wire.points.pData == points ); CHECK( ws.meshSelection.vertices.pData == vertices );
            CHECK( ws.meshSelection.vertices.nCount == selected ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            map_notice_t noticeKind{}; ( void )MapNotice_Last( &ws, &noticeKind ); CHECK( noticeKind == map_notice_t::ERROR );
            { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
            audit.calls = 0; audit.failOn = 0;
            s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
            s.Mouse( QEvent::MouseMove, s.At( 128, 0 ), Qt::LeftButton );
            s.Mouse( QEvent::MouseButtonRelease, s.At( 128, 64 ) + QPointF( 2, -1 ), Qt::NoButton );
            REQUIRE( s.Steps() == steps + 1 );
            edge_workflow_mesh_t welded( *ws.pDocument, mesh ); CHECK( welded.value.vertices.nCount == 3 );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "A direct vertex drag cannot overwrite a newer component selection or document edit", "[map][gui][tool-gestures][vertex-workflow][drag-weld][gesture-context]" )
{
    for ( bool documentEdit : { false, true } ) {
        CAPTURE( documentEdit );
        session_t s; const u64 mesh = EdgeWorkflowQuad( s ); auto &ws = s.workspace;
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
        const auto moving = VertexAt( ws, mesh, { 128, -64, 0 } );
        const auto different = VertexAt( ws, mesh, { 0, 64, 0 } );
        s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, s.At( 160, -64 ), Qt::LeftButton );
        REQUIRE( VertexSelectedIn( ws, moving ) );
        if ( documentEdit ) {
            REQUIRE( MapToolGestures_TransformComponents( &ws, map_component_transform_t::TRANSLATE,
                { 0, 16, 0 }, map_component_pivot_t::CENTER ) );
        } else {
            REQUIRE( MapWorkspace_SetMeshComponents( &ws, mesh, { &different, 1 }, {} ) );
        }
        edge_workflow_mesh_t beforeRelease( *ws.pDocument, mesh ); const usize steps = s.Steps();
        REQUIRE( ws.meshSelection.vertices.nCount == 1 ); const auto selected = ws.meshSelection.vertices.pData[0];
        s.Mouse( QEvent::MouseButtonRelease, s.At( 192, -64 ), Qt::NoButton );
        CHECK( s.Steps() == steps ); REQUIRE( ws.meshSelection.vertices.nCount == 1 );
        CHECK( ws.meshSelection.vertices.pData[0].value == selected.value );
        edge_workflow_mesh_t afterRelease( *ws.pDocument, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &beforeRelease.value, &afterRelease.value ) );
    }
}

TEST_CASE( "Another viewport cannot steer or release an active direct vertex drag", "[map][gui][tool-gestures][vertex-workflow][drag-weld][gesture-context]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s ); auto &ws = s.workspace;
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
    std::unique_ptr<QWidget> other( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
    other->resize( 800, 600 ); other->show(); QCoreApplication::processEvents();
    const auto moving = VertexAt( ws, mesh, { 128, -64, 0 } );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
    s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 160, -64 ), Qt::LeftButton );
    REQUIRE( VertexSelectedIn( ws, moving ) );
    const QPointF foreign = MapOrthoView_WorldToView( other.get(), { 256, 128 } );
    const auto sendOther = [&]( QEvent::Type type, Qt::MouseButtons held ) {
        QMouseEvent event( type, foreign, other->mapToGlobal( foreign ),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier );
        QCoreApplication::sendEvent( other.get(), &event );
    };
    sendOther( QEvent::MouseMove, Qt::LeftButton );
    sendOther( QEvent::MouseButtonRelease, Qt::NoButton );
    CHECK( s.Steps() == steps ); REQUIRE( ws.meshSelection.vertices.nCount == 1 ); CHECK( VertexSelectedIn( ws, moving ) );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    // Escape can cancel the capture; the original pane's later release must
    // not commit the last valid sample either.
    s.Key( Qt::Key_Escape ); s.Mouse( QEvent::MouseButtonRelease, s.At( 192, -64 ), Qt::NoButton );
    CHECK( s.Steps() == steps );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    s.Tool( "map.tool.select" ); s.Drag( 128, -64, 160, -64 );
    CHECK( s.Steps() == steps + 1 ); CheckPosition( Position( ws, mesh, moving ), { 160, -64, 0 } );
}

TEST_CASE( "A pending vertex press cannot restore a selection superseded before dragging starts", "[map][gui][tool-gestures][vertex-workflow][drag-weld][gesture-context][pending-press]" )
{
    for ( bool documentEdit : { false, true } ) {
        CAPTURE( documentEdit );
        session_t s; const u64 mesh = EdgeWorkflowQuad( s ); auto &ws = s.workspace;
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); s.Tool( "map.tool.select" );
        const auto moving = VertexAt( ws, mesh, { 128, -64, 0 } );
        const auto different = VertexAt( ws, mesh, { 0, 64, 0 } );
        s.Click( 128, -64 );
        REQUIRE( ws.meshSelection.vertices.nCount == 1 ); REQUIRE( VertexSelectedIn( ws, moving ) );
        s.Mouse( QEvent::MouseButtonPress, s.At( 128, -64 ), Qt::LeftButton );
        // The view's normal click has completed, but no >3-pixel mouse move
        // has turned the pending press into an active geometry gesture yet.
        if ( documentEdit ) {
            REQUIRE( MapToolGestures_TransformComponents( &ws, map_component_transform_t::TRANSLATE,
                { 0, 16, 0 }, map_component_pivot_t::CENTER ) );
        } else {
            REQUIRE( MapWorkspace_SetMeshComponents( &ws, mesh, { &different, 1 }, {} ) );
        }
        edge_workflow_mesh_t newest( *ws.pDocument, mesh ); const usize steps = s.Steps();
        REQUIRE( ws.meshSelection.vertices.nCount == 1 ); const auto selected = ws.meshSelection.vertices.pData[0];
        s.Mouse( QEvent::MouseMove, s.At( 160, -64 ), Qt::LeftButton );
        s.Mouse( QEvent::MouseButtonRelease, s.At( 192, -64 ), Qt::NoButton );
        CHECK( s.Steps() == steps ); REQUIRE( ws.meshSelection.vertices.nCount == 1 );
        CHECK( ws.meshSelection.vertices.pData[0].value == selected.value );
        edge_workflow_mesh_t afterRelease( *ws.pDocument, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &newest.value, &afterRelease.value ) );
    }
}

TEST_CASE( "Transform tools pick an edge before transforming it in every orthographic pane", "[map][gui][tool-gestures][edge-routing]" )
{
    // Exercise the toolbar order that the old preselected-component fixtures
    // skipped: choose Translate/Rotate/Scale, choose Edges, then click a side.
    for ( const auto pane : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const char *tool : { "map.tool.translate", "map.tool.rotate", "map.tool.scale" } ) {
            CAPTURE( static_cast<int>( pane ), tool );
            session_t s;
            s.top.reset( MapOrthoView_Create( nullptr, &s.workspace, pane ) );
            s.top->resize( 800, 600 ); s.top->show();
            s.Frame( { -512, -512, -512 }, { 512, 512, 512 } );
            const auto world = [pane]( f64 u, f64 v ) {
                return pane == map_ortho_axes_t::TOP ? math::vec3d_t{ u, v, 0 } :
                    pane == map_ortho_axes_t::FRONT ? math::vec3d_t{ 0, u, v } : math::vec3d_t{ u, 0, v };
            };
            map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD;
            quad.axis = pane == map_ortho_axes_t::TOP ? 2u : pane == map_ortho_axes_t::FRONT ? 0u : 1u;
            quad.bounds = { world( -256, -128 ), world( 256, 128 ) };
            REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) );
            const u64 mesh = s.Selected();
            edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
            const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, world( 256, -128 ) ), VertexAt( s.workspace, mesh, world( 256, 128 ) ) );
            s.Tool( tool ); s.Tool( "map.select_mode.edges" );
            const usize steps = s.Steps();
            REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
            s.Click( 256, 0 );
            REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
            REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
            CHECK( s.Steps() == steps );
            const f64 zoom = MapOrthoView_Zoom( s.top.get() );
            constexpr f64 angle = 15.0 * 3.14159265358979323846 / 180.0;
            if ( s.workspace.tool == map_tool_t::TRANSLATE ) { s.Drag( 256 + 40 / zoom, 0, 288 + 40 / zoom, 0 ); }
            else if ( s.workspace.tool == map_tool_t::ROTATE ) {
                const f64 radius = 60 / zoom;
                s.Drag( 256 + radius, 0, 256 + radius * std::cos( angle ), radius * std::sin( angle ) );
            } else { s.Drag( 256 + 60 / zoom, 0, 256 + 120 / zoom, 0 ); }
            CHECK( s.Steps() == steps + 1 );
            for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
                const auto &vertex = before.value.vertices.pData[i];
                auto expected = vertex.position;
                if ( vertex.sourceId.value == edge.a.value || vertex.sourceId.value == edge.b.value ) {
                    const f64 v = pane == map_ortho_axes_t::TOP ? vertex.position.y : vertex.position.z;
                    expected = s.workspace.tool == map_tool_t::TRANSLATE ? world( 288, v ) :
                        s.workspace.tool == map_tool_t::ROTATE ? world( 256 - v * std::sin( angle ), v * std::cos( angle ) ) : world( 256, v * 2 );
                }
                CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
            }
            CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
            edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
            REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
            REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
        }
    }
}

TEST_CASE( "Translate picks another edge without leaving the tool and Shift grows the newly picked side", "[map][gui][tool-gestures][edge-routing]" )
{
    session_t s;
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { -256, -128, 0 }, { 256, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) ); const u64 mesh = s.Selected();
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    const auto right = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 256, -128, 0 } ), VertexAt( s.workspace, mesh, { 256, 128, 0 } ) );
    const auto left = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { -256, -128, 0 } ), VertexAt( s.workspace, mesh, { -256, 128, 0 } ) );
    s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.translate" );
    s.Click( 256, 0 ); REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, right ) );
    s.Click( -256, 0 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, left ) );
    CHECK_FALSE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, right ) );
    const usize steps = s.Steps(); const f64 grab = -256 + 40 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( grab, 0, grab - 64, 0, Qt::ShiftModifier );
    CHECK( s.Steps() == steps + 1 ); CHECK( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, original );
    const auto outer = OnlySelectedEdge( s );
    CHECK( Position( s.workspace, mesh, outer.a ).x == Catch::Approx( -320 ) );
    CHECK( Position( s.workspace, mesh, outer.b ).x == Catch::Approx( -320 ) );
}

TEST_CASE( "Translate converts a newly created brush when its edge is clicked", "[map][gui][tool-gestures][edge-routing]" )
{
    session_t s;
    const u64 box = AddBox( s.workspace, { -256, -128, 0 }, { 256, 128, 128 } );
    REQUIRE( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { box } ) );
    s.Tool( "map.tool.translate" ); s.Tool( "map.select_mode.edges" );
    const usize beforePick = s.Steps();
    s.Click( 256, 0 );
    REQUIRE( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { box } ) );
    REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    CHECK( s.Steps() == beforePick + 1 ); // The brush-to-mesh conversion is undoable.
    const auto edge = OnlySelectedEdge( s );
    edge_workflow_mesh_t before( *s.workspace.pDocument, box );
    const f64 grab = 256 + 40 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( grab, 0, grab + 32, 0 );
    CHECK( s.Steps() == beforePick + 2 );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i]; auto expected = vertex.position;
        if ( vertex.sourceId.value == edge.a.value || vertex.sourceId.value == edge.b.value ) { expected.x += 32; }
        CheckPosition( Position( s.workspace, box, vertex.sourceId ), expected );
    }
}

TEST_CASE( "A component press that converts a brush continues into the same pointer drag", "[map][gui][tool-gestures][edge-routing][view-drag-cancel][brush-press-drag]" )
{
    for ( const bool perspective : { false, true } ) {
        for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES } ) {
            CAPTURE( perspective, static_cast<int>( mode ) );
            session_t s;
            const u64 object = AddBox( s.workspace, { -256, -128, 0 }, { 256, 128, 128 } );
            REQUIRE( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { object } ) );
            std::unique_ptr<QWidget> camera;
            if ( perspective ) {
                camera.reset( MapCameraView_Create( nullptr, &s.workspace ) );
                camera->resize( 1000, 760 ); camera->show(); QCoreApplication::processEvents();
                MapCameraView_LookAt( camera.get(), { 1024, 0, 768 }, { 0, 0, 64 } );
            }
            QWidget *view = perspective ? camera.get() : s.top.get();
            const auto at = [&]( math::vec3d_t world ) {
                if ( !perspective ) { return s.At( world.x, world.y ); }
                QPointF screen; REQUIRE( MapCameraView_WorldToView( view, world, &screen ) ); return screen;
            };
            const auto send = [&]( QEvent::Type type, math::vec3d_t world ) {
                const QPointF point = at( world );
                QMouseEvent event( type, point, view->mapToGlobal( point ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
                QCoreApplication::sendEvent( view, &event );
            };
            // Direct vertex pulls belong to Select; Translate uses its gizmo
            // for vertices and additionally supports direct edge pulls.
            s.Tool( mode == map_element_mode_t::VERTICES ? "map.tool.select" : "map.tool.translate" );
            MapWorkspace_SetElementMode( &s.workspace, mode );
            const usize steps = s.Steps();
            const math::vec3d_t from{ 256, mode == map_element_mode_t::VERTICES ? -128.0 : 0.0, 128 };
            // The press itself converts and selects. There is no intervening
            // release/repress: its document notification must retain this pickup.
            send( QEvent::MouseButtonPress, from );
            REQUIRE( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { object } ) );
            REQUIRE( s.Steps() == steps + 1 );
            edge_workflow_mesh_t converted( *s.workspace.pDocument, object );
            std::vector<geo::geometry_source_id_t> moved;
            if ( mode == map_element_mode_t::EDGES ) {
                const auto edge = OnlySelectedEdge( s ); moved = { edge.a, edge.b };
            } else {
                REQUIRE( s.workspace.meshSelection.vertices.nCount == ( perspective ? 1u : 2u ) );
                moved.assign( s.workspace.meshSelection.vertices.pData,
                    s.workspace.meshSelection.vertices.pData + s.workspace.meshSelection.vertices.nCount );
            }
            send( QEvent::MouseMove, { from.x + 16, from.y, from.z } );
            send( QEvent::MouseMove, { from.x + 32, from.y, from.z } );
            send( QEvent::MouseButtonRelease, { from.x + 32, from.y, from.z } );
            REQUIRE( s.Steps() == steps + 2 );
            for ( usize i = 0; i < converted.value.vertices.nCount; ++i ) {
                const auto &vertex = converted.value.vertices.pData[i];
                auto expected = vertex.position;
                if ( std::any_of( moved.begin(), moved.end(), [&]( auto id ) { return id.value == vertex.sourceId.value; } ) ) { expected.x += 32; }
                CheckPosition( Position( s.workspace, object, vertex.sourceId ), expected );
            }
            edge_workflow_mesh_t transformed( *s.workspace.pDocument, object );
            REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, object ); CHECK( geo::MeshSourceDescription_Equal( &converted.value, &restored.value ) ); }
            REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
            CHECK( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { object } ) != nullptr );
            REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
            REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, object ); CHECK( geo::MeshSourceDescription_Equal( &transformed.value, &restored.value ) ); }
        }
    }
}

TEST_CASE( "Perspective Translate picks a real edge before moving only its endpoints", "[map][gui][tool-gestures][edge-routing]" )
{
    session_t s;
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { -256, -128, 0 }, { 256, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) ); const u64 mesh = s.Selected();
    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
    const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 256, -128, 0 } ), VertexAt( s.workspace, mesh, { 256, 128, 0 } ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 1000, 760 ); camera->show(); MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
    const auto at = [&]( math::vec3d_t world ) { QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), world, &screen ) ); return screen; };
    const auto send = [&]( QEvent::Type type, QPointF where, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        QMouseEvent event( type, where, camera->mapToGlobal( where ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    s.Tool( "map.tool.translate" ); s.Tool( "map.select_mode.edges" );
    const usize steps = s.Steps(); const auto pick = at( { 256, 0, 0 } );
    send( QEvent::MouseButtonPress, pick ); send( QEvent::MouseButtonRelease, pick );
    REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
    const auto eye = MapCameraView_Position( camera.get() );
    const f64 distance = std::sqrt( ( eye.x - 256 ) * ( eye.x - 256 ) + eye.y * eye.y + eye.z * eye.z );
    const f64 grab = 256 + std::max( 1.0, distance * 0.15 ) * 0.7;
    send( QEvent::MouseButtonPress, at( { grab, 0, 0 } ) );
    send( QEvent::MouseMove, at( { grab + 32, 0, 0 } ) );
    send( QEvent::MouseButtonRelease, at( { grab + 32, 0, 0 } ) );
    CHECK( s.Steps() == steps + 1 );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i]; auto expected = vertex.position;
        if ( vertex.sourceId.value == edge.a.value || vertex.sourceId.value == edge.b.value ) { expected.x += 32; }
        CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
    }
    CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
    // The user's wall workflow: Shift-pull the picked edge upward in the real
    // perspective viewport. The existing floor stays fixed while a wall grows.
    edge_workflow_mesh_t floor( *s.workspace.pDocument, mesh );
    const f64 wallDistance = std::sqrt( ( eye.x - 288 ) * ( eye.x - 288 ) + eye.y * eye.y + eye.z * eye.z );
    const f64 zGrab = std::max( 1.0, wallDistance * 0.15 ) * 0.7;
    send( QEvent::MouseButtonPress, at( { 288, 0, zGrab } ), Qt::ShiftModifier );
    send( QEvent::MouseMove, at( { 288, 0, zGrab + 64 } ), Qt::ShiftModifier );
    send( QEvent::MouseButtonRelease, at( { 288, 0, zGrab + 64 } ) );
    REQUIRE( s.Steps() == steps + 2 );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, floor );
    const auto outer = OnlySelectedEdge( s );
    CHECK( outer.a.value != edge.a.value ); CHECK( outer.a.value != edge.b.value );
    CHECK( outer.b.value != edge.a.value ); CHECK( outer.b.value != edge.b.value );
    const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
    CHECK( a.x == Catch::Approx( 288 ) ); CHECK( b.x == Catch::Approx( 288 ) );
    CHECK( a.z == Catch::Approx( 64 ) ); CHECK( b.z == Catch::Approx( 64 ) );
    CHECK( std::min( a.y, b.y ) == Catch::Approx( -128 ) ); CHECK( std::max( a.y, b.y ) == Catch::Approx( 128 ) );
    if ( const QByteArray shot = qgetenv( "MASON_EDGE_ROUTING_SHOT" ); !shot.isEmpty() ) {
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
        MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
        REQUIRE( camera->grab().save( QString::fromUtf8( shot ) ) );
    }
}

TEST_CASE( "Choosing Edges after drawing a Quad commits the stage and enters component selection", "[map][gui][tool-gestures][edge-routing][block]" )
{
    session_t s;
    const auto *shape = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.new_brush_shape" ) ); REQUIRE( shape );
    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( "quad" );
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *shape, value ) == settings_registry_status_t::OK );
    s.Tool( "map.tool.block" );
    const usize steps = s.Steps(), objects = s.workspace.wire.objects.nCount;
    s.Drag( -256, -128, 256, 128 );
    REQUIRE( MapWorkspace_HasBlockPreview( &s.workspace ) );
    REQUIRE( s.workspace.editPreview.primitive.kind == map_primitive_kind_t::QUAD );
    REQUIRE( s.workspace.wire.objects.nCount == objects );
    s.Tool( "map.select_mode.edges" );
    REQUIRE( s.workspace.tool == map_tool_t::SELECT );
    REQUIRE( s.workspace.elementMode == map_element_mode_t::EDGES );
    REQUIRE_FALSE( MapWorkspace_HasBlockPreview( &s.workspace ) );
    REQUIRE( s.workspace.wire.objects.nCount == objects + 1 );
    REQUIRE( s.Steps() == steps + 1 );
    const u64 mesh = s.Selected(); REQUIRE( mesh != 0 ); CHECK( MeshFaces( s.workspace, mesh ) == 1 );
    s.Click( 256, 0 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    const auto edge = OnlySelectedEdge( s );
    CHECK( Position( s.workspace, mesh, edge.a ).x == Catch::Approx( 256 ) );
    CHECK( Position( s.workspace, mesh, edge.b ).x == Catch::Approx( 256 ) );
    s.Tool( "map.tool.translate" );
    const f64 grab = 256 + 40 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( grab, 0, grab + 64, 0, Qt::ShiftModifier );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 ); CHECK( s.Steps() == steps + 2 );
}

TEST_CASE( "A failed staged block commit keeps the construction when Edges is chosen", "[map][gui][tool-gestures][edge-routing][block][allocation]" )
{
    for ( const bool allocationFailure : { false, true } ) {
        CAPTURE( allocationFailure );
        session_t s; s.Tool( "map.tool.block" );
        s.Drag( -256, -128, 256, 128 );
        REQUIRE( MapWorkspace_HasBlockPreview( &s.workspace ) );
        const auto *document = s.workspace.pDocument; const u64 revision = document->geometry.revision;
        const auto mode = s.workspace.elementMode; const usize steps = s.Steps();
        const auto *previewPoints = s.workspace.editPreviewWire.points.pData;
        const auto bounds = s.workspace.editPreview.bounds;
        const u64 selectionRevision = s.workspace.selection.revision;
        command_result_t result{};
        if ( allocationFailure ) {
            edge_gui_allocation_t audit{}; audit.failOn = 1;
            const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
            { edge_gui_allocator_scope_t scope( s.gui, &allocator );
                result = EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.select_mode.edges" ), {} ); }
            CHECK( audit.calls >= 1 ); CHECK( audit.live == 0 );
        } else {
            // The failed preview is still a staged construction that the user
            // can repair. A category click must not erase it or claim success.
            s.workspace.editPreview.status = map_status_t::INVALID_ARGUMENT;
            result = EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.select_mode.edges" ), {} );
        }
        CHECK( result != command_result_t::OK );
        CHECK( s.workspace.tool == map_tool_t::BLOCK ); CHECK( s.workspace.elementMode == mode );
        CHECK( s.workspace.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( s.Steps() == steps );
        REQUIRE( MapWorkspace_HasBlockPreview( &s.workspace ) );
        CHECK( s.workspace.editPreviewWire.points.pData == previewPoints );
        CheckPosition( s.workspace.editPreview.bounds.box.minimum, bounds.box.minimum );
        CheckPosition( s.workspace.editPreview.bounds.box.maximum, bounds.box.maximum );
        CHECK( s.workspace.selection.revision == selectionRevision );
        s.workspace.editPreview.status = map_status_t::OK;
        s.Tool( "map.select_mode.edges" );
        CHECK( s.workspace.tool == map_tool_t::SELECT ); CHECK( s.workspace.elementMode == map_element_mode_t::EDGES );
        CHECK_FALSE( MapWorkspace_HasBlockPreview( &s.workspace ) ); CHECK( s.Steps() == steps + 1 );
    }
}

TEST_CASE( "Scale ring leaves nearby edges inside its circle available for selection", "[map][gui][tool-gestures][edge-routing][scale-pick]" )
{
    session_t s;
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { -32, -128, 0 }, { 32, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) ); const u64 mesh = s.Selected();
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    const auto right = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 32, -128, 0 } ), VertexAt( s.workspace, mesh, { 32, 128, 0 } ) );
    const auto left = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { -32, -128, 0 } ), VertexAt( s.workspace, mesh, { -32, 128, 0 } ) );
    s.Tool( "map.tool.scale" ); s.Tool( "map.select_mode.edges" );
    s.Click( 32, 0 ); REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, right ) );
    // The opposite edge lies inside the painted 60-pixel ring, away from
    // both ring and pivot. It is geometry to pick, not an invisible handle.
    const f64 separation = QLineF( s.At( 32, 0 ), s.At( -32, 0 ) ).length();
    REQUIRE( separation > 12 ); REQUIRE( separation < 48 );
    const usize steps = s.Steps();
    s.Click( -32, 0 );
    REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, left ) );
    CHECK_FALSE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, right ) );
    CHECK( s.Steps() == steps ); CheckOriginalVertices( s, mesh, original );
    const f64 ring = 60.0 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( -32 + ring, 0, -32 + ring * 2, 0 );
    CHECK( s.Steps() == steps + 1 );
    for ( usize i = 0; i < original.value.vertices.nCount; ++i ) {
        const auto &vertex = original.value.vertices.pData[i]; auto expected = vertex.position;
        if ( vertex.sourceId.value == left.a.value || vertex.sourceId.value == left.b.value ) { expected.y *= 2; }
        CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
    }
}

TEST_CASE( "Transform component picking retains add toggle and clear semantics for edges and vertices", "[map][gui][tool-gestures][edge-routing][component-pick]" )
{
    for ( const char *tool : { "map.tool.translate", "map.tool.rotate", "map.tool.scale" } ) {
        for ( const bool vertices : { false, true } ) {
            CAPTURE( tool, vertices );
            session_t s;
            map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
            quad.bounds = { { -256, -128, 0 }, { 256, 128, 0 } };
            REQUIRE( MapWorkspace_CreatePrimitive( &s.workspace, quad ) ); const u64 mesh = s.Selected();
            edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
            s.Tool( tool ); s.Tool( vertices ? "map.select_mode.vertices" : "map.select_mode.edges" );
            const auto *document = s.workspace.pDocument; const usize steps = s.Steps();
            const auto count = [&]() { return vertices ? s.workspace.meshSelection.vertices.nCount : s.workspace.meshSelection.edges.nCount; };
            const f64 v = vertices ? 128.0 : 0.0;
            s.Click( 256, v ); REQUIRE( count() == 1 );
            const auto firstVertex = vertices ? s.workspace.meshSelection.vertices.pData[0] : geo::geometry_source_id_t{};
            const auto firstEdge = vertices ? geo::mesh_edge_ref_t{} : s.workspace.meshSelection.edges.pData[0];
            s.Click( -256, -v, Qt::ShiftModifier ); REQUIRE( count() == 2 );
            s.Click( 256, v, Qt::ControlModifier ); REQUIRE( count() == 1 );
            if ( vertices ) { CHECK_FALSE( geo::MeshSelection_HasVertex( &s.workspace.meshSelection, firstVertex ) ); }
            else { CHECK_FALSE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, firstEdge ) ); }
            const bool moveVertex = vertices && s.workspace.tool == map_tool_t::TRANSLATE;
            const auto movedVertex = moveVertex ? s.workspace.meshSelection.vertices.pData[0] : geo::geometry_source_id_t{};
            if ( moveVertex ) {
                const f64 grab = -256 + 40 / MapOrthoView_Zoom( s.top.get() );
                s.Drag( grab, -128, grab + 32, -128 );
                CHECK( s.workspace.meshSelection.vertices.nCount == 1 );
                CHECK( geo::MeshSelection_HasVertex( &s.workspace.meshSelection, movedVertex ) );
            }
            s.Click( 400, 400 ); CHECK( count() == 0 );
            CHECK( s.Steps() == steps + ( moveVertex ? 1 : 0 ) );
            if ( !moveVertex ) { CHECK( s.workspace.pDocument == document ); }
            for ( usize i = 0; i < original.value.vertices.nCount; ++i ) {
                const auto &vertex = original.value.vertices.pData[i]; auto expected = vertex.position;
                if ( vertex.sourceId.value == movedVertex.value ) { expected.x += 32; }
                CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
            }
        }
    }
}

namespace
{
void EdgeDragAction( session_t &s, const char *action )
{
    // Exercise the user's actual visible Tool Properties control, including
    // its settings write, instead of configuring an unexposed internal flag.
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &s.workspace ) );
    REQUIRE( panel ); panel->resize( 420, 900 ); panel->show(); QCoreApplication::processEvents();
    auto *section = panel->findChild<QWidget *>( QStringLiteral( "MapEdgeDragSection" ) ); REQUIRE( section );
    REQUIRE( section->isVisible() );
    auto *control = section->findChild<QWidget *>( QStringLiteral( "editor.map.edge_drag_action" ) ); REQUIRE( control );
    auto *choice = control->findChild<QComboBox *>(); REQUIRE( choice );
    REQUIRE( choice->isVisible() ); REQUIRE( choice->isEnabled() );
    REQUIRE( choice->findText( QString::fromLatin1( action ) ) >= 0 );
    choice->setCurrentText( QString::fromLatin1( action ) ); QCoreApplication::processEvents();
    REQUIRE( StringView_Equals( EditorSettings_Text( &s.gui.settings, "editor.map.edge_drag_action", {} ), StringView_FromCString( action ) ) );
}
}

TEST_CASE( "The visible Extend choice grows connected faces with ordinary edge drags", "[map][gui][tool-gestures][edge-plain-drag][persistence]" )
{
    for ( const char *tool : { "map.tool.select", "map.tool.translate" } ) {
        CAPTURE( tool ); session_t s;
        const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
        s.Tool( tool ); EdgeDragAction( s, "extend" );
        edge_workflow_mesh_t original( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        for ( u32 i = 0; i < 2; ++i ) {
            const f64 x = 256 + 64 * i;
            // Off-centre on the edge, away from the Translate handles. No
            // programmatic component selection, Shift, or operation command.
            s.Drag( x, -96, x + 64, -96 );
            REQUIRE( s.Steps() == steps + i + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 + i );
            CheckOriginalVertices( s, mesh, original );
            const auto edge = OnlySelectedEdge( s );
            CHECK( Position( s.workspace, mesh, edge.a ).x == Catch::Approx( x + 64 ) );
            CHECK( Position( s.workspace, mesh, edge.b ).x == Catch::Approx( x + 64 ) );
        }
        edge_workflow_mesh_t result( *s.workspace.pDocument, mesh );
        REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
        REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &result.value, &restored.value ) ); }
        QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "plain-edge-strip.cymap" ) );
        REQUIRE( MapWorkspace_SaveAs( &s.workspace, path ).status == map_files_status_t::OK );
        session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
        edge_workflow_mesh_t saved( *loaded.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &result.value, &saved.value ) );
    }
}

TEST_CASE( "Extend is captured at press while clicks cancel and Move retain their meanings", "[map][gui][tool-gestures][edge-plain-drag]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
    CHECK( StringView_Equals( EditorSettings_Text( &s.gui.settings, "editor.map.edge_drag_action", {} ), StringView_FromCString( "move" ) ) );
    EdgeDragAction( s, "extend" );
    const usize steps = s.Steps(); edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    s.Click( 256, -96 ); s.Click( -256, -96, Qt::ShiftModifier ); REQUIRE( s.workspace.meshSelection.edges.nCount == 2 );
    s.Click( -256, -96, Qt::ControlModifier ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 ); CHECK( s.Steps() == steps );
    s.Mouse( QEvent::MouseButtonPress, s.At( 256, -96 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 320, -96 ), Qt::LeftButton ); s.Key( Qt::Key_Escape );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 320, -96 ), Qt::NoButton );
    CHECK( s.Steps() == steps );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
    s.Tool( "map.tool.select" );
    s.Mouse( QEvent::MouseButtonPress, s.At( 256, -96 ), Qt::LeftButton );
    // Change only the setting, without stealing focus from the active mouse
    // capture: the already pressed gesture keeps its chosen action.
    const auto *setting = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.edge_drag_action" ) ); REQUIRE( setting );
    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( "move" );
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *setting, value ) == settings_registry_status_t::OK );
    s.Mouse( QEvent::MouseMove, s.At( 320, -96 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 320, -96 ), Qt::NoButton );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 2 ); CheckOriginalVertices( s, mesh, original );
    const auto edge = OnlySelectedEdge( s );
    s.Drag( 320, -96, 352, -96 );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 ); CHECK( s.Steps() == steps + 2 );
    CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
    CHECK( Position( s.workspace, mesh, edge.a ).x == Catch::Approx( 352 ) );
    CHECK( Position( s.workspace, mesh, edge.b ).x == Catch::Approx( 352 ) );
}

TEST_CASE( "Visible Rotate Extend grows a connected curve around the painted custom pivot", "[map][gui][tool-gestures][edge-rotate-extend][pivot]" )
{
    session_t s;
    const auto setChoice = [&]( const char *path, const char *text ) {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
        setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };
    setChoice( "editor.map.new_brush_shape", "quad" ); setChoice( "editor.map.block_output", "mesh" );
    MapWorkspace_SetGridSize( &s.workspace, 32 ); MapWorkspace_SetAngleSnap( &s.workspace, 15 );
    s.Tool( "map.tool.block" ); s.Drag( 128, -64, 256, 64 );
    REQUIRE( MapWorkspace_HasBlockPreview( &s.workspace ) );
    s.Tool( "map.select_mode.edges" ); REQUIRE_FALSE( MapWorkspace_HasBlockPreview( &s.workspace ) );
    const u64 mesh = s.Selected(); REQUIRE( mesh != 0 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 1 );
    s.Click( 192, 64 ); const auto initialEdge = OnlySelectedEdge( s );
    const auto initialA = Position( s.workspace, mesh, initialEdge.a ), initialB = Position( s.workspace, mesh, initialEdge.b );
    CHECK( initialA.y == Catch::Approx( 64 ) ); CHECK( initialB.y == Catch::Approx( 64 ) );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    // Place the pivot through the real tool. Switching to Rotate must draw the
    // ring here immediately, before a hover or press repairs its cached state.
    s.Tool( "map.tool.pivot" ); s.Click( 0, 0 );
    math::vec3d_t pivot{}; REQUIRE( MapToolGestures_Pivot( &s.workspace, &pivot ) ); CheckPosition( pivot, {} );
    s.Tool( "map.tool.rotate" );
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() );
    const QPixmap shot = s.top->grab(); const QImage image = shot.toImage();
    const QPointF ringPoint = s.At( -radius, 0 ) * shot.devicePixelRatio();
    int bluePixels = 0;
    for ( int y = static_cast<int>( ringPoint.y() ) - 4; y <= static_cast<int>( ringPoint.y() ) + 4; ++y ) {
        for ( int x = static_cast<int>( ringPoint.x() ) - 4; x <= static_cast<int>( ringPoint.x() ) + 4; ++x ) {
            const QColor color = image.pixelColor( x, y );
            bluePixels += color.blue() > 150 && color.blue() > color.red() + 60 && color.blue() > color.green() + 30;
        }
    }
    CHECK( bluePixels > 0 );
    EdgeDragAction( s, "extend" ); // The control must also be visible in Rotate.
    const usize steps = s.Steps();
    const f64 radians = 3.14159265358979323846 / 6.0;
    const QPointF start = s.At( radius, 0 ), finish = s.At( radius * std::cos( radians ), radius * std::sin( radians ) );
    const auto drag = [&]() {
        s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton );
        s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    };
    SECTION( "Consecutive ordinary ring drags append faces and keep earlier geometry fixed" ) {
        for ( u32 step = 0; step < 2; ++step ) {
            edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
            const auto previous = OnlySelectedEdge( s );
            drag();
            REQUIRE( s.Steps() == steps + step + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 + step );
            CheckOriginalVertices( s, mesh, before );
            const auto outer = OnlySelectedEdge( s );
            CHECK( outer.a.value != previous.a.value ); CHECK( outer.a.value != previous.b.value );
            CHECK( outer.b.value != previous.a.value ); CHECK( outer.b.value != previous.b.value );
            const f64 angle = radians * ( step + 1 );
            const auto turned = [&]( math::vec3d_t point ) {
                return math::vec3d_t{ point.x * std::cos( angle ) - point.y * std::sin( angle ),
                                     point.x * std::sin( angle ) + point.y * std::cos( angle ), point.z };
            };
            const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
            const auto expectedA = turned( initialA ), expectedB = turned( initialB );
            if ( std::fabs( a.x - expectedA.x ) < 1e-6 ) { CheckPosition( a, expectedA ); CheckPosition( b, expectedB ); }
            else { CheckPosition( a, expectedB ); CheckPosition( b, expectedA ); }
            edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
            CHECK( after.value.vertices.nCount == 6 + 2 * step );
            usize connectedFaces = 0;
            for ( usize f = 0; f < after.value.faces.nCount; ++f ) {
                const auto &face = after.value.faces.pData[f];
                const auto has = [&]( u64 id ) {
                    for ( u32 c = 0; c < face.cCorners; ++c ) {
                        const auto index = after.value.corners.pData[face.iFirstCorner + c].iVertex;
                        if ( after.value.vertices.pData[index].sourceId.value == id ) { return true; }
                    }
                    return false;
                };
                connectedFaces += face.cCorners == 4 && has( previous.a.value ) && has( previous.b.value ) &&
                                  has( outer.a.value ) && has( outer.b.value );
            }
            CHECK( connectedFaces == 1 );
            REQUIRE( MapToolGestures_Pivot( &s.workspace, &pivot ) ); CheckPosition( pivot, {} );
        }
        if ( const QByteArray shotPath = qgetenv( "MASON_EDGE_PIVOT_SHOT" ); !shotPath.isEmpty() ) {
            s.Frame( { -128, -128, -128 }, { 384, 384, 128 } );
            REQUIRE( s.top->grab().save( QString::fromUtf8( shotPath ) + QStringLiteral( ".top.png" ) ) );
            std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
            camera->resize( 1000, 760 ); camera->show();
            MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
            MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
            REQUIRE( camera->grab().save( QString::fromUtf8( shotPath ) ) );
        }
        edge_workflow_mesh_t result( *s.workspace.pDocument, mesh );
        REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
        REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &result.value, &restored.value ) ); }
    }
    SECTION( "Changing or clearing the pivot cancels a captured rotation before release" ) {
        for ( const bool clear : { false, true } ) {
            CAPTURE( clear );
            s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton ); s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton );
            if ( clear ) { MapToolGestures_ClearPivot( &s.workspace ); }
            else { MapToolGestures_SetPivot( &s.workspace, { 32, 0, 0 } ); }
            s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
            CHECK( s.Steps() == steps );
            edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) );
            MapToolGestures_SetPivot( &s.workspace, {} );
        }
        drag(); REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 );
        CheckOriginalVertices( s, mesh, original );
    }
    SECTION( "Zero angle and Escape do not append faces or consume source IDs" ) {
        const auto revision = s.workspace.pDocument->geometry.revision, nextId = s.workspace.pDocument->nextId;
        const auto token = UndoRedo_StateToken( s.workspace.history.pUndo );
        s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton ); s.Mouse( QEvent::MouseButtonRelease, start, Qt::NoButton );
        CHECK( s.Steps() == steps ); CHECK( s.workspace.pDocument->geometry.revision == revision );
        s.Mouse( QEvent::MouseButtonPress, start, Qt::LeftButton ); s.Mouse( QEvent::MouseMove, finish, Qt::LeftButton );
        s.Key( Qt::Key_Escape ); s.Mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
        CHECK( s.Steps() == steps ); CHECK( s.workspace.pDocument->geometry.revision == revision );
        CHECK( s.workspace.pDocument->nextId == nextId );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
        edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) );
    }
}

TEST_CASE( "Select exposes an edge Translate arrow that respects visible Extend without Shift", "[map][gui][tool-gestures][edge-rotate-extend][select-gizmo]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
    s.Click( 256, -96 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    EdgeDragAction( s, "extend" ); REQUIRE( s.workspace.tool == map_tool_t::SELECT );
    // A modifier click on the selected edge's midpoint is still selection,
    // even though the Select gizmo's centre is painted on that same pixel.
    s.Click( 256, 0, Qt::ControlModifier ); REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
    s.Click( 256, 0, Qt::ShiftModifier ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
    const f64 grab = OnXHandle( s, { 256, 0, 0 } ); s.Drag( grab, 0, grab + 64, 0 );
    REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, original );
    const auto outer = OnlySelectedEdge( s );
    CHECK( Position( s.workspace, mesh, outer.a ).x == Catch::Approx( 320 ) );
    CHECK( Position( s.workspace, mesh, outer.b ).x == Catch::Approx( 320 ) );
    // The Y shaft lies on this long edge. Pulling the visible edge sideways
    // must remain a free edge drag, rather than become a zero-distance Y drag.
    const f64 shaftY = 40.0 / MapOrthoView_Zoom( s.top.get() );
    s.Drag( 320, shaftY, 384, shaftY );
    REQUIRE( s.Steps() == steps + 2 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 3 );
    const auto next = OnlySelectedEdge( s );
    CHECK( Position( s.workspace, mesh, next.a ).x == Catch::Approx( 384 ) );
    CHECK( Position( s.workspace, mesh, next.b ).x == Catch::Approx( 384 ) );
    CheckOriginalVertices( s, mesh, original );
}

TEST_CASE( "Perspective Extend grows a wall without Shift using direct drags or the Z arrow", "[map][gui][tool-gestures][edge-plain-drag][perspective]" )
{
    for ( bool gizmo : { false, true } ) {
        CAPTURE( gizmo ); session_t s;
        const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
        s.Tool( gizmo ? "map.tool.translate" : "map.tool.select" ); EdgeDragAction( s, "extend" );
        edge_workflow_mesh_t original( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
        camera->resize( 1000, 760 ); camera->show(); MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
        const auto at = [&]( math::vec3d_t world ) { QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), world, &screen ) ); return screen; };
        const auto send = [&]( QEvent::Type type, QPointF where, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
            QMouseEvent event( type, where, camera->mapToGlobal( where ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers );
            QCoreApplication::sendEvent( camera.get(), &event );
        };
        for ( u32 i = 0; i < 2; ++i ) {
            const f64 z = i * 64.0;
            if ( gizmo ) {
                // Pick a real edge, then take the visible Z arrow. The selected
                // Extend action makes an ordinary axis drag create a wall.
                const auto pick = at( { 256, -96, z } );
                send( QEvent::MouseButtonPress, pick ); send( QEvent::MouseButtonRelease, pick );
                REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
                const auto eye = MapCameraView_Position( camera.get() );
                const f64 distance = std::sqrt( ( eye.x - 256 ) * ( eye.x - 256 ) + eye.y * eye.y + ( eye.z - z ) * ( eye.z - z ) );
                const f64 handle = std::clamp( distance * 0.12, 8.0, 4096.0 ) * 0.6;
                send( QEvent::MouseButtonPress, at( { 256, 0, z + handle } ) );
                send( QEvent::MouseMove, at( { 256, 0, z + handle + 64 } ) );
                send( QEvent::MouseButtonRelease, at( { 256, 0, z + handle + 64 } ) );
            } else {
                const auto grab = at( { 256, -96, z } );
                send( QEvent::MouseButtonPress, grab ); // No Shift or Alt on press.
                send( QEvent::MouseMove, at( { 256, -96, z + 64 } ), Qt::AltModifier );
                send( QEvent::MouseButtonRelease, at( { 256, -96, z + 64 } ), Qt::AltModifier );
            }
            REQUIRE( s.Steps() == steps + i + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 + i );
            CheckOriginalVertices( s, mesh, original );
            const auto edge = OnlySelectedEdge( s );
            CHECK( Position( s.workspace, mesh, edge.a ).z == Catch::Approx( z + 64 ) );
            CHECK( Position( s.workspace, mesh, edge.b ).z == Catch::Approx( z + 64 ) );
        }
        if ( gizmo ) {
            if ( const QByteArray shot = qgetenv( "MASON_EDGE_PLAIN_DRAG_SHOT" ); !shot.isEmpty() ) {
                MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
                MapWorkspace_Frame( &s.workspace, CY_TRUE ); QCoreApplication::processEvents();
                REQUIRE( camera->grab().save( QString::fromUtf8( shot ) ) );
            }
        }
    }
}

TEST_CASE( "Invalid plain edge extension preserves the authored mesh and undo state", "[map][gui][tool-gestures][edge-plain-drag][failure]" )
{
    session_t s; const u64 box = AddBox( s.workspace, { -256, -128, 0 }, { 256, 128, 128 } );
    s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" ); EdgeDragAction( s, "extend" );
    s.Click( 256, -96 ); // Auto-conversion is a distinct existing undo step.
    REQUIRE( geo::GeometryDocument_FindMesh( &s.workspace.pDocument->geometry, { box } ) );
    edge_workflow_mesh_t before( *s.workspace.pDocument, box ); const usize steps = s.Steps();
    const auto token = UndoRedo_StateToken( s.workspace.history.pUndo ); const auto selected = OnlySelectedEdge( s );
    s.Drag( 256, -96, 320, -96 );
    CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
    edge_workflow_mesh_t after( *s.workspace.pDocument, box ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, selected ) );
    CHECK( MapNotice_Last( &s.workspace ).contains( QStringLiteral( "open boundary" ) ) );
}

TEST_CASE( "Plain edge extension allocation failures publish no geometry or history", "[map][gui][tool-gestures][edge-plain-drag][allocation]" )
{
    const auto prepare = []( session_t &s ) {
        const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } ); EdgeDragAction( s, "extend" );
        s.Mouse( QEvent::MouseButtonPress, s.At( 256, -96 ), Qt::LeftButton );
        s.Mouse( QEvent::MouseMove, s.At( 320, -96 ), Qt::LeftButton ); return mesh;
    };
    const auto release = []( session_t &s ) { s.Mouse( QEvent::MouseButtonRelease, s.At( 320, -96 ), Qt::NoButton ); };
    edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit }; usize calls = 0;
    { session_t s; prepare( s ); { edge_gui_allocator_scope_t scope( s.gui, &allocator ); release( s ); } calls = audit.calls; REQUIRE( calls >= 3 ); }
    REQUIRE( audit.live == 0 );
    for ( usize fail : { usize{ 1 }, usize{ 2 }, calls } ) {
        CAPTURE( fail ); session_t s; const u64 mesh = prepare( s ); edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
        const auto *document = s.workspace.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId;
        const auto edge = OnlySelectedEdge( s ); const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( s.workspace.history.pUndo );
        audit.calls = 0; audit.failOn = fail;
        { edge_gui_allocator_scope_t scope( s.gui, &allocator ); release( s ); }
        REQUIRE( audit.calls >= fail ); CHECK( audit.live == 0 ); CHECK( s.workspace.pDocument == document );
        CHECK( document->geometry.revision == revision ); CHECK( document->nextId == nextId ); CHECK( s.Steps() == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
        CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
        { edge_workflow_mesh_t unchanged( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
        audit.calls = 0; audit.failOn = 0;
        s.Drag( 256, -96, 320, -96 ); REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 );
    }
}

TEST_CASE( "Translate direct Extend displays its new outer edge before committing", "[map][gui][tool-gestures][edge-plain-drag][preview]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
    s.Tool( "map.tool.translate" ); EdgeDragAction( s, "extend" ); s.Click( 256, -96 );
    const auto yellowAtOuterEdge = [&]( const QPixmap &shot ) {
        const QImage image = shot.toImage();
        const QPointF at = s.At( 320, 64 ) * shot.devicePixelRatio();
        int count = 0;
        for ( int y = static_cast<int>( at.y() ) - 3; y <= static_cast<int>( at.y() ) + 3; ++y ) {
            for ( int x = static_cast<int>( at.x() ) - 3; x <= static_cast<int>( at.x() ) + 3; ++x ) {
                const QColor pixel = image.pixelColor( x, y );
                count += pixel.red() > 170 && pixel.green() > 120 && pixel.blue() < 120;
            }
        }
        return count;
    };
    CHECK( yellowAtOuterEdge( s.top->grab() ) == 0 );
    const usize steps = s.Steps();
    s.Mouse( QEvent::MouseButtonPress, s.At( 256, -96 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 320, -96 ), Qt::LeftButton ); QCoreApplication::processEvents();
    CHECK( MeshFaces( s.workspace, mesh ) == 1 ); CHECK( s.Steps() == steps );
    CHECK( yellowAtOuterEdge( s.top->grab() ) > 0 );
    s.Mouse( QEvent::MouseButtonRelease, s.At( 320, -96 ), Qt::NoButton );
    CHECK( MeshFaces( s.workspace, mesh ) == 2 ); CHECK( s.Steps() == steps + 1 );
}


TEST_CASE( "Rotate Extend sweeps an open doorway profile while sharing its curved outer rim", "[map][gui][tool-gestures][edge-rotate-extend][profile][modelling-repeat][curved-corridor-gate]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s );
    // Four joined surfaces, open at their front and back. The varying-height
    // front boundary is the profile to sweep, not a closed brush silhouette.
    const std::vector<math::vec3d_t> profile{ { 128, 0, 0 }, { 128, 0, 192 }, { 192, 0, 256 }, { 256, 0, 192 }, { 256, 0, 0 } };
    struct fixture_t { u64 mesh; std::vector<math::vec3d_t> points; std::vector<std::vector<u32>> faces; } fixture{ mesh, profile, {} };
    for ( auto point : profile ) { point.y = -32; fixture.points.push_back( point ); }
    for ( u32 i = 0; i < 4; ++i ) { fixture.faces.push_back( { i, i + 1, i + 6, i + 5 } ); }
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Create test doorway profile", []( void *context, map_document_t *document, vector_t<u64> *selection ) {
        const auto &value = *static_cast<fixture_t *>( context );
        if ( !Vector_PushBack( selection, value.mesh ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapCreate_ReplaceMeshPolygons( document, value.mesh, value.points, value.faces, StringView_FromCString( "materials/dev/dev_grid.cymat" ), 128.0 );
    }, &fixture ) );
    REQUIRE( MeshFaces( s.workspace, mesh ) == 4 );
    // Authored placement and nondefault material/UVs must survive the complete
    // modelling workflow, not just the isolated topology operation.
    u64 owner = 0;
    REQUIRE( MapDocument_AddLayer( s.workspace.pDocument, StringView_FromCString( "corridors" ), StringView_FromCString( "Corridors" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( s.workspace.pDocument, mesh, StringView_FromCString( "corridors" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( s.workspace.pDocument, StringView_FromCString( "corridors" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( s.workspace.pDocument, mesh, owner ) == map_status_t::OK );
    std::vector<map_surface_ref_t> surfaces;
    for ( usize i = 0; i < s.workspace.wire.faces.nCount; ++i ) {
        const auto &face = s.workspace.wire.faces.pData[i];
        if ( face.id == mesh ) { surfaces.push_back( { mesh, face.faceId } ); }
    }
    REQUIRE( surfaces.size() == 4 );
    REQUIRE( MapTextureEdit_SetMaterial( s.workspace.pDocument, { surfaces.data(), surfaces.size() }, StringView_FromCString( "materials/tests/curved_corridor.cymat" ), nullptr ) == map_status_t::OK );
    REQUIRE( MapTextureEdit_SetRepeat( s.workspace.pDocument, { surfaces.data(), surfaces.size() }, { 96, 192 } ) == map_status_t::OK );
    std::vector<map_uv_face_t> originalSurfaces( surfaces.size() );
    for ( usize i = 0; i < surfaces.size(); ++i ) {
        REQUIRE( MapTextureEdit_Describe( s.workspace.pDocument, surfaces[i], &originalSurfaces[i] ) == map_status_t::OK );
    }
    MapWorkspace_DocumentChanged( &s.workspace );
    const auto checkMetadata = [&]( const map_document_t &document ) {
        const map_geometry_record_t *placement = nullptr;
        for ( usize i = 0; i < document.geometryRecords.nCount; ++i ) {
            if ( document.geometryRecords.pData[i].id == mesh ) { placement = &document.geometryRecords.pData[i]; }
        }
        REQUIRE( placement ); CHECK( placement->owner == owner ); REQUIRE( placement->iLayer < document.layers.nCount );
        CHECK( std::string( document.layers.pData[placement->iLayer].id ) == "corridors" );
        for ( usize i = 0; i < surfaces.size(); ++i ) {
            map_uv_face_t actual{}; REQUIRE( MapTextureEdit_Describe( &document, surfaces[i], &actual ) == map_status_t::OK );
            const auto &before = originalSurfaces[i]; CHECK( actual.material == before.material );
            REQUIRE( actual.corners.size() == before.corners.size() ); REQUIRE( actual.uvs.size() == before.uvs.size() );
            for ( usize j = 0; j < before.corners.size(); ++j ) {
                CheckPosition( actual.corners[j], before.corners[j] );
                CHECK( actual.uvs[j].x == Catch::Approx( before.uvs[j].x ).margin( 1e-9 ) );
                CHECK( actual.uvs[j].y == Catch::Approx( before.uvs[j].y ).margin( 1e-9 ) );
            }
        }
    };
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &s.workspace ) );
    camera->resize( 1000, 760 ); camera->show(); MapCameraView_LookAt( camera.get(), { 192, 600, 160 }, { 192, 0, 128 } );
    QCoreApplication::processEvents();
    for ( usize i = 0; i + 1 < profile.size(); ++i ) {
        const auto a = profile[i], b = profile[i + 1]; QPointF where;
        REQUIRE( MapCameraView_WorldToView( camera.get(), { ( a.x + b.x ) * 0.5, 0, ( a.z + b.z ) * 0.5 }, &where ) );
        const auto modifiers = i == 0 ? Qt::NoModifier : Qt::ShiftModifier;
        QMouseEvent press( QEvent::MouseButtonPress, where, camera->mapToGlobal( where ), Qt::LeftButton, Qt::LeftButton, modifiers );
        QCoreApplication::sendEvent( camera.get(), &press );
        QMouseEvent release( QEvent::MouseButtonRelease, where, camera->mapToGlobal( where ), Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::sendEvent( camera.get(), &release );
        REQUIRE( s.workspace.meshSelection.edges.nCount == i + 1 );
    }
    s.Tool( "map.tool.pivot" ); s.Click( 0, -64 );
    math::vec3d_t pivot{}; REQUIRE( MapToolGestures_Pivot( &s.workspace, &pivot ) );
    CHECK( pivot.x == 0 ); CHECK( pivot.y == -64 );
    s.Tool( "map.tool.rotate" ); EdgeDragAction( s, "extend" ); MapWorkspace_SetAngleSnap( &s.workspace, 15 );
    const usize steps = s.Steps(); edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() ), radians = 3.14159265358979323846 / 12.0;
    bool repeatRims = false, finishCorridor = false;
    SECTION( "Consecutive visible ring drags" ) {}
    SECTION( "Build a curved corridor with a straight exit, bridged floor and one capped end" ) { finishCorridor = true; }
    SECTION( "Repeat the first ring drag on each resulting profile, continuing after Undo and Redo" ) { repeatRims = true; }
    for ( u32 step = 0; step < 6; ++step ) {
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
        if ( repeatRims && step > 0 ) {
            if ( step == 3 ) {
                const std::vector<geo::mesh_edge_ref_t> rim( s.workspace.meshSelection.edges.pData,
                    s.workspace.meshSelection.edges.pData + s.workspace.meshSelection.edges.nCount );
                REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
                REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
                edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh );
                CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) );
                REQUIRE( s.workspace.meshSelection.edges.nCount == rim.size() );
                for ( usize i = 0; i < rim.size(); ++i ) {
                    CHECK( s.workspace.meshSelection.edges.pData[i].a.value == rim[i].a.value );
                    CHECK( s.workspace.meshSelection.edges.pData[i].b.value == rim[i].b.value );
                }
            }
            REQUIRE( MapToolGestures_RepeatModelling( &s.workspace ) );
        } else { s.Drag( pivot.x + radius, pivot.y, pivot.x + radius * std::cos( radians ), pivot.y + radius * std::sin( radians ) ); }
        REQUIRE( s.Steps() == steps + step + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 4 + 4 * ( step + 1 ) );
        CheckOriginalVertices( s, mesh, before );
        edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
        CHECK( after.value.vertices.nCount == 10 + 5 * ( step + 1 ) );
        REQUIRE( s.workspace.meshSelection.edges.nCount == 4 );
        const f64 angle = radians * ( step + 1 );
        const auto turned = [&]( math::vec3d_t point ) {
            const f64 x = point.x - pivot.x, y = point.y - pivot.y;
            return math::vec3d_t{ pivot.x + x * std::cos( angle ) - y * std::sin( angle ),
                                 pivot.y + x * std::sin( angle ) + y * std::cos( angle ), point.z };
        };
        const auto same = []( math::vec3d_t a, math::vec3d_t b ) {
            return std::fabs( a.x - b.x ) < 1e-6 && std::fabs( a.y - b.y ) < 1e-6 && std::fabs( a.z - b.z ) < 1e-6;
        };
        std::vector<u64> rimIds;
        for ( usize i = 0; i < 4; ++i ) {
            const auto edge = s.workspace.meshSelection.edges.pData[i]; rimIds.push_back( edge.a.value ); rimIds.push_back( edge.b.value );
            const auto a = Position( s.workspace, mesh, edge.a ), b = Position( s.workspace, mesh, edge.b );
            usize matches = 0;
            for ( usize j = 0; j + 1 < profile.size(); ++j ) {
                const auto expectedA = turned( profile[j] ), expectedB = turned( profile[j + 1] );
                matches += ( same( a, expectedA ) && same( b, expectedB ) ) || ( same( a, expectedB ) && same( b, expectedA ) );
            }
            CHECK( matches == 1 );
        }
        std::sort( rimIds.begin(), rimIds.end() ); rimIds.erase( std::unique( rimIds.begin(), rimIds.end() ), rimIds.end() );
        CHECK( rimIds.size() == 5 ); // Adjacent faces share endpoints instead of making disconnected quads.
        math::vec3d_t currentPivot{}; REQUIRE( MapToolGestures_Pivot( &s.workspace, &currentPivot ) ); CheckPosition( currentPivot, pivot );
    }
    u32 operations = 6;
    if ( finishCorridor ) {
        auto &ws = s.workspace;
        const auto checkHistory = [&]( const edge_workflow_mesh_t &before, const edge_workflow_mesh_t &after ) {
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
            checkMetadata( *ws.pDocument );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
            checkMetadata( *ws.pDocument );
        };
        const auto boundaryEdges = [&]( const edge_workflow_mesh_t &snapshot ) {
            std::vector<geo::mesh_edge_ref_t> edges, boundary;
            const auto &description = snapshot.value;
            for ( usize i = 0; i < description.faces.nCount; ++i ) {
                const auto &face = description.faces.pData[i];
                for ( u32 j = 0; j < face.cCorners; ++j ) {
                    const auto a = description.corners.pData[face.iFirstCorner + j].iVertex;
                    const auto b = description.corners.pData[face.iFirstCorner + ( j + 1 ) % face.cCorners].iVertex;
                    edges.push_back( geo::MeshEdgeRef_Make( description.vertices.pData[a].sourceId, description.vertices.pData[b].sourceId ) );
                }
            }
            const auto less = []( const auto &a, const auto &b ) { return a.a.value != b.a.value ? a.a.value < b.a.value : a.b.value < b.b.value; };
            std::sort( edges.begin(), edges.end(), less );
            for ( usize i = 0; i < edges.size(); ) {
                usize end = i + 1;
                while ( end < edges.size() && edges[end].a.value == edges[i].a.value && edges[end].b.value == edges[i].b.value ) { ++end; }
                REQUIRE( end - i <= 2 );
                if ( end - i == 1 ) { boundary.push_back( edges[i] ); }
                i = end;
            }
            return boundary;
        };
        const auto clickEdge = [&]( geo::mesh_edge_ref_t edge, bool add ) {
            const auto a = Position( ws, mesh, edge.a ), b = Position( ws, mesh, edge.b );
            const math::vec3d_t midpoint{ ( a.x + b.x ) * 0.5, ( a.y + b.y ) * 0.5, ( a.z + b.z ) * 0.5 };
            QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), midpoint, &screen ) );
            map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( camera.get(), screen, &hit ) );
            REQUIRE( hit.object == mesh ); REQUIRE( hit.edge.a.value == edge.a.value ); REQUIRE( hit.edge.b.value == edge.b.value );
            const auto modifiers = add ? Qt::ShiftModifier : Qt::NoModifier;
            QMouseEvent press( QEvent::MouseButtonPress, screen, camera->mapToGlobal( screen ), Qt::LeftButton, Qt::LeftButton, modifiers );
            QCoreApplication::sendEvent( camera.get(), &press );
            QMouseEvent release( QEvent::MouseButtonRelease, screen, camera->mapToGlobal( screen ), Qt::LeftButton, Qt::NoButton, modifiers );
            QCoreApplication::sendEvent( camera.get(), &release );
            REQUIRE( geo::MeshSelection_HasEdge( &ws.meshSelection, edge ) );
        };
        // At 90 degrees the end profile lies at x=-64. Its tangent points -X;
        // a real Translate handle extends all four selected rim edges together.
        edge_workflow_mesh_t curved( *ws.pDocument, mesh );
        s.Tool( "map.tool.translate" );
        const f64 grab = -64.0 + 40.0 / MapOrthoView_Zoom( s.top.get() );
        s.Drag( grab, 128, grab - 128, 128 ); ++operations;
        REQUIRE( s.Steps() == steps + operations ); REQUIRE( MeshFaces( ws, mesh ) == 32 );
        REQUIRE( ws.meshSelection.edges.nCount == 4 );
        for ( usize i = 0; i < ws.meshSelection.edges.nCount; ++i ) {
            const auto edge = ws.meshSelection.edges.pData[i];
            CHECK( Position( ws, mesh, edge.a ).x == Catch::Approx( -192 ).margin( 1e-6 ) );
            CHECK( Position( ws, mesh, edge.b ).x == Catch::Approx( -192 ).margin( 1e-6 ) );
        }
        CheckOriginalVertices( s, mesh, curved ); edge_workflow_mesh_t extended( *ws.pDocument, mesh );
        REQUIRE( extended.value.vertices.nCount == 45 ); checkHistory( curved, extended );
        // From below, every bottom rail is visible without the roof occluding
        // the far side. Pick both full chains, not just two loop seeds.
        s.Tool( "map.tool.select" );
        MapCameraView_LookAt( camera.get(), { 300, -80, -700 }, { 24, 96, 0 } ); QCoreApplication::processEvents();
        const auto boundary = boundaryEdges( extended ); REQUIRE( boundary.size() == 24 );
        std::vector<geo::mesh_edge_ref_t> rails;
        for ( const auto edge : boundary ) {
            if ( std::fabs( Position( ws, mesh, edge.a ).z ) < 1e-6 && std::fabs( Position( ws, mesh, edge.b ).z ) < 1e-6 ) { rails.push_back( edge ); }
        }
        REQUIRE( rails.size() == 16 );
        for ( usize i = 0; i < rails.size(); ++i ) {
            clickEdge( rails[i], i != 0 ); REQUIRE( ws.meshSelection.edges.nCount == i + 1 );
        }
        REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.bridge" ), {} ) == command_result_t::OK ); ++operations;
        REQUIRE( s.Steps() == steps + operations ); REQUIRE( MeshFaces( ws, mesh ) == 40 );
        edge_workflow_mesh_t floored( *ws.pDocument, mesh );
        CHECK( floored.value.vertices.nCount == extended.value.vertices.nCount ); CheckOriginalVertices( s, mesh, extended );
        const auto openings = boundaryEdges( floored ); REQUIRE( openings.size() == 10 );
        std::vector<std::pair<math::vec3d_t, math::vec3d_t>> rings;
        rings.push_back( { { 128, -32, 0 }, { 256, -32, 0 } } );
        for ( u32 step = 0; step <= 6; ++step ) {
            const f64 angle = radians * step;
            const auto turn = [&]( math::vec3d_t point ) {
                const f64 x = point.x - pivot.x, y = point.y - pivot.y;
                return math::vec3d_t{ pivot.x + x * std::cos( angle ) - y * std::sin( angle ),
                    pivot.y + x * std::sin( angle ) + y * std::cos( angle ), 0 };
            };
            rings.push_back( { turn( profile.front() ), turn( profile.back() ) } );
        }
        auto exit = rings.back(); exit.first.x -= 128; exit.second.x -= 128; rings.push_back( exit );
        std::vector<usize> cells( rings.size() - 1, 0 ); usize floorFaces = 0;
        for ( usize i = 0; i < floored.value.faces.nCount; ++i ) {
            const auto &face = floored.value.faces.pData[i]; bool floor = true;
            for ( u32 j = 0; j < face.cCorners; ++j ) { floor &= std::fabs( floored.value.vertices.pData[floored.value.corners.pData[face.iFirstCorner + j].iVertex].position.z ) < 1e-6; }
            if ( !floor ) { continue; }
            ++floorFaces; REQUIRE( face.cCorners == 4 ); usize matchingCells = 0;
            for ( usize cell = 0; cell + 1 < rings.size(); ++cell ) {
                const math::vec3d_t expected[]{ rings[cell].first, rings[cell].second, rings[cell + 1].first, rings[cell + 1].second };
                usize matches = 0;
                for ( const auto point : expected ) {
                    for ( u32 corner = 0; corner < face.cCorners; ++corner ) {
                        const auto actual = floored.value.vertices.pData[floored.value.corners.pData[face.iFirstCorner + corner].iVertex].position;
                        matches += std::fabs( actual.x - point.x ) < 1e-6 && std::fabs( actual.y - point.y ) < 1e-6 && std::fabs( actual.z ) < 1e-6;
                    }
                }
                if ( matches == 4 ) { ++matchingCells; ++cells[cell]; }
            }
            CHECK( matchingCells == 1 );
        }
        CHECK( floorFaces == 8 ); for ( const usize count : cells ) { CHECK( count == 1 ); }
        checkHistory( extended, floored );
        // Floor closure separates the two end loops. One actual exit-edge
        // click is enough for Fill Hole to cap only that planar opening.
        MapCameraView_LookAt( camera.get(), { -800, 128, 128 }, { -192, 128, 128 } ); QCoreApplication::processEvents();
        const auto capSeed = geo::MeshEdgeRef_Make( VertexAt( ws, mesh, { -192, 64, 0 } ), VertexAt( ws, mesh, { -192, 64, 192 } ) );
        clickEdge( capSeed, false ); REQUIRE( ws.meshSelection.edges.nCount == 1 );
        REQUIRE( EditorCommands_Execute( &s.gui.commands, StringView_FromCString( "map.mesh.fill_hole" ), {} ) == command_result_t::OK ); ++operations;
        REQUIRE( s.Steps() == steps + operations ); REQUIRE( MeshFaces( ws, mesh ) == 41 );
        edge_workflow_mesh_t capped( *ws.pDocument, mesh );
        CHECK( capped.value.vertices.nCount == floored.value.vertices.nCount ); CheckOriginalVertices( s, mesh, floored );
        const auto entrance = boundaryEdges( capped ); REQUIRE( entrance.size() == 5 );
        for ( const auto edge : entrance ) {
            CHECK( Position( ws, mesh, edge.a ).y == Catch::Approx( -32 ).margin( 1e-6 ) );
            CHECK( Position( ws, mesh, edge.b ).y == Catch::Approx( -32 ).margin( 1e-6 ) );
        }
        for ( usize i = 0; i < capped.value.faces.nCount; ++i ) {
            map_uv_face_t face{}; REQUIRE( MapTextureEdit_Describe( ws.pDocument, { mesh, capped.value.faces.pData[i].sourceId.value }, &face ) == map_status_t::OK );
            CHECK( face.material == "materials/tests/curved_corridor.cymat" );
            for ( const auto uv : face.uvs ) { CHECK( std::isfinite( uv.x ) ); CHECK( std::isfinite( uv.y ) ); }
        }
        checkHistory( floored, capped ); checkMetadata( *ws.pDocument );
        math::vec3d_t currentPivot{}; REQUIRE( MapToolGestures_Pivot( &ws, &currentPivot ) ); CheckPosition( currentPivot, pivot );
        QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "curved-corridor.cymap" ) );
        REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
        session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
        edge_workflow_mesh_t reopened( *loaded.workspace.pDocument, mesh );
        REQUIRE( capped.value.faces.nCount == reopened.value.faces.nCount );
        // Material handles are document-local intern slots. Compare every
        // authored path first, then align only the test snapshot's slots so
        // exact description equality still checks all topology, IDs and UVs.
        for ( usize i = 0; i < capped.value.faces.nCount; ++i ) {
            const auto &before = capped.value.faces.pData[i]; auto &after = reopened.value.faces.pData[i];
            REQUIRE( before.sourceId.value == after.sourceId.value );
            REQUIRE( StringView_Equals( MapMaterials_Path( &ws.pDocument->materials, before.attributes.material.value ),
                MapMaterials_Path( &loaded.workspace.pDocument->materials, after.attributes.material.value ) ) );
            after.attributes.material = before.attributes.material;
        }
        CHECK( geo::MeshSourceDescription_Equal( &capped.value, &reopened.value ) ); checkMetadata( *loaded.workspace.pDocument );
        // Reopening must leave an editable corridor, not merely equivalent
        // saved geometry. Pick the entire remaining entrance through the real
        // Camera view, then continue it with the ordinary Top Translate handle.
        auto &loadedWs = loaded.workspace;
        edge_workflow_mesh_t loadedBefore( *loadedWs.pDocument, mesh );
        REQUIRE( loadedBefore.value.vertices.nCount == 45 ); REQUIRE( loadedBefore.value.faces.nCount == 41 );
        const auto loadedEntrance = boundaryEdges( loadedBefore ); REQUIRE( loadedEntrance.size() == 5 );
        camera_t loadedCamera( loaded );
        MapCameraView_LookAt( loadedCamera.view.get(), { 192, -600, 128 }, { 192, -32, 128 } ); QCoreApplication::processEvents();
        loaded.Tool( "map.select_mode.edges" ); loaded.Tool( "map.tool.select" );
        for ( usize i = 0; i < loadedEntrance.size(); ++i ) {
            const auto edge = loadedEntrance[i];
            const auto a = Position( loadedWs, mesh, edge.a ), b = Position( loadedWs, mesh, edge.b );
            CHECK( a.y == Catch::Approx( -32 ).margin( 1e-6 ) ); CHECK( b.y == Catch::Approx( -32 ).margin( 1e-6 ) );
            const QPointF at = loadedCamera.At( { ( a.x + b.x ) * 0.5, -32, ( a.z + b.z ) * 0.5 } );
            map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( loadedCamera.view.get(), at, &hit ) );
            REQUIRE( hit.object == mesh ); REQUIRE( hit.edge.a.value == edge.a.value ); REQUIRE( hit.edge.b.value == edge.b.value );
            const auto modifiers = i == 0 ? Qt::NoModifier : Qt::ShiftModifier;
            QMouseEvent press( QEvent::MouseButtonPress, at, loadedCamera.view->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, modifiers );
            QCoreApplication::sendEvent( loadedCamera.view.get(), &press );
            QMouseEvent release( QEvent::MouseButtonRelease, at, loadedCamera.view->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, modifiers );
            QCoreApplication::sendEvent( loadedCamera.view.get(), &release );
            REQUIRE( loadedWs.meshSelection.edges.nCount == i + 1 ); REQUIRE( geo::MeshSelection_HasEdge( &loadedWs.meshSelection, edge ) );
        }
        loaded.Tool( "map.tool.translate" ); EdgeDragAction( loaded, "extend" ); MapWorkspace_SetGridSize( &loadedWs, 16 );
        const usize loadedSteps = loaded.Steps();
        const f64 yHandle = -32 + 40.0 / MapOrthoView_Zoom( loaded.top.get() );
        loaded.Drag( 192, yHandle, 192, yHandle - 16 );
        REQUIRE( loaded.Steps() == loadedSteps + 1 );
        edge_workflow_mesh_t continued( *loadedWs.pDocument, mesh );
        REQUIRE( continued.value.vertices.nCount == 50 ); REQUIRE( continued.value.faces.nCount == 46 );
        CheckOriginalVertices( loaded, mesh, loadedBefore ); checkMetadata( *loadedWs.pDocument );
        const auto nextEntrance = boundaryEdges( continued ); REQUIRE( nextEntrance.size() == 5 );
        REQUIRE( loadedWs.meshSelection.edges.nCount == 5 );
        for ( const auto edge : nextEntrance ) {
            REQUIRE( geo::MeshSelection_HasEdge( &loadedWs.meshSelection, edge ) );
            CHECK( Position( loadedWs, mesh, edge.a ).y == Catch::Approx( -48 ).margin( 1e-6 ) );
            CHECK( Position( loadedWs, mesh, edge.b ).y == Catch::Approx( -48 ).margin( 1e-6 ) );
        }
        REQUIRE( MapWorkspace_Undo( &loadedWs ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *loadedWs.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &loadedBefore.value, &restored.value ) ); }
        REQUIRE( loadedWs.meshSelection.edges.nCount == 5 );
        for ( const auto edge : loadedEntrance ) { CHECK( geo::MeshSelection_HasEdge( &loadedWs.meshSelection, edge ) ); }
        checkMetadata( *loadedWs.pDocument );
        REQUIRE( MapWorkspace_Redo( &loadedWs ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *loadedWs.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &continued.value, &restored.value ) ); }
        REQUIRE( loadedWs.meshSelection.edges.nCount == 5 );
        for ( const auto edge : nextEntrance ) { CHECK( geo::MeshSelection_HasEdge( &loadedWs.meshSelection, edge ) ); }
        checkMetadata( *loadedWs.pDocument );
    }
    if ( const QByteArray path = qgetenv( "MASON_EDGE_PROFILE_SHOT" ); !path.isEmpty() ) {
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
        MapCameraView_LookAt( camera.get(), { 620, 720, 580 }, { 80, 80, 120 } ); QCoreApplication::processEvents();
        REQUIRE( camera->grab().save( QString::fromUtf8( path ) ) );
    }
    edge_workflow_mesh_t complete( *s.workspace.pDocument, mesh );
    for ( u32 i = 0; i < operations; ++i ) { REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK ); }
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) ); }
    for ( u32 i = 0; i < operations; ++i ) { REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK ); }
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &complete.value, &restored.value ) ); }
}

TEST_CASE( "Visible Scale Extend grows a face without moving the original quad", "[map][gui][tool-gestures][edge-rotate-extend][scale]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s );
    s.Click( 128, -32 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
    s.Tool( "map.tool.pivot" ); s.Click( 0, 0 );
    s.Tool( "map.tool.scale" ); EdgeDragAction( s, "extend" );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
    const f64 radius = 60.0 / MapOrthoView_Zoom( s.top.get() ); s.Drag( -radius, 0, -radius * 2, 0 );
    REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == 2 );
    CheckOriginalVertices( s, mesh, original );
    const auto outer = OnlySelectedEdge( s );
    const auto a = Position( s.workspace, mesh, outer.a ), b = Position( s.workspace, mesh, outer.b );
    CHECK( a.x == Catch::Approx( 256 ) ); CHECK( b.x == Catch::Approx( 256 ) );
    CHECK( std::min( a.y, b.y ) == Catch::Approx( -128 ) ); CHECK( std::max( a.y, b.y ) == Catch::Approx( 128 ) );
    CHECK( a.z == 0 ); CHECK( b.z == 0 );
}

namespace
{
void EdgePickConversion( session_t &s, bool enabled )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.components_convert_brushes" ) );
    REQUIRE( descriptor ); setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = enabled ? CY_TRUE : CY_FALSE;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}

u64 EdgePickRecedingQuad( session_t &s, bool reversedIds )
{
    auto &document = *s.workspace.pDocument;
    const auto next = [&]() {
        const auto id = geo::GeometrySourceIdAllocator_Allocate( &document.geometry.sourceIds.allocator );
        REQUIRE( id.status == geo::geometry_status_t::OK ); return id.id;
    };
    const auto root = next(); geo::mesh_source_description_t description{};
    REQUIRE( geo::MeshSourceDescription_Init( &description, document.pAllocator, root ) == geo::geometry_status_t::OK );
    geo::geometry_source_id_t vertices[]{ next(), next(), next(), next() };
    const math::vec3d_t points[]{ { 64, 32, 64 }, { 1024, 32, 64 }, { 1024, 128, 64 }, { 64, 128, 64 } };
    for ( usize i = 0; i < 4; ++i ) {
        REQUIRE( geo::MeshSourceDescription_TryAddVertex( &description, points[i], vertices[reversedIds ? 3 - i : i], nullptr ) == geo::geometry_status_t::OK );
    }
    const u32 corners[]{ 0, 3, 2, 1 };
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &description, { corners, 4 }, next(), {}, nullptr ) == geo::geometry_status_t::OK );
    geo::mesh_source_t mesh{};
    REQUIRE( geo::MeshSource_TryBuild( &description, document.pAllocator, &mesh ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &document.geometry, &mesh ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &mesh ); geo::MeshSourceDescription_Shutdown( &description );
    REQUIRE( MapDocument_SetGeometryLayer( &document, root.value, StringView_FromCString( "default" ) ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.workspace ); MapWorkspace_Select( &s.workspace, root.value, MAP_SELECT_REPLACE );
    s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" );
    return root.value;
}

void EdgePickCheckCutPixels( session_t &s, camera_t &camera, u64 mesh, const edge_workflow_mesh_t &before,
    QPointF first, QPointF second )
{
    edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
    usize newVertices = 0; bool firstFound = false, secondFound = false;
    for ( usize i = 0; i < after.value.vertices.nCount; ++i ) {
        const auto &vertex = after.value.vertices.pData[i]; bool existed = false;
        for ( usize j = 0; j < before.value.vertices.nCount; ++j ) { existed |= vertex.sourceId.value == before.value.vertices.pData[j].sourceId.value; }
        if ( existed ) { continue; }
        ++newVertices; const QPointF point = camera.At( vertex.position );
        const f64 firstDistance = std::hypot( point.x() - first.x(), point.y() - first.y() );
        const f64 secondDistance = std::hypot( point.x() - second.x(), point.y() - second.y() );
        CHECK( std::min( firstDistance, secondDistance ) < 0.05 );
        firstFound |= firstDistance < 0.05; secondFound |= secondDistance < 0.05;
    }
    CHECK( newVertices == 2 ); CHECK( firstFound ); CHECK( secondFound );
    CheckOriginalVertices( s, mesh, before );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}
} // namespace

TEST_CASE( "A camera edge crossing the near plane stays usable for direct movement and cutting", "[map][gui][tool-gestures][shared-edge-pick][near-plane]" )
{
    for ( const bool cut : { false, true } ) {
        DYNAMIC_SECTION( "cut = " << cut ) {
        CAPTURE( cut ); session_t s;
        const u64 mesh = AddBox( s.workspace, { -16, 16, -32 }, { 256, 64, 32 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" ); EdgeDragAction( s, "move" );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, 0, 0 }, { 1, 0, 0 } );
        const math::vec3d_t start{ 128, 16, 32 }, finish{ 128, 32, 32 };
        const QPointF press = camera.At( start ); QPointF clipped;
        REQUIRE_FALSE( MapCameraView_WorldToView( camera.view.get(), { -16, 16, 32 }, &clipped ) );
        map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( camera.view.get(), press, &hit ) ); REQUIRE( hit.object == mesh );
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        if ( cut ) {
            s.Tool( "map.tool.edge_cut" ); const math::vec3d_t other{ 128, 16, -32 };
            const QPointF second = camera.At( other ); camera.Click( start ); camera.Click( other ); camera.Key( Qt::Key_Return );
            REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( s.workspace, mesh ) == 7 );
            EdgePickCheckCutPixels( s, camera, mesh, before, press, second );
        } else {
            camera.Drag( start, finish );
            REQUIRE( s.Steps() == steps + 1 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
            CHECK( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, hit.edge ) );
            for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
                const auto &vertex = before.value.vertices.pData[i]; auto expected = vertex.position;
                if ( vertex.sourceId.value == hit.edge.a.value || vertex.sourceId.value == hit.edge.b.value ) { expected.y += 16; }
                CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
            }
            edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
            REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
            REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
            { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
        }
        }
    }
}

TEST_CASE( "Camera Knife and Edge Cut split receding edges at the clicked pixels regardless of canonical endpoint order", "[map][gui][tool-gestures][shared-edge-pick][perspective-cut]" )
{
    for ( const bool reversedIds : { false, true } ) {
        for ( const char *tool : { "map.tool.knife", "map.tool.edge_cut" } ) {
            CAPTURE( reversedIds, tool ); session_t s; const u64 mesh = EdgePickRecedingQuad( s, reversedIds );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, 0, 0 }, { 1, 0, 0 } );
            const math::vec3d_t a{ 256, 32, 64 }, b{ 256, 128, 64 };
            const QPointF first = camera.At( a ), second = camera.At( b );
            map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( camera.view.get(), first, &hit ) ); REQUIRE( hit.object == mesh );
            const auto canonicalA = Position( s.workspace, mesh, hit.edge.a );
            REQUIRE( canonicalA.x == ( reversedIds ? 1024.0 : 64.0 ) );
            edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
            s.Tool( tool ); camera.Click( a ); camera.Click( b ); camera.Key( Qt::Key_Return );
            REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( s.workspace, mesh ) == 2 );
            EdgePickCheckCutPixels( s, camera, mesh, before, first, second );
        }
    }
}

TEST_CASE( "Camera component regrab Loop Cut and Edge Arc cannot reach edges behind a solid brush", "[map][gui][tool-gestures][shared-edge-pick][occlusion]" )
{
    for ( const char *tool : { "map.tool.select", "map.tool.loop_cut", "map.tool.edge_arc" } ) {
        CAPTURE( tool ); session_t s;
        const u64 mesh = AddBox( s.workspace, { 512, -96, -96 }, { 768, 96, 96 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        const u64 blocker = AddBox( s.workspace, { 256, -200, -200 }, { 288, 200, 200 } );
        EdgePickConversion( s, false ); s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" ); EdgeDragAction( s, "move" );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, 0, 0 }, { 1, 0, 0 } );
        const math::vec3d_t start{ 512, 48, 96 }, finish{ 512, 80, 96 };
        map_mesh_edge_hit_t hit{}; REQUIRE_FALSE( MapCameraView_PickMeshEdge( camera.view.get(), camera.At( start ), &hit ) );
        const bool regrab = std::string( tool ) == "map.tool.select";
        MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        if ( regrab ) {
            const auto top = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 512, -96, 96 } ), VertexAt( s.workspace, mesh, { 512, 96, 96 } ) );
            const auto bottom = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 512, -96, -96 } ), VertexAt( s.workspace, mesh, { 512, 96, -96 } ) );
            REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, top, MAP_SELECT_REPLACE ) );
            REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, bottom, MAP_SELECT_ADD ) );
        }
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        s.Tool( tool );
        if ( regrab ) { camera.Drag( start, finish ); }
        else { camera.Click( start ); }
        CHECK( s.Steps() == steps ); CHECK( KindOf( s.workspace, blocker ) == map_wire_kind_t::BRUSH );
        { edge_workflow_mesh_t after( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); }
        if ( !regrab ) { CHECK( s.workspace.meshSelection.edges.nCount == 0 ); }
    }
}

TEST_CASE( "A failed camera edge visibility query never falls through into conversion or a tool edit", "[map][gui][tool-gestures][shared-edge-pick][allocation]" )
{
    for ( const char *tool : { "map.tool.knife", "map.tool.edge_cut", "map.tool.loop_cut", "map.tool.edge_arc" } ) {
        CAPTURE( tool ); session_t s;
        const u64 mesh = AddBox( s.workspace, { 512, -96, -96 }, { 768, 96, 96 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        const u64 brush = AddBox( s.workspace, { 256, -200, -200 }, { 288, 200, 200 } );
        s.Tool( "map.select_mode.edges" ); MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, 0, 0 }, { 1, 0, 0 } );
        s.Tool( tool ); const QPointF press = camera.At( { 512, 48, 96 } );
        const auto *document = s.workspace.pDocument; const usize steps = s.Steps();
        const auto editSerial = s.workspace.lastEdit.serial, selectionRevision = s.workspace.selection.revision;
        edge_workflow_mesh_t before( *document, mesh );
        edge_gui_allocation_t audit{ 0, 1, 0 }; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
        const auto *originalAllocator = s.workspace.pDocument->pAllocator;
        s.workspace.pDocument->pAllocator = &allocator;
        camera.Send( QEvent::MouseButtonPress, press, Qt::LeftButton );
        s.workspace.pDocument->pAllocator = originalAllocator;
        camera.Send( QEvent::MouseButtonRelease, press, Qt::NoButton );
        CHECK( audit.calls > 0 ); CHECK( audit.live == 0 );
        CHECK( s.workspace.pDocument == document ); CHECK( s.workspace.lastEdit.serial == editSerial );
        CHECK( s.workspace.selection.revision == selectionRevision ); CHECK( s.workspace.meshSelection.edges.nCount == 0 );
        CHECK( s.Steps() == steps ); CHECK( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH );
        { edge_workflow_mesh_t after( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); }
    }
}

TEST_CASE( "Top view Loop Cut uses the same frontmost overlapping edge as component selection", "[map][gui][tool-gestures][shared-edge-pick][ortho-depth]" )
{
    session_t s;
    const u64 lower = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 64 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const u64 upper = AddBox( s.workspace, { -128, -128, 128 }, { 128, 128, 192 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" );
    s.Frame( { -192, -192, 0 }, { 192, 192, 192 } );
    map_mesh_edge_hit_t hit{}; REQUIRE( MapOrthoView_PickMeshEdge( s.top.get(), s.At( 0, -128 ), &hit ) );
    REQUIRE( hit.object == upper );
    CHECK( Position( s.workspace, upper, hit.edge.a ).z == 192 ); CHECK( Position( s.workspace, upper, hit.edge.b ).z == 192 );
    edge_workflow_mesh_t lowerBefore( *s.workspace.pDocument, lower ), upperBefore( *s.workspace.pDocument, upper );
    const usize steps = s.Steps(); s.Tool( "map.tool.loop_cut" ); s.Click( 0, -128 );
    REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( s.workspace, upper ) > 6 );
    { edge_workflow_mesh_t after( *s.workspace.pDocument, lower ); CHECK( geo::MeshSourceDescription_Equal( &lowerBefore.value, &after.value ) ); }
    edge_workflow_mesh_t upperAfter( *s.workspace.pDocument, upper );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, upper ); CHECK( geo::MeshSourceDescription_Equal( &upperBefore.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, upper ); CHECK( geo::MeshSourceDescription_Equal( &upperAfter.value, &restored.value ) ); }
}

TEST_CASE( "An edge hidden after press cannot continue a pending direct drag or an active component preview", "[map][gui][tool-gestures][shared-edge-pick][pending-visibility]" )
{
    for ( const int phase : { 0, 1, 2, 3 } ) {
        CAPTURE( phase ); // Pending direct, active direct, active X gizmo, and X gizmo hidden then immediately shown.
        session_t s; const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
        EdgeDragAction( s, "move" );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( s.workspace, mesh, { 256, -128, 0 } ), VertexAt( s.workspace, mesh, { 256, 128, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        const f64 x = phase >= 2 ? OnXHandle( s, { 256, 0, 0 } ) : 256, y = phase >= 2 ? 0 : -96;
        const QPointF from = s.At( x, y ), to = s.At( x + 32, y );
        s.Mouse( QEvent::MouseMove, from, Qt::NoButton ); s.Mouse( QEvent::MouseButtonPress, from, Qt::LeftButton );
        REQUIRE( geo::MeshSelection_HasEdge( &s.workspace.meshSelection, edge ) );
        if ( phase != 0 ) {
            const QImage idle = s.top->grab().toImage();
            s.Mouse( QEvent::MouseMove, s.At( x + 16, y ), Qt::LeftButton );
            CHECK( s.top->grab().toImage() != idle ); // The interrupted gesture really reached its visual preview.
            CHECK( s.Steps() == steps ); CheckOriginalVertices( s, mesh, before );
        }
        const auto selectionRevision = s.workspace.selection.revision, geometryRevision = s.workspace.pDocument->geometry.revision;
        MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::MESHES, CY_TRUE );
        // Hiding a visgroup intentionally prunes its components from the selection.
        REQUIRE( s.workspace.selection.revision > selectionRevision ); REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
        REQUIRE( s.workspace.pDocument->geometry.revision == geometryRevision );
        const auto prunedRevision = s.workspace.selection.revision;
        // Restoring visibility before another input sample must not revive a cancelled capture.
        if ( phase == 3 ) { MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::MESHES, CY_FALSE ); }
        s.Mouse( QEvent::MouseMove, to, Qt::LeftButton ); s.Mouse( QEvent::MouseButtonRelease, to, Qt::NoButton );
        CHECK( s.Steps() == steps ); CHECK( s.workspace.selection.revision == prunedRevision );
        REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
        { edge_workflow_mesh_t after( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); }
        MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::MESHES, CY_FALSE );
        REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, edge, MAP_SELECT_REPLACE ) );
        s.Drag( x, y, x + 32, y );
        REQUIRE( s.Steps() == steps + 1 );
        for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
            const auto &vertex = before.value.vertices.pData[i]; auto expected = vertex.position;
            if ( vertex.sourceId.value == edge.a.value || vertex.sourceId.value == edge.b.value ) { expected.x += 32; }
            CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
        }
    }
}

TEST_CASE( "Knife and Edge Cut do not bypass edge occlusion by snapping to hidden corners", "[map][gui][tool-gestures][shared-edge-pick][hidden-corner]" )
{
    for ( const char *tool : { "map.tool.knife", "map.tool.edge_cut" } ) {
        CAPTURE( tool ); session_t s;
        const u64 mesh = AddBox( s.workspace, { 512, -96, -96 }, { 768, 96, 96 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
        const u64 blocker = AddBox( s.workspace, { 256, -200, -200 }, { 288, 200, 200 } );
        EdgePickConversion( s, false ); s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" );
        MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, 0, 0 }, { 1, 0, 0 } );
        const math::vec3d_t first{ 512, -96, -96 }, second{ 512, 96, 96 };
        map_mesh_edge_hit_t hit{};
        REQUIRE_FALSE( MapCameraView_PickMeshEdge( camera.view.get(), camera.At( first ), &hit ) );
        REQUIRE_FALSE( MapCameraView_PickMeshEdge( camera.view.get(), camera.At( second ), &hit ) );
        edge_workflow_mesh_t before( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
        s.Tool( tool ); camera.Click( first ); camera.Click( second ); camera.Key( Qt::Key_Return );
        CHECK( s.Steps() == steps ); CHECK( KindOf( s.workspace, blocker ) == map_wire_kind_t::BRUSH );
        { edge_workflow_mesh_t after( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); }
        // The same corners become usable when the occluder is hidden. Start
        // seven pixels outward from the corner: inside vertex tolerance (8),
        // outside the narrower edge tolerance (6) and outside the face itself.
        MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::BRUSHES, CY_TRUE );
        const QPointF corner = camera.At( first ), outward = corner - camera.At( { 512, 0, 0 } );
        const f64 length = std::hypot( outward.x(), outward.y() ); REQUIRE( length > 0 );
        const QPointF nearCorner = corner + outward * ( 7.0 / length );
        camera.Send( QEvent::MouseMove, nearCorner, Qt::NoButton );
        camera.Send( QEvent::MouseButtonPress, nearCorner, Qt::LeftButton );
        camera.Send( QEvent::MouseButtonRelease, nearCorner, Qt::NoButton );
        camera.Click( second ); camera.Key( Qt::Key_Return );
        REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( s.workspace, mesh ) == 7 );
        CheckOriginalVertices( s, mesh, before ); edge_workflow_mesh_t cut( *s.workspace.pDocument, mesh );
        REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
        REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
        { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) ); }
    }
}

TEST_CASE( "Face picking remains available inside Translate Rotate and Scale in Top and Camera", "[map][gui][tool-gestures][component-selection-workflow][face-routing]" )
{
    for ( const char *tool : { "map.tool.translate", "map.tool.rotate", "map.tool.scale" } ) {
        for ( const bool perspective : { false, true } ) {
            DYNAMIC_SECTION( tool << " perspective=" << perspective ) {
                session_t s;
                const u64 leftObject = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
                REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
                const u64 rightObject = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
                REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
                const u64 roots[]{ leftObject, rightObject }; MapWorkspace_SetSelection( &s.workspace, roots, 2 );
                s.Tool( "map.mesh.merge" ); const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
                s.Frame( { -384, -192, -64 }, { 384, 192, 256 } );
                std::unique_ptr<camera_t> camera;
                if ( perspective ) { camera = std::make_unique<camera_t>( s ); MapCameraView_LookAt( camera->view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); }
                const auto click = [&]( math::vec3d_t world, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
                    QWidget *view = perspective ? camera->view.get() : s.top.get();
                    const QPointF point = perspective ? camera->At( world ) : s.At( world.x, world.y );
                    QMouseEvent press( QEvent::MouseButtonPress, point, view->mapToGlobal( point ), Qt::LeftButton, Qt::LeftButton, modifiers );
                    QCoreApplication::sendEvent( view, &press );
                    QMouseEvent release( QEvent::MouseButtonRelease, point, view->mapToGlobal( point ), Qt::LeftButton, Qt::NoButton, modifiers );
                    QCoreApplication::sendEvent( view, &release );
                };
                MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
                s.Tool( tool ); const auto activeTool = s.workspace.tool; s.Tool( "map.select_mode.faces" );
                REQUIRE( s.workspace.tool == activeTool ); const usize steps = s.Steps();
                click( { -192, 32, 128 } );
                REQUIRE( MapWorkspace_HasMeshFace( &s.workspace ) ); REQUIRE( s.workspace.selectedMeshFaceObject == mesh );
                const u64 left = s.workspace.selectedMeshFaceId; REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 1 );
                click( { 192, 32, 128 }, Qt::ShiftModifier );
                REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 ); const u64 right = s.workspace.selectedMeshFaceId;
                REQUIRE( right != left ); CHECK( MapWorkspace_IsMeshFaceSelected( &s.workspace, mesh, left ) );
                click( { 192, 32, 128 }, Qt::ControlModifier | Qt::ShiftModifier ); // Toggle wins over additive Shift.
                REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 1 ); CHECK( s.workspace.selectedMeshFaceId == left );
                click( { 192, 32, 128 }, Qt::ControlModifier ); // Toggle an unselected face into the existing set.
                REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 ); CHECK( MapWorkspace_IsMeshFaceSelected( &s.workspace, mesh, left ) );
                click( { 192, 32, 128 } ); REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 1 );
                CHECK( s.workspace.selectedMeshFaceId == right ); CHECK( s.workspace.tool == activeTool ); CHECK( s.Steps() == steps );
                if ( std::string( tool ) == "map.tool.translate" ) {
                    edge_workflow_mesh_t before( *s.workspace.pDocument, mesh );
                    if ( perspective ) {
                        const auto eye = MapCameraView_Position( camera->view.get() );
                        const f64 distance = std::sqrt( ( eye.x - 192 ) * ( eye.x - 192 ) + eye.y * eye.y + ( eye.z - 128 ) * ( eye.z - 128 ) );
                        const f64 grab = 192 + std::max( 1.0, distance * 0.15 ) * 0.7;
                        camera->Drag( { grab, 0, 128 }, { grab + 32, 0, 128 } );
                    } else { const f64 grab = OnXHandle( s, { 192, 0, 128 } ); s.Drag( grab, 0, grab + 32, 0 ); }
                    REQUIRE( s.Steps() == steps + 1 );
                    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
                        const auto &vertex = before.value.vertices.pData[i]; auto expected = vertex.position;
                        if ( expected.x > 0 && expected.z == 128 ) { expected.x += 32; }
                        CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), expected );
                    }
                    edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
                    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
                    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
                    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
                    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
                }
            }
        }
    }
}

TEST_CASE( "Modified component body clicks preserve the current root when no component is hit", "[map][gui][tool-gestures][component-selection-workflow][component-body-modifiers]" )
{
    for ( const bool perspective : { false, true } ) {
        for ( const bool edges : { false, true } ) {
            for ( const auto modifiers : { Qt::KeyboardModifiers( Qt::ShiftModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ),
                                           Qt::KeyboardModifiers( Qt::MetaModifier ), Qt::KeyboardModifiers( Qt::ControlModifier | Qt::ShiftModifier ) } ) {
                DYNAMIC_SECTION( "camera=" << perspective << " edges=" << edges << " modifiers=" << static_cast<int>( modifiers ) ) {
                    session_t s;
                    const u64 a = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
                    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
                    const u64 b = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
                    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
                    s.Frame( { -384, -192, -64 }, { 384, 192, 256 } );
                    std::unique_ptr<camera_t> camera;
                    if ( perspective ) { camera = std::make_unique<camera_t>( s ); MapCameraView_LookAt( camera->view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); }
                    s.Tool( edges ? "map.select_mode.edges" : "map.select_mode.vertices" ); s.Tool( "map.tool.select" );
                    MapWorkspace_Select( &s.workspace, a, MAP_SELECT_REPLACE );
                    REQUIRE( s.Selected() == a ); REQUIRE( s.workspace.meshSelection.vertices.nCount == 0 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
                    const auto owner = s.workspace.meshSelection.meshId.value, revision = s.workspace.selection.revision;
                    const auto columnRevision = s.workspace.meshVertexColumnRevision;
                    const int columnAxis = s.workspace.meshVertexColumnAxis; const auto seed = s.workspace.selectedMeshEdgeSeed;
                    const auto *document = s.workspace.pDocument; const auto geometryRevision = document->geometry.revision;
                    const usize steps = s.Steps();
                    edge_workflow_mesh_t beforeA( *document, a ), beforeB( *document, b );
                    QWidget *view = perspective ? camera->view.get() : s.top.get();
                    // The middle of the top face is well away from every authored
                    // vertex and edge. The final plain click is a positive body-hit control.
                    const QPointF point = perspective ? camera->At( { 192, 16, 128 } ) : s.At( 192, 16 );
                    const auto send = [&]( QEvent::Type type, Qt::KeyboardModifiers keys ) {
                        QMouseEvent event( type, point, view->mapToGlobal( point ), Qt::LeftButton,
                            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, keys );
                        QCoreApplication::sendEvent( view, &event );
                    };
                    const auto unchanged = [&]() {
                        CHECK( s.Selected() == a ); CHECK( s.workspace.meshSelection.meshId.value == owner );
                        CHECK( s.workspace.meshSelection.vertices.nCount == 0 ); CHECK( s.workspace.meshSelection.edges.nCount == 0 );
                        CHECK( s.workspace.selection.revision == revision );
                        CHECK( s.workspace.meshVertexColumnAxis == columnAxis ); CHECK( s.workspace.meshVertexColumnRevision == columnRevision );
                        CHECK( s.workspace.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( s.workspace.selectedMeshEdgeSeed.b.value == seed.b.value );
                        CHECK( s.workspace.pDocument == document ); CHECK( s.workspace.pDocument->geometry.revision == geometryRevision ); CHECK( s.Steps() == steps );
                    };
                    send( QEvent::MouseButtonPress, modifiers ); unchanged();
                    send( QEvent::MouseButtonRelease, modifiers ); unchanged();
                    send( QEvent::MouseButtonPress, Qt::NoModifier ); send( QEvent::MouseButtonRelease, Qt::NoModifier );
                    CHECK( s.Selected() == b ); CHECK( s.workspace.meshSelection.vertices.nCount == 0 ); CHECK( s.workspace.meshSelection.edges.nCount == 0 );
                    CHECK( s.workspace.pDocument == document ); CHECK( s.workspace.pDocument->geometry.revision == geometryRevision ); CHECK( s.Steps() == steps );
                    edge_workflow_mesh_t afterA( *document, a ), afterB( *document, b );
                    CHECK( geo::MeshSourceDescription_Equal( &beforeA.value, &afterA.value ) );
                    CHECK( geo::MeshSourceDescription_Equal( &beforeB.value, &afterB.value ) );
                }
            }
        }
    }
}

TEST_CASE( "Modified component lassos cannot replace or clear a selection on another mesh", "[map][gui][tool-gestures][component-selection-workflow][lasso-owner]" )
{
    for ( const bool edges : { false, true } ) {
        for ( const auto modifiers : { Qt::KeyboardModifiers( Qt::ShiftModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ) } ) {
            DYNAMIC_SECTION( "edges=" << edges << " modifiers=" << static_cast<int>( modifiers ) ) {
                session_t s;
                const u64 a = EdgeWorkflowQuad( s, { { -256, -64, 0 }, { -128, 64, 0 } } );
                const u64 b = EdgeWorkflowQuad( s, { { 128, -64, 0 }, { 256, 64, 0 } } );
                s.Tool( edges ? "map.select_mode.edges" : "map.select_mode.vertices" ); s.Tool( "map.tool.select" );
                s.Click( -256, edges ? 0 : -64 ); REQUIRE( s.workspace.meshSelection.meshId.value == a );
                const auto revision = s.workspace.selection.revision, columnRevision = s.workspace.meshVertexColumnRevision;
                const int columnAxis = s.workspace.meshVertexColumnAxis; const auto seed = s.workspace.selectedMeshEdgeSeed;
                const usize steps = s.Steps(), count = edges ? s.workspace.meshSelection.edges.nCount : s.workspace.meshSelection.vertices.nCount;
                REQUIRE( count != 0 ); edge_workflow_mesh_t beforeA( *s.workspace.pDocument, a ), beforeB( *s.workspace.pDocument, b );
                s.Drag( 96, -96, 288, 96, modifiers );
                CHECK( s.workspace.meshSelection.meshId.value == a ); CHECK( s.Selected() == a );
                CHECK( s.workspace.selection.revision == revision );
                CHECK( ( edges ? s.workspace.meshSelection.edges.nCount : s.workspace.meshSelection.vertices.nCount ) == count );
                CHECK( s.workspace.meshVertexColumnRevision == columnRevision ); CHECK( s.workspace.meshVertexColumnAxis == columnAxis );
                CHECK( s.workspace.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( s.workspace.selectedMeshEdgeSeed.b.value == seed.b.value );
                CHECK( s.Steps() == steps ); CheckOriginalVertices( s, a, beforeA ); CheckOriginalVertices( s, b, beforeB );
            }
        }
    }
}

TEST_CASE( "Modified vertex presses and drags cannot steal another mesh or convert a foreign brush", "[map][gui][tool-gestures][component-selection-workflow][vertex-owner]" )
{
    for ( const bool perspective : { false, true } ) {
        for ( const bool brush : { false, true } ) {
            for ( const auto modifiers : { Qt::KeyboardModifiers( Qt::ShiftModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ) } ) {
                DYNAMIC_SECTION( "camera=" << perspective << " brush=" << brush << " modifiers=" << static_cast<int>( modifiers ) ) {
                    session_t s; const u64 a = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
                    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
                    const u64 b = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
                    if ( !brush ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) ); }
                    s.Frame( { -384, -192, -64 }, { 384, 192, 256 } );
                    std::unique_ptr<camera_t> camera;
                    if ( perspective ) { camera = std::make_unique<camera_t>( s ); MapCameraView_LookAt( camera->view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); }
                    const auto at = [&]( math::vec3d_t world ) { return perspective ? camera->At( world ) : s.At( world.x, world.y ); };
                    const auto send = [&]( QEvent::Type type, QPointF point, Qt::KeyboardModifiers keys ) {
                        QWidget *view = perspective ? camera->view.get() : s.top.get();
                        QMouseEvent event( type, point, view->mapToGlobal( point ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, keys );
                        QCoreApplication::sendEvent( view, &event );
                    };
                    s.Tool( "map.select_mode.vertices" ); s.Tool( "map.tool.select" );
                    const QPointF first = at( { -256, -64, 128 } );
                    send( QEvent::MouseButtonPress, first, Qt::NoModifier ); send( QEvent::MouseButtonRelease, first, Qt::NoModifier );
                    REQUIRE( s.workspace.meshSelection.meshId.value == a );
                    const usize count = s.workspace.meshSelection.vertices.nCount; REQUIRE( count == ( perspective ? 1u : 2u ) );
                    std::vector<u64> vertices;
                    for ( usize i = 0; i < count; ++i ) { vertices.push_back( s.workspace.meshSelection.vertices.pData[i].value ); }
                    const auto revision = s.workspace.selection.revision, columnRevision = s.workspace.meshVertexColumnRevision;
                    const int columnAxis = s.workspace.meshVertexColumnAxis; const auto *document = s.workspace.pDocument;
                    const auto geometryRevision = document->geometry.revision; const usize steps = s.Steps();
                    edge_workflow_mesh_t before( *document, a );
                    const auto unchanged = [&]() {
                        CHECK( s.workspace.meshSelection.meshId.value == a ); CHECK( s.Selected() == a );
                        CHECK( s.workspace.selection.revision == revision ); CHECK( s.workspace.meshSelection.vertices.nCount == count );
                        for ( usize i = 0; i < std::min( count, s.workspace.meshSelection.vertices.nCount ); ++i ) { CHECK( s.workspace.meshSelection.vertices.pData[i].value == vertices[i] ); }
                        CHECK( s.workspace.meshVertexColumnAxis == columnAxis ); CHECK( s.workspace.meshVertexColumnRevision == columnRevision );
                        CHECK( s.workspace.pDocument == document ); CHECK( s.workspace.pDocument->geometry.revision == geometryRevision );
                        CHECK( KindOf( s.workspace, b ) == ( brush ? map_wire_kind_t::BRUSH : map_wire_kind_t::MESH ) ); CHECK( s.Steps() == steps );
                    };
                    const QPointF from = at( { 128, -64, 128 } ), to = at( { 160, -64, 128 } );
                    send( QEvent::MouseButtonPress, from, modifiers ); unchanged();
                    send( QEvent::MouseMove, ( from + to ) * 0.5, modifiers ); send( QEvent::MouseMove, to, modifiers );
                    send( QEvent::MouseButtonRelease, to, modifiers ); unchanged(); CheckOriginalVertices( s, a, before );
                }
            }
        }
    }
}

TEST_CASE( "Same-mesh component lassos union and subtract without changing owner on empty subtraction", "[map][gui][tool-gestures][component-selection-workflow][lasso-modifiers]" )
{
    for ( const bool edges : { false, true } ) {
        DYNAMIC_SECTION( "edges=" << edges ) {
            session_t s; const u64 a = EdgeWorkflowQuad( s, { { -256, -64, 0 }, { -128, 64, 0 } } );
            EdgeWorkflowQuad( s, { { 128, -64, 0 }, { 256, 64, 0 } } );
            s.Tool( edges ? "map.select_mode.edges" : "map.select_mode.vertices" ); s.Tool( "map.tool.select" );
            s.Click( -256, edges ? 0 : -64 ); REQUIRE( s.workspace.meshSelection.meshId.value == a );
            const usize steps = s.Steps();
            const auto count = [&]() { return edges ? s.workspace.meshSelection.edges.nCount : s.workspace.meshSelection.vertices.nCount; };
            s.Drag( -288, -96, -96, 96, Qt::ShiftModifier ); REQUIRE( count() == 4 ); CHECK( s.workspace.meshSelection.meshId.value == a );
            s.Drag( -288, -96, -224, 96, Qt::MetaModifier ); REQUIRE( count() == ( edges ? 3u : 2u ) );
            s.Drag( -288, -96, -224, 96, Qt::ShiftModifier ); REQUIRE( count() == 4 );
            s.Drag( -288, -96, -224, 96, Qt::ControlModifier | Qt::ShiftModifier ); REQUIRE( count() == ( edges ? 3u : 2u ) );
            s.Drag( -288, -96, -224, 96, Qt::ShiftModifier ); REQUIRE( count() == 4 );
            const auto revision = s.workspace.selection.revision, columnRevision = s.workspace.meshVertexColumnRevision;
            const int columnAxis = s.workspace.meshVertexColumnAxis; const auto seed = s.workspace.selectedMeshEdgeSeed;
            s.Drag( -80, 128, 80, 192, Qt::ControlModifier ); // No eligible components.
            CHECK( s.workspace.selection.revision == revision ); CHECK( count() == 4 ); CHECK( s.Selected() == a );
            CHECK( s.workspace.meshVertexColumnRevision == columnRevision ); CHECK( s.workspace.meshVertexColumnAxis == columnAxis );
            CHECK( s.workspace.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( s.workspace.selectedMeshEdgeSeed.b.value == seed.b.value );
            s.Drag( -288, -96, -96, 96, Qt::ControlModifier ); REQUIRE( count() == 0 );
            const auto emptyRevision = s.workspace.selection.revision;
            const u64 emptyRoot = s.Selected();
            s.Drag( 96, -96, 288, 96, Qt::ControlModifier ); // Empty component selection cannot acquire a new owner by subtraction.
            CHECK( count() == 0 ); CHECK( s.workspace.selection.revision == emptyRevision ); CHECK( s.Selected() == emptyRoot );
            CHECK( s.Steps() == steps );
        }
    }
}

TEST_CASE( "An edge lasso ranks complete edges rather than unrelated vertices in its rectangle", "[map][gui][tool-gestures][component-selection-workflow][lasso-eligibility]" )
{
    session_t s; const u64 a = EdgeWorkflowQuad( s );
    struct fixture_t { u64 mesh; std::vector<math::vec3d_t> points; std::vector<std::vector<u32>> faces; } fixture{ a, {}, {} };
    for ( u32 i = 0; i < 5; ++i ) {
        const f64 y = -64 + 32.0 * i; const u32 first = static_cast<u32>( fixture.points.size() );
        fixture.points.push_back( { -80, y, 0 } ); fixture.points.push_back( { -192, y + 10, 0 } ); fixture.points.push_back( { -192, y - 10, 0 } );
        fixture.faces.push_back( { first, first + 1, first + 2 } );
    }
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Create partial-edge lasso fixture", []( void *context, map_document_t *document, vector_t<u64> *selection ) {
        const auto &value = *static_cast<fixture_t *>( context );
        if ( !Vector_PushBack( selection, value.mesh ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapCreate_ReplaceMeshPolygons( document, value.mesh, value.points, value.faces, StringView_FromCString( "materials/dev/dev_grid.cymat" ), 128.0 );
    }, &fixture ) );
    const u64 b = EdgeWorkflowQuad( s, { { 0, -64, 0 }, { 64, 64, 0 } } );
    s.Frame( { -256, -160, -64 }, { 160, 160, 64 } );
    const usize steps = s.Steps();
    for ( int repeat = 0; repeat < 2; ++repeat ) {
        CAPTURE( repeat ); MapWorkspace_Select( &s.workspace, a, MAP_SELECT_REPLACE );
        s.Tool( "map.select_mode.edges" ); s.Tool( "map.tool.select" );
        // A has five vertices inside but no complete edge. B has four vertices
        // and four complete edges. A's selected root must not hide B's eligibility.
        s.Drag( -96, -96, 96, 96 );
        CHECK( s.workspace.meshSelection.meshId.value == b ); CHECK( s.Selected() == b ); CHECK( s.workspace.meshSelection.edges.nCount == 4 );
        CHECK( s.Steps() == steps );
    }
}

TEST_CASE( "Modified face clicks preserve the current mesh when another mesh or brush is hit", "[map][gui][tool-gestures][component-selection-workflow][face-owner]" )
{
    for ( const int variant : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "variant=" << variant ) {
            const bool perspective = variant == 1, brush = variant == 2;
            const auto modifiers = variant == 1 ? Qt::ControlModifier : Qt::ShiftModifier;
            session_t s; const u64 a = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
            REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
            const u64 b = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } );
            if ( !brush ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) ); }
            s.Frame( { -384, -192, -64 }, { 384, 192, 256 } );
            std::unique_ptr<camera_t> camera;
            if ( perspective ) { camera = std::make_unique<camera_t>( s ); MapCameraView_LookAt( camera->view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); }
            const auto click = [&]( math::vec3d_t world, Qt::KeyboardModifiers keys ) {
                QWidget *view = perspective ? camera->view.get() : s.top.get(); const QPointF point = perspective ? camera->At( world ) : s.At( world.x, world.y );
                QMouseEvent press( QEvent::MouseButtonPress, point, view->mapToGlobal( point ), Qt::LeftButton, Qt::LeftButton, keys ); QCoreApplication::sendEvent( view, &press );
                QMouseEvent release( QEvent::MouseButtonRelease, point, view->mapToGlobal( point ), Qt::LeftButton, Qt::NoButton, keys ); QCoreApplication::sendEvent( view, &release );
            };
            s.Tool( "map.select_mode.faces" ); s.Tool( "map.tool.select" ); click( { -192, 32, 128 }, Qt::NoModifier );
            REQUIRE( s.workspace.selectedMeshFaceObject == a ); const u64 face = s.workspace.selectedMeshFaceId;
            s.Tool( "map.tool.translate" ); const auto revision = s.workspace.selection.revision; const usize steps = s.Steps();
            click( { 192, 32, 128 }, modifiers );
            CHECK( s.workspace.selectedMeshFaceObject == a ); CHECK( s.workspace.selectedMeshFaceId == face );
            CHECK( MapWorkspace_MeshFaces( &s.workspace ).nCount == 1 ); CHECK( s.Selected() == a ); CHECK( s.workspace.selection.revision == revision );
            CHECK( s.workspace.tool == map_tool_t::TRANSLATE ); CHECK( s.Steps() == steps );
            CHECK( KindOf( s.workspace, b ) == ( brush ? map_wire_kind_t::BRUSH : map_wire_kind_t::MESH ) );
        }
    }
}

TEST_CASE( "A failed face-add click preserves the active face and cannot fall back to replacing it", "[map][gui][tool-gestures][component-selection-workflow][face-add-allocation]" )
{
    session_t s; const u64 left = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const u64 right = AddBox( s.workspace, { 128, -64, 0 }, { 256, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    const u64 roots[]{ left, right }; MapWorkspace_SetSelection( &s.workspace, roots, 2 ); s.Tool( "map.mesh.merge" ); const u64 mesh = s.Selected();
    s.Tool( "map.select_mode.faces" ); s.Tool( "map.tool.select" ); s.Click( -192, 32 );
    REQUIRE( s.workspace.selectedMeshFaceObject == mesh ); const u64 active = s.workspace.selectedMeshFaceId;
    const auto revision = s.workspace.selection.revision, setOwner = s.workspace.meshFaceSetObject;
    const auto *rootData = s.workspace.selection.ids.pData, *faceData = s.workspace.meshFaceSet.pData;
    const usize count = s.workspace.meshFaceSet.nCount, steps = s.Steps();
    edge_gui_allocation_t audit{ 0, 1, 0 }; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    { edge_gui_allocator_scope_t scope( s.gui, &allocator ); s.Click( 192, 32, Qt::ShiftModifier ); }
    CHECK( audit.calls > 0 ); CHECK( audit.live == 0 );
    CHECK( s.workspace.selectedMeshFaceObject == mesh ); CHECK( s.workspace.selectedMeshFaceId == active );
    CHECK( s.workspace.meshFaceSetObject == setOwner ); CHECK( s.workspace.meshFaceSet.pData == faceData ); CHECK( s.workspace.meshFaceSet.nCount == count );
    CHECK( s.workspace.selection.ids.pData == rootData ); CHECK( s.Selected() == mesh ); CHECK( s.workspace.selection.revision == revision ); CHECK( s.Steps() == steps );
    s.Click( 192, 32, Qt::ShiftModifier ); REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == 2 );
    CHECK( MapWorkspace_IsMeshFaceSelected( &s.workspace, mesh, active ) ); CHECK( s.Steps() == steps );
}

TEST_CASE( "A failed component lasso preserves its complete selection and can be retried", "[map][gui][tool-gestures][component-selection-workflow][lasso-allocation]" )
{
    // The injected allocator outlives every fixture, including the successful
    // control whose published selection owns memory from this allocator.
    edge_gui_allocation_t audit{};
    const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    const auto prepare = []( session_t &s ) {
        const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
        s.Click( 256, 0 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
        REQUIRE( s.workspace.meshSelection.meshId.value == mesh ); return mesh;
    };
    const auto begin = []( session_t &s ) {
        s.Mouse( QEvent::MouseButtonPress, s.At( -288, -160 ), Qt::LeftButton, Qt::ShiftModifier );
        s.Mouse( QEvent::MouseMove, s.At( 0, 0 ), Qt::LeftButton, Qt::ShiftModifier );
        s.Mouse( QEvent::MouseMove, s.At( 288, 160 ), Qt::LeftButton, Qt::ShiftModifier );
    };
    const auto release = []( session_t &s ) {
        s.Mouse( QEvent::MouseButtonRelease, s.At( 288, 160 ), Qt::NoButton, Qt::ShiftModifier );
    };
    usize successfulCalls{};
    {
        session_t s; const u64 mesh = prepare( s ); const usize steps = s.Steps(); begin( s );
        { edge_gui_allocator_scope_t scope( s.gui, &allocator ); release( s ); }
        REQUIRE( s.workspace.meshSelection.edges.nCount == 4 ); CHECK( s.Selected() == mesh ); CHECK( s.Steps() == steps );
        successfulCalls = audit.calls; REQUIRE( successfulCalls >= 3 );
    }
    REQUIRE( audit.live == 0 );
    // Candidate collection, union scratch, and final publication must all
    // fail without partially replacing the selection. Inject on release only.
    for ( const usize failure : { usize{ 1 }, successfulCalls / 2, successfulCalls } ) {
        DYNAMIC_SECTION( "allocation=" << failure ) {
            audit.calls = 0; audit.failOn = failure;
            {
                session_t s; const u64 mesh = prepare( s ); edge_workflow_mesh_t original( *s.workspace.pDocument, mesh );
                const auto edge = OnlySelectedEdge( s ), seed = s.workspace.selectedMeshEdgeSeed;
                const auto *rootData = s.workspace.selection.ids.pData; const auto *document = s.workspace.pDocument;
                const auto *edgeData = s.workspace.meshSelection.edges.pData;
                const auto revision = s.workspace.selection.revision, geometryRevision = document->geometry.revision;
                const auto columnRevision = s.workspace.meshVertexColumnRevision; const int columnAxis = s.workspace.meshVertexColumnAxis;
                const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( s.workspace.history.pUndo );
                begin( s );
                { edge_gui_allocator_scope_t scope( s.gui, &allocator ); release( s ); }
                CHECK( audit.calls >= failure ); CHECK( audit.live == 0 );
                CHECK( s.workspace.pDocument == document ); CHECK( document->geometry.revision == geometryRevision );
                CHECK( s.workspace.selection.ids.pData == rootData ); CHECK( s.Selected() == mesh ); CHECK( s.workspace.selection.revision == revision );
                CHECK( s.workspace.meshSelection.meshId.value == mesh ); CHECK( s.workspace.meshSelection.edges.pData == edgeData );
                REQUIRE( s.workspace.meshSelection.edges.nCount == 1 );
                CHECK( OnlySelectedEdge( s ).a.value == edge.a.value ); CHECK( OnlySelectedEdge( s ).b.value == edge.b.value );
                CHECK( s.workspace.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( s.workspace.selectedMeshEdgeSeed.b.value == seed.b.value );
                CHECK( s.workspace.meshVertexColumnAxis == columnAxis ); CHECK( s.workspace.meshVertexColumnRevision == columnRevision );
                CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.workspace.history.pUndo ) ) );
                CheckOriginalVertices( s, mesh, original );
                begin( s ); release( s ); // Fresh gesture with the normal allocator.
                REQUIRE( s.workspace.meshSelection.edges.nCount == 4 ); CHECK( s.Selected() == mesh ); CHECK( s.Steps() == steps );
            }
            CHECK( audit.live == 0 );
        }
    }
}

TEST_CASE( "An equally eligible plain component lasso chooses the lower persistent mesh ID", "[map][gui][tool-gestures][component-selection-workflow][lasso-tie]" )
{
    session_t s; const u64 a = EdgeWorkflowQuad( s, { { -256, -64, 0 }, { -128, 64, 0 } } );
    const u64 b = EdgeWorkflowQuad( s, { { 128, -64, 0 }, { 256, 64, 0 } } );
    const usize steps = s.Steps();
    for ( int repeat = 0; repeat < 2; ++repeat ) {
        CAPTURE( repeat ); MapWorkspace_Select( &s.workspace, 0, MAP_SELECT_REPLACE );
        REQUIRE( s.workspace.selection.ids.nCount == 0 ); REQUIRE( s.workspace.meshSelection.edges.nCount == 0 );
        s.Drag( -288, -96, 288, 96 );
        CHECK( s.Selected() == std::min( a, b ) ); CHECK( s.workspace.meshSelection.meshId.value == std::min( a, b ) );
        CHECK( s.workspace.meshSelection.edges.nCount == 4 ); CHECK( s.Steps() == steps );
    }
}

namespace
{
u64 ExtrudeTopFace( const map_workspace_t &ws, u64 mesh )
{
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &face = ws.wire.faces.pData[i];
        if ( face.id == mesh && face.normal.z > 0.99 ) { return face.faceId; }
    }
    FAIL( "Fixture must have a top mesh face" ); return 0;
}
void CheckExtrudeRetry( session_t &s, camera_t &camera, u64 mesh, const edge_workflow_mesh_t &before )
{
    const usize steps = EditorHistory_AppliedStepCount( &s.workspace.history );
    map_uv_face_t capBefore{}; const map_surface_ref_t cap{ mesh, ExtrudeTopFace( s.workspace, mesh ) };
    REQUIRE( MapTextureEdit_Describe( s.workspace.pDocument, cap, &capBefore ) == map_status_t::OK );
    camera.Drag( { 0, 0, 128 }, { 0, 0, 192 } );
    REQUIRE( EditorHistory_AppliedStepCount( &s.workspace.history ) == steps + 1 ); REQUIRE( MeshFaces( s.workspace, mesh ) == before.value.faces.nCount + 4 );
    map_uv_face_t capAfter{}; REQUIRE( MapTextureEdit_Describe( s.workspace.pDocument, cap, &capAfter ) == map_status_t::OK );
    CHECK( capAfter.material == capBefore.material );
    const auto bounds = Bounds( s.workspace, mesh );
    CHECK( bounds.minimum.x == Catch::Approx( -64 ) ); CHECK( bounds.maximum.x == Catch::Approx( 64 ) );
    CHECK( bounds.minimum.y == Catch::Approx( -64 ) ); CHECK( bounds.maximum.y == Catch::Approx( 64 ) );
    CHECK( bounds.minimum.z == Catch::Approx( 0 ) ); CHECK( bounds.maximum.z == Catch::Approx( 192 ) );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i];
        if ( vertex.position.z == 0 ) { CheckPosition( Position( s.workspace, mesh, vertex.sourceId ), vertex.position ); }
    }
    edge_workflow_mesh_t after( *s.workspace.pDocument, mesh );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}
void ExtrudeMaterial( map_workspace_t &ws, u64 mesh )
{
    const map_surface_ref_t cap{ mesh, ExtrudeTopFace( ws, mesh ) };
    REQUIRE( MapTextureEdit_SetMaterial( ws.pDocument, { &cap, 1 }, StringView_FromCString( "materials/tests/extrude_capture.cymat" ), nullptr ) == map_status_t::OK );
    REQUIRE( MapTextureEdit_SetRepeat( ws.pDocument, { &cap, 1 }, { 96, 192 } ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws );
}
}

namespace
{
u64 FaceRegionQuad( session_t &s )
{
    const u64 mesh = EdgeWorkflowQuad( s, { { -256, -128, 0 }, { 256, 128, 0 } } );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &s.workspace, mesh, ExtrudeTopFace( s.workspace, mesh ) );
    REQUIRE( MapWorkspace_QuadSliceMeshFace( &s.workspace, 2, 1 ) );
    edge_workflow_mesh_t sliced( *s.workspace.pDocument, mesh );
    REQUIRE( sliced.value.faces.nCount == 2 ); REQUIRE( sliced.value.vertices.nCount == 6 );
    return mesh;
}
math::vec3d_t FaceRegionCenter( const edge_workflow_mesh_t &source, usize index )
{
    const auto &face = source.value.faces.pData[index]; math::vec3d_t center{};
    for ( u32 i = 0; i < face.cCorners; ++i ) {
        const auto &p = source.value.vertices.pData[source.value.corners.pData[face.iFirstCorner + i].iVertex].position;
        center.x += p.x; center.y += p.y; center.z += p.z;
    }
    return { center.x / face.cCorners, center.y / face.cCorners, center.z / face.cCorners };
}
void FaceRegionPick( session_t &s, camera_t &camera, u64 mesh, const edge_workflow_mesh_t &source )
{
    s.Tool( "map.tool.select" ); MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    for ( usize i = 0; i < source.value.faces.nCount; ++i ) {
        const QPointF at = camera.At( FaceRegionCenter( source, i ) );
        const auto modifiers = i == 0 ? Qt::NoModifier : Qt::ShiftModifier;
        QMouseEvent press( QEvent::MouseButtonPress, at, camera.view->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, modifiers );
        QCoreApplication::sendEvent( camera.view.get(), &press );
        QMouseEvent release( QEvent::MouseButtonRelease, at, camera.view->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::sendEvent( camera.view.get(), &release );
        REQUIRE( MapWorkspace_MeshFaces( &s.workspace ).nCount == i + 1 );
        REQUIRE( MapWorkspace_IsMeshFaceSelected( &s.workspace, mesh, source.value.faces.pData[i].sourceId.value ) );
    }
    s.Tool( "map.tool.extrude" );
}
} // namespace

TEST_CASE( "The camera Extrude tool moves a connected face region with one shared boundary", "[map][gui][tool-gestures][face-region-gesture]" )
{
    f64 distance = 32;
    SECTION( "Pull makes two caps and six outer walls without an internal seam wall" ) { distance = 32; }
    SECTION( "Push moves each of the six shared vertices once" ) { distance = -16; }
    session_t s; auto &ws = s.workspace; const u64 mesh = FaceRegionQuad( s );
    u64 owner = 0;
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "regions" ), StringView_FromCString( "Regions" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, mesh, StringView_FromCString( "regions" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "regions" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, mesh, owner ) == map_status_t::OK );
    std::vector<map_surface_ref_t> caps;
    { edge_workflow_mesh_t source( *ws.pDocument, mesh ); for ( usize i = 0; i < source.value.faces.nCount; ++i ) { caps.push_back( { mesh, source.value.faces.pData[i].sourceId.value } ); } }
    for ( usize i = 0; i < caps.size(); ++i ) {
        const auto material = StringView_FromCString( i == 0 ? "materials/tests/region_left.cymat" : "materials/tests/region_right.cymat" );
        REQUIRE( MapTextureEdit_SetMaterial( ws.pDocument, { &caps[i], 1 }, material, nullptr ) == map_status_t::OK );
        REQUIRE( MapTextureEdit_SetRepeat( ws.pDocument, { &caps[i], 1 }, { 96, 192 } ) == map_status_t::OK );
    }
    MapWorkspace_DocumentChanged( &ws );
    edge_workflow_mesh_t before( *ws.pDocument, mesh );
    std::vector<map_uv_face_t> capMetadata( caps.size() );
    for ( usize i = 0; i < caps.size(); ++i ) { REQUIRE( MapTextureEdit_Describe( ws.pDocument, caps[i], &capMetadata[i] ) == map_status_t::OK ); }
    const auto checkMetadata = [&]( const map_document_t &doc, f64 offset ) {
        const map_geometry_record_t *placement = nullptr;
        for ( usize i = 0; i < doc.geometryRecords.nCount; ++i ) { if ( doc.geometryRecords.pData[i].id == mesh ) { placement = &doc.geometryRecords.pData[i]; } }
        REQUIRE( placement ); CHECK( placement->owner == owner ); REQUIRE( placement->iLayer < doc.layers.nCount );
        CHECK( std::string( doc.layers.pData[placement->iLayer].id ) == "regions" );
        for ( usize i = 0; i < caps.size(); ++i ) {
            map_uv_face_t face{}; REQUIRE( MapTextureEdit_Describe( &doc, caps[i], &face ) == map_status_t::OK );
            CHECK( face.material == capMetadata[i].material ); REQUIRE( face.uvs.size() == capMetadata[i].uvs.size() );
            REQUIRE( face.corners.size() == capMetadata[i].corners.size() ); REQUIRE( face.corners.size() == face.uvs.size() );
            // MeshSource_TryDescribe starts each ring at its lowest vertex ID.
            // New cap IDs can rotate that start; UVs must follow the translated
            // authored corner, not the unrelated array index after canonicalisation.
            std::vector<bool> visited( face.corners.size(), false );
            for ( usize j = 0; j < capMetadata[i].corners.size(); ++j ) {
                auto expected = capMetadata[i].corners[j]; expected.z += offset;
                usize match = face.corners.size(), matches = 0;
                for ( usize k = 0; k < face.corners.size(); ++k ) {
                    const auto actual = face.corners[k];
                    if ( std::fabs( actual.x - expected.x ) < 1e-6 && std::fabs( actual.y - expected.y ) < 1e-6 &&
                         std::fabs( actual.z - expected.z ) < 1e-6 ) { match = k; ++matches; }
                }
                CAPTURE( caps[i].face, j, offset, matches ); REQUIRE( matches == 1 ); REQUIRE_FALSE( visited[match] ); visited[match] = true;
                CHECK( face.uvs[match].x == Catch::Approx( capMetadata[i].uvs[j].x ).margin( 1e-9 ) );
                CHECK( face.uvs[match].y == Catch::Approx( capMetadata[i].uvs[j].y ).margin( 1e-9 ) );
            }
            CHECK( std::all_of( visited.begin(), visited.end(), []( bool value ) { return value; } ) );
        }
    };
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -800, 600 }, { 0, 0, 0 } ); QCoreApplication::processEvents();
    FaceRegionPick( s, camera, mesh, before );
    const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto nextId = ws.pDocument->nextId, revision = ws.pDocument->geometry.revision;
    const auto from = FaceRegionCenter( before, 0 ); auto to = from; to.z += distance;
    camera.Send( QEvent::MouseButtonPress, camera.At( from ), Qt::LeftButton );
    camera.Send( QEvent::MouseMove, camera.At( to ), Qt::LeftButton );
    CHECK( s.Steps() == steps ); CHECK( ws.pDocument->nextId == nextId ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    { edge_workflow_mesh_t previewSource( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &previewSource.value ) ); }
    camera.Send( QEvent::MouseButtonRelease, camera.At( to ), Qt::NoButton );
    REQUIRE( s.Steps() == steps + 1 ); edge_workflow_mesh_t after( *ws.pDocument, mesh );
    REQUIRE( MapWorkspace_MeshFaces( &ws ).nCount == 2 );
    for ( const auto cap : caps ) { CHECK( MapWorkspace_IsMeshFaceSelected( &ws, mesh, cap.face ) ); }
    if ( distance > 0 ) {
        REQUIRE( after.value.faces.nCount == 8 ); REQUIRE( after.value.vertices.nCount == 12 ); CheckOriginalVertices( s, mesh, before );
        std::vector<std::pair<u64, u64>> originalEdges, boundary;
        for ( usize i = 0; i < before.value.faces.nCount; ++i ) {
            const auto &face = before.value.faces.pData[i];
            for ( u32 j = 0; j < face.cCorners; ++j ) {
                u64 a = before.value.vertices.pData[before.value.corners.pData[face.iFirstCorner + j].iVertex].sourceId.value;
                u64 b = before.value.vertices.pData[before.value.corners.pData[face.iFirstCorner + ( j + 1 ) % face.cCorners].iVertex].sourceId.value;
                if ( b < a ) { std::swap( a, b ); } originalEdges.push_back( { a, b } );
            }
        }
        std::sort( originalEdges.begin(), originalEdges.end() );
        for ( usize i = 0; i < originalEdges.size(); ) {
            usize end = i + 1; while ( end < originalEdges.size() && originalEdges[end] == originalEdges[i] ) { ++end; }
            if ( end - i == 1 ) { boundary.push_back( originalEdges[i] ); } else { CHECK( end - i == 2 ); } i = end;
        }
        REQUIRE( boundary.size() == 6 ); std::vector<usize> walls( boundary.size(), 0 );
        for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
            const auto &face = after.value.faces.pData[i]; REQUIRE( face.cCorners == 4 );
            const bool cap = face.sourceId.value == caps[0].face || face.sourceId.value == caps[1].face;
            std::vector<u64> base; usize raised = 0;
            for ( u32 j = 0; j < face.cCorners; ++j ) {
                const auto &vertex = after.value.vertices.pData[after.value.corners.pData[face.iFirstCorner + j].iVertex];
                if ( std::fabs( vertex.position.z ) < 1e-6 ) { base.push_back( vertex.sourceId.value ); }
                else { CHECK( vertex.position.z == Catch::Approx( 32 ).margin( 1e-6 ) ); ++raised; }
            }
            if ( cap ) { CHECK( base.empty() ); CHECK( raised == 4 ); }
            else {
                REQUIRE( base.size() == 2 ); CHECK( raised == 2 ); std::sort( base.begin(), base.end() );
                const auto edge = std::make_pair( base[0], base[1] ); const auto found = std::find( boundary.begin(), boundary.end(), edge );
                REQUIRE( found != boundary.end() ); ++walls[static_cast<usize>( found - boundary.begin() )];
            }
        }
        for ( usize count : walls ) { CHECK( count == 1 ); }
    } else {
        REQUIRE( after.value.faces.nCount == 2 ); REQUIRE( after.value.vertices.nCount == 6 );
        for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
            const auto &v = before.value.vertices.pData[i]; auto expected = v.position; expected.z -= 16;
            CheckPosition( Position( ws, mesh, v.sourceId ), expected );
        }
    }
    checkMetadata( *ws.pDocument, distance );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 2 ); checkMetadata( *ws.pDocument, 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
    checkMetadata( *ws.pDocument, distance );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "extruded-region.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
    edge_workflow_mesh_t reopened( *loaded.workspace.pDocument, mesh ); REQUIRE( reopened.value.faces.nCount == after.value.faces.nCount );
    for ( usize i = 0; i < after.value.faces.nCount; ++i ) {
        const auto &a = after.value.faces.pData[i]; auto &b = reopened.value.faces.pData[i]; REQUIRE( a.sourceId.value == b.sourceId.value );
        REQUIRE( StringView_Equals( MapMaterials_Path( &ws.pDocument->materials, a.attributes.material.value ), MapMaterials_Path( &loaded.workspace.pDocument->materials, b.attributes.material.value ) ) );
        b.attributes.material = a.attributes.material;
    }
    CHECK( geo::MeshSourceDescription_Equal( &after.value, &reopened.value ) ); checkMetadata( *loaded.workspace.pDocument, distance );
}

TEST_CASE( "A cancelled connected-region extrusion discards its preview and can be retried", "[map][gui][tool-gestures][face-region-gesture]" )
{
    session_t s; auto &ws = s.workspace; const u64 mesh = FaceRegionQuad( s ); edge_workflow_mesh_t before( *ws.pDocument, mesh );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -800, 600 }, { 0, 0, 0 } ); QCoreApplication::processEvents();
    FaceRegionPick( s, camera, mesh, before ); const auto from = FaceRegionCenter( before, 0 ); auto to = from; to.z += 32;
    const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    camera.Send( QEvent::MouseButtonPress, camera.At( from ), Qt::LeftButton ); camera.Send( QEvent::MouseMove, camera.At( to ), Qt::LeftButton );
    QFocusEvent lost( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.view.get(), &lost );
    camera.Send( QEvent::MouseMove, camera.At( to ), Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, camera.At( to ), Qt::NoButton );
    CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 2 );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    camera.Drag( from, to ); REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( ws, mesh ) == 8 );
}

TEST_CASE( "Extrude refuses disconnected or noncoplanar face selections as a whole", "[map][gui][tool-gestures][face-region-gesture]" )
{
    for ( const bool disconnected : { false, true } ) {
        CAPTURE( disconnected ); session_t s; auto &ws = s.workspace; const u64 mesh = EdgeWorkflowQuad( s );
        std::vector<math::vec3d_t> points;
        std::vector<std::vector<u32>> faces;
        if ( disconnected ) {
            points = { { -256,-64,0 },{ -128,-64,0 },{ -128,64,0 },{ -256,64,0 },{ 128,-64,0 },{ 256,-64,0 },{ 256,64,0 },{ 128,64,0 } };
            faces = { { 0,1,2,3 },{ 4,5,6,7 } };
        } else {
            points = { { -128,-64,0 },{ 0,-64,0 },{ 0,64,0 },{ -128,64,0 },{ 128,-64,64 },{ 128,64,64 } };
            faces = { { 0,1,2,3 },{ 1,4,5,2 } };
        }
        REQUIRE( MapCreate_ReplaceMeshPolygons( ws.pDocument, mesh, points, faces, StringView_FromCString( "materials/dev/dev_grid.cymat" ), 128 ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws ); edge_workflow_mesh_t before( *ws.pDocument, mesh );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -800, 600 }, { 0, 0, 0 } ); QCoreApplication::processEvents();
        FaceRegionPick( s, camera, mesh, before );
        const usize steps = s.Steps(); const auto revision = ws.pDocument->geometry.revision, selectionRevision = ws.selection.revision, nextId = ws.pDocument->nextId;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto *document = ws.pDocument;
        const auto from = FaceRegionCenter( before, 0 ); auto to = from; to.z += 32;
        camera.Drag( from, to );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 2 );
        CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
        CHECK_FALSE( MapNotice_Last( &ws ).isEmpty() );
    }
}

TEST_CASE( "Region Extrude preparation and release allocation failures preserve the selected caps", "[map][gui][tool-gestures][face-region-gesture][allocation]" )
{
    for ( const bool release : { false, true } ) {
        DYNAMIC_SECTION( "document allocation fails during " << ( release ? "release" : "press preparation" ) ) {
            // The allocator outlives the workspace, even if a regression
            // unexpectedly publishes storage through the failing operation.
            edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
            {
                session_t s; auto &ws = s.workspace; const u64 mesh = FaceRegionQuad( s ); edge_workflow_mesh_t before( *ws.pDocument, mesh );
                camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -800, 600 }, { 0, 0, 0 } ); QCoreApplication::processEvents();
                FaceRegionPick( s, camera, mesh, before );
                const auto from = FaceRegionCenter( before, 0 ); auto to = from; to.z += 32;
                const QPointF start = camera.At( from ), finish = camera.At( to );
                const auto *document = ws.pDocument; const auto geometryRevision = document->geometry.revision, nextId = document->nextId;
                const auto sourceNext = document->geometry.sourceIds.allocator.next.value, selectionRevision = ws.selection.revision;
                const auto activeFace = ws.selectedMeshFaceId, activeMesh = ws.selectedMeshFaceObject, faceOwner = ws.meshFaceSetObject;
                const auto *roots = ws.selection.ids.pData, *faces = ws.meshFaceSet.pData; const usize rootCount = ws.selection.ids.nCount;
                const usize faceCount = ws.meshFaceSet.nCount, steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo );
                const auto *wirePoints = ws.wire.points.pData;
                if ( release ) {
                    camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton );
                    const QImage pressed = camera.view->grab().toImage();
                    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); REQUIRE( camera.view->grab().toImage() != pressed );
                }
                {
                    struct document_allocator_scope_t {
                        map_workspace_t &workspace; map_document_t *document; const allocator_t *previous;
                        document_allocator_scope_t( map_workspace_t &w, const allocator_t *a ) : workspace( w ), document( w.pDocument ), previous( w.pDocument->pAllocator ) { document->pAllocator = a; }
                        ~document_allocator_scope_t() { if ( workspace.pDocument == document ) { document->pAllocator = previous; } }
                    } scope( ws, &allocator );
                    audit.failOn = 1;
                    camera.Send( release ? QEvent::MouseButtonRelease : QEvent::MouseButtonPress,
                        release ? finish : start, release ? Qt::NoButton : Qt::LeftButton );
                }
                CHECK( audit.calls > 0 ); CHECK( audit.live == 0 );
                map_notice_t kind{}; ( void )MapNotice_Last( &ws, &kind ); CHECK( kind == map_notice_t::ERROR );
                const auto checkUnchanged = [&]() {
                    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == geometryRevision ); CHECK( ws.pDocument->nextId == nextId );
                    CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext ); CHECK( ws.wire.points.pData == wirePoints );
                    CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == rootCount ); CHECK( s.Selected() == mesh );
                    CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.meshFaceSet.pData == faces ); CHECK( ws.meshFaceSet.nCount == faceCount );
                    CHECK( ws.meshFaceSetObject == faceOwner ); CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
                    for ( usize i = 0; i < before.value.faces.nCount; ++i ) { CHECK( MapWorkspace_IsMeshFaceSelected( &ws, mesh, before.value.faces.pData[i].sourceId.value ) ); }
                    CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
                    edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
                };
                checkUnchanged();
                camera.Send( QEvent::MouseMove, finish, Qt::LeftButton );
                camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
                camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
                checkUnchanged();
                camera.Drag( from, to ); REQUIRE( s.Steps() == steps + 1 );
                edge_workflow_mesh_t extruded( *ws.pDocument, mesh ); CHECK( extruded.value.faces.nCount == 8 ); CHECK( extruded.value.vertices.nCount == 12 );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
            }
            CHECK( audit.live == 0 );
        }
    }
}

namespace
{
void HollowCurveSetting( session_t &s, const char *path, const char *text )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}
void HollowCurveNumber( session_t &s, const char *path, f64 number )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = descriptor->type; value.flValue = number; value.nValue = static_cast<i64>( number );
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}
void HollowCurveClosed( session_t &s, bool closed )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.curve_closed" ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = closed ? CY_TRUE : CY_FALSE;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}
void HollowCurveBegin( session_t &s )
{
    HollowCurveSetting( s, "editor.map.curve_shape", "hollow_pipe" ); HollowCurveSetting( s, "editor.map.curve_output", "mesh" );
    HollowCurveSetting( s, "editor.map.default_material", "materials/tests/hollow_pipe.cymat" );
    HollowCurveNumber( s, "editor.map.cylinder_sides", 8 ); HollowCurveNumber( s, "editor.map.curve_outer_radius", 16 );
    HollowCurveNumber( s, "editor.map.curve_wall_thickness", 4 ); HollowCurveClosed( s, false );
    REQUIRE( EditorSettings_Integer( &s.gui.settings, "editor.map.cylinder_sides", 0 ) == 8 );
    s.Tool( "map.tool.curve" ); s.Click( -256, 0 ); s.Click( 0, 0 ); s.Click( 0, 256 );
}
}

TEST_CASE( "Curve builds an editable hollow pipe through a clicked bend in one history step", "[map][gui][tool-gestures][hollow-sweep-gesture]" )
{
    session_t s; auto &ws = s.workspace;
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId;
    const usize steps = s.Steps(); HollowCurveBegin( s );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
    CHECK( ws.wire.objects.nCount == 0 ); CHECK( s.Steps() == steps );
    s.Key( Qt::Key_Return ); REQUIRE( s.Steps() == steps + 1 ); const u64 mesh = s.Selected(); REQUIRE( mesh != 0 );
    REQUIRE( KindOf( ws, mesh ) == map_wire_kind_t::MESH ); edge_workflow_mesh_t pipe( *ws.pDocument, mesh );
    REQUIRE( pipe.value.faces.nCount > 32 ); CHECK( pipe.value.faces.nCount <= 2048 );
    std::vector<std::pair<u64, u64>> edges; usize startInner = 0, startOuter = 0, endInner = 0, endOuter = 0, startRims = 0, endRims = 0;
    bool finiteVertices = true;
    for ( usize i = 0; i < pipe.value.vertices.nCount; ++i ) {
        const auto p = pipe.value.vertices.pData[i].position;
        finiteVertices &= std::isfinite( p.x ) && std::isfinite( p.y ) && std::isfinite( p.z );
        if ( std::fabs( p.x + 256 ) < 1e-6 ) {
            const f64 radius = std::hypot( p.y, p.z ); CHECK( ( std::fabs( radius - 16 ) < 1e-6 || std::fabs( radius - 12 ) < 1e-6 ) );
            startInner += std::fabs( radius - 12 ) < 1e-6; startOuter += std::fabs( radius - 16 ) < 1e-6;
        }
        if ( std::fabs( p.y - 256 ) < 1e-6 ) {
            const f64 radius = std::hypot( p.x, p.z ); CHECK( ( std::fabs( radius - 16 ) < 1e-6 || std::fabs( radius - 12 ) < 1e-6 ) );
            endInner += std::fabs( radius - 12 ) < 1e-6; endOuter += std::fabs( radius - 16 ) < 1e-6;
        }
    }
    REQUIRE( finiteVertices ); CHECK( startInner == 8 ); CHECK( startOuter == 8 ); CHECK( endInner == 8 ); CHECK( endOuter == 8 );
    bool allQuads = true, materialPreserved = true, validUvCharts = true;
    for ( usize f = 0; f < pipe.value.faces.nCount; ++f ) {
        const auto &face = pipe.value.faces.pData[f]; allQuads &= face.cCorners == 4;
        materialPreserved &= StringView_Equals( MapMaterials_Path( &ws.pDocument->materials, face.attributes.material.value ), StringView_FromCString( "materials/tests/hollow_pipe.cymat" ) );
        bool atStart = true, atEnd = true; f64 uv0Area = 0, uv1Area = 0;
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &a = pipe.value.corners.pData[face.iFirstCorner + k], &b = pipe.value.corners.pData[face.iFirstCorner + ( k + 1 ) % face.cCorners];
            const auto &va = pipe.value.vertices.pData[a.iVertex], &vb = pipe.value.vertices.pData[b.iVertex];
            atStart &= std::fabs( va.position.x + 256 ) < 1e-6; atEnd &= std::fabs( va.position.y - 256 ) < 1e-6;
            edges.push_back( std::minmax( va.sourceId.value, vb.sourceId.value ) );
            validUvCharts &= std::isfinite( a.attributes.uv0.x ) && std::isfinite( a.attributes.uv0.y ) &&
                             std::isfinite( a.attributes.uv1.x ) && std::isfinite( a.attributes.uv1.y );
            uv0Area += a.attributes.uv0.x * b.attributes.uv0.y - a.attributes.uv0.y * b.attributes.uv0.x;
            uv1Area += a.attributes.uv1.x * b.attributes.uv1.y - a.attributes.uv1.y * b.attributes.uv1.x;
        }
        validUvCharts &= std::fabs( uv0Area ) > 1e-12 && std::fabs( uv1Area ) > 1e-12; startRims += atStart; endRims += atEnd;
    }
    CHECK( allQuads ); CHECK( materialPreserved ); CHECK( validUvCharts );
    // Annular end quads close the wall thickness; there is no disk over either
    // bore. Every wall/rim edge belongs to exactly two faces.
    CHECK( startRims == 8 ); CHECK( endRims == 8 ); std::sort( edges.begin(), edges.end() );
    bool manifold = true;
    for ( usize i = 0; i < edges.size(); ) { usize end = i + 1; while ( end < edges.size() && edges[end] == edges[i] ) { ++end; } manifold &= end - i == 2; i = end; }
    CHECK( manifold );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.objects.nCount == 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &pipe.value, &restored.value ) ); }
    const usize committed = s.Steps(); s.Key( Qt::Key_Return ); CHECK( s.Steps() == committed ); // The finished path was consumed.
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "bent-hollow-pipe.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); session_t loaded;
    REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK ); edge_workflow_mesh_t reopened( *loaded.workspace.pDocument, mesh );
    REQUIRE( reopened.value.faces.nCount == pipe.value.faces.nCount );
    bool matchingIds = true, matchingMaterials = true;
    for ( usize i = 0; i < pipe.value.faces.nCount; ++i ) {
        auto &face = reopened.value.faces.pData[i]; const auto &original = pipe.value.faces.pData[i]; matchingIds &= face.sourceId.value == original.sourceId.value;
        matchingMaterials &= StringView_Equals( MapMaterials_Path( &loaded.workspace.pDocument->materials, face.attributes.material.value ), MapMaterials_Path( &ws.pDocument->materials, original.attributes.material.value ) );
        face.attributes.material = original.attributes.material;
    }
    REQUIRE( matchingIds ); REQUIRE( matchingMaterials );
    CHECK( geo::MeshSourceDescription_Equal( &pipe.value, &reopened.value ) );
}

TEST_CASE( "Unsupported or tight hollow-pipe paths remain staged for correction", "[map][gui][tool-gestures][hollow-sweep-gesture]" )
{
    for ( int invalid : { 0, 1, 2, 3 } ) {
        DYNAMIC_SECTION( "invalid pipe setting = " << invalid ) {
            session_t s; auto &ws = s.workspace; HollowCurveBegin( s );
            if ( invalid == 0 ) { HollowCurveClosed( s, true ); }
            else if ( invalid == 1 ) { HollowCurveSetting( s, "editor.map.curve_output", "patch" ); }
            else if ( invalid == 2 ) { HollowCurveNumber( s, "editor.map.curve_wall_thickness", 16 ); }
            else { HollowCurveNumber( s, "editor.map.curve_outer_radius", 1024 ); }
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId;
            const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = s.Steps(); s.Key( Qt::Key_Return );
            // A failed construction still consumes Confirm; it must not
            // escape to a second user binding with the chain still staged.
            map_tool_event_t confirm{}; confirm.kind = map_tool_event_kind_t::CONFIRM;
            confirm.view.pView = s.top.get(); confirm.view.bOrtho = true;
            CHECK( MapToolGestures_Event( &ws, confirm ) );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
            CHECK( ws.wire.objects.nCount == 0 ); CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            CHECK_FALSE( MapNotice_Last( &ws ).isEmpty() );
            HollowCurveClosed( s, false ); HollowCurveSetting( s, "editor.map.curve_output", "mesh" );
            HollowCurveNumber( s, "editor.map.curve_outer_radius", 16 ); HollowCurveNumber( s, "editor.map.curve_wall_thickness", 4 );
            s.Key( Qt::Key_Return ); REQUIRE( s.Steps() == steps + 1 ); REQUIRE( s.Selected() != 0 ); CHECK( KindOf( ws, s.Selected() ) == map_wire_kind_t::MESH );
        }
    }
}

TEST_CASE( "Hollow-pipe confirmation allocation failure preserves the path for retry or Cancel", "[map][gui][tool-gestures][hollow-sweep-gesture][allocation]" )
{
    for ( bool cancel : { false, true } ) {
        DYNAMIC_SECTION( "cancel after refusal = " << cancel ) {
            edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
            {
                session_t s; auto &ws = s.workspace; HollowCurveBegin( s );
                const auto *document = ws.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId, selectionRevision = ws.selection.revision;
                const usize steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo ); audit.failOn = 1;
                { edge_gui_allocator_scope_t scope( s.gui, &allocator ); s.Key( Qt::Key_Return ); }
                CHECK( audit.calls > 0 ); CHECK( audit.live == 0 ); CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
                CHECK( ws.pDocument->nextId == nextId ); CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.wire.objects.nCount == 0 );
                CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
                map_notice_t kind{}; ( void )MapNotice_Last( &ws, &kind ); CHECK( kind == map_notice_t::ERROR );
                if ( cancel ) { s.Key( Qt::Key_Escape ); s.Key( Qt::Key_Return ); CHECK( s.Steps() == steps ); CHECK( ws.wire.objects.nCount == 0 ); HollowCurveBegin( s ); }
                s.Key( Qt::Key_Return ); REQUIRE( s.Steps() == steps + 1 ); CHECK( KindOf( ws, s.Selected() ) == map_wire_kind_t::MESH );
            }
            CHECK( audit.live == 0 );
        }
    }
}

TEST_CASE( "Extrude refuses a locked camera face without borrowing a foreign face selection", "[map][gui][tool-gestures][extrude-capture-policy][locked-press]" )
{
    for ( const bool foreignFace : { false, true } ) {
        DYNAMIC_SECTION( "foreign face selected = " << foreignFace ) {
            session_t s; auto &ws = s.workspace;
            const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
            s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
            const u64 other = AddBox( ws, { 256, -64, 0 }, { 384, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
            REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } );
            s.Tool( "map.tool.extrude" );
            if ( foreignFace ) { MapWorkspace_SelectMeshFace( &ws, other, ExtrudeTopFace( ws, other ) ); }
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
            const auto *roots = ws.selection.ids.pData, *faces = ws.meshFaceSet.pData;
            const usize rootCount = ws.selection.ids.nCount, faceCount = ws.meshFaceSet.nCount, steps = s.Steps();
            const u64 activeMesh = ws.selectedMeshFaceObject, activeFace = ws.selectedMeshFaceId;
            const auto token = UndoRedo_StateToken( ws.history.pUndo );
            edge_workflow_mesh_t before( *document, mesh ), otherBefore( *document, other );
            camera.Drag( { 0, 0, 128 }, { 0, 0, 192 } );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( s.Steps() == steps );
            CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == rootCount ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( ws.meshFaceSet.pData == faces ); CHECK( ws.meshFaceSet.nCount == faceCount );
            CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
            { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
            { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
            REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); CheckExtrudeRetry( s, camera, mesh, before );
            { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
        }
    }
}

TEST_CASE( "Lock then unlock cancels a dedicated Extrude drag before late release", "[map][gui][tool-gestures][extrude-capture-policy][lock-cancel]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.extrude" );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = s.Steps();
    const QPointF start = camera.At( { 0, 0, 128 } ), finish = camera.At( { 0, 0, 192 } );
    camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton );
    const QImage pressed = camera.view->grab().toImage();
    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton );
    REQUIRE( camera.view->grab().toImage() != pressed ); // The drag reached its visible face preview.
    REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); REQUIRE( MapLayers_SetLocked( &ws, layer, false ) );
    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); REQUIRE( s.Steps() == steps );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    CheckExtrudeRetry( s, camera, mesh, before );
}

TEST_CASE( "Dedicated Extrude captures are retired by view tool mode and document changes", "[map][gui][tool-gestures][extrude-capture-policy][interruption]" )
{
    for ( const int reason : { 0, 1, 2, 3, 4, 5 } ) {
        DYNAMIC_SECTION( "interruption = " << reason ) {
            session_t s; auto &ws = s.workspace;
            const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
            ExtrudeMaterial( ws, mesh );
            // Undo below replaces the document but keeps the target mesh at
            // exactly the same position and with the same source IDs.
            if ( reason == 5 ) { ( void )AddBox( ws, { 320, -32, 0 }, { 384, 32, 64 } ); MapWorkspace_Select( &ws, mesh, MAP_SELECT_REPLACE ); }
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.extrude" );
            edge_workflow_mesh_t before( *ws.pDocument, mesh );
            const QPointF start = camera.At( { 0, 0, 128 } ), finish = camera.At( { 0, 0, 192 } );
            camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton );
            const QImage pressed = camera.view->grab().toImage();
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); REQUIRE( camera.view->grab().toImage() != pressed );
            if ( reason == 0 ) { QFocusEvent event( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.view.get(), &event ); }
            else if ( reason == 1 ) { camera.view->resize( 840, 620 ); }
            else if ( reason == 2 ) { MapCameraView_LookAt( camera.view.get(), { 0, -700, 800 }, { 0, 0, 64 } ); }
            else if ( reason == 3 ) { MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE ); }
            else if ( reason == 4 ) { const auto mode = ws.elementMode; MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); MapWorkspace_SetElementMode( &ws, mode ); }
            else { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); }
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
            const usize steps = s.Steps(), applied = EditorHistory_AppliedStepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
            const auto activeMesh = ws.selectedMeshFaceObject, activeFace = ws.selectedMeshFaceId;
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
            camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); // A duplicate native release is harmless as well.
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); REQUIRE( s.Steps() == steps );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
            { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
            CheckExtrudeRetry( s, camera, mesh, before );
        }
    }
}

TEST_CASE( "Only the camera that captured Extrude may update or release its drag", "[map][gui][tool-gestures][extrude-capture-policy][owner]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); ExtrudeMaterial( ws, mesh );
    camera_t owner( s ), other( s );
    MapCameraView_LookAt( owner.view.get(), { 0, -600, 700 }, { 0, 0, 64 } );
    MapCameraView_LookAt( other.view.get(), { 600, 0, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.extrude" );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ); const usize steps = s.Steps();
    const QPointF start = owner.At( { 0, 0, 128 } ), finish = owner.At( { 0, 0, 192 } );
    owner.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); const QImage pressed = owner.view->grab().toImage();
    owner.Send( QEvent::MouseMove, finish, Qt::LeftButton ); REQUIRE( owner.view->grab().toImage() != pressed );
    other.view->resize( 840, 620 ); QFocusEvent leave( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( other.view.get(), &leave );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    other.Send( QEvent::MouseMove, other.At( { 0, 0, 256 } ), Qt::LeftButton );
    other.Send( QEvent::MouseButtonRelease, other.At( { 0, 0, 256 } ), Qt::NoButton );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); REQUIRE( s.Steps() == steps );
    owner.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    REQUIRE( s.Steps() == steps + 1 ); CHECK( MeshFaces( ws, mesh ) == before.value.faces.nCount + 4 );
    CHECK( Bounds( ws, mesh ).maximum.z == Catch::Approx( 192 ) ); CHECK( Bounds( ws, mesh ).minimum.z == Catch::Approx( 0 ) );
    edge_workflow_mesh_t after( *ws.pDocument, mesh );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t restored( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}

TEST_CASE( "Shift Extrude cannot replace a face selection belonging to another mesh", "[map][gui][tool-gestures][extrude-capture-policy][selection-owner]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); ExtrudeMaterial( ws, mesh );
    const u64 other = AddBox( ws, { 256, -64, 0 }, { 384, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.extrude" );
    MapWorkspace_SelectMeshFace( &ws, other, ExtrudeTopFace( ws, other ) );
    edge_workflow_mesh_t before( *ws.pDocument, mesh ), otherBefore( *ws.pDocument, other );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const auto *roots = ws.selection.ids.pData, *faces = ws.meshFaceSet.pData;
    const usize rootCount = ws.selection.ids.nCount, faceCount = ws.meshFaceSet.nCount, steps = s.Steps();
    const u64 activeMesh = ws.selectedMeshFaceObject, activeFace = ws.selectedMeshFaceId; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const QPointF start = camera.At( { 0, 0, 128 } ), finish = camera.At( { 0, 0, 192 } );
    const auto send = [&]( QEvent::Type type, QPointF at ) {
        QMouseEvent event( type, at, camera.view->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::ShiftModifier );
        QCoreApplication::sendEvent( camera.view.get(), &event );
    };
    send( QEvent::MouseButtonPress, start ); send( QEvent::MouseMove, finish ); send( QEvent::MouseButtonRelease, finish );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); REQUIRE( s.Steps() == steps );
    CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == rootCount ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( ws.meshFaceSet.pData == faces ); CHECK( ws.meshFaceSet.nCount == faceCount ); CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
    CheckExtrudeRetry( s, camera, mesh, before );
    { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
}

TEST_CASE( "Extrude allocation refusal preserves the face context and leaves no captured edit", "[map][gui][tool-gestures][extrude-capture-policy][allocation]" )
{
    for ( const bool failRelease : { false, true } ) {
        DYNAMIC_SECTION( "failure during " << ( failRelease ? "release" : "press" ) ) {
            // A wrongly published buffer must still have a live allocator
            // during fixture destruction, even when a REQUIRE fails below.
            edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
            {
                session_t s; auto &ws = s.workspace;
                const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); ExtrudeMaterial( ws, mesh );
                const u64 other = AddBox( ws, { 256, -64, 0 }, { 384, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
                camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.extrude" );
                MapWorkspace_SelectMeshFace( &ws, other, ExtrudeTopFace( ws, other ) );
                const QPointF start = camera.At( { 0, 0, 128 } ), finish = camera.At( { 0, 0, 192 } );
                if ( failRelease ) {
                    camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); const QImage pressed = camera.view->grab().toImage();
                    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); REQUIRE( camera.view->grab().toImage() != pressed );
                }
                edge_workflow_mesh_t before( *ws.pDocument, mesh ), otherBefore( *ws.pDocument, other );
                const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
                const auto *roots = ws.selection.ids.pData, *faces = ws.meshFaceSet.pData;
                const usize rootCount = ws.selection.ids.nCount, faceCount = ws.meshFaceSet.nCount, steps = s.Steps();
                const u64 activeMesh = ws.selectedMeshFaceObject, activeFace = ws.selectedMeshFaceId, editSerial = ws.lastEdit.serial;
                const auto token = UndoRedo_StateToken( ws.history.pUndo );
                {
                    edge_gui_allocator_scope_t scope( s.gui, &allocator ); audit.failOn = 1;
                    camera.Send( failRelease ? QEvent::MouseButtonRelease : QEvent::MouseButtonPress,
                        failRelease ? finish : start, failRelease ? Qt::NoButton : Qt::LeftButton );
                }
                REQUIRE( audit.calls > 0 ); CHECK( audit.live == 0 );
                if ( failRelease ) {
                    CHECK( ws.lastEdit.serial == editSerial + 1 ); CHECK( ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
                    CHECK( ws.lastEdit.outcome == map_edit_outcome_t::FAILED );
                    const QString notice = MapNotice_Last( &ws );
                    CHECK( notice.contains( QStringLiteral( "memory" ), Qt::CaseInsensitive ) );
                    CHECK_FALSE( notice.contains( QStringLiteral( "fold" ), Qt::CaseInsensitive ) );
                }
                camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
                CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); REQUIRE( s.Steps() == steps );
                CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == rootCount ); CHECK( ws.selection.revision == selectionRevision );
                CHECK( ws.meshFaceSet.pData == faces ); CHECK( ws.meshFaceSet.nCount == faceCount ); CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
                CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
                { edge_workflow_mesh_t unchanged( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
                { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
                CheckExtrudeRetry( s, camera, mesh, before );
                { edge_workflow_mesh_t unchanged( *ws.pDocument, other ); CHECK( geo::MeshSourceDescription_Equal( &otherBefore.value, &unchanged.value ) ); }
            }
            CHECK( audit.live == 0 );
        }
    }
}

namespace
{
struct paint_brush_source_t {
    geo::brush_source_t source{};
    paint_brush_source_t( const map_document_t &document, u64 id )
    {
        REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &document.geometry, { id }, Allocator_GetSystem(), &source ) == geo::geometry_status_t::OK );
    }
    ~paint_brush_source_t() { geo::BrushSource_Shutdown( &source ); }
    void Check( const map_document_t &document, u64 id ) const
    {
        const auto *brush = geo::GeometryDocument_FindBrush( &document.geometry, { id } ); CHECK( brush != nullptr );
        if ( brush == nullptr ) { return; }
        paint_brush_source_t current( document, id ); CHECK( geo::BrushSource_Equal( &source, &current.source ) );
    }
};
u32 PaintTopTint( const map_workspace_t &ws, u64 id )
{
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &face = ws.wire.faces.pData[i];
        if ( face.id == id && face.normal.z > 0.99 ) { return face.colorRgba; }
    }
    FAIL( "Paint fixture has no top face" ); return 0;
}
void CheckFreshBrushPaint( session_t &s, camera_t &camera, u64 brush, const paint_brush_source_t &before )
{
    const usize steps = EditorHistory_AppliedStepCount( &s.workspace.history );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH );
    camera.Drag( { -60, -60, 128 }, { 60, 60, 128 } );
    REQUIRE( EditorHistory_AppliedStepCount( &s.workspace.history ) == steps + 1 );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::MESH ); CHECK( MeshFaces( s.workspace, brush ) == 6 );
    CHECK( PaintTopTint( s.workspace, brush ) != 0xFFFFFFFFu );
    const auto bounds = Bounds( s.workspace, brush );
    CHECK( bounds.minimum.x == Catch::Approx( -64 ) ); CHECK( bounds.maximum.x == Catch::Approx( 64 ) );
    CHECK( bounds.minimum.y == Catch::Approx( -64 ) ); CHECK( bounds.maximum.y == Catch::Approx( 64 ) );
    CHECK( bounds.minimum.z == Catch::Approx( 0 ) ); CHECK( bounds.maximum.z == Catch::Approx( 128 ) );
    edge_workflow_mesh_t painted( *s.workspace.pDocument, brush );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH ); before.Check( *s.workspace.pDocument, brush );
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    edge_workflow_mesh_t restored( *s.workspace.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &painted.value, &restored.value ) );
}
}

TEST_CASE( "Paint cannot convert or tint a locked brush or mesh from the camera", "[map][gui][tool-gestures][paint-capture-policy][locked]" )
{
    for ( const bool mesh : { false, true } ) {
        DYNAMIC_SECTION( "target is mesh = " << mesh ) {
            session_t s; auto &ws = s.workspace;
            const u64 target = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } );
            std::unique_ptr<paint_brush_source_t> brushBefore;
            std::unique_ptr<edge_workflow_mesh_t> meshBefore;
            if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
            s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
            REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
            if ( mesh ) { meshBefore = std::make_unique<edge_workflow_mesh_t>( *ws.pDocument, target ); }
            else { brushBefore = std::make_unique<paint_brush_source_t>( *ws.pDocument, target ); }
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId;
            const auto sourceNext = document->geometry.sourceIds.allocator.next.value, selectionRevision = ws.selection.revision;
            const auto *roots = ws.selection.ids.pData; const usize count = ws.selection.ids.nCount, steps = s.Steps(); const auto token = UndoRedo_StateToken( ws.history.pUndo );
            const auto unchanged = [&]() {
                CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
                CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext ); CHECK( s.Steps() == steps );
                CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
                CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == count ); CHECK( ws.selection.revision == selectionRevision );
                CHECK( KindOf( ws, target ) == ( mesh ? map_wire_kind_t::MESH : map_wire_kind_t::BRUSH ) );
                if ( mesh ) { edge_workflow_mesh_t current( *ws.pDocument, target ); CHECK( geo::MeshSourceDescription_Equal( &meshBefore->value, &current.value ) ); }
                else { brushBefore->Check( *ws.pDocument, target ); }
            };
            const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
            camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); unchanged();
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); unchanged();
            REQUIRE( MapLayers_SetLocked( &ws, layer, false ) );
            if ( !mesh ) { CheckFreshBrushPaint( s, camera, target, *brushBefore ); }
            else {
                const usize retrySteps = EditorHistory_AppliedStepCount( &ws.history ); camera.Drag( { -60, -60, 128 }, { 60, 60, 128 } );
                REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == retrySteps + 1 ); CHECK( PaintTopTint( ws, target ) != 0xFFFFFFFFu );
                edge_workflow_mesh_t painted( *ws.pDocument, target );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                { edge_workflow_mesh_t restored( *ws.pDocument, target ); CHECK( geo::MeshSourceDescription_Equal( &meshBefore->value, &restored.value ) ); }
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                { edge_workflow_mesh_t restored( *ws.pDocument, target ); CHECK( geo::MeshSourceDescription_Equal( &painted.value, &restored.value ) ); }
            }
        }
    }
}

TEST_CASE( "Cancelling a camera Paint stroke leaves an unconverted brush and no late edit", "[map][gui][tool-gestures][paint-capture-policy][cancel]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
    paint_brush_source_t before( *ws.pDocument, brush ); const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision, nextId = document->nextId, sourceNext = document->geometry.sourceIds.allocator.next.value;
    const auto selectionRevision = ws.selection.revision; const auto *roots = ws.selection.ids.pData;
    const usize steps = s.Steps(), count = ws.selection.ids.nCount; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto unchanged = [&]() {
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
        CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext ); CHECK( s.Steps() == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == count ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( KindOf( ws, brush ) == map_wire_kind_t::BRUSH ); before.Check( *ws.pDocument, brush );
    };
    const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
    camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); unchanged();
    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); unchanged();
    QFocusEvent leave( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.view.get(), &leave ); unchanged();
    camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); unchanged();
    CheckFreshBrushPaint( s, camera, brush, before );
}

namespace
{
struct paint_snapshot_t {
    const map_document_t *document{};
    u64 revision{}, next{}, sourceNext{}, selectionRevision{}, activeMesh{}, activeFace{};
    const u64 *roots{};
    usize rootCount{}, steps{}, applied{};
    undo_state_token_t token{};
    u64 object{};
    std::unique_ptr<paint_brush_source_t> brush;
    std::unique_ptr<edge_workflow_mesh_t> mesh;
    paint_snapshot_t( const map_workspace_t &ws, u64 id ) : document( ws.pDocument ), revision( document->geometry.revision ), next( document->nextId ),
        sourceNext( document->geometry.sourceIds.allocator.next.value ), selectionRevision( ws.selection.revision ),
        activeMesh( ws.selectedMeshFaceObject ), activeFace( ws.selectedMeshFaceId ), roots( ws.selection.ids.pData ), rootCount( ws.selection.ids.nCount ),
        steps( EditorHistory_StepCount( &ws.history ) ), applied( EditorHistory_AppliedStepCount( &ws.history ) ), token( UndoRedo_StateToken( ws.history.pUndo ) ), object( id )
    {
        if ( KindOf( ws, id ) == map_wire_kind_t::BRUSH ) { brush = std::make_unique<paint_brush_source_t>( *document, id ); }
        else { mesh = std::make_unique<edge_workflow_mesh_t>( *document, id ); }
    }
    void Check( const map_workspace_t &ws ) const
    {
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == next );
        CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext );
        CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.nCount == rootCount ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( ws.selectedMeshFaceObject == activeMesh ); CHECK( ws.selectedMeshFaceId == activeFace );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        if ( brush ) { brush->Check( *ws.pDocument, object ); }
        else { edge_workflow_mesh_t current( *ws.pDocument, object ); CHECK( geo::MeshSourceDescription_Equal( &mesh->value, &current.value ) ); }
    }
};
void CheckFreshMeshPaint( session_t &s, camera_t &camera, u64 mesh, const edge_workflow_mesh_t &before )
{
    const usize steps = EditorHistory_AppliedStepCount( &s.workspace.history );
    camera.Drag( { -60, -60, 128 }, { 60, 60, 128 } );
    REQUIRE( EditorHistory_AppliedStepCount( &s.workspace.history ) == steps + 1 ); CHECK( PaintTopTint( s.workspace, mesh ) != 0xFFFFFFFFu );
    edge_workflow_mesh_t painted( *s.workspace.pDocument, mesh );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t current( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &current.value ) ); }
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    { edge_workflow_mesh_t current( *s.workspace.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &painted.value, &current.value ) ); }
}
void PaintReal( session_t &s, const char *path, f64 real )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = descriptor->type; value.flValue = real;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}
}

TEST_CASE( "Paint brush strokes are discarded when their view or editing context changes", "[map][gui][tool-gestures][paint-capture-policy][interruption]" )
{
    for ( const int reason : { 0, 1, 2, 3, 4, 5 } ) {
        DYNAMIC_SECTION( "interruption = " << reason ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
            paint_snapshot_t original( ws, brush );
            const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
            camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); original.Check( ws );
            if ( reason == 0 ) { camera.Key( Qt::Key_Escape ); }
            else if ( reason == 1 ) { camera.view->resize( 840, 620 ); }
            else if ( reason == 2 ) { MapCameraView_LookAt( camera.view.get(), { 0, -700, 800 }, { 0, 0, 64 } ); }
            else if ( reason == 3 ) { MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetTool( &ws, map_tool_t::PAINT ); }
            else if ( reason == 4 ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); }
            else { MapWorkspace_Select( &ws, brush, MAP_SELECT_REPLACE ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); }
            paint_snapshot_t interrupted( ws, brush );
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); interrupted.Check( ws );
            s.Tool( "map.tool.paint" ); CheckFreshBrushPaint( s, camera, brush, *original.brush );
        }
    }
}

TEST_CASE( "Paint mesh captures cannot survive layer locking or an Undo document replacement", "[map][gui][tool-gestures][paint-capture-policy][mesh-cancel]" )
{
    for ( const bool undo : { false, true } ) {
        DYNAMIC_SECTION( "Undo replaces document = " << undo ) {
            session_t s; auto &ws = s.workspace;
            const u64 mesh = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
            QString layer;
            if ( undo ) { ( void )AddBox( ws, { 320, -32, 0 }, { 384, 32, 64 } ); }
            else { s.Tool( "map.layer.create" ); layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id ); }
            MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
            paint_snapshot_t original( ws, mesh );
            const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
            camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); original.Check( ws );
            if ( undo ) { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); }
            else { REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); }
            paint_snapshot_t interrupted( ws, mesh );
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); interrupted.Check( ws );
            CheckFreshMeshPaint( s, camera, mesh, *original.mesh );
        }
    }
}

TEST_CASE( "Only the captured camera can add dabs or finish a Paint stroke", "[map][gui][tool-gestures][paint-capture-policy][owner]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t owner( s ), other( s ); MapCameraView_LookAt( owner.view.get(), { 0, -600, 700 }, { 0, 0, 64 } );
    MapCameraView_LookAt( other.view.get(), { 600, 0, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
    paint_snapshot_t before( ws, brush );
    const QPointF start = owner.At( { -60, -60, 128 } ), finish = owner.At( { 60, 60, 128 } );
    owner.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); owner.Send( QEvent::MouseMove, finish, Qt::LeftButton ); before.Check( ws );
    other.view->resize( 840, 620 ); QFocusEvent leave( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( other.view.get(), &leave );
    other.Send( QEvent::MouseMove, other.At( { 60, -60, 128 } ), Qt::LeftButton );
    other.Send( QEvent::MouseButtonRelease, other.At( { 60, -60, 128 } ), Qt::NoButton ); before.Check( ws );
    owner.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    REQUIRE( s.Steps() == before.steps + 1 ); REQUIRE( KindOf( ws, brush ) == map_wire_kind_t::MESH ); CHECK( PaintTopTint( ws, brush ) != 0xFFFFFFFFu );
    edge_workflow_mesh_t painted( *ws.pDocument, brush );
    usize foreignOnlyCorners = 0;
    for ( usize i = 0; i < painted.value.corners.nCount; ++i ) {
        const auto &corner = painted.value.corners.pData[i]; const auto &point = painted.value.vertices.pData[corner.iVertex].position;
        if ( point.x == 64 && point.y == -64 && point.z == 128 ) { ++foreignOnlyCorners; CHECK( corner.attributes.colorRgba == 0xFFFFFFFFu ); }
    }
    REQUIRE( foreignOnlyCorners > 0 ); // Only the foreign pane's rejected dab passed near this corner.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.brush->Check( *ws.pDocument, brush );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    edge_workflow_mesh_t restored( *ws.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &painted.value, &restored.value ) );
}

TEST_CASE( "Ineffective or disabled Paint strokes leave brushes unconverted", "[map][gui][tool-gestures][paint-capture-policy][no-effect]" )
{
    for ( const int kind : { 0, 1, 2, 3 } ) {
        DYNAMIC_SECTION( "stroke = " << kind ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
            paint_snapshot_t before( ws, brush );
            if ( kind == 0 ) { camera.Drag( { -4, -4, 128 }, { 4, 4, 128 } ); } // No vertices lie within the default radius.
            else if ( kind == 1 || kind == 2 ) {
                camera.DragHolding( { -60, -60, 128 }, { 60, 60, 128 }, kind == 1 ? Qt::ControlModifier : Qt::MetaModifier ); // White is already erased.
            } else {
                const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
                camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); camera.Send( QEvent::MouseMove, finish, Qt::LeftButton );
                EdgePickConversion( s, false ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
            }
            before.Check( ws ); EdgePickConversion( s, true ); CheckFreshBrushPaint( s, camera, brush, *before.brush );
        }
    }
}

TEST_CASE( "Paint release allocation failure preserves the unconverted brush for retry", "[map][gui][tool-gestures][paint-capture-policy][allocation]" )
{
    edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    {
        session_t s; auto &ws = s.workspace;
        const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
        paint_snapshot_t before( ws, brush );
        const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
        camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); before.Check( ws );
        {
            edge_gui_allocator_scope_t scope( s.gui, &allocator ); audit.failOn = 1;
            camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton );
        }
        REQUIRE( audit.calls > 0 ); CHECK( audit.live == 0 ); CHECK( ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
        CHECK( MapNotice_Last( &ws ).contains( QStringLiteral( "memory" ), Qt::CaseInsensitive ) ); before.Check( ws );
        camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); before.Check( ws );
        CheckFreshBrushPaint( s, camera, brush, *before.brush );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "A camera Paint stroke preserves an owned brush's layer material and source identity through history and reload", "[map][gui][tool-gestures][paint-capture-policy][metadata]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); u64 owner = 0, side = 0;
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, brush, StringView_FromCString( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { const auto &face = ws.wire.faces.pData[i]; if ( face.id == brush && face.normal.z > 0.99 ) { side = face.sideId; } }
    REQUIRE( side != 0 ); const map_surface_ref_t surface{ brush, side };
    REQUIRE( MapTextureEdit_SetMaterial( ws.pDocument, { &surface, 1 }, StringView_FromCString( "materials/tests/paint_owned.cymat" ), nullptr ) == map_status_t::OK );
    REQUIRE( MapTextureEdit_SetRepeat( ws.pDocument, { &surface, 1 }, { 96, 192 } ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
    const auto checkPlacement = [&]( const map_document_t &document ) {
        const map_geometry_record_t *record{};
        for ( usize i = 0; i < document.geometryRecords.nCount; ++i ) { if ( document.geometryRecords.pData[i].id == brush ) { record = &document.geometryRecords.pData[i]; } }
        REQUIRE( record ); CHECK( record->owner == owner ); REQUIRE( record->iLayer < document.layers.nCount );
        CHECK( std::string( document.layers.pData[record->iLayer].id ) == "detail" );
        map_uv_face_t face{}; REQUIRE( MapTextureEdit_Describe( &document, surface, &face ) == map_status_t::OK );
        CHECK( face.material == "materials/tests/paint_owned.cymat" );
    };
    paint_snapshot_t before( ws, brush ); checkPlacement( *ws.pDocument );
    camera.Drag( { -60, -60, 128 }, { 60, 60, 128 } ); REQUIRE( s.Steps() == before.steps + 1 );
    REQUIRE( KindOf( ws, brush ) == map_wire_kind_t::MESH ); checkPlacement( *ws.pDocument );
    edge_workflow_mesh_t painted( *ws.pDocument, brush ); usize colored = 0;
    for ( usize i = 0; i < painted.value.corners.nCount; ++i ) { colored += painted.value.corners.pData[i].attributes.colorRgba != 0xFFFFFFFFu; }
    CHECK( colored > 0 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( KindOf( ws, brush ) == map_wire_kind_t::BRUSH );
    before.brush->Check( *ws.pDocument, brush ); checkPlacement( *ws.pDocument );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkPlacement( *ws.pDocument );
    { edge_workflow_mesh_t restored( *ws.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &painted.value, &restored.value ) ); }
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_Save( ws.pDocument, &text ) == map_status_t::OK );
    map_document_t loaded; REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { TextBuffer_Data( &text ), TextBuffer_Length( &text ) } ) == map_status_t::OK );
    checkPlacement( loaded ); edge_workflow_mesh_t reloaded( loaded, brush );
    CHECK( geo::MeshSourceDescription_Equal( &painted.value, &reloaded.value ) );
}

TEST_CASE( "Paint captures radius and colour at press while the next stroke uses the new settings", "[map][gui][tool-gestures][paint-capture-policy][settings]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
    const auto color = [&]( const char *text ) {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.paint_color" ) ); REQUIRE( descriptor );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };
    PaintReal( s, "editor.map.paint_radius", 128 ); color( "#ff0000" );
    paint_snapshot_t before( ws, brush ); const QPointF center = camera.At( { 0, 0, 128 } );
    camera.Send( QEvent::MouseButtonPress, center, Qt::LeftButton ); before.Check( ws );
    PaintReal( s, "editor.map.paint_radius", 1 ); color( "#0000ff" );
    camera.Send( QEvent::MouseButtonRelease, center, Qt::NoButton ); REQUIRE( s.Steps() == before.steps + 1 );
    const u32 red = PaintTopTint( ws, brush ); CHECK( ( ( red >> 24u ) & 0xFFu ) > ( ( red >> 8u ) & 0xFFu ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.brush->Check( *ws.pDocument, brush );
    paint_snapshot_t small( ws, brush ); camera.Click( { 0, 0, 128 } ); small.Check( ws );
    PaintReal( s, "editor.map.paint_radius", 128 ); const usize applied = EditorHistory_AppliedStepCount( &ws.history );
    camera.Click( { 0, 0, 128 } ); REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1 );
    const u32 blue = PaintTopTint( ws, brush ); CHECK( ( ( blue >> 8u ) & 0xFFu ) > ( ( blue >> 24u ) & 0xFFu ) );
}

TEST_CASE( "An unselected Paint target cannot regain a retired stroke after temporary unavailability", "[map][gui][tool-gestures][paint-capture-policy][paint-visibility-policy]" )
{
    for ( const int change : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "availability change = " << change ) {
            session_t s; auto &ws = s.workspace;
            const u64 target = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } );
            if ( change == 1 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
            s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
            MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); REQUIRE( ws.selection.ids.nCount == 0 );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.paint" );
            paint_snapshot_t before( ws, target );
            const QPointF start = camera.At( { -60, -60, 128 } ), finish = camera.At( { 60, 60, 128 } );
            camera.Send( QEvent::MouseButtonPress, start, Qt::LeftButton ); camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); before.Check( ws );
            // No input or paint call observes the unavailable intermediate
            // state: its notification must retire the capture immediately.
            if ( change == 2 ) { EdgePickConversion( s, false ); EdgePickConversion( s, true ); }
            else { REQUIRE( MapLayers_SetHidden( &ws, layer, true ) ); REQUIRE( MapLayers_SetHidden( &ws, layer, false ) ); }
            REQUIRE( ws.selection.ids.nCount == 0 ); REQUIRE( ws.selection.revision == before.selectionRevision );
            camera.Send( QEvent::MouseMove, finish, Qt::LeftButton ); camera.Send( QEvent::MouseButtonRelease, finish, Qt::NoButton ); before.Check( ws );
            if ( change == 1 ) { CheckFreshMeshPaint( s, camera, target, *before.mesh ); }
            else { CheckFreshBrushPaint( s, camera, target, *before.brush ); }
        }
    }
}

namespace
{
void CheckLoopCutBox( session_t &s, camera_t &camera, u64 brush, const paint_brush_source_t &before, f64 height = 32, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    const usize steps = EditorHistory_AppliedStepCount( &s.workspace.history );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH );
    if ( modifiers == Qt::NoModifier ) { camera.Click( { 64, -64, height } ); }
    else {
        const QPointF at = camera.At( { 64, -64, 32 } );
        for ( const auto type : { QEvent::MouseButtonPress, QEvent::MouseButtonRelease } ) {
            QMouseEvent event( type, at, camera.view->mapToGlobal( at ), Qt::LeftButton,
                type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, modifiers );
            QCoreApplication::sendEvent( camera.view.get(), &event );
        }
    }
    REQUIRE( EditorHistory_AppliedStepCount( &s.workspace.history ) == steps + 1 );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::MESH ); REQUIRE( MeshFaces( s.workspace, brush ) == 10 );
    edge_workflow_mesh_t cut( *s.workspace.pDocument, brush ); REQUIRE( cut.value.vertices.nCount == 12 );
    usize onCut = 0;
    for ( usize i = 0; i < cut.value.vertices.nCount; ++i ) {
        const auto &point = cut.value.vertices.pData[i].position;
        CHECK( std::abs( point.x ) == Catch::Approx( 64 ) ); CHECK( std::abs( point.y ) == Catch::Approx( 64 ) );
        if ( point.z > 0.001 && point.z < 127.999 ) { ++onCut; CHECK( point.z == Catch::Approx( height ).margin( 1e-5 ) ); }
        else { CHECK( ( point.z == Catch::Approx( 0 ) || point.z == Catch::Approx( 128 ) ) ); }
    }
    CHECK( onCut == 4 );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH ); before.Check( *s.workspace.pDocument, brush );
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    edge_workflow_mesh_t restored( *s.workspace.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) );
}
}

TEST_CASE( "Loop Cut rejects a missed edge or invalid ring without converting the brush", "[map][gui][tool-gestures][loop-cut-atomic-policy][rejection]" )
{
    for ( const int kind : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "rejected click = " << kind ) {
            session_t s; auto &ws = s.workspace; u64 brush = 0;
            if ( kind == 2 ) {
                map_primitive_desc_t wedge{}; wedge.kind = map_primitive_kind_t::WEDGE; wedge.bounds = { { -64, -64, 0 }, { 64, 64, 128 } };
                wedge.wedgeCutAxis = 2; wedge.wedgeSlopeAxis = 0;
                REQUIRE( MapWorkspace_CreatePrimitive( &ws, wedge ) ); brush = s.Selected();
            } else { brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); }
            REQUIRE( KindOf( ws, brush ) == map_wire_kind_t::BRUSH ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
            paint_snapshot_t before( ws, brush );
            // The wedge's front triangle and sloped roof share this edge;
            // it cannot seed a ring consisting only of quadrilateral faces.
            camera.Click( kind == 0 ? math::vec3d_t{ 0, 0, 128 } : kind == 1 ? math::vec3d_t{ 300, 300, 128 } : math::vec3d_t{ 0, -64, 64 } );
            before.Check( ws );
            if ( kind != 2 ) { CheckLoopCutBox( s, camera, brush, *before.brush ); }
        }
    }
}

TEST_CASE( "A camera Loop Cut converts and splits a brush in one undoable operation", "[map][gui][tool-gestures][loop-cut-atomic-policy][success]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
    paint_brush_source_t before( *ws.pDocument, brush ); CheckLoopCutBox( s, camera, brush, before );
}

TEST_CASE( "Loop Cut respects locked targets and disabled brush conversion", "[map][gui][tool-gestures][loop-cut-atomic-policy][eligibility]" )
{
    for ( const int targetKind : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "target = " << targetKind ) {
            session_t s; auto &ws = s.workspace;
            const u64 object = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } );
            if ( targetKind == 1 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
            s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
            if ( targetKind == 2 ) { EdgePickConversion( s, false ); }
            else { REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); }
            MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
            paint_snapshot_t before( ws, object ); camera.Click( { 64, -64, 32 } ); before.Check( ws );
            if ( targetKind == 2 ) { EdgePickConversion( s, true ); }
            else { REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); }
            if ( targetKind != 1 ) { CheckLoopCutBox( s, camera, object, *before.brush ); }
            else {
                const usize steps = s.Steps(); camera.Click( { 64, -64, 32 } ); REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( ws, object ) == 10 );
                edge_workflow_mesh_t cut( *ws.pDocument, object );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                { edge_workflow_mesh_t restored( *ws.pDocument, object ); CHECK( geo::MeshSourceDescription_Equal( &before.mesh->value, &restored.value ) ); }
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                { edge_workflow_mesh_t restored( *ws.pDocument, object ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) ); }
            }
        }
    }
}

TEST_CASE( "Loop Cut supports centered camera clicks and a near-edge Top click on brushes", "[map][gui][tool-gestures][loop-cut-atomic-policy][placement]" )
{
    for ( const int route : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "placement = " << route ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            paint_brush_source_t before( *ws.pDocument, brush );
            if ( route != 2 ) {
                camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
                CheckLoopCutBox( s, camera, brush, before, 64, route == 0 ? Qt::ControlModifier : Qt::MetaModifier );
            } else {
                s.Tool( "map.tool.loop_cut" ); const usize steps = s.Steps();
                s.Click( 0, -63 ); // Just inside the top face, within one world unit of its front edge.
                REQUIRE( s.Steps() == steps + 1 ); REQUIRE( KindOf( ws, brush ) == map_wire_kind_t::MESH ); REQUIRE( MeshFaces( ws, brush ) == 10 );
                edge_workflow_mesh_t cut( *ws.pDocument, brush ); REQUIRE( cut.value.vertices.nCount == 12 ); usize onCut = 0;
                for ( usize i = 0; i < cut.value.vertices.nCount; ++i ) {
                    const auto &p = cut.value.vertices.pData[i].position;
                    if ( std::abs( p.x ) < 63.999 ) { ++onCut; CHECK( p.x == Catch::Approx( 0 ).margin( 1e-5 ) ); }
                }
                CHECK( onCut == 4 );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.Check( *ws.pDocument, brush );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                edge_workflow_mesh_t restored( *ws.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) );
            }
        }
    }
}

TEST_CASE( "Loop Cut allocation refusal preserves the brush before a successful retry", "[map][gui][tool-gestures][loop-cut-atomic-policy][allocation]" )
{
    edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    {
        session_t s; auto &ws = s.workspace;
        const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
        paint_snapshot_t before( ws, brush ); const QPointF at = camera.At( { 64, -64, 32 } );
        camera.Send( QEvent::MouseMove, at, Qt::NoButton );
        {
            edge_gui_allocator_scope_t scope( s.gui, &allocator ); audit.failOn = 1;
            camera.Send( QEvent::MouseButtonPress, at, Qt::LeftButton );
        }
        REQUIRE( audit.calls > 0 ); CHECK( audit.live == 0 ); before.Check( ws );
        CHECK( ws.lastEdit.status == map_status_t::OUT_OF_MEMORY ); CHECK( MapNotice_Last( &ws ).contains( QStringLiteral( "memory" ), Qt::CaseInsensitive ) );
        camera.Send( QEvent::MouseButtonRelease, at, Qt::NoButton ); before.Check( ws );
        CheckLoopCutBox( s, camera, brush, *before.brush );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Atomic camera Loop Cut preserves placement and authored surfaces through history and reload", "[map][gui][tool-gestures][loop-cut-atomic-policy][metadata]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); u64 owner = 0, side = 0;
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, brush, StringView_FromCString( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { const auto &face = ws.wire.faces.pData[i]; if ( face.id == brush && face.normal.z > 0.99 ) { side = face.sideId; } }
    REQUIRE( side != 0 ); const map_surface_ref_t top{ brush, side };
    REQUIRE( MapTextureEdit_SetMaterial( ws.pDocument, { &top, 1 }, StringView_FromCString( "materials/tests/loop_cut_owned.cymat" ), nullptr ) == map_status_t::OK );
    REQUIRE( MapTextureEdit_SetRepeat( ws.pDocument, { &top, 1 }, { 96, 192 } ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 400, -600, 400 }, { 0, 0, 64 } ); s.Tool( "map.tool.loop_cut" );
    paint_brush_source_t before( *ws.pDocument, brush ); const usize steps = s.Steps();
    const auto checkPlacement = [&]( const map_document_t &document ) {
        const map_geometry_record_t *record{};
        for ( usize i = 0; i < document.geometryRecords.nCount; ++i ) { if ( document.geometryRecords.pData[i].id == brush ) { record = &document.geometryRecords.pData[i]; } }
        REQUIRE( record ); CHECK( record->owner == owner ); REQUIRE( record->iLayer < document.layers.nCount ); CHECK( std::string( document.layers.pData[record->iLayer].id ) == "detail" );
        map_uv_face_t face{}; REQUIRE( MapTextureEdit_Describe( &document, top, &face ) == map_status_t::OK ); CHECK( face.material == "materials/tests/loop_cut_owned.cymat" );
    };
    camera.Click( { 64, -64, 32 } ); REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MeshFaces( ws, brush ) == 10 ); checkPlacement( *ws.pDocument );
    edge_workflow_mesh_t cut( *ws.pDocument, brush );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.Check( *ws.pDocument, brush ); checkPlacement( *ws.pDocument );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkPlacement( *ws.pDocument );
    { edge_workflow_mesh_t restored( *ws.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) ); }
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) ); REQUIRE( MapDocument_Save( ws.pDocument, &text ) == map_status_t::OK );
    map_document_t loaded; REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { TextBuffer_Data( &text ), TextBuffer_Length( &text ) } ) == map_status_t::OK );
    checkPlacement( loaded ); edge_workflow_mesh_t reloaded( loaded, brush ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &reloaded.value ) );
}

TEST_CASE( "A provisional brush Loop Cut respects occlusion at the closest edge rather than just the cursor ray", "[map][gui][tool-gestures][loop-cut-atomic-policy][candidate-occlusion]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 target = AddBox( ws, { 512, -96, -96 }, { 768, 96, 96 } );
    const u64 blocker = AddBox( ws, { 255.9, -64, 47.9 }, { 256.1, 64, 48.1 } );
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "occluder" ), StringView_FromCString( "Occluder" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, blocker, StringView_FromCString( "occluder" ) ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), {}, { 1, 0, 0 } ); s.Tool( "map.tool.loop_cut" );
    const math::vec3d_t clicked{ 512, 0, 95 };
    // The cursor ray passes below the thin blocker, but the ray through the
    // closest point on the front upper edge intersects it at z = 48.
    REQUIRE( MapCameraView_Pick( camera.view.get(), camera.At( clicked ) ) == target );
    REQUIRE( MapCameraView_Pick( camera.view.get(), camera.At( { 512, 0, 96 } ) ) == blocker );
    paint_snapshot_t before( ws, target ); paint_brush_source_t blockerBefore( *ws.pDocument, blocker );
    camera.Click( clicked ); before.Check( ws ); blockerBefore.Check( *ws.pDocument, blocker );

    REQUIRE( MapLayers_SetHidden( &ws, QStringLiteral( "occluder" ), true ) );
    REQUIRE( MapCameraView_Pick( camera.view.get(), camera.At( clicked ) ) == target );
    const usize steps = s.Steps(); camera.Click( clicked );
    REQUIRE( s.Steps() == steps + 1 ); REQUIRE( KindOf( ws, target ) == map_wire_kind_t::MESH ); REQUIRE( MeshFaces( ws, target ) == 10 );
    REQUIRE( KindOf( ws, blocker ) == map_wire_kind_t::BRUSH ); blockerBefore.Check( *ws.pDocument, blocker );
    edge_workflow_mesh_t cut( *ws.pDocument, target ); REQUIRE( cut.value.vertices.nCount == 12 ); usize onCut = 0;
    for ( usize i = 0; i < cut.value.vertices.nCount; ++i ) {
        const auto &point = cut.value.vertices.pData[i].position;
        if ( std::abs( point.y ) < 95.999 ) { ++onCut; CHECK( point.y == Catch::Approx( 0 ).margin( 1e-5 ) ); }
    }
    CHECK( onCut == 4 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.brush->Check( *ws.pDocument, target ); blockerBefore.Check( *ws.pDocument, blocker );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    edge_workflow_mesh_t restored( *ws.pDocument, target ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) ); blockerBefore.Check( *ws.pDocument, blocker );
}

namespace
{
void KnifeClick( session_t &s, camera_t &camera, bool perspective, f64 x )
{
    if ( perspective ) { camera.Click( { x, 0, 128 } ); } else { s.Click( x, 0 ); }
}
void KnifeKey( session_t &s, camera_t &camera, bool perspective, int key )
{
    if ( perspective ) { camera.Key( key ); } else { s.Key( key ); }
}
void CheckKnifeBrushCommit( session_t &s, camera_t &camera, bool perspective, u64 brush, const paint_brush_source_t &before,
    const QPointF *expectedCutPixels = nullptr )
{
    const usize applied = EditorHistory_AppliedStepCount( &s.workspace.history );
    KnifeKey( s, camera, perspective, Qt::Key_Return );
    REQUIRE( EditorHistory_AppliedStepCount( &s.workspace.history ) == applied + 1u );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::MESH ); REQUIRE( MeshFaces( s.workspace, brush ) == 7u );
    edge_workflow_mesh_t cut( *s.workspace.pDocument, brush );
    REQUIRE( cut.value.vertices.nCount == 10u ); usize cutCorners = 0u;
    for ( usize i = 0u; i < cut.value.vertices.nCount; ++i ) {
        const auto p = cut.value.vertices.pData[i].position;
        if ( std::abs( p.y ) < 63.999 && std::abs( p.z - 128.0 ) < 1e-6 ) {
            ++cutCorners; CHECK( std::abs( p.x ) == Catch::Approx( 64.0 ).margin( 1e-6 ) );
            if ( expectedCutPixels != nullptr ) {
                const QPointF actual = camera.At( p ), expected = expectedCutPixels[p.x < 0.0 ? 0u : 1u];
                CHECK( std::hypot( actual.x() - expected.x(), actual.y() - expected.y() ) < 0.01 );
            } else { CHECK( p.y == Catch::Approx( 0.0 ).margin( 1e-6 ) ); }
        }
    }
    CHECK( cutCorners == 2u );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( KindOf( s.workspace, brush ) == map_wire_kind_t::BRUSH ); before.Check( *s.workspace.pDocument, brush );
    REQUIRE( MapWorkspace_Redo( &s.workspace ) == editor_history_status_t::OK );
    edge_workflow_mesh_t restored( *s.workspace.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &cut.value, &restored.value ) );
}
}

TEST_CASE( "Knife point placement and abandonment never publish brush conversion", "[map][gui][tool-gestures][knife-atomic-policy][cancel]" )
{
    for ( const bool perspective : { false, true } ) {
        for ( const int cancel : { 0, 1, 2 } ) {
            DYNAMIC_SECTION( "camera = " << perspective << ", cancel = " << cancel ) {
                session_t s; auto &ws = s.workspace;
                const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
                camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.knife" );
                paint_snapshot_t before( ws, brush );
                KnifeClick( s, camera, perspective, -24 ); before.Check( ws );
                if ( cancel == 0 ) { KnifeKey( s, camera, perspective, Qt::Key_Escape ); }
                else if ( cancel == 1 ) { KnifeKey( s, camera, perspective, Qt::Key_Backspace ); }
                else { s.Tool( "map.tool.translate" ); }
                before.Check( ws ); s.Tool( "map.tool.knife" ); KnifeKey( s, camera, perspective, Qt::Key_Return ); before.Check( ws );
                KnifeClick( s, camera, perspective, -24 ); before.Check( ws );
                KnifeClick( s, camera, perspective, 24 ); before.Check( ws );
                CheckKnifeBrushCommit( s, camera, perspective, brush, *before.brush );
            }
        }
    }
}

TEST_CASE( "Knife staged brush points survive navigation and commit once from either pane", "[map][gui][tool-gestures][knife-atomic-policy][success]" )
{
    for ( const bool perspective : { false, true } ) {
        DYNAMIC_SECTION( "camera = " << perspective ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.knife" );
            paint_snapshot_t before( ws, brush ); KnifeClick( s, camera, perspective, -24 ); before.Check( ws );
            if ( perspective ) { MapCameraView_LookAt( camera.view.get(), { 100, -600, 700 }, { 0, 0, 64 } ); }
            else { s.Frame( { -256, -256, -256 }, { 256, 256, 256 } ); }
            KnifeClick( s, camera, perspective, 24 ); before.Check( ws );
            CheckKnifeBrushCommit( s, camera, perspective, brush, *before.brush );
        }
    }
}

TEST_CASE( "Knife refuses stale brush paths after locks visibility policy geometry or identity changes", "[map][gui][tool-gestures][knife-atomic-policy][context]" )
{
    for ( const int change : { 0, 1, 2, 3, 4 } ) {
        DYNAMIC_SECTION( "context change = " << change ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } );
            s.Tool( "map.layer.create" ); const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[1].id );
            MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.knife" );
            paint_snapshot_t before( ws, brush ); camera.Click( { -24, 0, 128 } ); camera.Click( { 24, 0, 128 } ); before.Check( ws );
            if ( change == 0 ) { REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); }
            else if ( change == 1 ) { REQUIRE( MapLayers_SetHidden( &ws, layer, true ) ); REQUIRE( MapLayers_SetHidden( &ws, layer, false ) ); }
            else if ( change == 2 ) { EdgePickConversion( s, false ); EdgePickConversion( s, true ); }
            else if ( change == 3 ) { ++ws.pDocument->geometry.revision; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT ); }
            else { ++ws.pDocument->nextId; MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); }
            paint_snapshot_t invalidated( ws, brush ); camera.Key( Qt::Key_Return ); invalidated.Check( ws );
            camera.Click( { -24, 0, 128 } ); camera.Click( { 24, 0, 128 } ); invalidated.Check( ws );
            CheckKnifeBrushCommit( s, camera, true, brush, *invalidated.brush );
        }
    }
}

TEST_CASE( "Knife allocation refusal retains the brush and permits a fresh preparation or confirmation retry", "[map][gui][tool-gestures][knife-atomic-policy][allocation]" )
{
    for ( const bool confirming : { false, true } ) {
        DYNAMIC_SECTION( "allocation during confirmation = " << confirming ) {
            edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
            {
                session_t s; auto &ws = s.workspace;
                const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
                camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.knife" );
                paint_snapshot_t before( ws, brush );
                if ( confirming ) {
                    camera.Click( { -24, 0, 128 } ); camera.Click( { 24, 0, 128 } ); before.Check( ws );
                    edge_gui_allocator_scope_t scope( s.gui, &allocator ); audit.failOn = 1u;
                    camera.Key( Qt::Key_Return );
                } else {
                    const QPointF at = camera.At( { -24, 0, 128 } );
                    const auto *previous = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &allocator; audit.failOn = 1u;
                    camera.Send( QEvent::MouseButtonPress, at, Qt::LeftButton ); ws.pDocument->pAllocator = previous;
                    camera.Send( QEvent::MouseButtonRelease, at, Qt::NoButton );
                }
                REQUIRE( audit.calls > 0u ); CHECK( audit.live == 0u ); before.Check( ws );
                CHECK( MapNotice_Last( &ws ).contains( QStringLiteral( "memory" ), Qt::CaseInsensitive ) );
                if ( !confirming ) { camera.Click( { -24, 0, 128 } ); camera.Click( { 24, 0, 128 } ); before.Check( ws ); }
                CheckKnifeBrushCommit( s, camera, true, brush, *before.brush );
            }
            CHECK( audit.live == 0u );
        }
    }
}

TEST_CASE( "Knife confirmation keeps current entity metadata instead of publishing its earlier conversion snapshot", "[map][gui][tool-gestures][knife-atomic-policy][metadata]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); u64 owner = 0;
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.knife" );
    paint_snapshot_t before( ws, brush ); camera.Click( { -24, 0, 128 } ); camera.Click( { 24, 0, 128 } ); before.Check( ws );
    bool changed = false;
    REQUIRE( MapEntityEdit_SetIdentityField( ws.pDocument, { &owner, 1u }, StringView_FromCString( "name" ), StringView_FromCString( "latest_owner_name" ), &changed ) == map_status_t::OK );
    REQUIRE( changed ); REQUIRE( ws.pDocument->geometry.revision == before.revision );
    MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
    CheckKnifeBrushCommit( s, camera, true, brush, *before.brush );
    const auto *record = MapDocument_FindObject( ws.pDocument, owner, nullptr ); REQUIRE( record != nullptr );
    string_view_t name{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, StringView_FromCString( "name" ) ), &name ) );
    CHECK( std::string( name.pData, name.cchLength ) == "latest_owner_name" );
    bool foundOwner = false;
    for ( usize i = 0u; i < ws.pDocument->geometryRecords.nCount; ++i ) {
        const auto &placement = ws.pDocument->geometryRecords.pData[i];
        if ( placement.id == brush ) { CHECK( placement.owner == owner ); foundOwner = true; }
    }
    CHECK( foundOwner );
}

TEST_CASE( "Edge Cut stages brush edge points and publishes conversion only with the cut", "[map][gui][tool-gestures][knife-atomic-policy][edge-cut]" )
{
    for ( const bool perspective : { false, true } ) {
        DYNAMIC_SECTION( "camera = " << perspective ) {
            session_t s; auto &ws = s.workspace;
            const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
            camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.edge_cut" );
            paint_snapshot_t before( ws, brush );
            // Just inside the top face: the shared picker snaps to the side
            // edge at x=-64 or x=64, with its canonical candidate vertex IDs.
            KnifeClick( s, camera, perspective, -63 ); before.Check( ws );
            KnifeKey( s, camera, perspective, Qt::Key_Escape ); before.Check( ws );
            s.Tool( "map.tool.edge_cut" ); KnifeKey( s, camera, perspective, Qt::Key_Return ); before.Check( ws );
            QPointF closest[2]{};
            if ( perspective ) {
                // Edge snapping minimizes screen distance. A cursor one world
                // unit inside a receding edge does not snap to world y=0.
                for ( usize i = 0u; i < 2u; ++i ) {
                    const f64 x = i == 0u ? -64.0 : 64.0;
                    const QPointF a = camera.At( { x, -64, 128 } ), b = camera.At( { x, 64, 128 } );
                    const QPointF cursor = camera.At( { i == 0u ? -63.0 : 63.0, 0, 128 } ), ab = b - a;
                    const f64 t = std::clamp( ( ( cursor - a ).x() * ab.x() + ( cursor - a ).y() * ab.y() ) /
                        ( ab.x() * ab.x() + ab.y() * ab.y() ), 0.0, 1.0 );
                    closest[i] = a + ab * t;
                }
            }
            KnifeClick( s, camera, perspective, -63 ); KnifeClick( s, camera, perspective, 63 ); before.Check( ws );
            CheckKnifeBrushCommit( s, camera, perspective, brush, *before.brush, perspective ? closest : nullptr );
        }
    }
}

namespace
{
void SculptChoice( session_t &s, const char *path, const char *text )
{
    const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
}
}

TEST_CASE( "Coarse brush sculpt preparation is private and one commit preserves root placement and surfacing", "[map][gui][tool-gestures][displace][sculpt-transaction][metadata]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); u64 owner = 0, side = 0;
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, brush, StringView_FromCString( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { const auto &face = ws.wire.faces.pData[i]; if ( face.id == brush && face.normal.z > 0.99 ) { side = face.sideId; } }
    REQUIRE( side != 0 ); const map_surface_ref_t surface{ brush, side };
    REQUIRE( MapTextureEdit_SetMaterial( ws.pDocument, { &surface, 1 }, StringView_FromCString( "materials/tests/sculpt_owned.cymat" ), nullptr ) == map_status_t::OK );
    REQUIRE( MapTextureEdit_SetRepeat( ws.pDocument, { &surface, 1 }, { 96, 192 } ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    PaintReal( s, "editor.map.displace_radius", 64 ); PaintReal( s, "editor.map.displace_strength", 10 );
    SculptChoice( s, "editor.map.displace_normal", "z" );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.displace" );
    paint_snapshot_t before( ws, brush ); const usize objects = ws.wire.objects.nCount;
    const QPointF center = camera.At( { 0, 0, 128 } );
    camera.Send( QEvent::MouseButtonPress, center, Qt::LeftButton ); before.Check( ws );
    const auto *preview = MapToolGestures_SculptPreviewWire( &ws ); REQUIRE( preview );
    const auto *previewObject = MapWireframe_FindObject( *preview, brush ); REQUIRE( previewObject );
    CHECK( previewObject->kind == map_wire_kind_t::MESH ); CHECK( previewObject->bounds.box.maximum.z > 128 );
    CHECK( preview->faces.nCount == 69 ); CHECK( ws.wire.faces.nCount == 6 ); CHECK( preview->objects.nCount == objects );
    QFocusEvent lose( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.view.get(), &lose );
    CHECK( MapToolGestures_SculptPreviewWire( &ws ) == nullptr ); before.Check( ws );
    camera.Send( QEvent::MouseButtonRelease, center, Qt::NoButton ); before.Check( ws );
    // Capture expected preparation before deformation, independently through
    // the established metadata-preserving conversion and quad slice adapters.
    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( ws.pDocument, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> prepared( raw );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, prepared->pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( prepared.get(), { &brush, 1 }, &roots ) == map_status_t::OK ); Vector_Shutdown( &roots );
    REQUIRE( MapMeshEdit_QuadSliceFace( prepared.get(), brush, side, 8, 8 ) == map_status_t::OK ); edge_workflow_mesh_t expected( *prepared, brush );
    camera.Send( QEvent::MouseButtonPress, center, Qt::LeftButton ); before.Check( ws );
    bool changed = false;
    REQUIRE( MapEntityEdit_SetIdentityField( ws.pDocument, { &owner, 1u }, StringView_FromCString( "name" ), StringView_FromCString( "current_sculpt_owner" ), &changed ) == map_status_t::OK );
    REQUIRE( changed ); REQUIRE( ws.pDocument->geometry.revision == before.revision ); MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
    REQUIRE( MapToolGestures_SculptPreviewWire( &ws ) );
    camera.Send( QEvent::MouseButtonRelease, center, Qt::NoButton );
    REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); CHECK( MapToolGestures_SculptPreviewWire( &ws ) == nullptr );
    CHECK( KindOf( ws, brush ) == map_wire_kind_t::MESH ); CHECK( ws.wire.objects.nCount == objects );
    edge_workflow_mesh_t sculpted( *ws.pDocument, brush ); REQUIRE( sculpted.value.faces.nCount == expected.value.faces.nCount );
    REQUIRE( sculpted.value.corners.nCount == expected.value.corners.nCount ); REQUIRE( sculpted.value.vertices.nCount == expected.value.vertices.nCount );
    usize moved = 0;
    for ( usize i = 0; i < sculpted.value.vertices.nCount; ++i ) {
        const auto &a = sculpted.value.vertices.pData[i], &b = expected.value.vertices.pData[i]; CHECK( a.sourceId.value == b.sourceId.value );
        moved += !math::Vec3d_EqualsExact( a.position, b.position );
    }
    CHECK( moved > 0 );
    for ( usize i = 0; i < sculpted.value.faces.nCount; ++i ) {
        const auto &a = sculpted.value.faces.pData[i], &b = expected.value.faces.pData[i];
        CHECK( a.sourceId.value == b.sourceId.value ); CHECK( a.attributes.material.value == b.attributes.material.value ); CHECK( a.attributes.smoothingGroups == b.attributes.smoothingGroups );
    }
    for ( usize i = 0; i < sculpted.value.corners.nCount; ++i ) {
        const auto &a = sculpted.value.corners.pData[i], &b = expected.value.corners.pData[i]; CHECK( a.iVertex == b.iVertex );
        CHECK( a.attributes.uv0.x == b.attributes.uv0.x ); CHECK( a.attributes.uv0.y == b.attributes.uv0.y );
        CHECK( a.attributes.uv1.x == b.attributes.uv1.x ); CHECK( a.attributes.uv1.y == b.attributes.uv1.y ); CHECK( a.attributes.colorRgba == b.attributes.colorRgba );
    }
    const auto checkPlacement = [&]( map_document_t &document ) {
        const map_geometry_record_t *placement = nullptr;
        for ( usize i = 0; i < document.geometryRecords.nCount; ++i ) { if ( document.geometryRecords.pData[i].id == brush ) { placement = &document.geometryRecords.pData[i]; } }
        REQUIRE( placement ); CHECK( placement->owner == owner ); REQUIRE( placement->iLayer < document.layers.nCount );
        CHECK( std::string( document.layers.pData[placement->iLayer].id ) == "detail" );
        const auto *record = MapDocument_FindObject( &document, owner, nullptr ); REQUIRE( record ); string_view_t name{};
        REQUIRE( KeyValue_GetString( KeyValue_Find( record, StringView_FromCString( "name" ) ), &name ) ); CHECK( std::string( name.pData, name.cchLength ) == "current_sculpt_owner" );
    };
    checkPlacement( *ws.pDocument ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); before.brush->Check( *ws.pDocument, brush ); checkPlacement( *ws.pDocument );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkPlacement( *ws.pDocument );
    { edge_workflow_mesh_t redone( *ws.pDocument, brush ); CHECK( geo::MeshSourceDescription_Equal( &sculpted.value, &redone.value ) ); }
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) ); REQUIRE( MapDocument_Save( ws.pDocument, &text ) == map_status_t::OK );
    map_document_t loaded; REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { TextBuffer_Data( &text ), TextBuffer_Length( &text ) } ) == map_status_t::OK );
    TextBuffer_Shutdown( &text ); checkPlacement( loaded ); edge_workflow_mesh_t reloaded( loaded, brush ); CHECK( geo::MeshSourceDescription_Equal( &sculpted.value, &reloaded.value ) );
}

TEST_CASE( "An ineffective flatten stroke discards its private brush conversion and slicing", "[map][gui][tool-gestures][displace][sculpt-transaction][no-effect]" )
{
    session_t s; auto &ws = s.workspace; const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    PaintReal( s, "editor.map.displace_radius", 64 ); SculptChoice( s, "editor.map.displace_mode", "flatten" ); SculptChoice( s, "editor.map.displace_normal", "z" );
    camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.displace" );
    paint_snapshot_t before( ws, brush ); const QPointF center = camera.At( { 0, 0, 128 } );
    camera.Send( QEvent::MouseButtonPress, center, Qt::LeftButton ); REQUIRE( MapToolGestures_SculptPreviewWire( &ws ) ); before.Check( ws );
    camera.Send( QEvent::MouseButtonRelease, center, Qt::NoButton ); before.Check( ws ); CHECK( MapToolGestures_SculptPreviewWire( &ws ) == nullptr );
    SculptChoice( s, "editor.map.displace_mode", "push_pull" ); camera.Click( { 0, 0, 128 } ); CHECK( s.Steps() == before.steps + 1 ); CHECK( KindOf( ws, brush ) == map_wire_kind_t::MESH );
}

TEST_CASE( "Displace retains all six brush modes with private preview and exact undo", "[map][gui][tool-gestures][displace][sculpt-transaction][modes]" )
{
    for ( const char *mode : { "push_pull", "smooth", "flatten", "inflate", "pinch", "noise" } ) { DYNAMIC_SECTION( mode ) {
        session_t s; auto &ws = s.workspace; map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2; quad.bounds = { { -64, -64, 128 }, { 64, 64, 128 } };
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); const u64 mesh = s.Selected(); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        PaintReal( s, "editor.map.displace_radius", 256 ); PaintReal( s, "editor.map.displace_strength", 10 ); PaintReal( s, "editor.map.displace_smooth", 10 );
        SculptChoice( s, "editor.map.displace_mode", mode ); SculptChoice( s, "editor.map.displace_normal", "z" ); SculptChoice( s, "editor.map.displace_falloff", "constant" );
        camera_t camera( s ); MapCameraView_LookAt( camera.view.get(), { 0, -600, 700 }, { 0, 0, 64 } ); s.Tool( "map.tool.displace" );
        paint_snapshot_t before( ws, mesh ); const QPointF center = camera.At( { 0, 0, 128 } );
        camera.Send( QEvent::MouseButtonPress, center, Qt::LeftButton ); REQUIRE( MapToolGestures_SculptPreviewWire( &ws ) ); before.Check( ws );
        camera.Send( QEvent::MouseButtonRelease, center, Qt::NoButton ); CHECK( MapToolGestures_SculptPreviewWire( &ws ) == nullptr );
        if ( std::string( mode ) == "flatten" ) { before.Check( ws ); }
        else {
            REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); edge_workflow_mesh_t after( *ws.pDocument, mesh );
            CHECK_FALSE( geo::MeshSourceDescription_Equal( &before.mesh->value, &after.value ) );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); edge_workflow_mesh_t undone( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.mesh->value, &undone.value ) );
        }
    } }
}

TEST_CASE( "A direct edge extension repeats on the resulting rim and cancelled or empty input retains its recipe", "[map][gui][tool-gestures][modelling-repeat]" )
{
    session_t s; const u64 mesh = EdgeWorkflowQuad( s );
    EdgeDragAction( s, "extend" ); MapWorkspace_SetGridSize( &s.workspace, 16 );
    edge_workflow_mesh_t original( *s.workspace.pDocument, mesh ); const usize steps = s.Steps();
    s.Drag( 128, 32, 160, 32 );
    REQUIRE( s.Steps() == steps + 1 ); REQUIRE( MapToolGestures_HasModellingRepeat( &s.workspace ) );
    const auto rim = OnlySelectedEdge( s ); CHECK( Position( s.workspace, mesh, rim.a ).x == Catch::Approx( 160 ) );
    // A later unfinished press is not an operation worth repeating.
    s.Mouse( QEvent::MouseButtonPress, s.At( 160, 32 ), Qt::LeftButton );
    s.Mouse( QEvent::MouseMove, s.At( 192, 32 ), Qt::LeftButton );
    CHECK_FALSE( MapToolGestures_RepeatModelling( &s.workspace ) );
    s.Key( Qt::Key_Escape ); s.Mouse( QEvent::MouseButtonRelease, s.At( 192, 32 ), Qt::NoButton );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, rim, MAP_SELECT_REPLACE ) );
    REQUIRE_FALSE( MapToolGestures_TransformComponents( &s.workspace, map_component_transform_t::TRANSLATE, {}, map_component_pivot_t::CENTER ) );
    REQUIRE( MapToolGestures_RepeatModelling( &s.workspace ) );
    const auto next = OnlySelectedEdge( s ); CHECK( next.a.value != rim.a.value ); CHECK( next.b.value != rim.b.value );
    CHECK( Position( s.workspace, mesh, next.a ).x == Catch::Approx( 192 ) );
    CHECK( Position( s.workspace, mesh, next.b ).x == Catch::Approx( 192 ) );
    CHECK( MeshFaces( s.workspace, mesh ) == 3 ); CHECK( s.Steps() == steps + 2 ); CheckOriginalVertices( s, mesh, original );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( OnlySelectedEdge( s ).a.value == rim.a.value ); CHECK( OnlySelectedEdge( s ).b.value == rim.b.value );
    REQUIRE( MapToolGestures_RepeatModelling( &s.workspace ) );
    CHECK( MeshFaces( s.workspace, mesh ) == 3 ); CHECK( Position( s.workspace, mesh, OnlySelectedEdge( s ).a ).x == Catch::Approx( 192 ) );
}

TEST_CASE( "Numeric component repeat retains its absolute pivot and checks mode owner and geometry context", "[map][gui][tool-gestures][modelling-repeat]" )
{
    for ( const bool scale : { false, true } ) {
        CAPTURE( scale ); session_t s; auto &ws = s.workspace; const u64 mesh = EdgeWorkflowQuad( s );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( ws, mesh, { 128, -64, 0 } ), VertexAt( ws, mesh, { 128, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge, MAP_SELECT_REPLACE ) );
        MapToolGestures_SetPivot( &ws, {} );
        const auto kind = scale ? map_component_transform_t::SCALE : map_component_transform_t::ROTATE;
        REQUIRE( MapToolGestures_TransformComponents( &ws, kind, scale ? math::vec3d_t{ 2, 1, 1 } : math::vec3d_t{ 0, 0, 15 }, map_component_pivot_t::PIVOT_POINT ) );
        MapToolGestures_SetPivot( &ws, { 1024, 1024, 0 } ); // Recipe keeps its original world transform.
        REQUIRE( MapToolGestures_RepeatModelling( &ws ) );
        const auto original = math::vec3d_t{ 128, -64, 0 };
        const f64 angle = 3.14159265358979323846 / 6.0;
        const auto expected = scale ? math::vec3d_t{ 512, -64, 0 } :
            math::vec3d_t{ original.x * std::cos( angle ) - original.y * std::sin( angle ), original.x * std::sin( angle ) + original.y * std::cos( angle ), 0 };
        CheckPosition( Position( ws, mesh, VertexAt( ws, mesh, expected ) ), expected );
        const usize steps = s.Steps();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
        CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) ); CHECK( s.Steps() == steps );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge, MAP_SELECT_REPLACE ) );
        ws.pDocument->bReadOnly = true; CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) ); ws.pDocument->bReadOnly = false;
        const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[0].id );
        REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) );
        REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge, MAP_SELECT_REPLACE ) );
        CHECK( s.Steps() == steps );
        // An authored edit outside this recipe cannot silently replay it on
        // a changed mesh even when persistent endpoint IDs still exist.
        REQUIRE( MapTopologyEdit_MoveVertices( ws.pDocument, mesh, { &edge.a, 1 }, { 1, 0, 0 } ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        CHECK( MapToolGestures_HasModellingRepeat( &ws ) ); CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); CHECK_FALSE( MapToolGestures_HasModellingRepeat( &ws ) );
    }
}

TEST_CASE( "Failed repeated transforms preserve geometry selection history and the recipe for retry", "[map][gui][tool-gestures][modelling-repeat][allocation]" )
{
    edge_gui_allocation_t audit{}; const allocator_t allocator{ &EdgeGuiAllocate, nullptr, &EdgeGuiFree, &audit };
    {
        session_t s; auto &ws = s.workspace; const u64 mesh = EdgeWorkflowQuad( s );
        const auto edge = geo::MeshEdgeRef_Make( VertexAt( ws, mesh, { 128, -64, 0 } ), VertexAt( ws, mesh, { 128, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge, MAP_SELECT_REPLACE ) );
        REQUIRE( MapToolGestures_TransformComponents( &ws, map_component_transform_t::TRANSLATE, { 16, 0, 0 }, map_component_pivot_t::CENTER ) );
        edge_workflow_mesh_t before( *ws.pDocument, mesh ); const auto *document = ws.pDocument;
        const usize steps = s.Steps(); const auto revision = ws.selection.revision;
        { edge_gui_allocator_scope_t scope( s.gui, &allocator ); audit.failOn = 1; CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) ); }
        CHECK( ws.pDocument == document ); CHECK( ws.selection.revision == revision ); CHECK( s.Steps() == steps );
        CHECK( OnlySelectedEdge( s ).a.value == edge.a.value ); CHECK( OnlySelectedEdge( s ).b.value == edge.b.value );
        { edge_workflow_mesh_t after( *ws.pDocument, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); }
        CHECK( MapToolGestures_HasModellingRepeat( &ws ) ); audit.failOn = 0;
        REQUIRE( MapToolGestures_RepeatModelling( &ws ) ); CHECK( s.Steps() == steps + 1 );
        CHECK( Position( ws, mesh, edge.a ).x == Catch::Approx( 160 ) ); CHECK( Position( ws, mesh, edge.b ).x == Catch::Approx( 160 ) );
        // A different mesh cannot borrow this operation merely because it has
        // a valid selected edge.
        const u64 other = EdgeWorkflowQuad( s, { { 512, -64, 0 }, { 640, 64, 0 } } );
        const auto foreign = geo::MeshEdgeRef_Make( VertexAt( ws, other, { 640, -64, 0 } ), VertexAt( ws, other, { 640, 64, 0 } ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, other, foreign, MAP_SELECT_REPLACE ) );
        const usize otherSteps = s.Steps(); CHECK_FALSE( MapToolGestures_RepeatModelling( &ws ) ); CHECK( s.Steps() == otherSteps );
    }
    CHECK( audit.live == 0 );
}
