//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_GeometryCommands_Tests.cpp
//  Purpose: Contract tests for the geometry operation commands: what each
//           one enables for, what it changes, and that it is one undo step.
//  Details: Commands run through the registry exactly as menus, toolbars
//           and Tool Properties run them.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_EntityOutputs.h"
#include "CypherMap_Modifiers.h"
#include "CypherMap_Export.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Panels.h"
#include "CypherMap_Groups.h"
#include "CypherMap_SelectionSets.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Check.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherMap_TextureEdit.h"
#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_Notice.h"
#include "CypherMapGui_FastTexture.h"
#include "CypherMapGui_FindEntities.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Autosave.h"
#include "CypherGeometry_CompilerInterchange.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentSurfaces.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentBrushAttributes.h"
#include "CypherEditor/Geometry/Mesh/CypherGeometry_MeshBoundaryOps.h"
#include "CypherEditor/Geometry/Mesh/CypherGeometry_MeshSource.h"
#include "CypherEditor/Gui/Widgets/CypherEditorGui_Section.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QTableWidget>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
    bool Enabled( const char *command )
    {
        return ( EditorCommands_State( &gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_ENABLED ) != 0;
    }
    command_result_t Run( const char *command ) { return EditorCommands_Execute( &gui.commands, StringView_FromCString( command ), {} ); }
    usize Steps() { return EditorHistory_StepCount( &workspace.history ); }
};

map_bounds_t Bounds( cypher::math::vec3d_t low, cypher::math::vec3d_t high )
{
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, low ); MapBounds_AddPoint( bounds, high );
    return bounds;
}
u64 AddBox( map_workspace_t &ws, cypher::math::vec3d_t low, cypher::math::vec3d_t high )
{
    REQUIRE( MapWorkspace_CreateBox( &ws, Bounds( low, high ) ) );
    return EditorSelection_At( &ws.selection, 0 );
}
cypher::math::aabbd_t Box( const map_workspace_t &ws, u64 id )
{
    const auto *object = MapWireframe_FindObject( ws.wire, id );
    REQUIRE( object != nullptr );
    return object->bounds.box;
}
void Select( map_workspace_t &ws, std::vector<u64> ids ) { MapWorkspace_SetSelection( &ws, ids.data(), ids.size() ); }

struct description_t {
    geo::mesh_source_description_t value{};
    description_t( const map_workspace_t &ws, u64 id )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
        const auto *mesh = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { id } ); REQUIRE( mesh );
        REQUIRE( geo::MeshSource_TryDescribe( mesh, &value ) == geo::geometry_status_t::OK );
    }
    ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    geo::geometry_source_id_t Corner( usize face, u32 k ) const
    {
        const auto &f = value.faces.pData[face];
        return value.vertices.pData[value.corners.pData[f.iFirstCorner + k].iVertex].sourceId;
    }
};

// A converted box mesh, in Objects mode, selected.
u64 AddBoxMesh( session_t &s )
{
    const u64 id = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.workspace ) );
    return id;
}
} // namespace

TEST_CASE( "Wired operations replace their placeholders and the rest stay disabled", "[map][gui][geometry-commands]" )
{
    session_t s;
    for ( const char *id : { "map.mesh.merge", "map.mesh.collapse", "map.mesh.dissolve", "map.mesh.split_edges", "map.mesh.connect_edges",
                             "map.mesh.fill_hole", "map.mesh.bridge", "map.mesh.bevel", "map.mesh.extrude_edges", "map.mesh.extend_edges",
                             "map.mesh.normals_hard", "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs",
                             "map.mesh.radial_align", "map.mesh.split", "map.select.grow", "map.select.shrink", "map.mesh.smooth", "map.mesh.subdivide", "map.mesh.to_brush", "map.mesh.boolean_union",
                             "map.mesh.solidify", "map.brush.carve", "map.brush.snap_to_grid", "map.transform.flip_horizontal",
                             "map.transform.flip_vertical", "map.transform.rotate_cw", "map.transform.rotate_ccw", "map.align.left",
                             "map.align.right", "map.align.top", "map.align.bottom", "map.select.touching", "map.select.same_material",
                             "map.texture.align_world", "map.texture.align_face", "map.texture.fit", "map.texture.justify_left",
                             "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom", "map.texture.justify_center",
                             "map.texture.rotate", "map.texture.scale", "map.texture.shift" } ) {
        CAPTURE( id );
        const auto *command = EditorCommands_Find( &s.gui.commands, StringView_FromCString( id ) );
        REQUIRE( command != nullptr );
        CHECK( command->pfnState != nullptr );
        CHECK_FALSE( s.Enabled( id ) ); // Nothing is selected in a new map.
        CHECK( s.Run( id ) == command_result_t::DISABLED );
    }
    // Operations whose adapters do not exist yet keep the honest placeholder.
    for ( const char *id : {
                             "map.terrain.paint" } ) {
        CAPTURE( id );
        REQUIRE( EditorCommands_Find( &s.gui.commands, StringView_FromCString( id ) ) != nullptr );
        CHECK_FALSE( s.Enabled( id ) );
    }
}

TEST_CASE( "Rotate 90 and flips follow the last hovered 2D view and are single undo steps", "[map][gui][geometry-commands][transform]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 128, 32, 64 } );
    REQUIRE( s.Enabled( "map.transform.rotate_cw" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.transform.rotate_cw" ) == command_result_t::OK ); // Top view by default.
    auto box = Box( ws, a );
    CHECK( box.maximum.x - box.minimum.x == Catch::Approx( 32 ) ); CHECK( box.maximum.y - box.minimum.y == Catch::Approx( 128 ) );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    box = Box( ws, a ); CHECK( box.maximum.x - box.minimum.x == Catch::Approx( 128 ) );

    // Front view: Y right, Z up. Clockwise turns Y into -Z.
    MapWorkspace_SetCursor( &ws, {}, ( 1u << 1 ) | ( 1u << 2 ) );
    REQUIRE( s.Run( "map.transform.rotate_ccw" ) == command_result_t::OK );
    box = Box( ws, a );
    CHECK( box.maximum.y - box.minimum.y == Catch::Approx( 64 ) ); CHECK( box.maximum.z - box.minimum.z == Catch::Approx( 32 ) );

    const u64 b = AddBox( ws, { 200, 0, 0 }, { 232, 32, 32 } );
    Select( ws, { a, b } );
    MapWorkspace_SetCursor( &ws, {}, ( 1u << 0 ) | ( 1u << 1 ) );
    const auto beforeA = Box( ws, a ), beforeB = Box( ws, b );
    const f64 center = ( std::min( beforeA.minimum.x, beforeB.minimum.x ) + std::max( beforeA.maximum.x, beforeB.maximum.x ) ) / 2;
    REQUIRE( s.Enabled( "map.transform.flip_horizontal" ) );
    REQUIRE( s.Run( "map.transform.flip_horizontal" ) == command_result_t::OK );
    CHECK( Box( ws, b ).minimum.x == Catch::Approx( 2 * center - beforeB.maximum.x ) );
    CHECK( Box( ws, a ).maximum.x == Catch::Approx( 2 * center - beforeA.minimum.x ) );
    CHECK( ws.selection.ids.nCount == 2 );
}

TEST_CASE( "Align and snap move each object by its own bounds", "[map][gui][geometry-commands][align]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    CHECK_FALSE( s.Enabled( "map.align.left" ) ); // One object has nothing to align to.
    const u64 b = AddBox( ws, { 100, 16, 32 }, { 132, 48, 96 } );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.align.left" ) );
    REQUIRE( s.Run( "map.align.left" ) == command_result_t::OK );
    CHECK( Box( ws, b ).minimum.x == Catch::Approx( 0 ) ); CHECK( Box( ws, a ).minimum.x == Catch::Approx( 0 ) );
    // Side view (X right, Z up): Top aligns the highest Z.
    MapWorkspace_SetCursor( &ws, {}, ( 1u << 0 ) | ( 1u << 2 ) );
    REQUIRE( s.Run( "map.align.top" ) == command_result_t::OK );
    CHECK( Box( ws, a ).maximum.z == Catch::Approx( 96 ) ); CHECK( Box( ws, b ).maximum.z == Catch::Approx( 96 ) );
    // Aligned already: no change, no undo step.
    const usize steps = s.Steps();
    CHECK( s.Run( "map.align.top" ) == command_result_t::FAILED ); CHECK( s.Steps() == steps );

    MapWorkspace_SetGridSize( &ws, 16.0 );
    const u64 c = AddBox( ws, { 5, 5, 5 }, { 37, 37, 37 } );
    REQUIRE( s.Enabled( "map.brush.snap_to_grid" ) );
    REQUIRE( s.Run( "map.brush.snap_to_grid" ) == command_result_t::OK );
    const auto snapped = Box( ws, c );
    CHECK( snapped.minimum.x == Catch::Approx( 0 ) ); CHECK( snapped.maximum.x == Catch::Approx( 32 ) );
}

TEST_CASE( "Carve cuts the selected brush out of the brushes it touches and keeps it selected", "[map][gui][geometry-commands][carve]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 target = AddBox( ws, { 0, 0, 0 }, { 128, 128, 128 } );
    const u64 far = AddBox( ws, { 512, 0, 0 }, { 576, 64, 64 } );
    const u64 cutter = AddBox( ws, { 96, 32, 32 }, { 160, 96, 96 } );
    REQUIRE( s.Enabled( "map.brush.carve" ) );
    const usize brushes = ws.pDocument->geometry.brushes.nCount, steps = s.Steps();
    REQUIRE( s.Run( "map.brush.carve" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount > brushes );
    CHECK( ( MapWireframe_FindObject( ws.wire, target ) == nullptr || Box( ws, target ).maximum.x <= 96 ) ); // The target was split.
    CHECK( MapWireframe_FindObject( ws.wire, far ) != nullptr );                                        // Untouched.
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == cutter );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == brushes );
    // A cutter touching nothing has nothing to carve.
    Select( ws, { far } );
    CHECK_FALSE( s.Enabled( "map.brush.carve" ) );
}

TEST_CASE( "Select Touching and Select Same Material extend the selection", "[map][gui][geometry-commands][selection]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = AddBox( ws, { 64, 0, 0 }, { 128, 64, 64 } );
    const u64 c = AddBox( ws, { 512, 0, 0 }, { 576, 64, 64 } );
    Select( ws, { a } );
    REQUIRE( s.Enabled( "map.select.touching" ) );
    REQUIRE( s.Run( "map.select.touching" ) == command_result_t::OK );
    CHECK( ws.selection.ids.nCount == 2 );
    CHECK( MapWorkspace_IsSelected( &ws, b ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, c ) );
    Select( ws, { a } );
    REQUIRE( s.Enabled( "map.select.same_material" ) );
    REQUIRE( s.Run( "map.select.same_material" ) == command_result_t::OK ); // All boxes use the default material.
    CHECK( ws.selection.ids.nCount == 3 );
}

TEST_CASE( "Vertex merge publishes one edit and keeps the merged vertex selected", "[map][gui][geometry-commands][vertices]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    description_t before( ws, mesh );
    const auto first = before.Corner( 0, 0 ), second = before.Corner( 0, 1 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, first ) );
    CHECK_FALSE( s.Enabled( "map.mesh.merge" ) ); // One vertex has nothing to merge with.
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, second, MAP_SELECT_ADD ) );
    CHECK( s.Enabled( "map.mesh.merge" ) ); CHECK( s.Enabled( "map.mesh.collapse" ) ); CHECK( s.Enabled( "map.select.grow" ) );
    CHECK( s.Enabled( "map.mesh.bevel" ) ); // Box corners are interior: they bevel.
    CHECK_FALSE( s.Enabled( "map.mesh.connect_edges" ) ); // Adjacent: they already share an edge.
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    CHECK( description_t( ws, mesh ).value.vertices.nCount == 7 );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( ws.meshSelection.vertices.nCount == 1 ); CHECK( ws.meshSelection.vertices.pData[0].value == first.value );
    // Navigation keeps components for inspection only.
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CHECK_FALSE( s.Enabled( "map.select.grow" ) );
}

TEST_CASE( "Edge operations enable for what they can change", "[map][gui][geometry-commands][edges]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    description_t before( ws, mesh );
    const auto edge = geo::MeshEdgeRef_Make( before.Corner( 0, 0 ), before.Corner( 0, 1 ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge ) );
    for ( const char *id : { "map.mesh.bevel", "map.mesh.split_edges", "map.mesh.collapse", "map.mesh.dissolve", "map.mesh.normals_hard",
                             "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs", "map.select.grow" } ) {
        CAPTURE( id ); CHECK( s.Enabled( id ) );
    }
    // A closed box has no open edges.
    for ( const char *id : { "map.mesh.fill_hole", "map.mesh.bridge", "map.mesh.extrude_edges", "map.mesh.extend_edges", "map.mesh.merge",
                             "map.mesh.connect_edges", "map.mesh.radial_align" } ) {
        CAPTURE( id ); CHECK_FALSE( s.Enabled( id ) );
    }
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.split_edges" ) == command_result_t::OK );
    CHECK( description_t( ws, mesh ).value.vertices.nCount == 9 ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( description_t( ws, mesh ).value.vertices.nCount == 8 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge ) );
    ws.pDocument->bReadOnly = CY_TRUE;
    CHECK_FALSE( s.Enabled( "map.mesh.bevel" ) ); CHECK( s.Run( "map.mesh.bevel" ) == command_result_t::DISABLED );
    ws.pDocument->bReadOnly = CY_FALSE;
    REQUIRE( s.Run( "map.mesh.normals_hard" ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 1 ); // The edge is reselected for the next operation.
}

TEST_CASE( "Solidify thickens open meshes and smoothing applies to whole meshes", "[map][gui][geometry-commands][mesh-objects]" )
{
    session_t s; auto &ws = s.workspace;
    map_bounds_t footprint{}; MapBounds_AddPoint( footprint, { 0, 0, 0 } ); MapBounds_AddPoint( footprint, { 128, 64, 0 } );
    map_primitive_desc_t quad = MapWorkspace_PrimitiveDefaults( &ws, footprint );
    quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) );
    const u64 id = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( s.Enabled( "map.mesh.solidify" ) ); REQUIRE( s.Enabled( "map.mesh.smooth" ) );
    REQUIRE( s.Run( "map.mesh.solidify" ) == command_result_t::OK );
    const auto *mesh = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { id } ); REQUIRE( mesh );
    CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 );
    CHECK( Box( ws, id ).maximum.z - Box( ws, id ).minimum.z == Catch::Approx( 16 ) ); // editor.map.mesh_solidify_thickness
    CHECK_FALSE( s.Enabled( "map.mesh.solidify" ) ); // Closed now.
}

TEST_CASE( "Texture commands align the selected face or every face of the selected objects", "[map][gui][geometry-commands][texture]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 box = AddBox( ws, { 0, 0, 0 }, { 200, 100, 64 } );
    REQUIRE( s.Enabled( "map.texture.fit" ) );
    const usize steps = s.Steps();
    const auto revision = ws.pDocument->geometry.revision;
    REQUIRE( s.Run( "map.texture.fit" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.revision != revision ); CHECK( s.Steps() == steps + 1 );
    CHECK( ws.selection.ids.nCount == 1 );
    // Faces mode: the one picked side, with console arguments.
    const auto *brush = geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { box } ); REQUIRE( brush );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    CHECK_FALSE( s.Enabled( "map.texture.rotate" ) ); // No face picked yet.
    MapWorkspace_SelectBrushFace( &ws, box, brush->sides.pData[0].sourceId.value );
    REQUIRE( s.Enabled( "map.texture.rotate" ) );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.texture.rotate 45" ) ) == command_result_t::OK );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.texture.scale 0.5 2" ) ) == command_result_t::OK );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.texture.shift ten" ) ) == command_result_t::INVALID_ARGUMENTS );
    CHECK( s.Steps() == steps + 3 );
    CHECK( ws.selectedBrushFaceObject == box ); // The face stays picked for the next adjustment.
}

TEST_CASE( "Merge joins whole meshes and Split separates them again", "[map][gui][geometry-commands][mesh-objects]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBoxMesh( s );
    const u64 b = AddBox( ws, { 200, 0, 0 }, { 264, 64, 64 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.mesh.merge" ) );
    CHECK_FALSE( s.Enabled( "map.mesh.split" ) ); // Each is one piece.
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.meshes.nCount == 1 );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == std::min( a, b ) );
    CHECK( description_t( ws, ws.selection.ids.pData[0] ).value.faces.nCount == 12 );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( s.Enabled( "map.mesh.split" ) );
    REQUIRE( s.Run( "map.mesh.split" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.meshes.nCount == 2 ); CHECK( ws.selection.ids.nCount == 2 );
    CHECK( ws.pDocument->nextId > std::max( a, b ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.meshes.nCount == 1 );
}

TEST_CASE( "Booleans, Convert to Brush and Replace Materials run from the registry", "[map][gui][geometry-commands][boolean]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 128, 128, 128 } );
    const u64 b = AddBox( ws, { 64, 32, 0 }, { 192, 96, 64 } );
    Select( ws, { a, b } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    REQUIRE( s.Enabled( "map.mesh.boolean_intersect" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.boolean_intersect" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.meshes.nCount == 1 ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( ws.selection.ids.nCount == 1 );
    REQUIRE( s.Enabled( "map.mesh.to_brush" ) );
    REQUIRE( s.Run( "map.mesh.to_brush" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.pDocument->geometry.meshes.nCount == 0 );
    const auto material = EditorSettings_Text( &s.gui.settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const std::string line = "map.texture.replace " + std::string( material.pData, material.cchLength ) + " materials/dev/other.cymat";
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( line.c_str() ) ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 3 );
}

TEST_CASE( "Groups select as one in Groups mode and New Layer moves the selection", "[map][gui][geometry-commands][groups]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    const u64 c = AddBox( ws, { 256, 0, 0 }, { 320, 64, 64 } );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.group.create" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.group.create" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    const u64 group = MapDocument_GroupOf( ws.pDocument, a );
    REQUIRE( group != 0 ); CHECK( MapDocument_GroupOf( ws.pDocument, b ) == group ); CHECK( MapDocument_GroupOf( ws.pDocument, c ) == 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS );
    MapWorkspace_Select( &ws, a, MAP_SELECT_REPLACE );
    CHECK( ws.selection.ids.nCount == 2 ); CHECK( MapWorkspace_IsSelected( &ws, b ) );
    MapWorkspace_Select( &ws, c, MAP_SELECT_ADD );
    CHECK( ws.selection.ids.nCount == 3 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    Select( ws, { a } );
    REQUIRE( s.Enabled( "map.group.ungroup" ) );
    REQUIRE( s.Run( "map.group.ungroup" ) == command_result_t::OK );
    CHECK( MapDocument_GroupOf( ws.pDocument, a ) == 0 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( MapDocument_GroupOf( ws.pDocument, b ) != 0 );

    Select( ws, { c } );
    const usize layers = ws.pDocument->layers.nCount;
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
    REQUIRE( ws.pDocument->layers.nCount == layers + 1 );
    const map_wire_object_t *object = MapWireframe_FindObject( ws.wire, c ); REQUIRE( object );
    CHECK( object->iLayer == layers );
    // Move to Layer takes it back by name.
    REQUIRE( s.Enabled( "map.layer.move_selection" ) );
    const std::string back = std::string( "map.layer.move_selection " ) + ws.pDocument->layers.pData[0].id;
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( back.c_str() ) ) == command_result_t::OK );
    object = MapWireframe_FindObject( ws.wire, c ); REQUIRE( object );
    CHECK( object->iLayer == 0u );
}

TEST_CASE( "New Visgroup from Selection records the members for the visibility panel", "[map][gui][geometry-commands][visgroup]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.visgroup.create" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.visgroup.create Arena" ) ) == command_result_t::OK );
    const auto *list = KeyValue_Find( SettingsDocument_Root( &ws.pDocument->root ), StringView_FromCString( "visgroups" ) );
    REQUIRE( KeyValue_ChildCount( list ) == 1 );
    u64 id = 0; REQUIRE( KeyValue_GetU64( KeyValue_Find( KeyValue_ChildAt( list, 0 ), StringView_FromCString( "id" ) ), &id ) );
    vector_t<u64> members{}; REQUIRE( Vector_Init( &members, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_VisgroupMembers( ws.pDocument, id, &members ) == map_status_t::OK );
    CHECK( members.nCount == 2 );
    Vector_Shutdown( &members );
    // The Auto Vis Groups panel lists it under Visgroups; unchecking hides its members.
    std::unique_ptr<QWidget> panel( MapVisgroups_Create( nullptr, &ws ) );
    auto *tree = panel->findChild<QTreeWidget *>(); REQUIRE( tree );
    QTreeWidgetItem *row = nullptr;
    for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) { if ( ( *it )->text( 0 ) == QStringLiteral( "Arena" ) ) { row = *it; } }
    REQUIRE( row != nullptr ); CHECK( row->text( 1 ) == QStringLiteral( "2" ) );
    row->setCheckState( 0, Qt::Unchecked );
    CHECK( EditorSelection_Contains( &ws.hidden, a ) ); CHECK( EditorSelection_Contains( &ws.hidden, b ) );
    for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) { if ( ( *it )->text( 0 ) == QStringLiteral( "Arena" ) ) { row = *it; } }
    row->setCheckState( 0, Qt::Checked );
    CHECK_FALSE( EditorSelection_Contains( &ws.hidden, a ) );
    panel.reset();
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( KeyValue_ChildCount( KeyValue_Find( SettingsDocument_Root( &ws.pDocument->root ), StringView_FromCString( "visgroups" ) ) ) == 0 );
}

TEST_CASE( "Create Prefab writes a file that Insert Prefab places again", "[map][gui][geometry-commands][prefab]" )
{
    session_t s; auto &ws = s.workspace;
    QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QByteArray path = ( directory.path() + QStringLiteral( "/crate.cyprefab" ) ).toUtf8();
    AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    REQUIRE( s.Enabled( "map.prefab.create" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( ( "map.prefab.create \"" + path.toStdString() + "\"" ).c_str() ) ) == command_result_t::OK );
    REQUIRE( QFile::exists( QString::fromUtf8( path ) ) );
    const usize brushes = ws.pDocument->geometry.brushes.nCount, steps = s.Steps();
    MapWorkspace_SetCursor( &ws, { 256, 256, 0 }, 3u );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( ( "map.prefab.insert \"" + path.toStdString() + "\"" ).c_str() ) ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == brushes + 1 ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( ws.selection.ids.nCount == 1 );
    CHECK( Box( ws, ws.selection.ids.pData[0] ).minimum.x == Catch::Approx( 224 ) );
}

namespace
{
geo::geometry_source_id_t VertexAt( const description_t &d, cypher::math::vec3d_t at )
{
    for ( usize v = 0; v < d.value.vertices.nCount; ++v ) {
        const auto p = d.value.vertices.pData[v].position;
        if ( p.x == at.x && p.y == at.y && p.z == at.z ) { return d.value.vertices.pData[v].sourceId; }
    }
    FAIL( "no vertex there" ); return {};
}
u64 FaceFacing( const map_workspace_t &ws, u64 id, cypher::math::vec3d_t n )
{
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) {
        const auto &face = ws.wire.faces.pData[f];
        if ( face.id == id && face.normal.x * n.x + face.normal.y * n.y + face.normal.z * n.z > 0.99 ) { return face.faceId != 0 ? face.faceId : face.sideId; }
    }
    FAIL( "no face facing that way" ); return 0;
}
} // namespace

TEST_CASE( "Component queries: connected, invert, path, convert, ribs, sharp and open edges", "[map][gui][geometry-commands][queries]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s ); // -64..64, z 0..128
    description_t d( ws, mesh );
    const auto a = VertexAt( d, { -64, -64, 128 } ), b = VertexAt( d, { 64, 64, 128 } ), c = VertexAt( d, { 64, -64, 128 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, a ) );
    REQUIRE( s.Run( "map.select.connected" ) == command_result_t::OK );
    CHECK( ws.meshSelection.vertices.nCount == 8 );
    REQUIRE( s.Run( "map.select.invert_components" ) == command_result_t::OK );
    CHECK( ws.meshSelection.vertices.nCount == 0 ); // Everything was selected.
    // Path across the top face: two edges through a corner.
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, a ) ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, b, MAP_SELECT_ADD ) );
    REQUIRE( s.Enabled( "map.select.path" ) );
    REQUIRE( s.Run( "map.select.path" ) == command_result_t::OK );
    CHECK( ws.meshSelection.vertices.nCount == 3 );
    REQUIRE( s.Run( "map.select.to_edges" ) == command_result_t::OK );
    CHECK( ws.elementMode == map_element_mode_t::EDGES ); CHECK( ws.meshSelection.edges.nCount == 2 );
    REQUIRE( s.Run( "map.select.to_vertices" ) == command_result_t::OK );
    CHECK( ws.elementMode == map_element_mode_t::VERTICES ); CHECK( ws.meshSelection.vertices.nCount == 3 );
    // Ribs of one top edge: the other two edges at each of its ends.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( a, c ) ) );
    REQUIRE( s.Run( "map.select.ribs" ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 4 );
    CHECK_FALSE( geo::MeshSelection_HasEdge( &ws.meshSelection, geo::MeshEdgeRef_Make( a, c ) ) );
    // A box's twelve edges are all sharp; it has no open ones until a face goes.
    CHECK_FALSE( s.Enabled( "map.select.boundary_edges" ) );
    REQUIRE( s.Run( "map.select.sharp_edges" ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 12 );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.select.sharp_edges 100" ) ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 0 ); // None meets at 100 degrees or more.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, FaceFacing( ws, mesh, { 0, 0, 1 } ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.delete_face" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( s.Enabled( "map.select.boundary_edges" ) );
    REQUIRE( s.Run( "map.select.boundary_edges" ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 4 );
}

TEST_CASE( "Offset, Flatten, Weld, Extrude to New Brush and the patch matrix through their commands", "[map][gui][geometry-commands][operations]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    // Nothing to weld on a clean box: no edit, no undo step.
    const usize clean = s.Steps();
    REQUIRE( s.Enabled( "map.mesh.weld" ) );
    CHECK( s.Run( "map.mesh.weld" ) == command_result_t::FAILED );
    CHECK( s.Run( "map.mesh.merge_by_distance" ) == command_result_t::FAILED );
    CHECK( s.Steps() == clean );
    // Offset the top face out by 16.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, FaceFacing( ws, mesh, { 0, 0, 1 } ) );
    REQUIRE( s.Enabled( "map.mesh.offset" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.mesh.offset 16" ) ) == command_result_t::OK );
    CHECK( Box( ws, mesh ).maximum.z == Catch::Approx( 144 ) );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.mesh.offset far" ) ) == command_result_t::INVALID_ARGUMENTS );
    // Flatten X on two diagonal top corners: both to their average X.
    description_t d( ws, mesh );
    const auto a = VertexAt( d, { -64, -64, 144 } ), b = VertexAt( d, { 64, 64, 144 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, a ) ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, b, MAP_SELECT_ADD ) );
    CHECK_FALSE( s.Enabled( "map.mesh.flatten" ) ); // Best fit needs three.
    REQUIRE( s.Enabled( "map.mesh.flatten_x" ) );
    REQUIRE( s.Run( "map.mesh.flatten_x" ) == command_result_t::OK );
    CHECK( ws.meshSelection.vertices.nCount == 2 );
    description_t flat( ws, mesh );
    for ( usize v = 0; v < flat.value.vertices.nCount; ++v ) {
        const auto &vertex = flat.value.vertices.pData[v];
        if ( vertex.sourceId.value == a.value || vertex.sourceId.value == b.value ) { CHECK( vertex.position.x == Catch::Approx( 0 ) ); }
    }
    // A brush face extruded into its own brush, which becomes the selection.
    const u64 brush = AddBox( ws, { 200, 0, 0 }, { 264, 64, 64 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &ws, brush, FaceFacing( ws, brush, { 0, 0, 1 } ) );
    REQUIRE( s.Enabled( "map.brush.extrude_new" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.brush.extrude_new 32" ) ) == command_result_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 added = ws.selection.ids.pData[0];
    CHECK( added != brush );
    CHECK( Box( ws, added ).minimum.z == Catch::Approx( 64 ) ); CHECK( Box( ws, added ).maximum.z == Catch::Approx( 96 ) );
    // Patch matrix on a patch made through the edit path.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    u64 patch = 0;
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Create Patch", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto *id = static_cast<u64 *>( context );
        const auto status = MapCreate_Patch( copy, map_patch_shape_t::PLANE, { { 0, 200, 0 }, { 128, 328, 0 } }, 2, 8, StringView_FromCString( "materials/dev/dev_grid.cymat" ), id );
        return status == map_status_t::OK && !Vector_PushBack( out, *id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &patch ) );
    REQUIRE( ws.selection.ids.nCount == 1 ); REQUIRE( ws.selection.ids.pData[0] == patch );
    CHECK_FALSE( s.Enabled( "map.patch.remove_row" ) );
    REQUIRE( s.Run( "map.patch.insert_row" ) == command_result_t::OK );
    REQUIRE( s.Enabled( "map.patch.remove_row" ) );
    for ( const char *id : { "map.patch.insert_column", "map.patch.transpose", "map.patch.invert", "map.patch.remove_row" } ) {
        CAPTURE( id ); CHECK( s.Run( id ) == command_result_t::OK );
    }
    Select( ws, { mesh } );
    CHECK_FALSE( s.Enabled( "map.patch.invert" ) ); // A mesh is not a patch.
}

TEST_CASE( "Faces mode holds several mesh faces: queries, Bridge, Delete and Offset act on all", "[map][gui][geometry-commands][face-set]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    const u64 top = FaceFacing( ws, mesh, { 0, 0, 1 } ), bottom = FaceFacing( ws, mesh, { 0, 0, -1 } ), east = FaceFacing( ws, mesh, { 1, 0, 0 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    REQUIRE( MapWorkspace_MeshFaces( &ws ).nCount == 1 );
    // Shift adds and makes the face active; another mesh's face is refused.
    REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, east ) );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 2 ); CHECK( ws.selectedMeshFaceId == east );
    CHECK( MapWorkspace_IsMeshFaceSelected( &ws, mesh, top ) ); CHECK_FALSE( MapWorkspace_IsMeshFaceSelected( &ws, mesh, bottom ) );
    CHECK_FALSE( MapWorkspace_AddMeshFace( &ws, mesh + 1000, top ) );
    // Ctrl removes; removing the active face makes another one active.
    REQUIRE( MapWorkspace_RemoveMeshFace( &ws, mesh, east ) );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 1 ); CHECK( ws.selectedMeshFaceId == top );
    // A plain pick replaces the set.
    REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, east ) );
    MapWorkspace_SelectMeshFace( &ws, mesh, bottom );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 1 );

    // Queries from the active face.
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    REQUIRE( s.Run( "map.select.same_corners" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 6 ); CHECK( ws.selectedMeshFaceId == top ); // All quads.
    REQUIRE( s.Run( "map.select.shrink" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 6 ); // Everything selected: no face is on the border.
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    REQUIRE( s.Run( "map.select.shrink" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 0 ); // One face is all border.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    REQUIRE( s.Run( "map.select.grow" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 5 ); // The top and its four neighbours.
    REQUIRE( s.Run( "map.select.invert_components" ) == command_result_t::OK );
    REQUIRE( MapWorkspace_MeshFaces( &ws ).nCount == 1 ); CHECK( ws.selectedMeshFaceId == bottom );
    REQUIRE( s.Run( "map.select.faces_facing" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 1 );
    REQUIRE( s.Run( "map.select.coplanar" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 1 );
    REQUIRE( s.Run( "map.select.faces_material" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 6 ); // One material everywhere.
    // Face path between opposite faces crosses one side.
    MapWorkspace_SelectMeshFace( &ws, mesh, top ); REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, bottom ) );
    REQUIRE( s.Enabled( "map.select.face_path" ) );
    REQUIRE( s.Run( "map.select.face_path" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 3 );
    // Offset both caps out by 8: the box grows 16 tall.
    MapWorkspace_SelectMeshFace( &ws, mesh, top ); REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, bottom ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.mesh.offset 8" ) ) == command_result_t::OK );
    CHECK( Box( ws, mesh ).maximum.z == Catch::Approx( 136 ) ); CHECK( Box( ws, mesh ).minimum.z == Catch::Approx( -8 ) );
    CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 2 ); // Still both.
    // Delete two faces at once.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, top ); REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, east ) );
    REQUIRE( s.Run( "map.mesh.delete_face" ) == command_result_t::OK );
    CHECK( description_t( ws, mesh ).value.faces.nCount == 4 );
}

TEST_CASE( "Bridge joins the two selected faces of a merged mesh with a tube", "[map][gui][geometry-commands][face-set]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBoxMesh( s ); // x -64..64
    const u64 b = AddBox( ws, { 200, -64, 0 }, { 328, 64, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    Select( ws, { a, b } );
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 mesh = ws.selection.ids.pData[0];
    const u64 east = FaceFacing( ws, mesh, { 1, 0, 0 } ); // The first box's side facing the second...
    u64 west = 0;                                         // ...and the second's facing back.
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) {
        const auto &face = ws.wire.faces.pData[f];
        const auto corner = ws.wire.points.pData[ws.wire.faceIndices.pData[face.iFirstIndex]];
        if ( face.id == mesh && face.normal.x < -0.99 && corner.x == 200 ) { west = face.faceId; }
    }
    REQUIRE( west != 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, east );
    CHECK_FALSE( s.Enabled( "map.mesh.bridge" ) ); // One face: nothing to bridge to.
    REQUIRE( MapWorkspace_AddMeshFace( &ws, mesh, west ) );
    REQUIRE( s.Enabled( "map.mesh.bridge" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.bridge" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    CHECK( description_t( ws, mesh ).value.faces.nCount == 12 - 2 + 4 );
}

TEST_CASE( "Bridge joins two chains of open edges strip by strip", "[map][gui][geometry-commands][bridge-chains]" )
{
    session_t s; auto &ws = s.workspace;
    u64 quads[2]{};
    const cypher::math::vec3d_t lows[]{ { 0, 0, 0 }, { 192, 0, 0 } };
    for ( int i = 0; i < 2; ++i ) {
        map_primitive_desc_t quad = MapWorkspace_PrimitiveDefaults( &ws, Bounds( lows[i], { lows[i].x + 128, 64, 0 } ) );
        quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) );
        quads[i] = EditorSelection_At( &ws.selection, 0 );
    }
    Select( ws, { quads[0], quads[1] } );
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    const u64 mesh = ws.selection.ids.pData[0];
    // Split the two facing sides so each is a chain of two edges.
    description_t d( ws, mesh );
    const auto a0 = VertexAt( d, { 128, 0, 0 } ), a1 = VertexAt( d, { 128, 64, 0 } );
    const auto b0 = VertexAt( d, { 192, 0, 0 } ), b1 = VertexAt( d, { 192, 64, 0 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( a0, a1 ) ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( b0, b1 ), MAP_SELECT_ADD ) );
    REQUIRE( s.Run( "map.mesh.split_edges" ) == command_result_t::OK );
    description_t split( ws, mesh );
    const auto am = VertexAt( split, { 128, 32, 0 } ), bm = VertexAt( split, { 192, 32, 0 } );
    bool first = true;
    for ( const auto edge : { geo::MeshEdgeRef_Make( a0, am ), geo::MeshEdgeRef_Make( am, a1 ), geo::MeshEdgeRef_Make( b0, bm ), geo::MeshEdgeRef_Make( bm, b1 ) } ) {
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, edge, first ? MAP_SELECT_REPLACE : MAP_SELECT_ADD ) );
        first = false;
    }
    REQUIRE( s.Enabled( "map.mesh.bridge" ) );
    REQUIRE( s.Run( "map.mesh.bridge" ) == command_result_t::OK );
    description_t bridged( ws, mesh );
    CHECK( bridged.value.faces.nCount == 4 ); // Two quads and the two-row strip.
    CHECK( geo::MeshBoundary_CountBoundaryEdges( &geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } )->mesh ) == 8 );
    // Three edges on one side and one on the other is not two chains.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( a0, am ) ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( am, a1 ), MAP_SELECT_ADD ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, geo::MeshEdgeRef_Make( b0, bm ), MAP_SELECT_ADD ) );
    CHECK_FALSE( s.Enabled( "map.mesh.bridge" ) );
}

TEST_CASE( "Select Inside takes what lies within the selection; Select Tall ignores the view's depth", "[map][gui][geometry-commands][select-inside]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 inner = AddBox( ws, { 16, 16, 16 }, { 48, 48, 48 } );
    const u64 above = AddBox( ws, { 16, 16, 300 }, { 48, 48, 340 } ); // Over the volume in Top, far above it.
    const u64 outside = AddBox( ws, { 400, 0, 0 }, { 450, 64, 64 } );
    const u64 volume = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    Select( ws, { volume } );
    REQUIRE( s.Enabled( "map.select.inside" ) );
    REQUIRE( s.Run( "map.select.inside" ) == command_result_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == inner );
    Select( ws, { volume } );
    ws.cursorAxes = 0; // Top: X and Y count, Z does not.
    REQUIRE( s.Run( "map.select.tall" ) == command_result_t::OK );
    CHECK( ws.selection.ids.nCount == 2 );
    CHECK( MapWorkspace_IsSelected( &ws, inner ) ); CHECK( MapWorkspace_IsSelected( &ws, above ) );
    CHECK_FALSE( MapWorkspace_IsSelected( &ws, outside ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, volume ) );
    Select( ws, { outside } );
    CHECK( s.Run( "map.select.inside" ) == command_result_t::FAILED ); // Nothing inside: unchanged.
    CHECK( MapWorkspace_IsSelected( &ws, outside ) );
}

TEST_CASE( "Make Room and Thicken Patch through their commands", "[map][gui][geometry-commands][make-room]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 brush = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    REQUIRE( s.Enabled( "map.brush.make_room" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.brush.make_room 8" ) ) == command_result_t::OK );
    CHECK( ws.selection.ids.nCount >= 6 ); // The walls.
    CHECK_FALSE( MapWorkspace_IsSelected( &ws, brush ) );
    map_bounds_t all{};
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) { MapBounds_AddBounds( all, MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] )->bounds ); }
    CHECK( all.box.minimum.x == Catch::Approx( -8 ) ); CHECK( all.box.maximum.z == Catch::Approx( 72 ) );

    u64 patch = 0;
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Create Patch", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto *id = static_cast<u64 *>( context );
        const auto status = MapCreate_Patch( copy, map_patch_shape_t::PLANE, { { 0, 200, 0 }, { 128, 328, 0 } }, 2, 8, StringView_FromCString( "materials/dev/dev_grid.cymat" ), id );
        return status == map_status_t::OK && !Vector_PushBack( out, *id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &patch ) );
    CHECK_FALSE( s.Enabled( "map.brush.make_room" ) ); // A patch is not a brush.
    REQUIRE( s.Enabled( "map.patch.thicken" ) );
    REQUIRE( s.Run( "map.patch.thicken" ) == command_result_t::OK );
    CHECK( ws.selection.ids.nCount == 6 ); // The patch, its back and four seams.
}

TEST_CASE( "Entity I/O edits outputs one undo step at a time and lists the inputs that reach an entity", "[map][gui][entity-io]" )
{
    session_t s; auto &ws = s.workspace;
    struct entities_t { u64 trigger{ 0 }, door{ 0 }; } made;
    REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Place", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *placed ) noexcept {
        auto &m = *static_cast<entities_t *>( context );
        bool changed = false;
        *placed = true;
        auto status = MapCreate_Entity( copy, StringView_FromCString( "trigger_once" ), { 0, 0, 0 }, {}, &m.trigger );
        if ( status == map_status_t::OK ) { status = MapEntityEdit_SetIdentityField( copy, { &m.trigger, 1 }, StringView_FromCString( "name" ), StringView_FromCString( "t1" ), &changed ); }
        if ( status == map_status_t::OK ) { status = MapCreate_Entity( copy, StringView_FromCString( "func_door" ), { 128, 0, 0 }, {}, &m.door ); }
        if ( status == map_status_t::OK ) { status = MapEntityEdit_SetIdentityField( copy, { &m.door, 1 }, StringView_FromCString( "name" ), StringView_FromCString( "door1" ), &changed ); }
        return status == map_status_t::OK && !Vector_PushBack( out, m.trigger ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &made ) );
    REQUIRE( MapEntityIo_Entity( &ws ) == made.trigger );
    REQUIRE( s.Run( "map.entity.io" ) == command_result_t::OK );
    QDialog *dialog = MapEntityIo_Show( &ws, nullptr ); REQUIRE( dialog );
    auto *outputs = dialog->findChild<QTableWidget *>( QStringLiteral( "MapEntityOutputs" ) ); REQUIRE( outputs );
    auto *inputs = dialog->findChild<QTableWidget *>( QStringLiteral( "MapEntityInputs" ) ); REQUIRE( inputs );
    CHECK( outputs->rowCount() == 0 );
    const usize steps = s.Steps();
    dialog->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputAdd" ) )->click();
    REQUIRE( outputs->rowCount() == 1 );
    CHECK( s.Steps() == steps + 1 );
    // Typing into the cells: each edit commits on the next event-loop turn.
    outputs->item( 0, 0 )->setText( QStringLiteral( "on_trigger" ) ); QCoreApplication::processEvents();
    outputs->item( 0, 1 )->setText( QStringLiteral( "door1" ) ); QCoreApplication::processEvents();
    outputs->item( 0, 2 )->setText( QStringLiteral( "open" ) ); QCoreApplication::processEvents();
    outputs->item( 0, 4 )->setData( Qt::EditRole, 1.5 ); QCoreApplication::processEvents();
    std::vector<map_output_t> written;
    REQUIRE( MapEntityOutputs_Read( ws.pDocument, made.trigger, written ) == map_status_t::OK );
    REQUIRE( written.size() == 1 );
    CHECK( written[0].output == "on_trigger" ); CHECK( written[0].target == "door1" ); CHECK( written[0].input == "open" ); CHECK( written[0].delay == 1.5 );
    CHECK( s.Steps() == steps + 5 );
    // Duplicate, move the copy up, and remove it again.
    outputs->selectRow( 0 );
    dialog->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputDuplicate" ) )->click();
    REQUIRE( outputs->rowCount() == 2 );
    outputs->item( 1, 1 )->setText( QStringLiteral( "nobody" ) ); QCoreApplication::processEvents();
    outputs->selectRow( 1 );
    dialog->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputUp" ) )->click();
    REQUIRE( MapEntityOutputs_Read( ws.pDocument, made.trigger, written ) == map_status_t::OK );
    REQUIRE( written.size() == 2 ); CHECK( written[0].target == "nobody" );
    CHECK( outputs->item( 0, 1 )->toolTip().contains( QStringLiteral( "No entity has this name" ) ) );
    outputs->selectRow( 0 );
    dialog->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputRemove" ) )->click();
    REQUIRE( MapEntityOutputs_Read( ws.pDocument, made.trigger, written ) == map_status_t::OK );
    REQUIRE( written.size() == 1 ); CHECK( written[0].target == "door1" );
    // The door's Inputs list the trigger's connection.
    MapWorkspace_Select( &ws, made.door, MAP_SELECT_REPLACE );
    REQUIRE( inputs->rowCount() == 1 );
    CHECK( inputs->item( 0, 0 )->text() == QStringLiteral( "t1" ) ); CHECK( inputs->item( 0, 2 )->text() == QStringLiteral( "open" ) );
    // Undo walks the edits back.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapEntityOutputs_Read( ws.pDocument, made.trigger, written ) == map_status_t::OK );
    CHECK( written.size() == 2 );
    // Nothing selected: nothing to edit.
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    CHECK( outputs->rowCount() == 0 );
    CHECK_FALSE( dialog->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputAdd" ) )->isEnabled() );
}

namespace
{
QMenu *OpenViewMenuAt( QWidget *view, QPointF at )
{
    QContextMenuEvent context( QContextMenuEvent::Mouse, at.toPoint(), view->mapToGlobal( at.toPoint() ) );
    QCoreApplication::sendEvent( view, &context );
    QCoreApplication::processEvents();
    for ( QMenu *menu : view->findChildren<QMenu *>( QStringLiteral( "MapViewMenu" ) ) ) { if ( menu->isVisible() ) { return menu; } }
    return nullptr;
}
QAction *MenuAction( QMenu *menu, const QString &name )
{
    for ( QAction *action : menu->findChildren<QAction *>() ) { if ( action->objectName() == name ) { return action; } }
    for ( QAction *action : menu->actions() ) {
        if ( action->objectName() == name ) { return action; }
        if ( action->menu() != nullptr ) { if ( QAction *found = MenuAction( action->menu(), name ) ) { return found; } }
    }
    return nullptr;
}
} // namespace

TEST_CASE( "A right click in a view opens the editing menu: it selects what was clicked and acts at the click", "[map][gui][view-menu]" )
{
    session_t s; auto &ws = s.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    const u64 box = AddBox( ws, { 0, 0, 0 }, { 128, 128, 64 } );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) );
    views->resize( 1200, 900 ); views->show(); QCoreApplication::processEvents();
    QWidget *top = MapViews_PaneView( views.get(), 1 );
    REQUIRE( top != nullptr );
    MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
    // Right click on the unselected box's outline: it becomes the selection.
    // (Clipboard and Delete items come from Mason's own commands, which
    // this bare workspace does not register.)
    QMenu *menu = OpenViewMenuAt( top, MapOrthoView_WorldToView( top, { 0, 64 } ) );
    REQUIRE( menu != nullptr );
    CHECK( MapWorkspace_IsSelected( &ws, box ) );
    for ( const char *name : { "map.view.paste_here", "map.view.pivot_here", "map.group.create", "map.brush.hollow", "map.brush.to_mesh",
                               "map.texture.fit", "map.select.touching", "map.entity.io" } ) {
        CAPTURE( name ); CHECK( MenuAction( menu, QString::fromLatin1( name ) ) != nullptr );
    }
    REQUIRE( MenuAction( menu, QStringLiteral( "map.brush.hollow" ) ) != nullptr );
    CHECK( MenuAction( menu, QStringLiteral( "map.brush.hollow" ) )->isEnabled() ); // One brush selected: Hollow can run.
    // Copy, then Paste Here at an empty spot: the copy is centred there.
    REQUIRE( MapWorkspace_CopySelection( &ws ) );
    menu->close(); QCoreApplication::processEvents();
    menu = OpenViewMenuAt( top, MapOrthoView_WorldToView( top, { 512, 512 } ) );
    REQUIRE( menu != nullptr );
    CHECK( MapWorkspace_IsSelected( &ws, box ) ); // Empty space keeps the selection.
    QAction *pasteHere = MenuAction( menu, QStringLiteral( "map.view.paste_here" ) ); REQUIRE( pasteHere );
    REQUIRE( pasteHere->isEnabled() );
    pasteHere->trigger();
    REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 pasted = ws.selection.ids.pData[0];
    CHECK( pasted != box );
    const auto bounds = MapWireframe_FindObject( ws.wire, pasted )->bounds.box;
    CHECK( ( bounds.minimum.x + bounds.maximum.x ) * 0.5 == Catch::Approx( 512 ).margin( 16 ) );
    CHECK( ( bounds.minimum.y + bounds.maximum.y ) * 0.5 == Catch::Approx( 512 ).margin( 16 ) );
    CHECK( bounds.minimum.z == Catch::Approx( 0 ) ); // The Top pane keeps the depth.
    menu->close(); QCoreApplication::processEvents();
    // Place an entity and set the pivot at a clicked point.
    menu = OpenViewMenuAt( top, MapOrthoView_WorldToView( top, { -256, 256 } ) );
    REQUIRE( menu != nullptr );
    QMenu *place = menu->findChild<QMenu *>( QStringLiteral( "MapViewMenuPlaceEntity" ) ); REQUIRE( place );
    REQUIRE( !place->actions().isEmpty() );
    place->actions().front()->trigger();
    REQUIRE( ws.selection.ids.nCount == 1 );
    const auto *entity = MapWireframe_FindEntity( ws.wire, ws.selection.ids.pData[0] ); REQUIRE( entity );
    CHECK( entity->origin.x == Catch::Approx( -256 ).margin( 16 ) ); CHECK( entity->origin.y == Catch::Approx( 256 ).margin( 16 ) );
    menu->close(); QCoreApplication::processEvents();
    menu = OpenViewMenuAt( top, MapOrthoView_WorldToView( top, { 32, -32 } ) );
    REQUIRE( menu != nullptr );
    MenuAction( menu, QStringLiteral( "map.view.pivot_here" ) )->trigger();
    cypher::math::vec3d_t pivot{};
    REQUIRE( MapToolGestures_Pivot( &ws, &pivot ) );
    CHECK( pivot.x == Catch::Approx( 32 ).margin( 16 ) ); CHECK( pivot.y == Catch::Approx( -32 ).margin( 16 ) );
    menu->close(); QCoreApplication::processEvents();
}

TEST_CASE( "Mesh operations take brushes: they become meshes in the same undo step", "[map][gui][geometry-commands][brushes-as-meshes]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 128, 128, 128 } );
    REQUIRE( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { a } ) != nullptr );
    REQUIRE( s.Enabled( "map.mesh.smooth" ) );
    REQUIRE( s.Enabled( "map.mesh.subdivide" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.subdivide" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    CHECK( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { a } ) != nullptr );
    CHECK( description_t( ws, a ).value.faces.nCount == 24 ); // A cube's six quads, subdivided once.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { a } ) != nullptr ); // Undo gives the brush back.
    // Two overlapping brushes subtract like meshes.
    const u64 b = AddBox( ws, { 64, 64, 64 }, { 192, 192, 192 } );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.mesh.boolean_subtract" ) );
    REQUIRE( s.Run( "map.mesh.boolean_subtract" ) == command_result_t::OK );
    REQUIRE( ws.selection.ids.nCount >= 1 );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { b } ) == nullptr ); // The cutter is gone.
}

TEST_CASE( "Vertices mode: Bevel, Dissolve and Fill Hole act on picked vertices", "[map][gui][geometry-commands][vertex-ops]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    description_t d( ws, mesh );
    const auto corner = VertexAt( d, { 64, 64, 128 } ), other = VertexAt( d, { -64, -64, 0 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, corner ) );
    REQUIRE( s.Enabled( "map.mesh.bevel" ) ); REQUIRE( s.Enabled( "map.mesh.dissolve" ) );
    CHECK_FALSE( s.Enabled( "map.mesh.fill_hole" ) ); // A closed box has no border.
    REQUIRE( s.Run( "map.mesh.bevel" ) == command_result_t::OK );
    CHECK( description_t( ws, mesh ).value.faces.nCount == 7 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, other ) );
    REQUIRE( s.Run( "map.mesh.dissolve" ) == command_result_t::OK );
    CHECK( description_t( ws, mesh ).value.vertices.nCount == 10 - 1 );
    // Open the top, then close it again from two of its border vertices.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, FaceFacing( ws, mesh, { 0, 0, 1 } ) );
    REQUIRE( s.Run( "map.mesh.delete_face" ) == command_result_t::OK );
    description_t open( ws, mesh );
    const auto a = VertexAt( open, { -64, -64, 128 } ), b = VertexAt( open, { 64, -64, 128 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, a ) ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, b, MAP_SELECT_ADD ) );
    CHECK_FALSE( s.Enabled( "map.mesh.dissolve" ) ); // On the border now.
    REQUIRE( s.Enabled( "map.mesh.fill_hole" ) );
    REQUIRE( s.Run( "map.mesh.fill_hole" ) == command_result_t::OK );
    CHECK( geo::MeshBoundary_CountBoundaryEdges( &geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } )->mesh ) == 0 );
}

TEST_CASE( "Shift+T paints every face of the selected objects and reports it", "[map][gui][geometry-commands][texture][notice]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -256, -64, 0 }, { -128, 64, 128 } );
    const u64 mesh = AddBoxMesh( s );
    // Every face first takes another material, so Shift+T has work to do.
    const auto material = EditorSettings_Text( &s.gui.settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
    const std::string line = "map.texture.replace " + std::string( material.pData, material.cchLength ) + " materials/dev/other.cymat";
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( line.c_str() ) ) == command_result_t::OK );
    Select( s.workspace, { brush, mesh } );
    REQUIRE( s.Enabled( "map.tool.apply_material" ) );
    const usize steps = s.Steps();
    CHECK( s.Run( "map.tool.apply_material" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "to 12 faces" ) ) ); CHECK( kind == map_notice_t::SUCCESS );
    CHECK( s.Run( "map.tool.apply_material" ) == command_result_t::OK ); // Nothing left to change: done, not failed...
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "already uses" ) ) ); CHECK( kind == map_notice_t::INFO );
    CHECK( s.Steps() == steps + 1 );                                      // ...and no empty undo step.
    Select( s.workspace, {} );
    CHECK_FALSE( s.Enabled( "map.tool.apply_material" ) );
}

TEST_CASE( "Clip started from Vertices mode works on the mesh being edited", "[map][gui][geometry-commands][clip]" )
{
    session_t s;
    const u64 mesh = AddBoxMesh( s );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::VERTICES );
    description_t d( s.workspace, mesh );
    REQUIRE( MapWorkspace_SelectMeshVertex( &s.workspace, mesh, d.value.vertices.pData[0].sourceId ) );
    REQUIRE_FALSE( MapWorkspace_CanClipSelection( &s.workspace ) );
    CHECK( s.Run( "map.tool.clip" ) == command_result_t::OK );
    CHECK( s.workspace.tool == map_tool_t::CLIP );
    CHECK( s.workspace.elementMode == map_element_mode_t::OBJECTS );
    CHECK( MapWorkspace_IsSelected( &s.workspace, mesh ) );
    CHECK( MapWorkspace_CanClipSelection( &s.workspace ) );
}

TEST_CASE( "Set Texture Scale gives every selected face the scale typed (0.25 is 128 units per repeat)", "[map][gui][geometry-commands][texture]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    const u64 mesh = AddBoxMesh( s );
    Select( s.workspace, { brush, mesh } );
    const usize steps = s.Steps();
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.texture.set_scale 0.5 0.25" ) ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    const auto *solid = geometry::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ); REQUIRE( solid != nullptr );
    const auto *store = geometry::GeometryDocument_FindBrushAttributes( &s.workspace.pDocument->geometry, { brush } ); REQUIRE( store != nullptr );
    for ( usize i = 0; i < solid->sides.nCount; ++i ) {
        const auto &uv = store->records.pData[solid->sides.pData[i].iAttributeIndex].uvProjection;
        CHECK( std::fabs( uv.worldUnitsPerUv.x ) == Catch::Approx( 256 ) ); CHECK( std::fabs( uv.worldUnitsPerUv.y ) == Catch::Approx( 128 ) );
    }
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.texture.set_scale 0" ) ) == command_result_t::INVALID_ARGUMENTS );
}

TEST_CASE( "Subdivision levels and modifiers: live, one step each, baked by Apply", "[map][gui][geometry-commands][modifiers]" )
{
    session_t s;
    const u64 mesh = AddBoxMesh( s );
    const auto faces = [&]() { usize n = 0; for ( usize i = 0; i < s.workspace.wire.faces.nCount; ++i ) { n += s.workspace.wire.faces.pData[i].id == mesh; } return n; };
    REQUIRE( faces() == 6 );
    CHECK_FALSE( s.Enabled( "map.mesh.subdivision_bake" ) ); // Nothing to bake yet.
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.subdivision_increase" ) == command_result_t::OK );
    REQUIRE( s.Run( "map.mesh.subdivision_increase" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 2 );
    CHECK( MapModifiers_SubdivisionLevel( s.workspace.pDocument, mesh ) == 2 );
    CHECK( faces() == 96 );
    REQUIRE( s.Run( "map.mesh.subdivision_level_1" ) == command_result_t::OK );
    CHECK( faces() == 24 );
    CHECK( description_t( s.workspace, mesh ).value.faces.nCount == 6 ); // The cage is still the box.
    REQUIRE( s.Run( "map.mesh.modifier_mirror_x" ) == command_result_t::OK );
    CHECK( faces() == 48 ); // Two boxes, smoothed.
    REQUIRE( s.Run( "map.mesh.modifier_remove_last" ) == command_result_t::OK ); // The subdivision stays last; the mirror went before it,
    CHECK( faces() == 12 );                                                      // so removing the last leaves the mirrored pair unsmoothed.
    REQUIRE( s.Run( "map.mesh.modifiers_apply" ) == command_result_t::OK );
    CHECK( description_t( s.workspace, mesh ).value.faces.nCount == 12 );
    CHECK_FALSE( s.Enabled( "map.mesh.modifiers_apply" ) );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( faces() == 12 ); // The mirror is back, live.
    REQUIRE( s.Run( "map.mesh.modifiers_clear" ) == command_result_t::OK );
    CHECK( faces() == 6 );
}

TEST_CASE( "Create Displacement turns a picked face into a sculptable grid mesh", "[map][gui][geometry-commands][displacement]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -128, -128, 0 }, { 128, 128, 64 } );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    u64 top = 0;
    for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
        const auto &face = s.workspace.wire.faces.pData[f];
        if ( face.id == brush && face.normal.z > 0.99 ) { top = face.sideId; }
    }
    REQUIRE( top != 0 );
    CHECK_FALSE( s.Enabled( "map.mesh.create_displacement" ) );
    MapWorkspace_SelectBrushFace( &s.workspace, brush, top );
    REQUIRE( s.Enabled( "map.mesh.create_displacement" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.create_displacement" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    REQUIRE( EditorSelection_Count( &s.workspace.selection ) == 1 );
    const u64 grid = EditorSelection_At( &s.workspace.selection, 0 );
    CHECK( grid != brush );
    description_t d( s.workspace, grid );
    CHECK( d.value.vertices.nCount == 81 ); CHECK( d.value.faces.nCount == 64 ); // Power 3: 9 x 9.
    const auto *object = MapWireframe_FindObject( s.workspace.wire, grid ); REQUIRE( object != nullptr );
    CHECK( object->bounds.box.minimum.z == Catch::Approx( 64 ) ); CHECK( object->bounds.box.maximum.z == Catch::Approx( 64 ) ); // Flat, on the face.
    CHECK( s.workspace.elementMode == map_element_mode_t::OBJECTS );
}

TEST_CASE( "Compile Map cooks the geometry and writes an interchange file the compilers can read", "[map][gui][geometry-commands][compile]" )
{
    session_t s;
    REQUIRE( MapWorkspace_New( &s.workspace ) == map_status_t::OK );
    ( void )AddBox( s.workspace, { -256, -256, -16 }, { 256, 256, 0 } );
    ( void )AddBoxMesh( s );
    CHECK( s.Run( "map.compile" ) == command_result_t::FAILED ); // Unsaved: nowhere to put the output.
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "compile_test.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &s.workspace, path ).status == map_files_status_t::OK );
    REQUIRE( s.Run( "map.compile" ) == command_result_t::OK );
    map_notice_t kind{};
    const QString notice = MapNotice_Last( &s.workspace, &kind );
    CHECK( kind == map_notice_t::SUCCESS ); CHECK( notice.contains( QStringLiteral( "2 objects" ) ) );
    QFile file( folder.filePath( QStringLiteral( "compile_test.interchange.cykv" ) ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QByteArray text = file.readAll();
    geometry::geometry_interchange_t interchange{};
    REQUIRE( geometry::GeometryInterchange_Init( &interchange, Allocator_GetSystem() ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::GeometryInterchange_TryRead( { text.constData(), static_cast<usize>( text.size() ) }, &interchange ) == geometry::geometry_status_t::OK );
    CHECK( interchange.soup.objects.nCount == 2 ); CHECK( interchange.soup.triangles.nCount == 24 ); // Two boxes, 12 triangles each.
    geometry::GeometryInterchange_Shutdown( &interchange );
}

TEST_CASE( "Box, cylindrical and planar mapping, smoothing groups and auto hard edges on meshes", "[map][gui][geometry-commands][surfacing]" )
{
    session_t s;
    const u64 mesh = AddBoxMesh( s );
    for ( const char *id : { "map.texture.project_box", "map.texture.project_cylinder", "map.texture.project_planar", "map.mesh.smoothing_set", "map.mesh.auto_hard_edges" } ) {
        CAPTURE( id ); REQUIRE( s.Enabled( id ) );
        const usize steps = s.Steps();
        REQUIRE( s.Run( id ) == command_result_t::OK );
        CHECK( s.Steps() <= steps + 1 );
    }
    description_t d( s.workspace, mesh );
    for ( usize f = 0; f < d.value.faces.nCount; ++f ) { CHECK( d.value.faces.pData[f].attributes.smoothingGroups == 1u ); }
    REQUIRE( s.Run( "map.mesh.smoothing_clear" ) == command_result_t::OK );
    description_t cleared( s.workspace, mesh );
    for ( usize f = 0; f < cleared.value.faces.nCount; ++f ) { CHECK( cleared.value.faces.pData[f].attributes.smoothingGroups == 0u ); }
    // On a brush, mapping makes it a mesh first (two undo steps), keeping its ID.
    const u64 brush = AddBox( s.workspace, { 200, 200, 0 }, { 264, 264, 64 } );
    Select( s.workspace, { brush } );
    REQUIRE( s.Enabled( "map.texture.project_box" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.texture.project_box" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 2 );
    const auto *converted = MapWireframe_FindObject( s.workspace.wire, brush );
    REQUIRE( converted != nullptr ); CHECK( converted->kind == map_wire_kind_t::MESH );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "is now a mesh" ) ) );
    // Undo twice: the mapping, then the conversion.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    CHECK( MapWireframe_FindObject( s.workspace.wire, brush )->kind == map_wire_kind_t::BRUSH );
}

TEST_CASE( "Patch from Face covers a brush face with patches; Brush Sweep extrudes it in steps", "[map][gui][geometry-commands][brush-face-tools]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 64 } );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    u64 east = 0;
    for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
        const auto &face = s.workspace.wire.faces.pData[f];
        if ( face.id == brush && face.normal.x > 0.99 ) { east = face.sideId; }
    }
    REQUIRE( east != 0 );
    MapWorkspace_SelectBrushFace( &s.workspace, brush, east );
    REQUIRE( s.Run( "map.patch.from_face" ) == command_result_t::OK );
    CHECK( geometry::GeometryDocument_PatchCount( &s.workspace.pDocument->geometry ) == 1 ); // A quad face: one patch.
    CHECK( s.workspace.pDocument->geometry.brushes.nCount == 1 );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &s.workspace, brush, east );
    REQUIRE( s.Run( "map.brush.sweep" ) == command_result_t::OK ); // Straight, 128 out in 4 segments.
    CHECK( s.workspace.pDocument->geometry.brushes.nCount == 5 );
    CHECK( EditorSelection_Count( &s.workspace.selection ) == 4 );
    map_bounds_t swept{};
    for ( usize i = 0; i < 4; ++i ) { MapBounds_AddBounds( swept, MapWireframe_FindObject( s.workspace.wire, EditorSelection_At( &s.workspace.selection, i ) )->bounds ); }
    CHECK( swept.box.minimum.x == Catch::Approx( 128 ) ); CHECK( swept.box.maximum.x == Catch::Approx( 256 ) );
}

TEST_CASE( "Wrap Texture continues a face's material and alignment over the coplanar faces it touches", "[map][gui][geometry-commands][brush-face-tools][wrap]" )
{
    session_t s;
    const u64 a = AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 64 } );
    const u64 b = AddBox( s.workspace, { 128, 0, 0 }, { 256, 128, 64 } );
    const u64 c = AddBox( s.workspace, { 256, 0, 0 }, { 384, 128, 64 } );
    const u64 apart = AddBox( s.workspace, { 1000, 0, 0 }, { 1128, 128, 64 } ); // Same plane, not touching.
    const auto top = [&]( u64 brush ) {
        for ( usize f = 0; f < s.workspace.wire.faces.nCount; ++f ) {
            const auto &face = s.workspace.wire.faces.pData[f];
            if ( face.id == brush && face.normal.z > 0.99 ) { return face.sideId; }
        }
        return u64{ 0 };
    };
    const auto record = [&]( u64 brush ) {
        const auto *solid = geometry::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ); REQUIRE( solid != nullptr );
        const auto *store = geometry::GeometryDocument_FindBrushAttributes( &s.workspace.pDocument->geometry, solid->sourceId ); REQUIRE( store != nullptr );
        for ( usize k = 0; k < solid->sides.nCount; ++k ) {
            if ( solid->sides.pData[k].sourceId.value == top( brush ) ) { return store->records.pData[solid->sides.pData[k].iAttributeIndex]; }
        }
        FAIL( "no top side" ); return geometry::geometry_brush_side_attributes_t{};
    };
    // The seed face wears another material, shifted off the grid.
    struct seed_t { u64 brush, side; } seed{ a, top( a ) };
    REQUIRE( seed.side != 0 );
    REQUIRE( MapWorkspace_ApplyEdit( &s.workspace, "Seed", []( void *context, map_document_t *copy, vector_t<u64> * ) noexcept {
        const auto &one = *static_cast<seed_t *>( context );
        const map_surface_ref_t face{ one.brush, one.side };
        const auto status = MapTextureEdit_SetMaterial( copy, { &face, 1 }, StringView_FromCString( "materials/dev/other.cymat" ), nullptr );
        return status != map_status_t::OK ? status : MapTextureEdit_Shift( copy, { &face, 1 }, { 32.0, 16.0 }, 128.0 );
    }, &seed ) );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &s.workspace, a, seed.side );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.texture.wrap_coplanar" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "across 2 coplanar faces" ) ) ); CHECK( kind == map_notice_t::SUCCESS );
    const auto source = record( a );
    for ( u64 brush : { b, c } ) {
        CAPTURE( brush );
        const auto wrapped = record( brush );
        CHECK( wrapped.material.value == source.material.value );
        CHECK( wrapped.uvProjection.origin.x == source.uvProjection.origin.x ); CHECK( wrapped.uvProjection.origin.y == source.uvProjection.origin.y );
        CHECK( wrapped.uvProjection.uAxis.x == source.uvProjection.uAxis.x ); CHECK( wrapped.uvProjection.vAxis.y == source.uvProjection.vAxis.y );
    }
    CHECK( record( apart ).material.value != source.material.value );
    // Wrapping again finds nothing to change and says so; no empty undo step.
    CHECK( s.Run( "map.texture.wrap_coplanar" ) == command_result_t::FAILED );
    CHECK( MapNotice_Last( &s.workspace, &kind ).contains( QStringLiteral( "already continue" ) ) ); CHECK( kind == map_notice_t::INFO );
    CHECK( s.Steps() == steps + 1 );
}

TEST_CASE( "UV seams mark and clear, UV islands select, Repair Mesh rebuilds and reports", "[map][gui][geometry-commands][uv-seams]" )
{
    session_t s;
    const u64 mesh = AddBoxMesh( s );
    description_t d( s.workspace, mesh );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::EDGES );
    const auto &face = d.value.faces.pData[0];
    const auto a = d.value.vertices.pData[d.value.corners.pData[face.iFirstCorner].iVertex].sourceId;
    const auto b = d.value.vertices.pData[d.value.corners.pData[face.iFirstCorner + 1].iVertex].sourceId;
    REQUIRE( MapWorkspace_SelectMeshEdge( &s.workspace, mesh, geometry::MeshEdgeRef_Make( a, b ) ) );
    REQUIRE( s.Run( "map.mesh.seam_mark" ) == command_result_t::OK );
    REQUIRE( s.Run( "map.mesh.seam_clear" ) == command_result_t::OK );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &s.workspace, mesh, face.sourceId.value );
    REQUIRE( s.Run( "map.select.uv_island" ) == command_result_t::OK );
    CHECK( MapWorkspace_MeshFaces( &s.workspace ).nCount >= 1 );
    MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::OBJECTS );
    Select( s.workspace, { mesh } );
    REQUIRE( s.Run( "map.mesh.repair" ) == command_result_t::OK ); // A clean box: nothing to fix, still a closed box.
    description_t repaired( s.workspace, mesh );
    CHECK( repaired.value.faces.nCount == 6 ); CHECK( repaired.value.vertices.nCount == 8 );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &s.workspace, &kind ).startsWith( QStringLiteral( "Repaired:" ) ) ); CHECK( kind == map_notice_t::SUCCESS );
}

TEST_CASE( "Autosave writes numbered copies of unsaved work and keeps the newest few", "[map][gui][autosave]" )
{
    session_t s;
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString root = folder.filePath( QStringLiteral( "arena.cymap" ) );
    ( void )AddBox( s.workspace, { 0, 0, 0 }, { 128, 128, 64 } );
    REQUIRE( MapWorkspace_SaveAs( &s.workspace, root ).status == map_files_status_t::OK );
    const QString autosaves = MapAutosave_Folder( &s.workspace );
    CHECK( autosaves == QDir( folder.path() ).filePath( QStringLiteral( "autosave" ) ) );
    map_autosave_t state{};
    QString path, newest;
    CHECK( MapAutosave_Run( &s.workspace, &state, autosaves, 2, &path ) == map_autosave_status_t::UNCHANGED ); // Saved: nothing to keep.
    for ( int i = 1; i <= 3; ++i ) {
        CAPTURE( i );
        ( void )AddBox( s.workspace, { 256.0 * i, 0, 0 }, { 256.0 * i + 64, 64, 64 } );
        REQUIRE( MapAutosave_Run( &s.workspace, &state, autosaves, 2, &path ) == map_autosave_status_t::SAVED );
        CHECK( path == QDir( autosaves ).filePath( QStringLiteral( "arena.%1.cymap" ).arg( i ) ) );
        newest = path;
        CHECK( MapAutosave_Run( &s.workspace, &state, autosaves, 2, &path ) == map_autosave_status_t::UNCHANGED ); // Nothing new since.
    }
    // The copy is the map as it is now; the open map stays where it was, unsaved.
    CHECK( s.workspace.path == root );
    CHECK( MapWorkspace_IsModified( &s.workspace ) );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), newest.toUtf8().constData() ).status == map_files_status_t::OK );
    CHECK( loaded.geometry.brushes.nCount == 4u );
    // Only the newest two copies stay, each one file.
    QDir dir( autosaves );
    CHECK_FALSE( dir.exists( QStringLiteral( "arena.1.cymap" ) ) );
    CHECK( dir.exists( QStringLiteral( "arena.2.cymap" ) ) ); CHECK( dir.exists( QStringLiteral( "arena.3.cymap" ) ) );
    CHECK( dir.entryList( QDir::AllEntries | QDir::NoDotAndDotDot ).size() == 2 );
}

TEST_CASE( "Groups: select, add, remove, copy with the objects, dissolve on delete, and rename and select from the Outliner", "[map][gui][groups]" )
{
    session_t s;
    auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    const u64 c = AddBox( ws, { 256, 0, 0 }, { 320, 64, 64 } );
    Select( ws, { a, b } );
    REQUIRE( s.Run( "map.group.create" ) == command_result_t::OK );
    const u64 group = MapDocument_GroupOf( ws.pDocument, a );
    REQUIRE( group != 0 ); REQUIRE( MapDocument_GroupOf( ws.pDocument, b ) == group );
    // Objects mode picks inside the group; Select Group widens to all of it.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    Select( ws, { a } );
    REQUIRE( s.Run( "map.group.select" ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &ws.selection ) == 2 );
    // A leaves the group; then joins it again with B selected.
    Select( ws, { a } );
    REQUIRE( s.Run( "map.group.remove" ) == command_result_t::OK );
    CHECK( MapDocument_GroupOf( ws.pDocument, a ) == 0 ); CHECK( MapDocument_GroupOf( ws.pDocument, b ) == group );
    Select( ws, { a, b, c } );
    CHECK( s.Enabled( "map.group.remove" ) ); // B is grouped.
    Select( ws, { a, b } );
    REQUIRE( s.Run( "map.group.add" ) == command_result_t::OK );
    CHECK( MapDocument_GroupOf( ws.pDocument, a ) == group );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).startsWith( QStringLiteral( "Added 1 object" ) ) );
    Select( ws, { c } );
    CHECK_FALSE( s.Enabled( "map.group.add" ) ); // No group selected.
    // Groups mode: a duplicate is a new group around the copies.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS );
    MapWorkspace_Select( &ws, a, MAP_SELECT_REPLACE );
    REQUIRE( EditorSelection_Count( &ws.selection ) == 2 );
    REQUIRE( MapWorkspace_DuplicateSelection( &ws, { 0, 256, 0 } ) );
    REQUIRE( EditorSelection_Count( &ws.selection ) == 2 );
    const u64 copy = EditorSelection_At( &ws.selection, 0 );
    const u64 copyGroup = MapDocument_GroupOf( ws.pDocument, copy );
    CHECK( copyGroup != 0 ); CHECK( copyGroup != group );
    CHECK( MapDocument_GroupOf( ws.pDocument, EditorSelection_At( &ws.selection, 1 ) ) == copyGroup );
    // Deleting the copies takes their group with them.
    REQUIRE( MapWorkspace_DeleteSelection( &ws ) );
    CHECK( MapGroups_All( ws.pDocument ) == std::vector<u64>{ group } );
    // The Outliner: clicking the group row selects its members; a double-click renames it.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    MapWorkspace_SetSelection( &ws, nullptr, 0 );
    std::unique_ptr<QWidget> outliner( MapOutliner_Create( nullptr, &ws ) );
    auto *tree = outliner->findChild<QTreeWidget *>( QStringLiteral( "MapOutlinerTree" ) );
    REQUIRE( tree != nullptr );
    QTreeWidgetItem *row = nullptr;
    for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) {
        if ( ( *it )->data( 0, Qt::UserRole ).toULongLong() == group ) { row = *it; }
    }
    REQUIRE( row != nullptr );
    CHECK( ( row->flags() & Qt::ItemIsSelectable ) != 0 );
    row->setSelected( true );
    CHECK( EditorSelection_Count( &ws.selection ) == 2 );
    CHECK( MapWorkspace_IsSelected( &ws, a ) ); CHECK( MapWorkspace_IsSelected( &ws, b ) );
    const usize steps = s.Steps();
    emit tree->itemDoubleClicked( row, 0 );
    row->setText( 0, QStringLiteral( "pillars" ) );
    QCoreApplication::processEvents(); // The rename lands after the item's signal.
    CHECK( MapGroups_Name( ws.pDocument, group ) == "pillars" );
    CHECK( s.Steps() == steps + 1 );
    CHECK( EditorSelection_Count( &ws.selection ) == 2 ); // Renaming keeps the selection.

    // The Outliner's keys (keymap context `outliner`) come before window
    // shortcuts: F2 renames the current group, F frames the selection.
    const auto groupRow = [&]() -> QTreeWidgetItem * {
        for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) { if ( ( *it )->data( 0, Qt::UserRole ).toULongLong() == group ) { return *it; } }
        return nullptr;
    };
    const auto overrides = [&]( Qt::Key key ) {
        QKeyEvent event( QEvent::ShortcutOverride, key, Qt::NoModifier );
        event.ignore();
        QCoreApplication::sendEvent( tree, &event );
        return event.isAccepted();
    };
    REQUIRE( groupRow() != nullptr );
    tree->setCurrentItem( groupRow() );
    CHECK( overrides( Qt::Key_F2 ) );
    QKeyEvent f2( QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier );
    QCoreApplication::sendEvent( tree, &f2 );
    groupRow()->setText( 0, QStringLiteral( "columns" ) );
    QCoreApplication::processEvents();
    CHECK( MapGroups_Name( ws.pDocument, group ) == "columns" );
    CHECK( overrides( Qt::Key_F ) ); // A selection to frame.
    MapWorkspace_SetSelection( &ws, nullptr, 0 );
    CHECK_FALSE( overrides( Qt::Key_F ) ); // Nothing to frame: the key is left to the window.
    CHECK_FALSE( overrides( Qt::Key_G ) );
}

TEST_CASE( "Cap Patch closes a patch cylinder in one undo step and says when nothing is closed", "[map][gui][geometry-commands][patch-cap]" )
{
    session_t s;
    auto &ws = s.workspace;
    u64 tube = 0;
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Create Patch", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto *id = static_cast<u64 *>( context );
        const auto status = MapCreate_Patch( copy, map_patch_shape_t::CYLINDER, { { 0, 0, 0 }, { 128, 128, 128 } }, 2, 8, StringView_FromCString( "materials/dev/dev_grid.cymat" ), id );
        return status == map_status_t::OK && !Vector_PushBack( out, *id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &tube ) );
    REQUIRE( s.Enabled( "map.patch.cap" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.patch.cap" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    CHECK( geometry::GeometryDocument_PatchCount( &ws.pDocument->geometry ) == 3 );
    CHECK( EditorSelection_Count( &ws.selection ) == 3 );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ) == QStringLiteral( "2 caps added" ) );
    // The caps themselves are flat: nothing more to close.
    std::vector<u64> caps;
    for ( usize i = 0; i < ws.pDocument->geometry.patches.nCount; ++i ) {
        const u64 id = ws.pDocument->geometry.patches.pData[i]->sourceId.value;
        if ( id != tube ) { caps.push_back( id ); }
    }
    Select( ws, caps );
    CHECK( s.Run( "map.patch.cap" ) == command_result_t::FAILED );
    CHECK( MapNotice_Last( &ws, &kind ).startsWith( QStringLiteral( "Cap Patch: no selected patch" ) ) ); CHECK( kind == map_notice_t::INFO );
    CHECK( s.Steps() == steps + 1 );
}

TEST_CASE( "Fast Texture fits the picked face into the rectangle dragged or the cell clicked on its texture", "[map][gui][fast-texture]" )
{
    namespace geo = cypher::editor::geometry;
    session_t s;
    auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    u64 top = 0;
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) {
        const auto &face = ws.wire.faces.pData[f];
        if ( face.id == mesh && face.normal.z > 0.99 ) { top = face.faceId; }
    }
    REQUIRE( top != 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    REQUIRE( s.Run( "map.texture.fast" ) == command_result_t::OK );
    QDialog *window = MapFastTexture_Show( &ws, nullptr );
    REQUIRE( window != nullptr );
    CHECK( MapFastTexture_Material( window ).endsWith( QStringLiteral( ".cymat" ) ) );
    const auto uvBounds = [&]() {
        geo::mesh_source_description_t d{};
        REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { mesh } ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } ), &d ) == geo::geometry_status_t::OK );
        QRectF bounds;
        for ( usize i = 0; i < d.faces.nCount; ++i ) {
            if ( d.faces.pData[i].sourceId.value != top ) { continue; }
            f64 u0 = 1e9, v0 = 1e9, u1 = -1e9, v1 = -1e9;
            for ( u32 k = 0; k < d.faces.pData[i].cCorners; ++k ) {
                const auto uv = d.corners.pData[d.faces.pData[i].iFirstCorner + k].attributes.uv0;
                u0 = std::min( u0, uv.x ); v0 = std::min( v0, uv.y ); u1 = std::max( u1, uv.x ); v1 = std::max( v1, uv.y );
            }
            bounds = QRectF( QPointF( u0, v0 ), QPointF( u1, v1 ) );
        }
        geo::MeshSourceDescription_Shutdown( &d );
        return bounds;
    };
    const usize steps = s.Steps();
    REQUIRE( MapFastTexture_Choose( window, QRectF( 0.5, 0.0, 0.5, 0.25 ) ) );
    CHECK( s.Steps() == steps + 1 );
    auto bounds = uvBounds();
    CHECK( bounds.left() == Catch::Approx( 0.5 ) ); CHECK( bounds.right() == Catch::Approx( 1.0 ) );
    CHECK( bounds.top() == Catch::Approx( 0.0 ).margin( 1e-9 ) ); CHECK( bounds.bottom() == Catch::Approx( 0.25 ) );
    CHECK( MapWorkspace_HasMeshFace( &ws ) ); // The pick survives.
    // A click on the canvas picks the grid cell under it (4 x 4 by default).
    auto *canvas = window->findChild<QWidget *>( QStringLiteral( "MapFastTextureCanvas" ) );
    REQUIRE( canvas != nullptr );
    window->resize( 560, 640 ); window->show(); QCoreApplication::processEvents();
    const QRectF area = QRectF( canvas->rect() ).adjusted( 8, 8, -8, -8 );
    const f64 side = std::min( area.width(), area.height() );
    const QPointF origin( area.center().x() - side / 2, area.center().y() - side / 2 );
    const QPointF at = origin + QPointF( side * 0.1, side * 0.6 ); // Column 0, row 2.
    QMouseEvent press( QEvent::MouseButtonPress, at, canvas->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( canvas, &press );
    QMouseEvent release( QEvent::MouseButtonRelease, at, canvas->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( canvas, &release );
    CHECK( s.Steps() == steps + 2 );
    bounds = uvBounds();
    CHECK( bounds.left() == Catch::Approx( 0.0 ).margin( 1e-9 ) ); CHECK( bounds.right() == Catch::Approx( 0.25 ) );
    CHECK( bounds.top() == Catch::Approx( 0.5 ) ); CHECK( bounds.bottom() == Catch::Approx( 0.75 ) );
    window->close();
}

TEST_CASE( "Groups mode outlines each group and names it in the 2D and 3D views", "[map][gui][groups][group-outline]" )
{
    session_t s;
    auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } ), b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    Select( ws, { a, b } );
    REQUIRE( s.Run( "map.group.create" ) == command_result_t::OK );
    MapWorkspace_SetSelection( &ws, nullptr, 0 );
    for ( const bool camera : { false, true } ) {
        CAPTURE( camera );
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        view->resize( 640, 480 ); view->show(); QCoreApplication::processEvents();
        MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); QCoreApplication::processEvents();
        const QImage objects = view->grab().toImage();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS ); QCoreApplication::processEvents();
        CHECK( view->grab().toImage() != objects ); // The frame and its name appear.
    }
}

TEST_CASE( "Hotspot regions are marked in Fast Texture, saved beside the material, and applied to faces", "[map][gui][fast-texture][hotspots]" )
{
    namespace geo = cypher::editor::geometry;
    session_t s;
    auto &ws = s.workspace;
    QTemporaryDir content; REQUIRE( content.isValid() );
    const QString material = QString::fromUtf8( EditorSettings_Text( &s.gui.settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) ).pData );
    REQUIRE( QDir( content.path() ).mkpath( QFileInfo( material ).path() ) );
    { QFile file( QDir( content.path() ).filePath( material ) ); REQUIRE( file.open( QIODevice::WriteOnly ) ); file.write( "@cykv 1\n" ); }
    MapHotspots_SetResolver( &ws, [&]( const QString &path ) {
        const QString file = QDir( content.path() ).filePath( path );
        return QFileInfo::exists( file ) ? file : QString();
    } );
    const u64 mesh = AddBoxMesh( s );
    u64 top = 0;
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) { if ( ws.wire.faces.pData[f].id == mesh && ws.wire.faces.pData[f].normal.z > 0.99 ) { top = ws.wire.faces.pData[f].faceId; } }
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( &ws, mesh, top );
    QDialog *window = MapFastTexture_Show( &ws, nullptr );
    REQUIRE( window != nullptr );
    CHECK( MapFastTexture_HotspotCount( window ) == 0u );
    CHECK_FALSE( s.Run( "map.texture.hotspot" ) == command_result_t::OK ); // No regions yet.
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "has no hotspot rectangles" ) ) );
    auto *add = window->findChild<QPushButton *>( QStringLiteral( "MapFastTextureAddHotspot" ) );
    auto *remove = window->findChild<QPushButton *>( QStringLiteral( "MapFastTextureRemoveHotspot" ) );
    REQUIRE( add != nullptr ); REQUIRE( remove != nullptr );
    REQUIRE( MapFastTexture_Choose( window, QRectF( 0.0, 0.5, 0.5, 0.5 ) ) ); add->click(); // A square panel.
    REQUIRE( MapFastTexture_Choose( window, QRectF( 0.0, 0.0, 1.0, 0.25 ) ) ); add->click(); // A strip.
    CHECK( MapFastTexture_HotspotCount( window ) == 2u );
    const QString sidecar = QDir( content.path() ).filePath( QString::fromStdString( MapHotspots_SidecarPath( StringView_FromCString( material.toUtf8().constData() ) ) ) );
    CHECK( QFileInfo::exists( sidecar ) );
    // The 128 x 128 top takes the square panel.
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.texture.hotspot" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    CHECK( MapNotice_Last( &ws, &kind ).startsWith( QStringLiteral( "Hotspots applied to 1 face" ) ) );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { mesh } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } ), &d ) == geo::geometry_status_t::OK );
    for ( usize i = 0; i < d.faces.nCount; ++i ) {
        if ( d.faces.pData[i].sourceId.value != top ) { continue; }
        for ( u32 k = 0; k < d.faces.pData[i].cCorners; ++k ) {
            const auto uv = d.corners.pData[d.faces.pData[i].iFirstCorner + k].attributes.uv0;
            CHECK( uv.x >= -1e-9 ); CHECK( uv.x <= 0.5 + 1e-9 ); CHECK( uv.y >= 0.5 - 1e-9 ); CHECK( uv.y <= 1.0 + 1e-9 );
        }
    }
    geo::MeshSourceDescription_Shutdown( &d );
    // Remove the region under the chosen rectangle's centre.
    REQUIRE( MapFastTexture_Choose( window, QRectF( 0.125, 0.625, 0.25, 0.25 ) ) ); remove->click();
    CHECK( MapFastTexture_HotspotCount( window ) == 1u );
    window->close();
    MapHotspots_SetResolver( &ws, {} );
}

TEST_CASE( "Tie to Entity makes a brush entity of the selection; Move to World puts it back and drops the empty entity", "[map][gui][entity-tie]" )
{
    session_t s;
    auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } ), b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    Select( ws, { a, b } );
    REQUIRE( s.Enabled( "map.entity.tie" ) );
    CHECK_FALSE( s.Enabled( "map.entity.move_to_world" ) ); // World geometry already.
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.entity.tie" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    const auto *pa = MapWireframe_FindObject( ws.wire, a ), *pb = MapWireframe_FindObject( ws.wire, b );
    REQUIRE( pa != nullptr ); REQUIRE( pb != nullptr );
    REQUIRE( pa->owner != 0 ); CHECK( pb->owner == pa->owner );
    const u64 entity = pa->owner;
    const auto *record = MapWireframe_FindEntity( ws.wire, entity );
    REQUIRE( record != nullptr ); CHECK( QString::fromUtf8( record->className ) == QStringLiteral( "func_detail" ) );
    CHECK( MapWorkspace_IsSelected( &ws, entity ) );
    // A third box joins that entity when both are selected.
    const u64 c = AddBox( ws, { 256, 0, 0 }, { 320, 64, 64 } );
    Select( ws, { c, entity } );
    REQUIRE( s.Run( "map.entity.tie" ) == command_result_t::OK );
    CHECK( MapWireframe_FindObject( ws.wire, c )->owner == entity );
    // Move to World on the entity: every piece back in the world, the entity gone.
    Select( ws, { entity } );
    REQUIRE( s.Enabled( "map.entity.move_to_world" ) );
    REQUIRE( s.Run( "map.entity.move_to_world" ) == command_result_t::OK );
    for ( u64 id : { a, b, c } ) { CAPTURE( id ); CHECK( MapWireframe_FindObject( ws.wire, id )->owner == 0u ); }
    CHECK( MapWireframe_FindEntity( ws.wire, entity ) == nullptr );
    CHECK( EditorSelection_Count( &ws.selection ) == 3 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( MapWireframe_FindObject( ws.wire, a )->owner == entity );
}

TEST_CASE( "Find Entities lists entities by class, name or value; Find and Replace changes the values in one undo step", "[map][gui][find-entities]" )
{
    session_t s;
    auto &ws = s.workspace;
    const auto entity = [&]( const char *cls, const char *name, cypher::math::vec3d_t at ) {
        struct job_t { const char *cls, *name; cypher::math::vec3d_t at; u64 id; } job{ cls, name, at, 0 };
        REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Entity", []( void *context, map_document_t *copy, vector_t<u64> *, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            *changed = true;
            auto status = MapCreate_Entity( copy, StringView_FromCString( j.cls ), j.at, {}, &j.id );
            bool named = false;
            if ( status == map_status_t::OK ) { status = MapEntityEdit_SetIdentityField( copy, { &j.id, 1 }, StringView_FromCString( "name" ), StringView_FromCString( j.name ), &named ); }
            if ( status == map_status_t::OK ) { status = MapEntity_SetString( copy, j.id, StringView_FromCString( "message" ), StringView_FromCString( "open the door" ) ); }
            return status;
        }, &job ) );
        return job.id;
    };
    const u64 a = entity( "logic_relay", "door_relay", { 0, 0, 0 } ), b = entity( "trigger_once", "door_trigger", { 64, 0, 0 } ), c = entity( "light", "lamp", { 128, 0, 0 } );
    CHECK( MapFindEntities_Search( &ws, QStringLiteral( "DOOR" ) ).size() >= 2 );  // Names and values, any case.
    CHECK( MapFindEntities_Search( &ws, QStringLiteral( "light" ), QStringLiteral( "class" ) ).size() == 1 );
    CHECK( MapFindEntities_Search( &ws, {} ).size() >= 3 ); // Empty: every entity once.
    QDialog *window = s.Run( "edit.find_replace" ) == command_result_t::OK ? MapFindEntities_Show( &ws, nullptr, true ) : nullptr;
    REQUIRE( window != nullptr );
    window->findChild<QLineEdit *>( QStringLiteral( "MapFindText" ) )->setText( QStringLiteral( "door" ) );
    window->findChild<QLineEdit *>( QStringLiteral( "MapFindKey" ) )->setText( QStringLiteral( "message" ) );
    window->findChild<QLineEdit *>( QStringLiteral( "MapFindReplaceWith" ) )->setText( QStringLiteral( "gate" ) );
    CHECK( window->findChild<QTreeWidget *>( QStringLiteral( "MapFindResults" ) )->topLevelItemCount() == 3 );
    const usize steps = s.Steps();
    window->findChild<QPushButton *>( QStringLiteral( "MapFindReplaceAll" ) )->click();
    CHECK( s.Steps() == steps + 1 );
    CHECK( MapFindEntities_Search( &ws, QStringLiteral( "open the gate" ), QStringLiteral( "message" ) ).size() == 3 );
    CHECK( MapFindEntities_Search( &ws, QStringLiteral( "door_relay" ) ).size() == 1 ); // Names untouched: only `message` was asked.
    CHECK( MapFindEntities_Replace( &ws, { a, b, c }, QStringLiteral( "door_" ), QStringLiteral( "gate_" ), QStringLiteral( "name" ) ) == 2 );
    CHECK( MapFindEntities_Search( &ws, QStringLiteral( "gate_trigger" ) ).size() == 1 );
    window->close();
}

TEST_CASE( "Export Wavefront OBJ writes every brush, mesh and patch with UVs, normals and materials", "[map][gui][export-obj]" )
{
    session_t s;
    auto &ws = s.workspace;
    ( void )AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 mesh = AddBoxMesh( s );
    ( void )mesh;
    u64 patch = 0;
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Create Patch", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
        auto *id = static_cast<u64 *>( context );
        const auto status = MapCreate_Patch( copy, map_patch_shape_t::CYLINDER, { { 200, 0, 0 }, { 264, 64, 128 } }, 2, 8, StringView_FromCString( "materials/dev/dev_grid.cymat" ), id );
        return status == map_status_t::OK && !Vector_PushBack( out, *id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &patch ) );
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "arena.obj" ) );
    const std::string line = "file.export " + path.toStdString();
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( line.c_str() ) ) == command_result_t::OK );
    QFile obj( path ); REQUIRE( obj.open( QIODevice::ReadOnly ) );
    const QStringList lines = QString::fromUtf8( obj.readAll() ).split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    int vertices = 0, uvs = 0, normals = 0, faces = 0, objects = 0, materials = 0;
    bool indicesValid = true, uvsHaveTwo = true;
    for ( const QString &l : lines ) {
        if ( l.startsWith( QStringLiteral( "v " ) ) ) { ++vertices; }
        else if ( l.startsWith( QStringLiteral( "vt " ) ) ) { ++uvs; uvsHaveTwo = uvsHaveTwo && l.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts ).size() == 3; }
        else if ( l.startsWith( QStringLiteral( "vn " ) ) ) { ++normals; }
        else if ( l.startsWith( QStringLiteral( "o " ) ) ) { ++objects; }
        else if ( l.startsWith( QStringLiteral( "usemtl " ) ) ) { ++materials; }
        else if ( l.startsWith( QStringLiteral( "f " ) ) ) {
            ++faces;
            for ( const QString &corner : l.mid( 2 ).split( QLatin1Char( ' ' ), Qt::SkipEmptyParts ) ) {
                const int index = corner.split( QLatin1Char( '/' ) ).first().toInt();
                indicesValid = indicesValid && index >= 1 && index <= vertices;
            }
        }
    }
    CHECK( objects == 3 ); CHECK( vertices > 0 ); CHECK( uvs == vertices ); CHECK( normals == vertices ); CHECK( materials >= 3 );
    CHECK( faces >= 12 + 12 ); CHECK( indicesValid ); CHECK( uvsHaveTwo );
    CHECK( QFileInfo::exists( folder.filePath( QStringLiteral( "arena.mtl" ) ) ) );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).startsWith( QStringLiteral( "Exported 3 objects" ) ) );
}

TEST_CASE( "Import OBJ round-trips an export, keeps UVs, turns Y up and splits bent faces", "[map][gui][import-obj]" )
{
    namespace geo = cypher::editor::geometry;
    session_t s;
    auto &ws = s.workspace;
    ( void )AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    ( void )AddBoxMesh( s );
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString exported = folder.filePath( QStringLiteral( "boxes.obj" ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( ( "file.export " + exported.toStdString() ).c_str() ) ) == command_result_t::OK );
    const usize meshesBefore = ws.pDocument->geometry.meshes.nCount;
    const usize steps = s.Steps();
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( ( "file.import " + exported.toStdString() ).c_str() ) ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    CHECK( ws.pDocument->geometry.meshes.nCount == meshesBefore + 2 ); // The brush and the mesh, each a mesh now.
    CHECK( EditorSelection_Count( &ws.selection ) == 2 );
    for ( usize i = 0; i < EditorSelection_Count( &ws.selection ); ++i ) { // Welded back into closed solids.
        const auto *imported = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { EditorSelection_At( &ws.selection, i ) } );
        REQUIRE( imported != nullptr ); CHECK( geo::MeshBoundary_CountBoundaryEdges( &imported->mesh ) == 0u );
    }
    // A hand-written file: Y up, one bent quad, a degenerate face, UVs.
    const QString hand = folder.filePath( QStringLiteral( "hand.obj" ) );
    {
        QFile file( hand ); REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "o ramp\nv 0 0 0\nv 10 0 0\nv 10 3 10\nv 0 0 10\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                    "f 1/1 2/2 3/3 4/4\nf 1 1 2\n" );
    }
    const auto keep = ws.pDocument->geometry.meshes.nCount;
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( ( "file.import " + hand.toStdString() ).c_str() ) ) == command_result_t::OK );
    REQUIRE( ws.pDocument->geometry.meshes.nCount == keep + 1 );
    const u64 ramp = EditorSelection_At( &ws.selection, 0 );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { ramp } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { ramp } ), &d ) == geo::geometry_status_t::OK );
    CHECK( d.faces.nCount == 2 ); // The bent quad as two triangles; the degenerate face skipped.
    bool keptUv = false;
    for ( usize i = 0; i < d.corners.nCount; ++i ) { keptUv = keptUv || ( d.corners.pData[i].attributes.uv0.x == 1.0 && d.corners.pData[i].attributes.uv0.y == 0.0 ); }
    CHECK( keptUv ); // vt 1 1 -> (1, 0) with V running down.
    geo::MeshSourceDescription_Shutdown( &d );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "1 bent faces split" ) ) );
    CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "1 degenerate faces skipped" ) ) );
}

TEST_CASE( "Project onto World drops a mesh onto the surface below it", "[map][gui][mesh-project]" )
{
    namespace geo = cypher::editor::geometry;
    session_t s;
    auto &ws = s.workspace;
    ( void )AddBox( ws, { -256, -256, -64 }, { 256, 256, 0 } );                 // The floor.
    ( void )AddBox( ws, { 64, -256, 0 }, { 256, 256, 64 } );                    // A step under part of it.
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2; quad.bounds = { { -128, -32, 200 }, { 128, 32, 200 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); // A flat sheet, as a road or decal strip would be.
    const u64 sheet = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { sheet } ) != nullptr );
    REQUIRE( s.Enabled( "map.mesh.project" ) );
    REQUIRE( s.Run( "map.mesh.project" ) == command_result_t::OK );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { sheet } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { sheet } ), &d ) == geo::geometry_status_t::OK );
    for ( usize i = 0; i < d.vertices.nCount; ++i ) {
        const auto p = d.vertices.pData[i].position;
        CAPTURE( p.x, p.y, p.z );
        CHECK( p.z == Catch::Approx( p.x > 64 ? 64.0 : 0.0 ) ); // Each corner on what is under it.
    }
    geo::MeshSourceDescription_Shutdown( &d );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).startsWith( QStringLiteral( "Projected 4 vertices" ) ) );
}

TEST_CASE( "Selection sets: make one from the selection, grow, shrink, rename, hide, show, select and delete it from the panel", "[map][gui][selection_sets]" )
{
    session_t s;
    auto &ws = s.workspace;
    const u64 a = AddBox( ws, { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = AddBox( ws, { 128, 0, 0 }, { 192, 64, 64 } );
    const u64 c = AddBox( ws, { 256, 0, 0 }, { 320, 64, 64 } );
    std::unique_ptr<QWidget> panel( MapSelectionSets_Create( nullptr, &ws ) );
    auto *tree = panel->findChild<QTreeWidget *>();
    auto *newSet = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetNew" ) );
    auto *add = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetAdd" ) );
    auto *remove = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetRemove" ) );
    auto *hide = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetHide" ) );
    auto *show = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetShow" ) );
    auto *del = panel->findChild<QToolButton *>( QStringLiteral( "MapSelectionSetDelete" ) );
    REQUIRE( tree != nullptr ); REQUIRE( newSet != nullptr ); REQUIRE( add != nullptr ); REQUIRE( remove != nullptr );
    REQUIRE( hide != nullptr ); REQUIRE( show != nullptr ); REQUIRE( del != nullptr );
    const auto members = [&]() { const auto sets = MapSelectionSets_All( ws.pDocument ); return sets.empty() ? std::vector<u64>{} : sets[0].members; };

    // Nothing selected: no set to make.
    Select( ws, {} );
    CHECK_FALSE( newSet->isEnabled() ); CHECK_FALSE( s.Enabled( "map.selection_set.create" ) );
    Select( ws, { a, b } );
    REQUIRE( newSet->isEnabled() );
    const usize before = s.Steps();
    newSet->click();
    REQUIRE( MapSelectionSets_All( ws.pDocument ).size() == 1u );
    CHECK( s.Steps() == before + 1 );
    CHECK( members() == std::vector<u64>{ a, b } );
    REQUIRE( tree->topLevelItemCount() == 1 );
    CHECK( tree->currentItem() == tree->topLevelItem( 0 ) ); // The new set is current.
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Set 1" ) );
    CHECK( tree->topLevelItem( 0 )->text( 1 ) == QStringLiteral( "2" ) );

    // C joins, then A leaves.
    Select( ws, { c } );
    add->click();
    CHECK( members() == std::vector<u64>{ a, b, c } );
    add->click(); // Already in: a note, no undo step.
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).endsWith( QStringLiteral( "already holds the selection" ) ) );
    Select( ws, { a } );
    remove->click();
    CHECK( members() == std::vector<u64>{ b, c } );

    // Rename in place (F2); the rename lands after the edit's signal.
    tree->topLevelItem( 0 )->setText( 0, QStringLiteral( "Pillars" ) );
    QCoreApplication::processEvents();
    REQUIRE( MapSelectionSets_All( ws.pDocument ).size() == 1u );
    CHECK( MapSelectionSets_All( ws.pDocument )[0].name == "Pillars" );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Pillars" ) );
    // An empty name puts the old one back.
    tree->topLevelItem( 0 )->setText( 0, QStringLiteral( "  " ) );
    QCoreApplication::processEvents();
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Pillars" ) );

    // Hide takes the members out of view and out of the selection; Show brings them back.
    Select( ws, { a, b } );
    hide->click();
    CHECK( EditorSelection_Contains( &ws.hidden, b ) ); CHECK( EditorSelection_Contains( &ws.hidden, c ) );
    CHECK_FALSE( EditorSelection_Contains( &ws.hidden, a ) );
    CHECK( EditorSelection_Count( &ws.selection ) == 1 ); CHECK( EditorSelection_Contains( &ws.selection, a ) );
    show->click();
    CHECK_FALSE( EditorSelection_Contains( &ws.hidden, b ) ); CHECK_FALSE( EditorSelection_Contains( &ws.hidden, c ) );

    // Double-click selects the members that still exist.
    Select( ws, { c } );
    REQUIRE( MapWorkspace_DeleteSelection( &ws ) );
    emit tree->itemDoubleClicked( tree->topLevelItem( 0 ), 0 );
    CHECK( EditorSelection_Count( &ws.selection ) == 1 ); CHECK( EditorSelection_Contains( &ws.selection, b ) );

    // Delete forgets the set; Undo brings it back with its name.
    del->click();
    CHECK( MapSelectionSets_All( ws.pDocument ).empty() ); CHECK( tree->topLevelItemCount() == 0 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapSelectionSets_All( ws.pDocument ).size() == 1u );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Pillars" ) );

    // The commands without a set say so.
    CHECK( s.Run( "map.selection_set.add" ) == command_result_t::INVALID_ARGUMENTS );
}

namespace
{
constexpr const char *kGameProfile = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    id = "reap"
    name = "REAP"
    version = 1u
    editor = { default_point_class = "info_player_start" categories = [ "Info", "Lights", "Logic" ] }
    classes = [
        { name = "targetable" kind = "abstract" properties = [ { key = "start_disabled" type = "bool" default = false } ] inputs = [ { name = "enable" }, { name = "disable" } ] },
        { name = "light" kind = "point" base = [ "targetable" ] label = "Point Light" category = "Lights" description = "A light."
          properties = [ { key = "color" type = "color" default = "#ffffff" }, { key = "intensity" type = "real" default = 300.0 min = 0.0 unit = "candela" description = "How bright." } ]
          inputs = [ { name = "turn_on" }, { name = "turn_off" } ] },
        { name = "info_player_start" kind = "point" label = "Player Start" category = "Info"
          properties = [ { key = "team" type = "choice" default = "any" choices = [ "any", "red", "blue" ] } ] outputs = [ { name = "on_spawn" } ] },
        { name = "logic_relay" kind = "point" base = [ "targetable" ] category = "Logic" inputs = [ { name = "trigger" } ] outputs = [ { name = "on_trigger" } ] },
        { name = "prop_static" kind = "point" category = "Props" properties = [ { key = "model" type = "asset" default = "" extensions = [ ".cymodel" ] required = true } ] },
        { name = "func_door" kind = "solid" category = "Doors" properties = [ { key = "speed" type = "real" default = 200.0 } ] }
    ]
}
)cykv";

// An entity made directly, without the tool (so without the class's defaults).
u64 RawEntity( map_workspace_t &ws, const char *className, cypher::math::vec3d_t at, const char *name = nullptr )
{
    struct job_t { const char *pClass; cypher::math::vec3d_t at; const char *pName; u64 id; } job{ className, at, name, 0 };
    REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Entity", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        auto &j = *static_cast<job_t *>( context );
        auto status = MapCreate_Entity( copy, StringView_FromCString( j.pClass ), j.at, {}, &j.id );
        bool named = false;
        if ( status == map_status_t::OK && j.pName != nullptr ) { status = MapEntityEdit_SetIdentityField( copy, { &j.id, 1 }, StringView_FromCString( "name" ), StringView_FromCString( j.pName ), &named ); }
        *changed = status == map_status_t::OK;
        return status == map_status_t::OK && !Vector_PushBack( out, j.id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &job ) );
    return job.id;
}

bool HasIssue( const QVector<map_issue_t> &issues, const char *kind, u64 id )
{
    return std::any_of( issues.begin(), issues.end(), [&]( const map_issue_t &issue ) { return issue.kind == QLatin1String( kind ) && issue.id == id; } );
}
} // namespace

TEST_CASE( "The game profile gives the entity tool its classes and defaults, Entity I/O its names, and Check its rules", "[map][gui][game]" )
{
    session_t s;
    auto &ws = s.workspace;
    // A user scope, so the class the tool places can be chosen.
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &s.gui.settings, settings_scope_t::USER, &user );
    struct detach_t { gui::editor_gui_t &gui; ~detach_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); } } detach{ s.gui };
    CHECK( MapGame_Profile( &ws ) == nullptr ); // No content folders yet.
    CHECK( MapGame_Status( &ws ).contains( QStringLiteral( "No content folders" ) ) );
    QTemporaryDir content; REQUIRE( content.isValid() );
    REQUIRE( QDir( content.path() ).mkpath( QStringLiteral( "game" ) ) );
    { QFile file( QDir( content.path() ).filePath( QStringLiteral( "game/reap.cygame" ) ) ); REQUIRE( file.open( QIODevice::WriteOnly ) ); file.write( kGameProfile ); }
    MapHotspots_SetResolver( &ws, [&]( const QString &path ) {
        const QString file = QDir( content.path() ).filePath( path );
        return QFileInfo::exists( file ) ? file : QString();
    } );
    const map_game_profile_t *profile = MapGame_Profile( &ws );
    REQUIRE( profile != nullptr );
    CHECK( profile->classes.size() == 6u );
    CHECK( MapGame_Status( &ws ) == QStringLiteral( "Game profile REAP: 6 classes" ) );

    // Next Class walks the game's point classes in category order: Info, Lights, Logic, then the rest.
    const auto pointClass = [&]() { const auto v = EditorSettings_Text( &s.gui.settings, "editor.map.default_point_class", {} ); return std::string( v.pData, v.cchLength ); };
    CHECK( pointClass() == "info_player_start" );
    REQUIRE( s.Run( "map.entity.next_class" ) == command_result_t::OK );
    CHECK( pointClass() == "light" );

    // The entity tool places the class with every default written out.
    MapWorkspace_SetTool( &ws, map_tool_t::ENTITY );
    std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    view->resize( 640, 480 ); view->show(); QCoreApplication::processEvents();
    const QPointF centre( 320, 240 );
    for ( const auto type : { QEvent::MouseButtonPress, QEvent::MouseButtonRelease } ) {
        QMouseEvent event( type, centre, view->mapToGlobal( centre ), Qt::LeftButton, type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &event );
    }
    REQUIRE( EditorSelection_Count( &ws.selection ) == 1 );
    const u64 lamp = EditorSelection_At( &ws.selection, 0 );
    const key_value_t *properties = KeyValue_Find( MapEntityEdit_FindEntity( ws.pDocument, lamp ), StringView_FromCString( "properties" ) );
    f64 intensity = 0.0; bool_t disabled = CY_TRUE; string_view_t color{};
    CHECK( KeyValue_GetF64( KeyValue_Find( properties, StringView_FromCString( "intensity" ) ), &intensity ) ); CHECK( intensity == 300.0 );
    CHECK( KeyValue_GetBool( KeyValue_Find( properties, StringView_FromCString( "start_disabled" ) ), &disabled ) ); CHECK( disabled == CY_FALSE );
    CHECK( KeyValue_GetString( KeyValue_Find( properties, StringView_FromCString( "color" ) ), &color ) );
    CHECK( MapGame_Inputs( &ws, StringView_FromCString( "light" ) ) == QStringList{ QStringLiteral( "enable" ), QStringLiteral( "disable" ), QStringLiteral( "turn_on" ), QStringLiteral( "turn_off" ) } );

    // The Entity tool's class list: by category; a click chooses what it places.
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    auto *classes = panel->findChild<QTreeWidget *>( QStringLiteral( "MapEntityClasses" ) );
    REQUIRE( classes != nullptr );
    REQUIRE( classes->topLevelItemCount() == 4 ); // Info, Lights, Logic, Props: no abstract or solid classes.
    CHECK( classes->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Info" ) );
    CHECK( classes->currentItem() != nullptr ); CHECK( classes->currentItem()->data( 0, Qt::UserRole ).toString() == QStringLiteral( "light" ) );
    QTreeWidgetItem *relay = classes->topLevelItem( 2 )->child( 0 );
    REQUIRE( relay != nullptr );
    emit classes->itemClicked( relay, 0 );
    CHECK( pointClass() == "logic_relay" );
    auto *filter = panel->findChild<QLineEdit *>( QStringLiteral( "MapEntityClassFilter" ) );
    REQUIRE( filter != nullptr );
    filter->setText( QStringLiteral( "player" ) );
    CHECK_FALSE( classes->topLevelItem( 0 )->isHidden() ); CHECK( classes->topLevelItem( 1 )->isHidden() );

    // Check: classes the game lacks or cannot place, values the definitions refuse, required values,
    // missing defaults, and outputs and inputs the classes do not have.
    const u64 mystery = RawEntity( ws, "mystery_thing", { 512, 0, 0 } );
    const u64 base = RawEntity( ws, "targetable", { 640, 0, 0 } );
    const u64 prop = RawEntity( ws, "prop_static", { 768, 0, 0 } );
    const u64 bare = RawEntity( ws, "info_player_start", { 896, 0, 0 } );
    const u64 relayId = RawEntity( ws, "logic_relay", { 1024, 0, 0 }, "relay" );
    REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Set", []( void *context, map_document_t *copy, vector_t<u64> *, bool *changed ) noexcept {
        const u64 *ids = static_cast<const u64 *>( context );
        key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
        key_value_document_t *doc = KeyValue_CreateDocument( desc );
        key_value_parse_options_t options{};
        ( void )KeyValue_ParseText( StringView_FromCString( "@cykv 1\n@schema \"cypher.test\" 1\n{ v = \"bright\" }" ), options, doc );
        bool a = false, b = false, c = false;
        auto status = MapEntityEdit_SetProperty( copy, { &ids[0], 1 }, StringView_FromCString( "intensity" ), KeyValue_Find( KeyValue_Root( doc ), StringView_FromCString( "v" ) ), &a );
        if ( status == map_status_t::OK ) { status = MapEntityEdit_SetIdentityField( copy, { &ids[0], 1 }, StringView_FromCString( "name" ), StringView_FromCString( "lamp" ), &b ); }
        if ( status == map_status_t::OK ) {
            std::vector<map_output_t> outputs{ { "on_boom", "lamp", "turn_on", "", 0.0, -1 }, { "on_trigger", "lamp", "explode", "", 0.0, -1 } };
            status = MapEntityOutputs_Write( copy, ids[1], outputs, &c );
        }
        KeyValue_DestroyDocument( doc );
        *changed = a || b || c;
        return status;
    }, std::array<u64, 2>{ lamp, relayId }.data() ) );
    auto issues = MapCheck_Run( &ws );
    CHECK( HasIssue( issues, "unknown_class", mystery ) );
    CHECK( HasIssue( issues, "abstract_class", base ) );
    CHECK( HasIssue( issues, "required_property", prop ) );
    CHECK( HasIssue( issues, "invalid_property", lamp ) );
    CHECK( HasIssue( issues, "missing_defaults", bare ) );
    CHECK( HasIssue( issues, "unknown_output", relayId ) );
    CHECK( HasIssue( issues, "unknown_input", relayId ) );
    CHECK_FALSE( HasIssue( issues, "missing_defaults", lamp ) ); // Placed by the tool.
    // Fixes: the bad value goes back to its default, and the bare entity gets its class's defaults.
    QVector<map_issue_t> fixes;
    for ( const auto &issue : issues ) { if ( issue.kind == QLatin1String( "invalid_property" ) || issue.kind == QLatin1String( "missing_defaults" ) ) { fixes.append( issue ); } }
    CHECK( MapCheck_Fix( &ws, fixes ) == 3 ); // The lamp's value; the bare start's and the relay's defaults.
    issues = MapCheck_Run( &ws );
    CHECK_FALSE( HasIssue( issues, "invalid_property", lamp ) ); CHECK_FALSE( HasIssue( issues, "missing_defaults", bare ) );
    properties = KeyValue_Find( MapEntityEdit_FindEntity( ws.pDocument, lamp ), StringView_FromCString( "properties" ) );
    CHECK( KeyValue_GetF64( KeyValue_Find( properties, StringView_FromCString( "intensity" ) ), &intensity ) ); CHECK( intensity == 300.0 );
    string_view_t team{};
    CHECK( KeyValue_GetString( KeyValue_Find( KeyValue_Find( MapEntityEdit_FindEntity( ws.pDocument, bare ), StringView_FromCString( "properties" ) ), StringView_FromCString( "team" ) ), &team ) );

    // Reload reads the file again; a broken profile says why and leaves raw keys.
    { QFile file( QDir( content.path() ).filePath( QStringLiteral( "game/reap.cygame" ) ) ); REQUIRE( file.open( QIODevice::WriteOnly ) ); file.write( "@cykv 1\n@schema \"cypher.game\" 1\n{ id = \"reap\" }\n" ); }
    CHECK( s.Run( "map.game.reload" ) == command_result_t::FAILED );
    CHECK( MapGame_Profile( &ws ) == nullptr );
    map_notice_t kind{};
    CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "needs id, name and version" ) ) );
    MapHotspots_SetResolver( &ws, {} );
    MapGame_Forget( &ws );
}

TEST_CASE( "Mesh operations on a picked brush face publish conversion and modeling together", "[map][gui][geometry-commands][brush-to-mesh]" )
{
    session_t s;
    auto &ws = s.workspace;
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &s.gui.settings, settings_scope_t::USER, &user );
    struct detach_t { gui::editor_gui_t &gui; ~detach_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); } } detach{ s.gui };
    const u64 brush = AddBox( ws, { -64, -64, 0 }, { 64, 64, 128 } );
    u64 top = 0;
    for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) { if ( ws.wire.faces.pData[f].id == brush && ws.wire.faces.pData[f].normal.z > 0.99 ) { top = ws.wire.faces.pData[f].sideId; } }
    REQUIRE( top != 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &ws, brush, top );
    REQUIRE( s.Enabled( "map.mesh.extrude" ) );
    REQUIRE( s.Enabled( "map.mesh.inset" ) );
    CHECK_FALSE( s.Enabled( "map.mesh.bevel" ) ); // Not for one face, mesh or brush.
    // Inset too deep fails without publishing a conversion.
    const auto *inset = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.mesh_inset_distance" ) );
    REQUIRE( inset != nullptr );
    setting_value_t deep{}; deep.type = setting_type_t::REAL; deep.flValue = 1000.0;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *inset, deep ) == settings_registry_status_t::OK );
    const usize steps = s.Steps();
    CHECK( s.Run( "map.mesh.inset" ) != command_result_t::OK );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps );
    CHECK( s.Steps() == steps ); CHECK_FALSE( EditorHistory_CanRedo( &ws.history ) );
    CHECK( MapWireframe_FindObject( ws.wire, brush )->kind == map_wire_kind_t::BRUSH );
    CHECK( MapWorkspace_HasBrushFace( &ws ) ); CHECK( ws.selectedBrushFaceSide == top );
    // Extrude works: the brush is a mesh with the same ID, the face (same ID) extruded and still picked.
    REQUIRE( s.Run( "map.mesh.extrude" ) == command_result_t::OK );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWireframe_FindObject( ws.wire, brush )->kind == map_wire_kind_t::MESH );
    description_t extruded( ws, brush );
    CHECK( extruded.value.faces.nCount == 10u );
    CHECK( MapWorkspace_HasMeshFace( &ws ) ); CHECK( ws.selectedMeshFaceId == top );
    CHECK( MapWireframe_FindObject( ws.wire, brush )->bounds.box.maximum.z == Catch::Approx( 144.0 ) );
    // Off: brush faces keep only the brush operations.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    const auto *convert = EditorSettings_Find( &s.gui.settings, StringView_FromCString( "editor.map.components_convert_brushes" ) );
    setting_value_t off{}; off.type = setting_type_t::BOOL; off.bValue = CY_FALSE;
    REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *convert, off ) == settings_registry_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &ws, brush, top );
    CHECK_FALSE( s.Enabled( "map.mesh.extrude" ) );
}

// Builds assets/avia_split.cymap: the AVIA rent-a-car office by Split
// airport (a white prefab office with an open shed under one corrugated
// roof, red AVIA signs, cars on a gravel lot, umbrellas and a patio, the
// fence, the utility pole and the hills behind) beside the road, with a
// boom barrier, jersey barriers and cones at the drive. Every piece is made
// with the operations the tools use: primitives (Block), Hollow, Carve,
// polygon extrusion (Polygon), groups, terrain brushes and entities.
// Run on demand: cypher_editor_map_gui_contract_tests "[.build-avia-split]".
TEST_CASE( "Build the AVIA rent-a-car Split map", "[.build-avia-split]" )
{
    using vec = cypher::math::vec3d_t;
    session_t s;
    auto &ws = s.workspace;
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &s.gui.settings, settings_scope_t::USER, &user );
    struct detach_t { gui::editor_gui_t &gui; ~detach_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); } } detach{ s.gui };
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );

    constexpr f64 M = 39.37; // Units per metre.
    const auto setText = [&]( const char *path, const char *text ) {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };
    const auto setReal = [&]( const char *path, f64 number ) {
        const auto *descriptor = EditorSettings_Find( &s.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = descriptor->type; value.flValue = number;
        REQUIRE( EditorSettings_Write( &s.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };
    std::string material = "materials/dev/grid.cymat";
    const auto use = [&]( const char *path ) { material = path; setText( "editor.map.default_material", path ); };
    const auto primitive = [&]( map_primitive_kind_t kind, vec lo, vec hi, u32 axis = 2, u32 sides = 16 ) {
        map_primitive_desc_t desc = MapWorkspace_PrimitiveDefaults( &ws, {} );
        desc.kind = kind; desc.axis = axis; desc.nSides = sides; desc.coneTopRadiusRatio = 0.0; desc.sphereSubdivisions = 1;
        desc.bounds = { { lo.x * M, lo.y * M, lo.z * M }, { hi.x * M, hi.y * M, hi.z * M } };
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, desc ) );
        REQUIRE( EditorSelection_Count( &ws.selection ) == 1 );
        return EditorSelection_At( &ws.selection, 0 );
    };
    const auto box = [&]( vec lo, vec hi ) { return primitive( map_primitive_kind_t::BOX, lo, hi ); };
    // An extruded outline, as the Polygon tool makes: points in the plane x = x0, extruded `width` along +X.
    const auto profile = [&]( f64 x0, f64 y0, std::vector<std::pair<f64, f64>> yz, f64 width ) {
        std::vector<vec> points;
        for ( const auto &[y, z] : yz ) { points.push_back( { x0 * M, ( y0 + y ) * M, z * M } ); }
        struct job_t { std::vector<vec> *points; vec extrusion; std::string *material; u64 id; } job{ &points, { width * M, 0, 0 }, &material, 0 };
        REQUIRE( MapWorkspace_ApplyEdit( &ws, "Create Polygon Mesh", []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            const auto status = MapCreate_PolygonMesh( copy, { j.points->data(), j.points->size() }, j.extrusion, { j.material->data(), j.material->size() }, 128.0, &j.id );
            return status == map_status_t::OK && !Vector_PushBack( out, j.id ) ? map_status_t::OUT_OF_MEMORY : status;
        }, &job ) );
        return job.id;
    };
    const auto group = [&]( std::vector<u64> ids, const char *name ) {
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        MapWorkspace_SetSelection( &ws, ids.data(), ids.size() );
        REQUIRE( s.Run( "map.group.create" ) == command_result_t::OK );
        const u64 id = MapDocument_GroupOf( ws.pDocument, ids.front() );
        REQUIRE( id != 0 );
        struct job_t { u64 group; const char *name; } job{ id, name };
        REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Rename Group", []( void *context, map_document_t *copy, vector_t<u64> *, bool *changed ) noexcept {
            const auto &j = *static_cast<const job_t *>( context );
            return MapGroups_Rename( copy, j.group, StringView_FromCString( j.name ), changed );
        }, &job ) );
        return id;
    };
    // Cut the cutters out of what they touch (Carve), then delete them.
    const auto carve = [&]( std::vector<u64> cutters ) {
        MapWorkspace_SetSelection( &ws, cutters.data(), cutters.size() );
        REQUIRE( s.Run( "map.brush.carve" ) == command_result_t::OK );
        MapWorkspace_SetSelection( &ws, cutters.data(), cutters.size() );
        REQUIRE( MapWorkspace_DeleteSelection( &ws ) );
    };
    const auto entity = [&]( const char *className, vec at ) {
        struct job_t { const char *pClass; vec at; } job{ className, { at.x * M, at.y * M, at.z * M } };
        REQUIRE( MapWorkspace_ApplyMetadataEdit( &ws, "Place Entity", []( void *context, map_document_t *copy, vector_t<u64> *, bool *changed ) noexcept {
            const auto &j = *static_cast<const job_t *>( context );
            u64 id = 0;
            const auto status = MapCreate_Entity( copy, StringView_FromCString( j.pClass ), j.at, {}, &id );
            *changed = status == map_status_t::OK;
            return status;
        }, &job ) );
    };

    // ---- Ground: gravel lot, verge, road with markings, curb and drive ----
    use( "materials/blockout/concrete_light.cymat" );
    const u64 lot = box( { -26, -13.8, -0.15 }, { 22, 12, 0 } );
    use( "materials/dev/surfaces/surface_asphalt.cymat" );
    const u64 road = box( { -70, -22, -0.25 }, { 70, -14.15, -0.05 } );
    const u64 drive = box( { -2, -14.15, -0.2 }, { 4, -10, 0.005 } );
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    std::vector<u64> curbs{ box( { -70, -14.15, -0.25 }, { -2, -13.85, 0.12 } ), box( { 4, -14.15, -0.25 }, { 70, -13.85, 0.12 } ) };
    use( "materials/dev/surfaces/surface_ceramic_tile_white.cymat" );
    std::vector<u64> markings;
    for ( f64 x = -66; x < 66; x += 6 ) { markings.push_back( box( { x, -18.07, -0.05 }, { x + 3, -17.93, -0.04 } ) ); }
    markings.push_back( box( { -70, -21.7, -0.05 }, { 70, -21.58, -0.04 } ) );
    markings.push_back( box( { -70, -14.5, -0.05 }, { 70, -14.38, -0.04 } ) );
    group( markings, "Road markings" );
    group( { road, drive, curbs[0], curbs[1] }, "Road" );

    // ---- Roadblock at the drive: boom barrier, jersey barriers, cones ----
    use( "materials/dev/surfaces/surface_hazard_red_white.cymat" );
    const u64 post = box( { 4.3, -12.7, 0 }, { 4.65, -12.35, 1.05 } );
    const u64 arm = box( { -2.3, -12.56, 0.92 }, { 4.3, -12.46, 1.02 } );
    use( "materials/dev/surfaces/surface_concrete_cast_dark.cymat" );
    const u64 weight = box( { 4.65, -12.62, 0.85 }, { 5.15, -12.42, 1.1 } );
    group( { post, arm, weight }, "Boom barrier" );
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    const std::vector<std::pair<f64, f64>> jersey{ { 0, 0 }, { 0.6, 0 }, { 0.6, 0.08 }, { 0.42, 0.3 }, { 0.375, 0.8 }, { 0.225, 0.8 }, { 0.18, 0.3 }, { 0, 0.08 } };
    std::vector<u64> barriers;
    for ( f64 x = -25.5; x + 3 <= -3; x += 3.2 ) { barriers.push_back( profile( x, -13.6, jersey, 3.0 ) ); }
    for ( f64 x = 5.5; x + 3 <= 22; x += 3.2 ) { barriers.push_back( profile( x, -13.6, jersey, 3.0 ) ); }
    group( barriers, "Jersey barriers" );
    use( "materials/blockout/trim_orange.cymat" );
    std::vector<u64> cones;
    for ( const vec at : { vec{ -2.7, -12.0, 0 }, vec{ -2.7, -10.9, 0 }, vec{ 5.4, -11.6, 0 }, vec{ 5.4, -10.6, 0 } } ) {
        cones.push_back( primitive( map_primitive_kind_t::CONE, { at.x - 0.18, at.y - 0.18, 0 }, { at.x + 0.18, at.y + 0.18, 0.72 }, 2, 12 ) );
    }
    group( cones, "Cones" );

    // ---- The office: hollowed prefab with a window, a door and a side window ----
    // Carve replaces the walls it cuts with new pieces: the office is every object made from here on.
    std::vector<u64> existing;
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) { existing.push_back( ws.wire.objects.pData[i].id ); }
    use( "materials/dev/surfaces/surface_plaster_warm.cymat" );
    box( { 0, 4.0, 0 }, { 8.4, 7.0, 2.7 } );
    setReal( "editor.map.hollow_thickness", 0.12 * M );
    REQUIRE( s.Run( "map.brush.hollow" ) == command_result_t::OK );
    use( "materials/dev/grid.cymat" );
    carve( { box( { 1.4, 3.9, 0.95 }, { 3.6, 4.3, 2.0 } ), box( { 5.2, 3.9, 0.12 }, { 6.2, 4.3, 2.1 } ), box( { 8.2, 5.0, 1.0 }, { 8.6, 6.2, 1.9 } ) } );
    std::vector<u64> office;
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
        const u64 id = ws.wire.objects.pData[i].id;
        if ( std::find( existing.begin(), existing.end(), id ) == existing.end() ) { office.push_back( id ); }
    }
    std::printf( "AVIA office walls after hollow and carve: %zu pieces\n", office.size() );
    use( "materials/dev/surfaces/surface_steel_panel_blue_dense.cymat" );
    const u64 glass = box( { 1.4, 4.04, 0.95 }, { 3.6, 4.08, 2.0 } );
    const u64 sideGlass = box( { 8.32, 5.0, 1.0 }, { 8.36, 6.2, 1.9 } );
    const u64 door = box( { 5.2, 4.05, 0.12 }, { 6.2, 4.09, 2.1 } );
    // The open shed under the same roof, its dark storage door, and the post at its front corner.
    use( "materials/dev/surfaces/surface_plaster_warm.cymat" );
    const u64 shedBack = box( { -4.6, 6.88, 0 }, { 0, 7.0, 2.7 } );
    const u64 shedSide = box( { -4.6, 4.3, 0 }, { -4.48, 7.0, 2.7 } );
    use( "materials/dev/surfaces/surface_steel_panel_blue_dense.cymat" );
    const u64 shedDoor = box( { -3.4, 6.84, 0 }, { -1.4, 6.88, 2.2 } );
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    const u64 shedPost = box( { -4.65, 4.0, 0 }, { -4.45, 4.2, 2.7 } );
    // The roof slab overhangs all round; a white fascia runs along the front.
    use( "materials/blockout/wall_ribbed.cymat" );
    const u64 roof = box( { -5.0, 3.6, 2.7 }, { 8.8, 7.4, 2.88 } );
    use( "materials/dev/surfaces/surface_ceramic_tile_white.cymat" );
    const u64 fascia = box( { -5.0, 3.52, 2.5 }, { 8.8, 3.6, 2.92 } );
    // The red AVIA signs: above the window, and on the shed's fascia.
    use( "materials/blockout/warning_red.cymat" );
    const u64 sign = box( { 1.0, 3.92, 2.08 }, { 4.0, 4.0, 2.46 } );
    const u64 shedSign = box( { -3.8, 3.46, 2.55 }, { -1.4, 3.52, 2.88 } );
    // Concrete apron in front, and the air conditioner on the end wall.
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    const u64 apron = box( { -5.0, 2.9, 0 }, { 8.8, 4.0, 0.08 } );
    use( "materials/dev/surfaces/surface_ceramic_tile_white.cymat" );
    const u64 ac = box( { 8.45, 4.6, 1.7 }, { 9.0, 5.4, 2.25 } );
    std::vector<u64> building = office;
    for ( u64 id : { glass, sideGlass, door, shedBack, shedSide, shedDoor, shedPost, roof, fascia, sign, shedSign, apron, ac } ) { building.push_back( id ); }
    group( building, "AVIA office" );

    // ---- Cars: body and glasshouse outlines extruded across, four wheels each ----
    struct car_t { const char *name; const char *paint; f64 x, y, length, width, wheel; std::vector<std::pair<f64, f64>> body, cabin; };
    const car_t cars[]{
        { "Skoda Kodiaq (silver)", "materials/blockout/metal_panel.cymat", -7.2, 1.0, 4.7, 1.88, 0.37,
          { { 0, 0.38 }, { 0, 0.95 }, { 0.95, 1.08 }, { 4.7, 1.12 }, { 4.7, 0.4 } }, { { 1.2, 1.08 }, { 1.95, 1.68 }, { 4.45, 1.68 }, { 4.62, 1.12 } } },
        { "Peugeot 208 (red)", "materials/blockout/warning_red.cymat", -1.6, 1.4, 4.05, 1.75, 0.33,
          { { 0, 0.28 }, { 0, 0.78 }, { 0.85, 0.92 }, { 4.05, 0.98 }, { 4.05, 0.3 } }, { { 1.05, 0.93 }, { 1.75, 1.42 }, { 3.35, 1.42 }, { 4.0, 0.98 } } },
        { "Peugeot 3008 (white)", "materials/dev/surfaces/surface_ceramic_tile_white.cymat", 4.6, 1.2, 4.45, 1.84, 0.36,
          { { 0, 0.36 }, { 0, 0.92 }, { 0.9, 1.05 }, { 4.45, 1.1 }, { 4.45, 0.38 } }, { { 1.1, 1.05 }, { 1.85, 1.62 }, { 3.95, 1.62 }, { 4.38, 1.1 } } },
        { "Sedan (dark)", "materials/dev/surfaces/surface_steel_panel_blue_dense.cymat", -22.5, 2.5, 4.6, 1.8, 0.33,
          { { 0, 0.3 }, { 0, 0.75 }, { 1.0, 0.88 }, { 4.6, 0.9 }, { 4.6, 0.32 } }, { { 1.25, 0.88 }, { 2.0, 1.4 }, { 3.6, 1.4 }, { 4.2, 0.9 } } } };
    std::vector<u64> carGroups;
    for ( const auto &car : cars ) {
        std::vector<u64> parts;
        use( car.paint );
        parts.push_back( profile( car.x, car.y, car.body, car.width ) );
        use( "materials/dev/surfaces/surface_steel_panel_blue_dense.cymat" );
        parts.push_back( profile( car.x + 0.08, car.y, car.cabin, car.width - 0.16 ) );
        use( "materials/dev/surfaces/surface_rubber_stud_floor_dense.cymat" );
        for ( const f64 axle : { 0.85, car.length - 0.8 } ) {
            for ( const f64 side : { car.x - 0.02, car.x + car.width - 0.2 } ) {
                parts.push_back( primitive( map_primitive_kind_t::CYLINDER, { side, car.y + axle - car.wheel, 0 }, { side + 0.22, car.y + axle + car.wheel, 2 * car.wheel }, 0, 16 ) );
            }
        }
        carGroups.push_back( group( parts, car.name ) );
    }

    // ---- Umbrellas, the patio table and chairs ----
    std::vector<u64> patio;
    for ( const vec at : { vec{ -9.6, 5.2, 0 }, vec{ 11.6, 5.4, 0 } } ) {
        use( "materials/dev/surfaces/surface_concrete_cast_dark.cymat" );
        patio.push_back( primitive( map_primitive_kind_t::CYLINDER, { at.x - 0.04, at.y - 0.04, 0 }, { at.x + 0.04, at.y + 0.04, 2.45 }, 2, 8 ) );
        use( "materials/dev/surfaces/surface_ceramic_tile_white.cymat" );
        patio.push_back( primitive( map_primitive_kind_t::CONE, { at.x - 1.5, at.y - 1.5, 1.95 }, { at.x + 1.5, at.y + 1.5, 2.5 }, 2, 8 ) );
    }
    use( "materials/dev/surfaces/surface_concrete_cast_dark.cymat" );
    patio.push_back( primitive( map_primitive_kind_t::CYLINDER, { 11.15, 5.0, 0.7 }, { 12.05, 5.9, 0.74 }, 2, 16 ) );
    patio.push_back( primitive( map_primitive_kind_t::CYLINDER, { 11.56, 5.41, 0 }, { 11.64, 5.49, 0.7 }, 2, 8 ) );
    for ( const vec chair : { vec{ 10.4, 5.2, 0 }, vec{ 12.35, 5.2, 0 }, vec{ 11.37, 4.15, 0 }, vec{ 11.37, 6.2, 0 } } ) {
        patio.push_back( box( { chair.x, chair.y, 0 }, { chair.x + 0.46, chair.y + 0.46, 0.46 } ) );
        patio.push_back( box( { chair.x, chair.y + 0.41, 0.46 }, { chair.x + 0.46, chair.y + 0.46, 0.9 } ) );
    }
    group( patio, "Patio and umbrellas" );

    // ---- Fence (right side and back), utility pole, electrical box ----
    std::vector<u64> fence;
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    fence.push_back( box( { 21.85, -10, 0 }, { 22.15, 12.15, 0.25 } ) );
    fence.push_back( box( { -26, 11.85, 0 }, { 22.15, 12.15, 0.25 } ) );
    use( "materials/blockout/metal_grate.cymat" );
    fence.push_back( box( { 21.98, -10, 0.25 }, { 22.02, 12.0, 2.0 } ) );
    fence.push_back( box( { -26, 11.98, 0.25 }, { 22.0, 12.02, 2.0 } ) );
    use( "materials/dev/surfaces/surface_concrete_cast_dark.cymat" );
    for ( f64 y = -10; y <= 12; y += 2.5 ) { fence.push_back( primitive( map_primitive_kind_t::CYLINDER, { 21.95, y - 0.05, 0 }, { 22.05, y + 0.05, 2.1 }, 2, 8 ) ); }
    for ( f64 x = -25.5; x <= 21.5; x += 2.5 ) { fence.push_back( primitive( map_primitive_kind_t::CYLINDER, { x - 0.05, 11.95, 0 }, { x + 0.05, 12.05, 2.1 }, 2, 8 ) ); }
    group( fence, "Fence" );
    use( "materials/dev/surfaces/surface_concrete_cast.cymat" );
    const u64 pole = primitive( map_primitive_kind_t::CYLINDER, { 15.84, 5.84, 0 }, { 16.16, 6.16, 9.0 }, 2, 12 );
    const u64 crossarm = box( { 15.1, 5.95, 8.35 }, { 16.9, 6.05, 8.5 } );
    const u64 lampArm = box( { 16.0, 5.97, 7.55 }, { 17.6, 6.03, 7.65 } );
    use( "materials/dev/surfaces/surface_concrete_cast_dark.cymat" );
    const u64 lamp = box( { 17.35, 5.88, 7.4 }, { 17.95, 6.12, 7.56 } );
    use( "materials/dev/surfaces/surface_ceramic_tile_white.cymat" );
    const u64 cabinet = box( { 18.5, 9.0, 0 }, { 19.1, 9.45, 1.25 } );
    group( { pole, crossarm, lampArm, lamp, cabinet }, "Utility pole" );

    // ---- Olive trees and shrubs ----
    std::vector<u64> trees;
    for ( const vec at : { vec{ -14, 9.6, 0 }, vec{ -8, 10.2, 0 }, vec{ -2.5, 10.4, 0 }, vec{ 3.5, 10.6, 0 }, vec{ 13.5, 10.2, 0 } } ) {
        use( "materials/dev/surfaces/surface_wood_planks.cymat" );
        trees.push_back( primitive( map_primitive_kind_t::CYLINDER, { at.x - 0.18, at.y - 0.18, 0 }, { at.x + 0.18, at.y + 0.18, 2.4 }, 2, 8 ) );
        use( "materials/dev/blockout/blockout_exterior.cymat" );
        trees.push_back( primitive( map_primitive_kind_t::SPHERE, { at.x - 1.7, at.y - 1.7, 1.9 }, { at.x + 1.7, at.y + 1.7, 5.0 } ) );
    }
    for ( const vec at : { vec{ -24, 6, 0 }, vec{ -18, 9, 0 }, vec{ -12.5, 1.5, 0 }, vec{ 19.5, 3, 0 } } ) {
        trees.push_back( primitive( map_primitive_kind_t::SPHERE, { at.x - 1.0, at.y - 1.0, 0 }, { at.x + 1.0, at.y + 1.0, 1.3 } ) );
    }
    group( trees, "Trees" );

    // ---- Hills beyond the fence (terrain, raised with the Terrain tool's brushes) ----
    use( "materials/dev/blockout/blockout_exterior.cymat" );
    map_terrain_create_desc_t hills{}; hills.origin = { -120 * M, 14 * M, 0 }; hills.cellSize = 2.5 * M; hills.cellsX = 96; hills.cellsY = 64;
    REQUIRE( MapWorkspace_CreateTerrain( &ws, hills ) );
    struct dab_t { f64 x, y, radius, amount; };
    const std::vector<dab_t> dabs{ { -90, 120, 45, 22 }, { -50, 135, 55, 30 }, { -10, 125, 40, 18 }, { 30, 140, 60, 34 }, { 70, 120, 45, 20 },
                                   { 100, 135, 40, 16 }, { -70, 80, 30, 7 }, { 45, 85, 35, 8 }, { 0, 95, 30, 6 }, { -30, 105, 35, 10 } };
    struct terrain_job_t { const std::vector<dab_t> *dabs; f64 m; } terrainJob{ &dabs, M };
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Terrain raise", []( void *context, map_document_t *copy, vector_t<u64> * ) noexcept {
        const auto &j = *static_cast<const terrain_job_t *>( context );
        const auto *field = copy->geometry.heightFields.pData[0];
        geo::heightfield_t edited{};
        auto status = geo::HeightField_TryClone( field, copy->pAllocator, &edited );
        for ( usize i = 0; status == geo::geometry_status_t::OK && i < j.dabs->size(); ++i ) {
            const auto &d = ( *j.dabs )[i];
            geo::heightfield_brush_t brush{}; brush.op = geo::heightfield_brush_op_t::RAISE;
            brush.centerX = d.x * j.m; brush.centerY = d.y * j.m; brush.radius = d.radius * j.m; brush.amount = d.amount * j.m;
            status = geo::HeightField_TryApplyBrush( &edited, brush, nullptr );
        }
        geo::heightfield_brush_t smooth{}; smooth.op = geo::heightfield_brush_op_t::SMOOTH; smooth.centerX = 0; smooth.centerY = 110 * j.m; smooth.radius = 150 * j.m; smooth.strength = 0.6;
        if ( status == geo::geometry_status_t::OK ) { status = geo::HeightField_TryApplyBrush( &edited, smooth, nullptr ); }
        if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryReplaceHeightField( &copy->geometry, &edited ); }
        geo::HeightField_Shutdown( &edited );
        if ( status != geo::geometry_status_t::OK ) { return map_status_t::GEOMETRY_FAILED; }
        ++copy->geometry.revision;
        return map_status_t::OK;
    }, &terrainJob ) );

    // ---- Entities: the player starts at the drive, a light over the lot ----
    entity( "info_player_start", { 1.0, -8.0, 0.0 } );
    entity( "light", { 2.0, 0.0, 8.0 } );

    MapWorkspace_SetSelection( &ws, nullptr, 0 );
    ( void )lot;
    CHECK( carGroups.size() == 4u );
    const QString root = QDir( QStringLiteral( CYPHER_MAP_EXAMPLE_DIR ) ).absoluteFilePath( QStringLiteral( "../../../../assets/avia_split.cymap" ) );
    const auto saved = MapWorkspace_SaveAs( &ws, QDir::cleanPath( root ) );
    INFO( root.toStdString() );
    REQUIRE( saved.status == map_files_status_t::OK );
    std::printf( "AVIA map: %zu objects, %zu entities, %zu groups -> %s\n", static_cast<size_t>( ws.wire.objects.nCount ), static_cast<size_t>( ws.wire.entities.nCount ),
        MapGroups_All( ws.pDocument ).size(), QDir::cleanPath( root ).toUtf8().constData() );
}

TEST_CASE( "Array and Radial Array work on a single box; Bridge joins its top and bottom", "[map][gui][geometry-commands][menu-fixes]" )
{
    SECTION( "Array: copies stand apart, not face to face" ) {
        session_t s; const u64 mesh = AddBoxMesh( s );
        const usize steps = s.Steps();
        REQUIRE( s.Run( "map.mesh.modifier_array" ) == command_result_t::OK );
        CHECK( s.Steps() == steps + 1 );
        CHECK( Box( s.workspace, mesh ).maximum.x - Box( s.workspace, mesh ).minimum.x > 128.0 * 2.0 );
    }
    SECTION( "Radial: copies circle an axis beside the box" ) {
        session_t s; const u64 mesh = AddBoxMesh( s );
        REQUIRE( s.Run( "map.mesh.modifier_radial" ) == command_result_t::OK );
        CHECK( Box( s.workspace, mesh ).maximum.y - Box( s.workspace, mesh ).minimum.y > 256.0 );
    }
    SECTION( "Bridge: inset top and bottom faces bore a hole through the box" ) {
        session_t s; const u64 mesh = AddBoxMesh( s );
        MapWorkspace_SetElementMode( &s.workspace, map_element_mode_t::FACES );
        u64 inner[2]{};
        const cypher::math::vec3d_t normals[2]{ { 0, 0, 1 }, { 0, 0, -1 } };
        for ( int i = 0; i < 2; ++i ) {
            MapWorkspace_SelectMeshFace( &s.workspace, mesh, FaceFacing( s.workspace, mesh, normals[i] ) );
            REQUIRE( s.Run( "map.mesh.inset" ) == command_result_t::OK );
            REQUIRE( s.workspace.selectedMeshFaceId != 0 ); // The inset face stays picked.
            inner[i] = s.workspace.selectedMeshFaceId;
        }
        MapWorkspace_SelectMeshFace( &s.workspace, mesh, inner[0] );
        REQUIRE( MapWorkspace_AddMeshFace( &s.workspace, mesh, inner[1] ) );
        const usize before = description_t( s.workspace, mesh ).value.faces.nCount;
        REQUIRE( s.Run( "map.mesh.bridge" ) == command_result_t::OK );
        CHECK( description_t( s.workspace, mesh ).value.faces.nCount == before - 2u + 4u ); // Two faces out, four tube walls in.
    }
}

TEST_CASE( "Weld on a brush with nothing to weld leaves it a brush; Merge needs two objects", "[map][gui][geometry-commands][menu-fixes]" )
{
    session_t s;
    const u64 brush = AddBox( s.workspace, { -64, -64, 0 }, { 64, 64, 128 } );
    const usize applied = EditorHistory_AppliedStepCount( &s.workspace.history );
    CHECK( s.Run( "map.mesh.weld" ) == command_result_t::FAILED );
    CHECK( EditorHistory_AppliedStepCount( &s.workspace.history ) == applied ); // No conversion left behind.
    CHECK_FALSE( EditorHistory_CanRedo( &s.workspace.history ) );                // Nor offered for Redo.
    CHECK( geo::GeometryDocument_FindBrush( &s.workspace.pDocument->geometry, { brush } ) != nullptr );
    CHECK( s.workspace.lastEdit.outcome == map_edit_outcome_t::UNCHANGED );
    // One brush alone cannot be joined with anything.
    CHECK_FALSE( s.Enabled( "map.mesh.merge" ) );
    const u64 other = AddBox( s.workspace, { 256, 0, 0 }, { 320, 64, 64 } );
    Select( s.workspace, { brush, other } );
    CHECK( s.Enabled( "map.mesh.merge" ) );
}

TEST_CASE( "Connect joins cofacial vertex pairs in a projected selection without collapsing depth", "[map][gui][geometry-commands][vertex-workflow][connect]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s );
    description_t before( ws, mesh );
    const auto lowA = VertexAt( before, { -64, -64, 0 } ), lowB = VertexAt( before, { 64, 64, 0 } );
    const auto highA = VertexAt( before, { -64, -64, 128 } ), highB = VertexAt( before, { 64, 64, 128 } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, lowA ) );
    for ( const auto id : { highA, lowB, highB } ) { REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, id, MAP_SELECT_ADD ) ); }
    REQUIRE( ws.meshSelection.vertices.nCount == 4 ); // The two handles visible in Top represent four vertices.
    REQUIRE( s.Enabled( "map.mesh.connect_edges" ) );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.mesh.connect_edges" ) == command_result_t::OK );
    description_t connected( ws, mesh );
    REQUIRE( connected.value.vertices.nCount == before.value.vertices.nCount );
    CHECK( connected.value.faces.nCount == before.value.faces.nCount + 2 );
    CHECK( s.Steps() == steps + 1 );
    for ( usize i = 0; i < before.value.vertices.nCount; ++i ) {
        const auto &vertex = before.value.vertices.pData[i];
        CHECK( VertexAt( connected, vertex.position ).value == vertex.sourceId.value );
    }
    const auto hasEdge = []( const description_t &description, geo::geometry_source_id_t a, geo::geometry_source_id_t b ) {
        for ( usize f = 0; f < description.value.faces.nCount; ++f ) {
            const auto &face = description.value.faces.pData[f];
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                const auto u = description.Corner( f, k ), v = description.Corner( f, ( k + 1 ) % face.cCorners );
                if ( ( u.value == a.value && v.value == b.value ) || ( u.value == b.value && v.value == a.value ) ) { return true; }
            }
        }
        return false;
    };
    CHECK( hasEdge( connected, lowA, lowB ) ); CHECK( hasEdge( connected, highA, highB ) );
    CHECK( Box( ws, mesh ).maximum.z - Box( ws, mesh ).minimum.z == Catch::Approx( 128 ) );
    CHECK_FALSE( s.Enabled( "map.mesh.connect_edges" ) ); // Both new edges already exist.
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { description_t restored( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { description_t restored( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &connected.value, &restored.value ) ); }
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "connected-columns.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    session_t loaded;
    REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
    description_t saved( loaded.workspace, mesh ); CHECK( geo::MeshSourceDescription_Equal( &connected.value, &saved.value ) );
}

TEST_CASE( "Connect leaves an already connected projected vertex selection untouched", "[map][gui][geometry-commands][vertex-workflow][connect]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s ); description_t original( ws, mesh );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    for ( const auto p : { cypher::math::vec3d_t{ -64, -64, 0 }, { 64, -64, 0 } } ) {
        REQUIRE( MapWorkspace_SelectMeshVertex( &ws, mesh, VertexAt( original, p ), MAP_SELECT_ADD ) );
    }
    const usize steps = s.Steps();
    CHECK_FALSE( s.Enabled( "map.mesh.connect_edges" ) );
    CHECK( s.Run( "map.mesh.connect_edges" ) == command_result_t::DISABLED );
    CHECK( s.Steps() == steps );
    description_t unchanged( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) );
}

TEST_CASE( "Edge Connect rejects an unrelated selected shell and publishes a corrected pair as one undo step", "[map][gui][geometry-commands][edge-connect-preflight]" )
{
    session_t s; auto &ws = s.workspace;
    u64 quads[2]{};
    for ( usize i = 0; i < 2; ++i ) {
        const f64 x = i * 256.0;
        auto quad = MapWorkspace_PrimitiveDefaults( &ws, Bounds( { x, 0, 0 }, { x + 128, 64, 0 } ) );
        quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) );
        quads[i] = EditorSelection_At( &ws.selection, 0 );
    }
    Select( ws, { quads[0], quads[1] } );
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    const u64 mesh = EditorSelection_At( &ws.selection, 0 );
    description_t before( ws, mesh ); REQUIRE( before.value.faces.nCount == 2 );
    const auto a = geo::MeshEdgeRef_Make( VertexAt( before, { 0, 0, 0 } ), VertexAt( before, { 128, 0, 0 } ) );
    const auto b = geo::MeshEdgeRef_Make( VertexAt( before, { 0, 64, 0 } ), VertexAt( before, { 128, 64, 0 } ) );
    const auto unrelated = geo::MeshEdgeRef_Make( VertexAt( before, { 256, 0, 0 } ), VertexAt( before, { 384, 0, 0 } ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, a ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, b, MAP_SELECT_ADD ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, unrelated, MAP_SELECT_ADD ) );
    const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision, nextId = document->nextId, selection = ws.selection.revision;
    const usize steps = s.Steps(); REQUIRE( s.Enabled( "map.mesh.connect_edges" ) );
    REQUIRE( s.Run( "map.mesh.connect_edges" ) == command_result_t::FAILED );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
    CHECK( ws.selection.revision == selection ); CHECK( ws.meshSelection.edges.nCount == 3 ); CHECK( s.Steps() == steps );
    { description_t unchanged( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
    map_notice_t kind{}; const QString notice = MapNotice_Last( &ws, &kind );
    CHECK( kind == map_notice_t::WARNING ); CHECK( notice.contains( QStringLiteral( "Remove unrelated edges" ) ) );

    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, unrelated, MAP_SELECT_TOGGLE ) );
    REQUIRE( ws.meshSelection.edges.nCount == 2 );
    REQUIRE( s.Run( "map.mesh.connect_edges" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1 );
    description_t connected( ws, mesh ); CHECK( connected.value.vertices.nCount == 10 ); CHECK( connected.value.faces.nCount == 3 );
    // The second surface keeps its original face, corners and vertices.
    const auto &other = before.value.faces.pData[1]; bool retained = false;
    for ( usize f = 0; f < connected.value.faces.nCount; ++f ) {
        const auto &face = connected.value.faces.pData[f];
        if ( face.sourceId.value != other.sourceId.value ) { continue; }
        retained = true; REQUIRE( face.cCorners == other.cCorners );
        for ( u32 k = 0; k < face.cCorners; ++k ) { CHECK( connected.Corner( f, k ).value == before.Corner( 1, k ).value ); }
    }
    CHECK( retained );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    { description_t restored( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    { description_t restored( ws, mesh ); CHECK( geo::MeshSourceDescription_Equal( &connected.value, &restored.value ) ); }
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "connected-shell.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.workspace, path ).status == map_files_status_t::OK );
    description_t persisted( loaded.workspace, mesh ); CHECK( geo::MeshSourceDescription_Equal( &connected.value, &persisted.value ) );
}


namespace
{
struct workspace_lifetime_fixture_t {
    session_t session;
    QWidget windowOwner;
    ~workspace_lifetime_fixture_t()
    {
        // Also clean up on a failed REQUIRE in the old implementation. These
        // callbacks deliberately own their captures rather than dangling refs.
        MapHotspots_SetResolver( &session.workspace, {} );
        MapAssets_SetLister( &session.workspace, {} );
        MapAssets_SetRoots( &session.workspace, {} );
    }
    void PrepareFace()
    {
        auto &ws = session.workspace;
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        const u64 mesh = AddBoxMesh( session ); u64 top{};
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
            const auto &face = ws.wire.faces.pData[i];
            if ( face.id == mesh && face.normal.z > 0.99 ) { top = face.faceId; }
        }
        REQUIRE( top != 0 ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        MapWorkspace_SelectMeshFace( &ws, mesh, top ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    }
};
}

TEST_CASE( "Fast Texture cannot reuse a detached dialog after workspace shutdown and reinitialization", "[map][gui][workspace-lifetime][fast-texture]" )
{
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    workspace_lifetime_fixture_t f; auto &ws = f.session.workspace; f.PrepareFace();
    QPointer<QDialog> old = MapFastTexture_Show( &ws, &f.windowOwner ); REQUIRE( old );
    REQUIRE( MapFastTexture_Choose( old, QRectF( 0.5, 0.0, 0.5, 0.5 ) ) );
    MapWorkspace_Shutdown( &ws );
    REQUIRE( old ); // Its deletion is queued; the address is deliberately reused first.
    REQUIRE( MapWorkspace_Init( &ws, &f.session.gui ) ); f.PrepareFace();
    QPointer<QDialog> fresh = MapFastTexture_Show( &ws, &f.windowOwner ); REQUIRE( fresh );
    CHECK( fresh.data() != old.data() ); CHECK( MapFastTexture_Choose( fresh, QRectF( 0.0, 0.5, 0.5, 0.5 ) ) );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    CHECK( old.isNull() ); REQUIRE( fresh );
    // An old queued destructor must not erase the replacement cache entry.
    CHECK( MapFastTexture_Show( &ws, &f.windowOwner ) == fresh.data() );
    CHECK( MapFastTexture_Choose( fresh, QRectF( 0.0, 0.0, 0.25, 0.25 ) ) );
}

TEST_CASE( "Closing or rejecting Fast Texture immediately retires its cache before deferred destruction", "[map][gui][workspace-lifetime][fast-texture]" )
{
    for ( const int mode : { 0, 1, 2 } ) {
        DYNAMIC_SECTION( "close mode=" << mode ) {
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            workspace_lifetime_fixture_t f; auto &ws = f.session.workspace; f.PrepareFace();
            QPointer<QDialog> old = MapFastTexture_Show( &ws, &f.windowOwner ); REQUIRE( old );
            REQUIRE( MapFastTexture_Choose( old, QRectF( 0.5, 0.0, 0.5, 0.5 ) ) );
            if ( mode == 1 ) { old->reject(); }
            else { if ( mode == 2 ) { old->hide(); } old->close(); }
            REQUIRE( old );
            QPointer<QDialog> fresh = MapFastTexture_Show( &ws, &f.windowOwner ); REQUIRE( fresh );
            CHECK( fresh.data() != old.data() ); CHECK( MapFastTexture_Choose( fresh, QRectF( 0.0, 0.5, 0.5, 0.5 ) ) );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            CHECK( old.isNull() ); REQUIRE( fresh );
            CHECK( MapFastTexture_Show( &ws, &f.windowOwner ) == fresh.data() );
            CHECK( MapFastTexture_Choose( fresh, QRectF( 0.0, 0.0, 0.25, 0.25 ) ) );
        }
    }
}

TEST_CASE( "Workspace shutdown releases asset callbacks roots and generation before address reuse", "[map][gui][workspace-lifetime][asset-callbacks]" )
{
    workspace_lifetime_fixture_t f; auto &ws = f.session.workspace;
    struct calls_t { int resolver{}, lister{}; };
    const auto calls = std::make_shared<calls_t>();
    auto resolverOwner = std::make_shared<int>( 1 ), listerOwner = std::make_shared<int>( 2 );
    const std::weak_ptr<int> resolverWeak = resolverOwner, listerWeak = listerOwner;
    MapHotspots_SetResolver( &ws, [owner = resolverOwner, calls]( const QString & ) {
        ( void )owner; ++calls->resolver; return QStringLiteral( "retired-material.cymat" );
    } );
    MapAssets_SetLister( &ws, [owner = listerOwner, calls]( const QStringList & ) {
        ( void )owner; ++calls->lister; return QStringList{ QStringLiteral( "retired-material.cymat" ) };
    } );
    MapAssets_SetRoots( &ws, { QStringLiteral( "/retired-content-root" ) } );
    resolverOwner.reset(); listerOwner.reset();
    REQUIRE_FALSE( resolverWeak.expired() ); REQUIRE_FALSE( listerWeak.expired() );
    REQUIRE( MapAssets_Generation( &ws ) != 0 ); REQUIRE_FALSE( MapAssets_Roots( &ws ).isEmpty() );
    REQUIRE_FALSE( MapAssets_File( &ws, QStringLiteral( "material.cymat" ) ).isEmpty() ); REQUIRE_FALSE( MapAssets_List( &ws, {} ).isEmpty() );
    MapWorkspace_Shutdown( &ws );
    CHECK( resolverWeak.expired() ); CHECK( listerWeak.expired() );
    CHECK( MapAssets_Generation( &ws ) == 0 ); CHECK( MapAssets_Roots( &ws ).isEmpty() );
    const int resolverCalls = calls->resolver, listerCalls = calls->lister;
    CHECK( MapAssets_File( &ws, QStringLiteral( "material.cymat" ) ).isEmpty() ); CHECK( MapAssets_List( &ws, {} ).isEmpty() );
    REQUIRE( MapWorkspace_Init( &ws, &f.session.gui ) );
    CHECK( MapAssets_Generation( &ws ) == 0 ); CHECK( MapAssets_Roots( &ws ).isEmpty() );
    CHECK( MapAssets_File( &ws, QStringLiteral( "material.cymat" ) ).isEmpty() ); CHECK( MapAssets_List( &ws, {} ).isEmpty() );
    CHECK( calls->resolver == resolverCalls ); CHECK( calls->lister == listerCalls );
}

TEST_CASE( "Edge command history restores the resulting rim for continued extension", "[map][gui][geometry-commands][component-history-commands]" )
{
    session_t s; auto &ws = s.workspace;
    auto primitive = MapWorkspace_PrimitiveDefaults( &ws, Bounds( { -64, -64, 0 }, { 64, 64, 0 } ) );
    primitive.kind = map_primitive_kind_t::QUAD; primitive.axis = 2;
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, primitive ) );
    const u64 mesh = EditorSelection_At( &ws.selection, 0 );
    description_t before( ws, mesh );
    const auto original = geo::MeshEdgeRef_Make( before.Corner( 0, 0 ), before.Corner( 0, 1 ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, original ) );
    const usize steps = s.Steps();
    for ( const char *command : { "map.mesh.extend_edges", "map.mesh.extrude_edges" } ) {
        CAPTURE( command );
        REQUIRE( s.Enabled( command ) );
        REQUIRE( s.Run( command ) == command_result_t::OK );
        REQUIRE( ws.meshSelection.edges.nCount == 1 );
        const auto result = ws.meshSelection.edges.pData[0];
        CHECK_FALSE( ( result.a.value == original.a.value && result.b.value == original.b.value ) );
        const auto seed = ws.selectedMeshEdgeSeed;
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        REQUIRE( ws.meshSelection.edges.nCount == 1 );
        CHECK( ws.meshSelection.edges.pData[0].a.value == original.a.value );
        CHECK( ws.meshSelection.edges.pData[0].b.value == original.b.value );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        REQUIRE( ws.meshSelection.edges.nCount == 1 );
        CHECK( ws.meshSelection.edges.pData[0].a.value == result.a.value );
        CHECK( ws.meshSelection.edges.pData[0].b.value == result.b.value );
        CHECK( ws.selectedMeshEdgeSeed.a.value == seed.a.value );
        CHECK( ws.selectedMeshEdgeSeed.b.value == seed.b.value );
        REQUIRE( s.Enabled( command ) );
        REQUIRE( s.Run( command ) == command_result_t::OK );
        description_t continued( ws, mesh );
        CHECK( continued.value.faces.nCount == 3 );
        CHECK( continued.value.vertices.nCount == 8 );
        CHECK( s.Steps() == steps + 2 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        description_t restored( ws, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &before.value, &restored.value ) );
    }
}

TEST_CASE( "Snap Edge history preserves the chosen target selection", "[map][gui][geometry-commands][component-history-commands]" )
{
    session_t s; auto &ws = s.workspace;
    std::vector<u64> roots;
    for ( const auto bounds : { Bounds( { -64, -32, 0 }, { 0, 32, 0 } ), Bounds( { 128, -32, 0 }, { 192, 32, 0 } ) } ) {
        auto primitive = MapWorkspace_PrimitiveDefaults( &ws, bounds );
        primitive.kind = map_primitive_kind_t::QUAD; primitive.axis = 2;
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, primitive ) );
        roots.push_back( EditorSelection_At( &ws.selection, 0 ) );
    }
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
    Select( ws, roots );
    REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    const u64 mesh = EditorSelection_At( &ws.selection, 0 );
    description_t before( ws, mesh );
    const auto source = geo::MeshEdgeRef_Make( VertexAt( before, { 0, -32, 0 } ), VertexAt( before, { 0, 32, 0 } ) );
    const auto target = geo::MeshEdgeRef_Make( VertexAt( before, { 128, -32, 0 } ), VertexAt( before, { 128, 32, 0 } ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, source ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, mesh, target, MAP_SELECT_ADD ) );
    REQUIRE( s.Run( "map.mesh.snap_edge_to_edge" ) == command_result_t::OK );
    REQUIRE( ws.meshSelection.edges.nCount == 1 );
    CHECK( geo::MeshSelection_HasEdge( &ws.meshSelection, target ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.meshSelection.edges.nCount == 2 );
    CHECK( geo::MeshSelection_HasEdge( &ws.meshSelection, source ) );
    CHECK( geo::MeshSelection_HasEdge( &ws.meshSelection, target ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.meshSelection.edges.nCount == 1 );
    CHECK( geo::MeshSelection_HasEdge( &ws.meshSelection, target ) );
    CHECK( ws.selectedMeshEdgeSeed.a.value == target.a.value );
    CHECK( ws.selectedMeshEdgeSeed.b.value == target.b.value );
}

namespace
{
std::string ExportMapSnapshot( map_document_t *map )
{
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_Save( map, &text ) == map_status_t::OK );
    return { TextBuffer_Data( &text ), TextBuffer_Length( &text ) };
}
void ExportArray( map_document_t *map, u64 mesh )
{
    geo::modifier_t array{}; array.kind = geo::modifier_kind_t::LINEAR_ARRAY;
    array.cCopies = 2; array.step = { 256, 0, 0 }; array.bWeld = false;
    bool changed = false; REQUIRE( MapModifiers_Write( map, mesh, { array }, &changed ) == map_status_t::OK ); REQUIRE( changed );
}
struct export_allocation_t { usize calls{}, failOn{}, live{}; };
void *ExportAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &s = *static_cast<export_allocation_t *>( context );
    if ( ++s.calls == s.failOn ) { return nullptr; }
    void *p = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment ); if ( p ) { ++s.live; } return p;
}
void ExportFree( void *context, void *p, usize bytes, usize alignment ) noexcept
{
    if ( p ) { --static_cast<export_allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), p, bytes, alignment );
}
} // namespace

TEST_CASE( "OBJ refuses failed modifier evaluation instead of exporting the cage", "[map][gui][export-obj][export-integrity]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 mesh = AddBoxMesh( s ); ExportArray( ws.pDocument, mesh );
    std::string obj, mtl; map_export_report_t report{};
    REQUIRE( MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report ) == map_status_t::OK );
    CHECK( report.objects == 1 ); CHECK( report.triangles == 24 ); CHECK( report.skipped == 0 );
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( ws.pDocument, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    auto *array = KeyValue_ChildAt( KeyValue_Find( record, StringView_FromCString( "modifiers" ) ), 0 ); REQUIRE( array );
    auto *copies = KeyValue_Find( array, StringView_FromCString( "copies" ) ); REQUIRE( copies );
    REQUIRE( KeyValue_SetI64( store->store.pDocument, copies, 1000000 ) ); // Retained, unsupported stage parameters.
    const std::string before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    obj = "previous OBJ"; mtl = "previous MTL"; report = { 71, 72, 73, 74, 75 };
    CHECK( MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report ) == map_status_t::INVALID_ARGUMENT );
    CHECK( obj == "previous OBJ" ); CHECK( mtl == "previous MTL" );
    CHECK( report.objects == 71 ); CHECK( report.triangles == 72 ); CHECK( report.skipped == 73 );
    CHECK( report.skippedTerrain == 74 ); CHECK( report.skippedInstances == 75 );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "protected.obj" ) );
    const auto put = []( const QString &path, const QByteArray &text ) { QFile f( path ); REQUIRE( f.open( QIODevice::WriteOnly ) ); REQUIRE( f.write( text ) == text.size() ); };
    put( path, "old OBJ" ); put( folder.filePath( QStringLiteral( "protected.mtl" ) ), "old MTL" );
    const std::string command = "file.export \"" + path.toStdString() + "\"";
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( command.c_str() ) ) == command_result_t::FAILED );
    QFile oldObj( path ), oldMtl( folder.filePath( QStringLiteral( "protected.mtl" ) ) );
    REQUIRE( oldObj.open( QIODevice::ReadOnly ) ); REQUIRE( oldMtl.open( QIODevice::ReadOnly ) );
    CHECK( oldObj.readAll() == "old OBJ" ); CHECK( oldMtl.readAll() == "old MTL" );
    map_notice_t kind{}; CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "before writing" ) ) ); CHECK( kind == map_notice_t::ERROR );
    // A disabled stage is intentionally the source cage, rather than a failed evaluation.
    // Saving canonicalizes retained records; previous KeyValue pointers expire.
    record = MapDocument_FindObject( ws.pDocument, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    array = KeyValue_ChildAt( KeyValue_Find( record, StringView_FromCString( "modifiers" ) ), 0 ); REQUIRE( array );
    copies = KeyValue_Find( array, StringView_FromCString( "copies" ) ); REQUIRE( copies );
    REQUIRE( KeyValue_SetI64( store->store.pDocument, copies, 2 ) );
    auto *enabled = KeyValue_Find( array, StringView_FromCString( "enabled" ) ); REQUIRE( enabled );
    REQUIRE( KeyValue_SetBool( store->store.pDocument, enabled, CY_FALSE ) );
    REQUIRE( MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report ) == map_status_t::OK );
    CHECK( report.triangles == 12 ); CHECK( report.skipped == 0 );
}

TEST_CASE( "OBJ allocation refusals preserve complete outputs and the authored map", "[map][gui][export-obj][export-integrity][allocation]" )
{
    session_t s; auto &ws = s.workspace; const u64 mesh = AddBoxMesh( s ); ExportArray( ws.pDocument, mesh );
    const std::string fixture = ExportMapSnapshot( ws.pDocument );
    export_allocation_t audit{}; const allocator_t allocator{ &ExportAllocate, nullptr, &ExportFree, &audit };
    {
        // Ownership must use one immutable allocator for the document and its
        // nested geometry. Replacing only map.pAllocator is not a valid fault
        // seam: clone snapshots use geometry.pAllocator and release via their
        // owning document allocator.
        map_document_t map{};
        REQUIRE( MapDocument_Load( &map, &allocator, { fixture.data(), fixture.size() } ) == map_status_t::OK );
        const std::string before = ExportMapSnapshot( &map );
        const usize baselineLive = audit.live; REQUIRE( baselineLive > 0 );
        audit.calls = 0;
        std::string expectedObj, expectedMtl; map_export_report_t expected{};
        REQUIRE( MapExport_Obj( &map, "scene.mtl", expectedObj, expectedMtl, &expected ) == map_status_t::OK );
        const usize calls = audit.calls; REQUIRE( calls > 10 ); CHECK( audit.live == baselineLive ); usize refusals = 0;
        for ( usize failOn : { usize{ 1 }, calls / 2, calls - 2, calls - 1, calls } ) {
            CAPTURE( failOn, calls, baselineLive ); audit.calls = 0; audit.failOn = failOn;
            std::string obj = "kept OBJ", mtl = "kept MTL"; map_export_report_t report{ 71, 72, 73, 74, 75 };
            const auto status = MapExport_Obj( &map, "scene.mtl", obj, mtl, &report );
            audit.failOn = 0; // Snapshot validation is not part of the injected operation.
            if ( status == map_status_t::OK ) { CHECK( obj == expectedObj ); CHECK( mtl == expectedMtl ); CHECK( report.triangles == expected.triangles ); }
            else {
                ++refusals; CHECK( status == map_status_t::OUT_OF_MEMORY ); CHECK( obj == "kept OBJ" ); CHECK( mtl == "kept MTL" );
                CHECK( report.objects == 71 ); CHECK( report.triangles == 72 ); CHECK( report.skipped == 73 );
                CHECK( report.skippedTerrain == 74 ); CHECK( report.skippedInstances == 75 );
            }
            CHECK( audit.live == baselineLive ); CHECK( ExportMapSnapshot( &map ) == before );
            CHECK( audit.live == baselineLive );
        }
        CHECK( refusals >= 3 ); audit.failOn = 0;
        std::string obj, mtl; map_export_report_t report{};
        REQUIRE( MapExport_Obj( &map, "scene.mtl", obj, mtl, &report ) == map_status_t::OK );
        CHECK( obj == expectedObj ); CHECK( mtl == expectedMtl ); CHECK( audit.live == baselineLive );
        CHECK( ExportMapSnapshot( &map ) == before );
    }
    CHECK( audit.live == 0 );
    CHECK( ExportMapSnapshot( ws.pDocument ) == fixture );
}

TEST_CASE( "OBJ resolves transformed linked prefabs and refuses missing content before file replacement", "[map][gui][export-obj][export-integrity][prefab]" )
{
    session_t s; auto &ws = s.workspace; QTemporaryDir folder; REQUIRE( folder.isValid() );
    struct resolver_scope_t {
        map_workspace_t *ws;
        ~resolver_scope_t() { MapAssets_SetRoots( ws, {} ); MapHotspots_SetResolver( ws, {} ); }
    } cleanup{ &ws };
    MapAssets_SetRoots( &ws, { folder.path() } );
    MapHotspots_SetResolver( &ws, [&]( const QString &path ) { const QString f = QDir( folder.path() ).filePath( path ); return QFileInfo::exists( f ) ? f : QString(); } );
    AddBox( ws, { -32, -16, 0 }, { 32, 16, 16 } );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( "map.prefab.create_instance prefabs/export_plank" ) ) == command_result_t::OK );
    const u64 instance = EditorSelection_At( &ws.selection, 0 ); REQUIRE( MapInstances_IsInstance( &ws, instance ) );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 256, 0, 0 } ) );
    REQUIRE( MapWorkspace_RotateSelection( &ws, { 0, 0, 90 }, { 256, 0, 0 } ) );
    const auto bounds = Box( ws, instance ); const std::string before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    // Without a resolver the core API must account for the instance explicitly.
    std::string obj, mtl; map_export_report_t report{};
    REQUIRE( MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report ) == map_status_t::OK );
    CHECK( report.objects == 0 ); CHECK( report.skippedInstances == 1 ); CHECK( report.skipped == 1 );
    const QString output = folder.filePath( QStringLiteral( "scene.obj" ) ); const std::string command = "file.export \"" + output.toStdString() + "\"";
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( command.c_str() ) ) == command_result_t::OK );
    QFile file( output ); REQUIRE( file.open( QIODevice::ReadOnly ) ); const QByteArray exported = file.readAll(); file.close();
    map_bounds_t exportedBounds{}; usize faces = 0;
    std::istringstream lines( exported.toStdString() ); std::string line;
    while ( std::getline( lines, line ) ) {
        if ( line.starts_with( "v " ) ) { std::istringstream value( line.substr( 2 ) ); cypher::math::vec3d_t p{}; REQUIRE( static_cast<bool>( value >> p.x >> p.y >> p.z ) ); MapBounds_AddPoint( exportedBounds, p ); }
        if ( line.starts_with( "f " ) ) { ++faces; }
    }
    REQUIRE( exportedBounds.bHas ); CHECK( faces == 12 );
    CHECK( exportedBounds.box.minimum.x == Catch::Approx( bounds.minimum.x ) ); CHECK( exportedBounds.box.maximum.x == Catch::Approx( bounds.maximum.x ) );
    CHECK( exportedBounds.box.minimum.y == Catch::Approx( bounds.minimum.y ) ); CHECK( exportedBounds.box.maximum.y == Catch::Approx( bounds.maximum.y ) );
    CHECK( exportedBounds.box.minimum.z == Catch::Approx( bounds.minimum.z ) ); CHECK( exportedBounds.box.maximum.z == Catch::Approx( bounds.maximum.z ) );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps ); CHECK( MapInstances_IsInstance( &ws, instance ) );
    const QString prefab = folder.filePath( QStringLiteral( "prefabs/export_plank.cyprefab" ) ); REQUIRE( QFile::remove( prefab ) ); MapInstances_ReloadFiles( &ws );
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( command.c_str() ) ) == command_result_t::FAILED );
    REQUIRE( file.open( QIODevice::ReadOnly ) ); CHECK( file.readAll() == exported );
    map_notice_t kind{}; CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "before writing" ) ) ); CHECK( kind == map_notice_t::ERROR );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
}

TEST_CASE( "OBJ refuses unsupported enabled stored stages but accepts supported or disabled stages", "[map][gui][export-obj][export-integrity]" )
{
    session_t s; auto &ws = s.workspace; const u64 mesh = AddBoxMesh( s ); ExportArray( ws.pDocument, mesh );
    map_layer_store_t *store = nullptr;
    auto *record = MapDocument_FindObject( ws.pDocument, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    auto *stage = KeyValue_ChildAt( KeyValue_Find( record, StringView_FromCString( "modifiers" ) ), 0 ); REQUIRE( stage );
    const char *type = "subdivide"; const char *scheme = nullptr; bool enabled = true, supported = false;
    SECTION( "Enabled unknown stage" ) { type = "future_deform"; }
    SECTION( "Disabled unknown stage" ) { type = "future_deform"; enabled = false; }
    SECTION( "Enabled Loop subdivision" ) { scheme = "loop"; }
    SECTION( "Disabled Loop subdivision" ) { scheme = "loop"; enabled = false; }
    SECTION( "Enabled unknown subdivision" ) { scheme = "future_subdivision"; }
    SECTION( "Disabled unknown subdivision" ) { scheme = "future_subdivision"; enabled = false; }
    SECTION( "Linear subdivision" ) { scheme = "linear"; supported = true; }
    SECTION( "Catmull Clark subdivision" ) { scheme = "catmull_clark"; supported = true; }
    SECTION( "Missing scheme uses default subdivision" ) { supported = true; }
    SECTION( "Named default subdivision" ) { scheme = "default"; supported = true; }
    auto *typeValue = KeyValue_Find( stage, StringView_FromCString( "type" ) ); REQUIRE( typeValue );
    REQUIRE( KeyValue_SetString( store->store.pDocument, typeValue, StringView_FromCString( type ) ) );
    auto *enabledValue = KeyValue_Find( stage, StringView_FromCString( "enabled" ) ); REQUIRE( enabledValue );
    REQUIRE( KeyValue_SetBool( store->store.pDocument, enabledValue, enabled ) );
    if ( scheme != nullptr ) {
        auto *schemeValue = KeyValue_ObjectInsert( store->store.pDocument, stage, StringView_FromCString( "scheme" ), key_value_type_t::STRING ); REQUIRE( schemeValue );
        REQUIRE( KeyValue_SetString( store->store.pDocument, schemeValue, StringView_FromCString( scheme ) ) );
    }
    const auto before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    std::string obj = "retained OBJ", mtl = "retained MTL";
    map_export_report_t report{ 71, 72, 73, 74, 75 };
    const auto status = MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report );
    if ( !enabled || supported ) {
        REQUIRE( status == map_status_t::OK ); CHECK( report.objects == 1 ); CHECK( report.skipped == 0 );
        CHECK( report.triangles == ( enabled ? 48u : 12u ) );
        CHECK( obj.find( "f " ) != std::string::npos ); CHECK( mtl.find( "newmtl " ) != std::string::npos );
    } else {
        CHECK( status == map_status_t::INVALID_ARGUMENT );
        CHECK( obj == "retained OBJ" ); CHECK( mtl == "retained MTL" );
        CHECK( report.objects == 71 ); CHECK( report.triangles == 72 ); CHECK( report.skipped == 73 );
        CHECK( report.skippedTerrain == 74 ); CHECK( report.skippedInstances == 75 );
    }
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
}

TEST_CASE( "OBJ prepares both destination files before replacing an existing OBJ", "[map][gui][export-obj][export-integrity]" )
{
    session_t s; auto &ws = s.workspace; AddBoxMesh( s );
    const auto before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString objPath = directory.filePath( QStringLiteral( "protected.obj" ) );
    const QString mtlPath = directory.filePath( QStringLiteral( "protected.mtl" ) );
    { QFile existing( objPath ); REQUIRE( existing.open( QIODevice::WriteOnly ) ); REQUIRE( existing.write( "previous OBJ bytes" ) == 18 ); }
    REQUIRE( QDir().mkdir( mtlPath ) ); // Valid OBJ target, unreplaceable sibling MTL target.
    const std::string command = "file.export \"" + objPath.toStdString() + "\"";
    CHECK( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( command.c_str() ) ) == command_result_t::FAILED );
    { QFile existing( objPath ); REQUIRE( existing.open( QIODevice::ReadOnly ) ); CHECK( existing.readAll() == "previous OBJ bytes" ); }
    CHECK( QFileInfo( mtlPath ).isDir() );
    CHECK( QDir( directory.path() ).entryList( QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot ) == QStringList{ QStringLiteral( "protected.obj" ) } );
    map_notice_t kind{}; const auto notice = MapNotice_Last( &ws, &kind );
    CHECK( kind == map_notice_t::ERROR ); CHECK( notice.contains( QStringLiteral( "before replacing" ) ) );
    CHECK( notice.contains( QDir::toNativeSeparators( mtlPath ) ) );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
    // Once both destinations are writable, the same registered command succeeds.
    REQUIRE( QDir().rmdir( mtlPath ) );
    REQUIRE( EditorCommands_ExecuteLine( &s.gui.commands, StringView_FromCString( command.c_str() ) ) == command_result_t::OK );
    QFile obj( objPath ), mtl( mtlPath ); REQUIRE( obj.open( QIODevice::ReadOnly ) ); REQUIRE( mtl.open( QIODevice::ReadOnly ) );
    CHECK( obj.readAll().contains( "mtllib protected.mtl" ) ); CHECK( mtl.readAll().contains( "newmtl " ) );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
}

TEST_CASE( "OBJ generated modifier identities are private and capacity failure preserves outputs", "[map][gui][export-obj][export-integrity]" )
{
    session_t s; auto &ws = s.workspace; const u64 mesh = AddBoxMesh( s ); ExportArray( ws.pDocument, mesh );
    // Fewer unused identities remain than the array needs. This is a private
    // export preparation failure, not permission to export the original cage.
    ws.pDocument->nextId = CY_U64_MAX - 3u;
    ws.pDocument->geometry.sourceIds.allocator.next.value = CY_U64_MAX - 3u;
    const auto before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    const auto next = ws.pDocument->nextId, sourceNext = ws.pDocument->geometry.sourceIds.allocator.next.value;
    std::string obj = "retained OBJ", mtl = "retained MTL"; map_export_report_t report{ 71, 72, 73, 74, 75 };
    CHECK( MapExport_Obj( ws.pDocument, "scene.mtl", obj, mtl, &report ) == map_status_t::LIMIT_EXCEEDED );
    CHECK( obj == "retained OBJ" ); CHECK( mtl == "retained MTL" );
    CHECK( report.objects == 71 ); CHECK( report.triangles == 72 ); CHECK( report.skipped == 73 );
    CHECK( report.skippedTerrain == 74 ); CHECK( report.skippedInstances == 75 );
    CHECK( ws.pDocument->nextId == next ); CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); CHECK( s.Steps() == steps );
}

namespace
{
struct brush_face_settings_t {
    settings_registry_t *registry;
    settings_document_t store{};
    explicit brush_face_settings_t( settings_registry_t *settings ) : registry( settings )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~brush_face_settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Number( const char *path, f64 number )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) ); REQUIRE( descriptor );
        setting_value_t value{}; value.type = descriptor->type;
        if ( value.type == setting_type_t::INTEGER ) { value.nValue = static_cast<i64>( number ); } else { value.flValue = number; }
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
};

key_value_t *BrushFaceRecord( key_value_t *root, u64 face )
{
    auto *faces = KeyValue_Find( root, StringView_FromCString( "faces" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( faces ); ++i ) {
        auto *entry = KeyValue_ChildAt( faces, i ); u64 id = 0;
        if ( KeyValue_GetU64( KeyValue_Find( entry, StringView_FromCString( "id" ) ), &id ) && id == face ) { return entry; }
    }
    return nullptr;
}

struct brush_face_command_t {
    session_t session;
    brush_face_settings_t settings;
    u64 root{}, face{}, owner{}, group{};
    explicit brush_face_command_t( const allocator_t *allocator = Allocator_GetSystem() ) : session( allocator ), settings( &session.gui.settings )
    {
        auto &ws = session.workspace; auto *map = ws.pDocument;
        REQUIRE( MapEdit_CreateBox( map, { { -64, -64, 0 }, { 64, 64, 128 } }, StringView_FromCString( "materials/test/face_adapter.cymat" ), {}, &root ) == map_status_t::OK );
        REQUIRE( MapDocument_AddLayer( map, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( map, root, StringView_FromCString( "detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( map, StringView_FromCString( "detail" ), StringView_FromCString( "func_structure" ), {}, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( map, root, owner ) == map_status_t::OK );
        REQUIRE( MapDocument_AddGroup( map, StringView_FromCString( "Authored panel" ), { &root, 1 }, &group ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
            const auto &side = ws.wire.faces.pData[i]; if ( side.id == root && side.normal.z > .99 ) { face = side.sideId; }
        }
        REQUIRE( face != 0 );
        const map_surface_ref_t surface{ root, face };
        REQUIRE( MapTextureEdit_Shift( map, { &surface, 1 }, { 11, -7 }, 128 ) == map_status_t::OK );
        (void)ExportMapSnapshot( map ); // Materialize residual records before attaching future fields.
        map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( map, root, &store ); REQUIRE( record ); REQUIRE( store );
        // Authoritative brush planes are separate from retained records. An
        // untouched face need not already have a residual entry after Save.
        auto *residualFaces = KeyValue_Find( record, StringView_FromCString( "faces" ) );
        if ( !residualFaces ) { residualFaces = KeyValue_ObjectInsert( store->store.pDocument, record, StringView_FromCString( "faces" ), key_value_type_t::ARRAY ); }
        REQUIRE( residualFaces ); auto *side = BrushFaceRecord( record, face );
        if ( !side ) {
            side = KeyValue_ArrayAppend( store->store.pDocument, residualFaces, key_value_type_t::OBJECT ); REQUIRE( side );
            auto *identity = KeyValue_ObjectInsert( store->store.pDocument, side, StringView_FromCString( "id" ), key_value_type_t::U64 ); REQUIRE( identity );
            REQUIRE( KeyValue_SetU64( store->store.pDocument, identity, face ) );
        }
        for ( const auto target : { record, side } ) {
            auto *marker = KeyValue_ObjectInsert( store->store.pDocument, target, StringView_FromCString( "future_adapter_data" ), key_value_type_t::U64 ); REQUIRE( marker );
            REQUIRE( KeyValue_SetU64( store->store.pDocument, marker, target == record ? 701 : 702 ) );
        }
        MapWorkspace_DocumentChanged( &ws ); Pick();
        settings.Number( "editor.map.mesh_extrude_distance", 16 ); settings.Number( "editor.map.mesh_inset_distance", 8 );
        settings.Number( "editor.map.mesh_slice_u", 2 ); settings.Number( "editor.map.mesh_slice_v", 2 );
    }
    void Pick()
    {
        auto &ws = session.workspace;
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        MapWorkspace_SelectBrushFace( &ws, root, face ); REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
    }
    void CheckMetadata( map_document_t *map ) const
    {
        const map_geometry_record_t *placement = nullptr;
        for ( usize i = 0; i < map->geometryRecords.nCount; ++i ) { if ( map->geometryRecords.pData[i].id == root ) { placement = &map->geometryRecords.pData[i]; } }
        REQUIRE( placement ); CHECK( placement->owner == owner ); REQUIRE( placement->iLayer < map->layers.nCount );
        CHECK( StringView_Equals( StringView_FromCString( map->layers.pData[placement->iLayer].id ), StringView_FromCString( "detail" ) ) );
        CHECK( MapGroups_Parent( map, root ) == group ); CHECK( MapGroups_Name( map, group ) == "Authored panel" );
        auto *record = MapDocument_FindObject( map, root, nullptr ); REQUIRE( record );
        u64 rootMarker = 0, faceMarker = 0;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( record, StringView_FromCString( "future_adapter_data" ) ), &rootMarker ) ); CHECK( rootMarker == 701 );
        auto *side = BrushFaceRecord( record, face ); REQUIRE( side );
        REQUIRE( KeyValue_GetU64( KeyValue_Find( side, StringView_FromCString( "future_adapter_data" ) ), &faceMarker ) ); CHECK( faceMarker == 702 );
        map_uv_face_t described{}; REQUIRE( MapTextureEdit_Describe( map, { root, face }, &described ) == map_status_t::OK );
        CHECK( described.material == "materials/test/face_adapter.cymat" );
    }
    std::string PrepareRedo()
    {
        auto &ws = session.workspace; MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); Select( ws, { root } );
        REQUIRE( MapWorkspace_TranslateSelection( &ws, { 32, 0, 0 } ) ); const auto translated = ExportMapSnapshot( ws.pDocument );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); Pick(); REQUIRE( EditorHistory_CanRedo( &ws.history ) ); return translated;
    }
    void CheckMeshSelection() const
    {
        const auto &ws = session.workspace; REQUIRE( EditorSelection_Count( &ws.selection ) == 1 ); CHECK( EditorSelection_At( &ws.selection, 0 ) == root );
        CHECK( ws.elementMode == map_element_mode_t::FACES ); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceObject == root ); CHECK( ws.selectedMeshFaceId == face );
        const auto faces = MapWorkspace_MeshFaces( &ws ); REQUIRE( faces.nCount == 1 ); CHECK( faces.pData[0] == face );
    }
};

struct brush_face_unchanged_t {
    std::string saved;
    const map_document_t *document;
    const cypher::math::vec3d_t *points;
    const u64 *roots;
    u64 revision, selectionRevision, nextId, nextSource, brush, side, mesh, face;
    usize steps, applied;
    bool redo;
    undo_state_token_t token;
    explicit brush_face_unchanged_t( map_workspace_t &ws ) : saved( ExportMapSnapshot( ws.pDocument ) ), document( ws.pDocument ), points( ws.wire.points.pData ),
        roots( ws.selection.ids.pData ), revision( ws.pDocument->geometry.revision ), selectionRevision( ws.selection.revision ), nextId( ws.pDocument->nextId ),
        nextSource( ws.pDocument->geometry.sourceIds.allocator.next.value ), brush( ws.selectedBrushFaceObject ), side( ws.selectedBrushFaceSide ),
        mesh( ws.selectedMeshFaceObject ), face( ws.selectedMeshFaceId ), steps( EditorHistory_StepCount( &ws.history ) ), applied( EditorHistory_AppliedStepCount( &ws.history ) ),
        redo( EditorHistory_CanRedo( &ws.history ) ), token( UndoRedo_StateToken( ws.history.pUndo ) ) {}
    void Check( map_workspace_t &ws ) const
    {
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( ws.selection.ids.pData == roots );
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.pDocument->nextId == nextId );
        CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == nextSource );
        CHECK( ws.selectedBrushFaceObject == brush ); CHECK( ws.selectedBrushFaceSide == side ); CHECK( ws.selectedMeshFaceObject == mesh ); CHECK( ws.selectedMeshFaceId == face );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( EditorHistory_CanRedo( &ws.history ) == redo ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( ExportMapSnapshot( ws.pDocument ) == saved );
    }
};
}

TEST_CASE( "Registered brush face modeling converts, edits and selects in one reversible publication", "[map][gui][geometry-commands][atomic-brush-face-adapter]" )
{
    const char *command = "map.mesh.extrude"; usize vertices = 12, faces = 10; f64 high = 144;
    SECTION( "Extrude retains its cap" ) {}
    SECTION( "Corner Inset retains its inset face" ) { command = "map.mesh.inset"; high = 128; }
    SECTION( "Quad Slice retains its first cell" ) { command = "map.mesh.quad_slice"; vertices = 13; faces = 9; high = 128; }
    brush_face_command_t f; auto &s = f.session; auto &ws = s.workspace;
    const std::string original = ExportMapSnapshot( ws.pDocument ); const usize before = s.Steps();
    REQUIRE( s.Enabled( command ) ); REQUIRE( s.Run( command ) == command_result_t::OK );
    CHECK( s.Steps() == before + 1 ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before + 1 );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { f.root } ) == nullptr );
    description_t result( ws, f.root ); CHECK( result.value.vertices.nCount == vertices ); CHECK( result.value.faces.nCount == faces );
    CHECK( Box( ws, f.root ).maximum.z == Catch::Approx( high ) ); f.CheckMeshSelection(); f.CheckMetadata( ws.pDocument );
    const std::string modeled = ExportMapSnapshot( ws.pDocument );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == original );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) ); CHECK( ws.selectedBrushFaceObject == f.root ); CHECK( ws.selectedBrushFaceSide == f.face ); f.CheckMetadata( ws.pDocument );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == modeled ); f.CheckMeshSelection();
    { description_t redone( ws, f.root ); CHECK( geo::MeshSourceDescription_Equal( &result.value, &redone.value ) ); }
    QTemporaryDir directory; REQUIRE( directory.isValid() ); const auto path = directory.filePath( QStringLiteral( "atomic-face.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); session_t reopened;
    REQUIRE( MapWorkspace_Open( &reopened.workspace, path ).status == map_files_status_t::OK ); f.CheckMetadata( reopened.workspace.pDocument );
    CHECK( ExportMapSnapshot( reopened.workspace.pDocument ) == modeled );
}

TEST_CASE( "Refused brush face commands preserve an existing Redo branch and original face", "[map][gui][geometry-commands][atomic-brush-face-adapter]" )
{
    brush_face_command_t f; auto &s = f.session; auto &ws = s.workspace;
    const auto redo = f.PrepareRedo(); const char *command = "map.mesh.inset";
    SECTION( "Corner Inset cannot consume the face" ) { f.settings.Number( "editor.map.mesh_inset_distance", 1000 ); }
    SECTION( "A one by one Quad Slice makes no edit" ) { command = "map.mesh.quad_slice"; f.settings.Number( "editor.map.mesh_slice_u", 1 ); f.settings.Number( "editor.map.mesh_slice_v", 1 ); }
    SECTION( "Measured-width Inset remains explicitly mesh-only" ) { command = "map.mesh.inset_width"; CHECK_FALSE( s.Enabled( command ) ); }
    const brush_face_unchanged_t before( ws ); CHECK( s.Run( command ) != command_result_t::OK ); before.Check( ws );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
    CHECK( MapWireframe_FindObject( ws.wire, f.root )->kind == map_wire_kind_t::BRUSH );
}

TEST_CASE( "Locked brush faces cannot enter implicit mesh modeling and work after unlocking", "[map][gui][geometry-commands][atomic-brush-face-adapter]" )
{
    brush_face_command_t f; auto &s = f.session; auto &ws = s.workspace;
    REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "detail" ), true ) );
    const brush_face_unchanged_t locked( ws );
    for ( const char *command : { "map.mesh.extrude", "map.mesh.inset", "map.mesh.quad_slice" } ) {
        CAPTURE( command ); MapWorkspace_SelectBrushFace( &ws, f.root, f.face ); CHECK_FALSE( s.Enabled( command ) );
        CHECK( s.Run( command ) == command_result_t::DISABLED ); locked.Check( ws );
    }
    REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "detail" ), false ) ); f.Pick();
    const usize steps = s.Steps(); REQUIRE( s.Run( "map.mesh.quad_slice" ) == command_result_t::OK ); CHECK( s.Steps() == steps + 1 ); f.CheckMeshSelection();
}

TEST_CASE( "Brush face adapter allocation refusals retain Redo and retry without a conversion step", "[map][gui][geometry-commands][atomic-brush-face-adapter][allocation]" )
{
    // The allocator owns the whole session, including the document's nested
    // geometry, and outlives every selection/history allocation transferred on
    // success. No allocator pointer is replaced on a live document.
    export_allocation_t audit{}; const allocator_t allocator{ &ExportAllocate, nullptr, &ExportFree, &audit };
    usize successfulCalls = 0;
    {
        brush_face_command_t f( &allocator ); (void)f.PrepareRedo(); audit.calls = 0;
        REQUIRE( f.session.Run( "map.mesh.extrude" ) == command_result_t::OK ); successfulCalls = audit.calls;
    }
    REQUIRE( successfulCalls > 10 ); CHECK( audit.live == 0 ); usize refused = 0;
    for ( const usize failOn : { usize{ 1 }, successfulCalls / 2, successfulCalls - 1 } ) {
        CAPTURE( failOn, successfulCalls );
        {
            brush_face_command_t f( &allocator ); auto &s = f.session; auto &ws = s.workspace;
            const auto redo = f.PrepareRedo(); const brush_face_unchanged_t before( ws ); audit.calls = 0; audit.failOn = failOn;
            const auto status = s.Run( "map.mesh.extrude" ); audit.failOn = 0;
            if ( status != command_result_t::OK ) {
                ++refused; before.Check( ws ); map_notice_t kind{}; (void)MapNotice_Last( &ws, &kind ); CHECK( kind == map_notice_t::ERROR );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); f.Pick();
                REQUIRE( s.Run( "map.mesh.extrude" ) == command_result_t::OK );
            }
            // A sampled final allocation may belong to optional notification
            // work. Either way, no intermediate converted-only history survives.
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); CHECK( s.Steps() == before.applied + 1 ); f.CheckMeshSelection();
        }
        CHECK( audit.live == 0 );
    }
    CHECK( refused >= 2 );
}

TEST_CASE( "Tool Properties mouse buttons publish brush conversion once and refuse inset without losing Redo", "[map][gui][toolpanels][atomic-brush-face-adapter][widget-events]" )
{
    brush_face_command_t f; auto &s = f.session; auto &ws = s.workspace;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); REQUIRE( panel );
    panel->resize( 520, 1200 ); panel->show(); QCoreApplication::processEvents();
    const auto click = [&]( const char *command ) {
        auto *body = panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) ); REQUIRE( body );
        gui::EditorSection_SetExpanded( body->parentWidget(), true ); QCoreApplication::processEvents();
        QPointer<QPushButton> button = body->findChild<QPushButton *>( QString::fromLatin1( command ) ); REQUIRE( button );
        REQUIRE( button->isEnabled() ); REQUIRE( button->isVisible() );
        const QPointF centre = button->rect().center();
        QMouseEvent press( QEvent::MouseButtonPress, centre, button->mapToGlobal( centre ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( button, &press ); REQUIRE( button );
        QMouseEvent release( QEvent::MouseButtonRelease, centre, button->mapToGlobal( centre ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( button, &release ); QCoreApplication::processEvents(); REQUIRE( button );
    };
    const auto original = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    click( "map.mesh.extrude" ); CHECK( s.Steps() == steps + 1 ); f.CheckMeshSelection();
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == original );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) ); CHECK( ws.selectedBrushFaceSide == f.face ); REQUIRE( EditorHistory_CanRedo( &ws.history ) );
    f.settings.Number( "editor.map.mesh_inset_distance", 1000 ); QCoreApplication::processEvents();
    const brush_face_unchanged_t before( ws ); click( "map.mesh.inset" ); before.Check( ws );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); f.CheckMeshSelection(); CHECK( s.Steps() == steps + 1 );
}

namespace
{
u64 CarveBox( map_workspace_t &ws, cypher::math::vec3d_t low, cypher::math::vec3d_t high )
{
    u64 root = 0;
    REQUIRE( MapEdit_CreateBox( ws.pDocument, { low, high }, StringView_FromCString( "materials/test/carve.cymat" ), {}, &root ) == map_status_t::OK );
    return root;
}
void CarveLockLayer( map_workspace_t &ws, u64 target, bool hidden = false )
{
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "protected" ), StringView_FromCString( "Protected" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, target, StringView_FromCString( "protected" ) ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "protected" ), true ) );
    if ( hidden ) { REQUIRE( MapLayers_SetHidden( &ws, QStringLiteral( "protected" ), true ) ); }
}
void CarveSelect( map_workspace_t &ws, std::vector<u64> cutters )
{
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); Select( ws, cutters );
    REQUIRE( ws.selection.ids.nCount == cutters.size() );
}
std::string CarvePrepareRedo( map_workspace_t &ws )
{
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 0, 16, 0 } ) );
    const auto translated = ExportMapSnapshot( ws.pDocument ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( EditorHistory_CanRedo( &ws.history ) ); return translated;
}
struct carve_brush_snapshot_t {
    geo::brush_source_t source{};
    explicit carve_brush_snapshot_t( const map_document_t *map, u64 id )
    {
        REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { id }, Allocator_GetSystem(), &source ) == geo::geometry_status_t::OK );
    }
    ~carve_brush_snapshot_t() { geo::BrushSource_Shutdown( &source ); }
    void Check( const map_document_t *map, u64 id ) const
    {
        carve_brush_snapshot_t actual( map, id ); CHECK( geo::BrushSource_Equal( &source, &actual.source ) );
    }
};
}

TEST_CASE( "Carve refuses every cutter when an unselected locked victim would change", "[map][gui][geometry-commands][carve-target-locks]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 unlocked = CarveBox( ws, { 0, 0, 0 }, { 128, 128, 128 } );
    const u64 locked = CarveBox( ws, { 256, 0, 0 }, { 384, 128, 128 } );
    const u64 first = CarveBox( ws, { 96, 32, 32 }, { 160, 96, 96 } );
    const u64 second = CarveBox( ws, { 352, 32, 32 }, { 416, 96, 96 } );
    CarveLockLayer( ws, locked ); CarveSelect( ws, { first, second } );
    const auto redo = CarvePrepareRedo( ws ); const brush_face_unchanged_t before( ws );
    carve_brush_snapshot_t cutterA( ws.pDocument, first ), cutterB( ws.pDocument, second );
    REQUIRE( s.Enabled( "map.brush.carve" ) ); CHECK( s.Run( "map.brush.carve" ) == command_result_t::FAILED ); before.Check( ws );
    REQUIRE( ws.selection.ids.nCount == 2 ); CHECK( ws.selection.ids.pData[0] == first ); CHECK( ws.selection.ids.pData[1] == second );
    map_notice_t kind{}; CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "locked layer" ) ) ); CHECK( kind == map_notice_t::WARNING );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "protected" ), false ) );
    REQUIRE( s.Run( "map.brush.carve" ) == command_result_t::OK ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 );
    CHECK( s.Steps() == before.applied + 1 ); CHECK_FALSE( EditorHistory_CanRedo( &ws.history ) );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { unlocked } ) == nullptr );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { locked } ) == nullptr );
    cutterA.Check( ws.pDocument, first ); cutterB.Check( ws.pDocument, second );
    const auto carved = ExportMapSnapshot( ws.pDocument ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before.saved ); REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CHECK( ExportMapSnapshot( ws.pDocument ) == carved );
}

TEST_CASE( "Carve allows unrelated, touching, separated angled and hidden locked brushes without modifying them", "[map][gui][geometry-commands][carve-target-locks]" )
{
    session_t s; auto &ws = s.workspace; u64 protectedRoot = 0, cutter = 0;
    const u64 victim = CarveBox( ws, { -128, -128, -16 }, { 128, 128, 128 } ); bool hidden = false;
    SECTION( "Unrelated locked layer" ) {
        cutter = CarveBox( ws, { 96, 32, 32 }, { 160, 96, 96 } ); protectedRoot = CarveBox( ws, { 512, 0, 0 }, { 576, 64, 64 } );
    }
    SECTION( "Locked face only touches the cutter" ) {
        cutter = CarveBox( ws, { 96, 32, 32 }, { 160, 96, 96 } ); protectedRoot = CarveBox( ws, { 160, 32, 32 }, { 224, 96, 96 } );
    }
    SECTION( "Parallel slats have overlapping bounds but no shared volume" ) {
        cutter = CarveBox( ws, { -64, -4, 0 }, { 64, 4, 32 } ); protectedRoot = CarveBox( ws, { -64, -4, 0 }, { 64, 4, 32 } );
        const u64 both[]{ cutter, protectedRoot };
        REQUIRE( MapEdit_Rotate( ws.pDocument, { both, 2 }, { 0, 0, 45 }, {} ) == map_status_t::OK );
        REQUIRE( MapEdit_Translate( ws.pDocument, { &protectedRoot, 1 }, { -16, 16, 0 } ) == map_status_t::OK );
    }
    SECTION( "Hidden brushes are outside Carve's visible target scope" ) {
        cutter = CarveBox( ws, { 96, 32, 32 }, { 160, 96, 96 } ); protectedRoot = CarveBox( ws, { 128, 40, 40 }, { 152, 88, 88 } ); hidden = true;
    }
    CarveLockLayer( ws, protectedRoot, hidden ); CarveSelect( ws, { cutter } );
    carve_brush_snapshot_t protectedBefore( ws.pDocument, protectedRoot ), cutterBefore( ws.pDocument, cutter );
    const auto original = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    REQUIRE( s.Run( "map.brush.carve" ) == command_result_t::OK ); CHECK( s.Steps() == steps + 1 );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { victim } ) == nullptr );
    protectedBefore.Check( ws.pDocument, protectedRoot ); cutterBefore.Check( ws.pDocument, cutter );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == cutter );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == original );
}

TEST_CASE( "Carve with only a touching locked target is a truthful no-op that retains Redo", "[map][gui][geometry-commands][carve-target-locks]" )
{
    session_t s; auto &ws = s.workspace;
    const u64 victim = CarveBox( ws, { 128, 0, 0 }, { 256, 128, 128 } ); const u64 cutter = CarveBox( ws, { 0, 0, 0 }, { 128, 128, 128 } );
    CarveLockLayer( ws, victim ); CarveSelect( ws, { cutter } ); const auto redo = CarvePrepareRedo( ws );
    const brush_face_unchanged_t before( ws ); REQUIRE( s.Enabled( "map.brush.carve" ) );
    CHECK( s.Run( "map.brush.carve" ) == command_result_t::OK ); before.Check( ws );
    map_notice_t kind{}; CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "no changes" ) ) ); CHECK( kind == map_notice_t::INFO );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
}

TEST_CASE( "Carve state is allocation-free and preparation refusal leaves the map and Redo available", "[map][gui][geometry-commands][carve-target-locks][allocation]" )
{
    for ( const usize failOn : { usize{ 1 }, usize{ 2 }, usize{ 3 } } ) {
        CAPTURE( failOn ); export_allocation_t audit{}; const allocator_t allocator{ &ExportAllocate, nullptr, &ExportFree, &audit };
        {
            // The audit allocator owns the complete session through teardown.
            session_t s( &allocator ); auto &ws = s.workspace;
            (void)CarveBox( ws, { 0, 0, 0 }, { 128, 128, 128 } ); const u64 protectedRoot = CarveBox( ws, { 160, 32, 32 }, { 224, 96, 96 } );
            const u64 cutter = CarveBox( ws, { 96, 32, 32 }, { 160, 96, 96 } ); CarveLockLayer( ws, protectedRoot ); CarveSelect( ws, { cutter } );
            const auto redo = CarvePrepareRedo( ws ); const brush_face_unchanged_t before( ws );
            audit.calls = 0; audit.failOn = 1; CHECK( s.Enabled( "map.brush.carve" ) ); CHECK( audit.calls == 0 );
            audit.failOn = failOn; const auto status = s.Run( "map.brush.carve" ); audit.failOn = 0;
            CHECK( status == command_result_t::FAILED ); CHECK( audit.calls >= failOn ); before.Check( ws );
            map_notice_t kind{}; CHECK( MapNotice_Last( &ws, &kind ).contains( QStringLiteral( "memory" ) ) ); CHECK( kind == map_notice_t::ERROR );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( s.Run( "map.brush.carve" ) == command_result_t::OK );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); CHECK( s.Steps() == before.applied + 1 );
        }
        CHECK( audit.live == 0 );
    }
}

namespace
{
void JoinCommandStack( map_document_t &map, u64 mesh, bool enabled, bool unknown = false, f64 offset = 256.0 )
{
    if ( !unknown ) {
        geo::modifier_t stage{}; stage.kind = geo::modifier_kind_t::MIRROR; stage.axis = 0; stage.offset = offset; stage.bWeld = false; stage.bEnabled = enabled;
        bool changed = false; REQUIRE( MapModifiers_Write( &map, mesh, { stage }, &changed ) == map_status_t::OK ); REQUIRE( changed ); return;
    }
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    auto *stages = KeyValue_ObjectInsert( store->store.pDocument, record, StringView_FromCString( "modifiers" ), key_value_type_t::ARRAY ); REQUIRE( stages );
    auto *stage = KeyValue_ArrayAppend( store->store.pDocument, stages, key_value_type_t::OBJECT ); REQUIRE( stage );
    auto *type = KeyValue_ObjectInsert( store->store.pDocument, stage, StringView_FromCString( "type" ), key_value_type_t::STRING ); REQUIRE( type );
    REQUIRE( KeyValue_SetString( store->store.pDocument, type, StringView_FromCString( "future_lattice" ) ) );
    auto *payload = KeyValue_ObjectInsert( store->store.pDocument, stage, StringView_FromCString( "future_payload" ), key_value_type_t::U64 ); REQUIRE( payload );
    REQUIRE( KeyValue_SetU64( store->store.pDocument, payload, 782u ) );
}
void JoinCommandEmptyStack( map_document_t &map, u64 mesh )
{
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    REQUIRE( KeyValue_ObjectInsert( store->store.pDocument, record, StringView_FromCString( "modifiers" ), key_value_type_t::ARRAY ) );
}
std::string JoinCommandPrepareRedo( map_workspace_t &ws, u64 a, u64 b )
{
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    Select( ws, { a, b } ); REQUIRE( ws.selection.ids.nCount == 2u );
    // Creating an unrelated object establishes a real Redo without requiring
    // the retained stack to support a whole-object transform.
    ( void )AddBox( ws, { 1024, 0, 0 }, { 1088, 64, 64 } ); const auto redo = ExportMapSnapshot( ws.pDocument );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); Select( ws, { a, b } );
    REQUIRE( EditorHistory_CanRedo( &ws.history ) ); return redo;
}
}

TEST_CASE( "Registered Join preserves retained modifiers and Redo before any brush conversion", "[map][gui][geometry-commands][join-modifier-preservation]" )
{
    bool mixed = false, donor = false, both = false, different = false, enabled = true, unknown = false;
    SECTION( "Modifier on survivor" ) {}
    SECTION( "Disabled donor modifier" ) { donor = true; enabled = false; }
    SECTION( "Identical stacks on both meshes" ) { both = true; }
    SECTION( "Different stacks on both meshes" ) { both = true; different = true; }
    SECTION( "A mixed brush and mesh with an enabled mesh stage" ) { mixed = true; donor = true; }
    SECTION( "A mixed brush and mesh with a future stage" ) { mixed = true; donor = true; unknown = true; }
    SECTION( "The brush itself retains a future modifier record" ) { mixed = true; unknown = true; }
    session_t s; auto &ws = s.workspace;
    const u64 a = mixed ? AddBox( ws, { 192, -64, 0 }, { 320, 64, 128 } ) : AddBoxMesh( s );
    const u64 b = AddBoxMesh( s ); ( void )ExportMapSnapshot( ws.pDocument );
    JoinCommandStack( *ws.pDocument, donor ? b : a, enabled, unknown );
    if ( both ) { JoinCommandStack( *ws.pDocument, b, true, false, different ? 512.0 : 256.0 ); }
    const auto redo = JoinCommandPrepareRedo( ws, a, b ); const brush_face_unchanged_t before( ws ); description_t oldB( ws, b );
    std::unique_ptr<description_t> oldA; std::unique_ptr<carve_brush_snapshot_t> brush;
    if ( mixed ) { brush = std::make_unique<carve_brush_snapshot_t>( ws.pDocument, a ); }
    else { oldA = std::make_unique<description_t>( ws, a ); }
    REQUIRE( s.Enabled( "map.mesh.merge" ) ); CHECK( s.Run( "map.mesh.merge" ) == command_result_t::FAILED ); before.Check( ws );
    REQUIRE( ws.selection.ids.nCount == 2u ); CHECK( ws.selection.ids.pData[0] == a ); CHECK( ws.selection.ids.pData[1] == b );
    { description_t after( ws, b ); CHECK( geo::MeshSourceDescription_Equal( &oldB.value, &after.value ) ); }
    if ( mixed ) { brush->Check( ws.pDocument, a ); CHECK( geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { a } ) == nullptr ); }
    else { description_t after( ws, a ); CHECK( geo::MeshSourceDescription_Equal( &oldA->value, &after.value ) ); }
    map_notice_t kind{}; const auto notice = MapNotice_Last( &ws, &kind ); CHECK( kind == map_notice_t::WARNING ); CHECK( notice.contains( QStringLiteral( "modifier" ), Qt::CaseInsensitive ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == redo );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == before.saved );
    Select( ws, { a, b } ); const brush_face_unchanged_t again( ws ); CHECK( s.Run( "map.mesh.merge" ) == command_result_t::FAILED ); again.Check( ws );
}

TEST_CASE( "Registered Join with missing or empty stacks remains one undoable edit and survives reopen", "[map][gui][geometry-commands][join-modifier-preservation]" )
{
    bool empty = false;
    SECTION( "Missing modifier members" ) {}
    SECTION( "Explicitly empty modifier arrays" ) { empty = true; }
    session_t s; auto &ws = s.workspace; const u64 a = AddBoxMesh( s ), b = AddBoxMesh( s );
    ( void )ExportMapSnapshot( ws.pDocument );
    if ( empty ) { JoinCommandEmptyStack( *ws.pDocument, a ); JoinCommandEmptyStack( *ws.pDocument, b ); }
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); Select( ws, { a, b } );
    description_t oldA( ws, a ), oldB( ws, b ); const auto before = ExportMapSnapshot( ws.pDocument ); const usize steps = s.Steps();
    REQUIRE( s.Enabled( "map.mesh.merge" ) ); REQUIRE( s.Run( "map.mesh.merge" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u ); REQUIRE( ws.selection.ids.nCount == 1u ); CHECK( ws.selection.ids.pData[0] == a );
    description_t joined( ws, a ); CHECK( joined.value.vertices.nCount == 16u ); CHECK( joined.value.faces.nCount == 12u );
    for ( const auto *old : { &oldA, &oldB } ) {
        for ( usize i = 0; i < old->value.vertices.nCount; ++i ) {
            const auto &expected = old->value.vertices.pData[i];
            const auto *actual = std::find_if( joined.value.vertices.pData, joined.value.vertices.pData + joined.value.vertices.nCount,
                [&]( const auto &candidate ) { return candidate.sourceId.value == expected.sourceId.value; } );
            REQUIRE( actual != joined.value.vertices.pData + joined.value.vertices.nCount );
            CHECK( actual->position.x == expected.position.x ); CHECK( actual->position.y == expected.position.y ); CHECK( actual->position.z == expected.position.z );
        }
        for ( usize i = 0; i < old->value.faces.nCount; ++i ) { const auto id = old->value.faces.pData[i].sourceId.value;
            CHECK( std::any_of( joined.value.faces.pData, joined.value.faces.pData + joined.value.faces.nCount, [&]( const auto &candidate ) { return candidate.sourceId.value == id; } ) ); }
    }
    const auto committed = ExportMapSnapshot( ws.pDocument ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ExportMapSnapshot( ws.pDocument ) == before ); REQUIRE( ws.selection.ids.nCount == 2u );
    CHECK( ws.selection.ids.pData[0] == a ); CHECK( ws.selection.ids.pData[1] == b );
    { description_t restoredA( ws, a ), restoredB( ws, b ); CHECK( geo::MeshSourceDescription_Equal( &oldA.value, &restoredA.value ) ); CHECK( geo::MeshSourceDescription_Equal( &oldB.value, &restoredB.value ) ); }
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ExportMapSnapshot( ws.pDocument ) == committed );
    QTemporaryDir directory; REQUIRE( directory.isValid() ); const auto path = directory.filePath( QStringLiteral( "joined.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); session_t reopened;
    REQUIRE( MapWorkspace_Open( &reopened.workspace, path ).status == map_files_status_t::OK );
    description_t loaded( reopened.workspace, a ); CHECK( geo::MeshSourceDescription_Equal( &joined.value, &loaded.value ) );
    CHECK( reopened.workspace.pDocument->geometry.meshes.nCount == 1u ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &reopened.workspace.pDocument->geometry.sourceIds ) );
}
