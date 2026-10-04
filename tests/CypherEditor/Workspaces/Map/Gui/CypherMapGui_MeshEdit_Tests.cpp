//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Mesh command publication, source identity, persistence and rollback.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"
#include "CypherEditor/Geometry/Mesh/CypherGeometry_MeshSource.h"
#include "CypherMap_Clipboard.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
struct clipboard_scope_t {
    clipboard_scope_t() { REQUIRE( QGuiApplication::platformName() == QStringLiteral( "offscreen" ) ); QApplication::clipboard()->clear(); }
    ~clipboard_scope_t() { QApplication::clipboard()->clear(); }
};
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    session_t()
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};

struct description_t {
    geo::mesh_source_description_t value{};
    description_t( const map_document_t &document, u64 id )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
        const auto *mesh = geo::GeometryDocument_FindMesh( &document.geometry, { id } );
        REQUIRE( mesh != nullptr );
        REQUIRE( geo::MeshSource_TryDescribe( mesh, &value ) == geo::geometry_status_t::OK );
    }
    ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
};

struct allocation_failure_t { usize calls{}, failOn{}, live{}; };
void *Allocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_failure_t *>( context );
    if ( ++state.calls == state.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++state.live; }
    return memory;
}
void Free( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<allocation_failure_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

map_bounds_t Box( f64 x )
{
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE;
    bounds.box = { { x, -32, 16 }, { x + 64, 32, 80 } };
    return bounds;
}
u64 AddBox( map_workspace_t &workspace, f64 x )
{
    REQUIRE( MapWorkspace_CreateBox( &workspace, Box( x ) ) );
    REQUIRE( workspace.selection.ids.nCount == 1 );
    return workspace.selection.ids.pData[0];
}
std::vector<u64> Selection( const map_workspace_t &workspace )
{
    std::vector<u64> result;
    for ( usize i = 0; i < workspace.selection.ids.nCount; ++i ) { result.push_back( workspace.selection.ids.pData[i] ); }
    return result;
}
bool Enabled( session_t &session, const char *command )
{
    return ( EditorCommands_State( &session.gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_ENABLED ) != 0;
}
command_result_t Execute( session_t &session, const char *command )
{
    return EditorCommands_Execute( &session.gui.commands, StringView_FromCString( command ), {} );
}
void CheckMeshWire( const map_workspace_t &workspace, u64 id, const map_bounds_t &expected )
{
    const auto *object = MapWireframe_FindObject( workspace.wire, id );
    REQUIRE( object != nullptr ); CHECK( object->kind == map_wire_kind_t::MESH );
    CHECK( cypher::math::Vec3d_EqualsExact( object->bounds.box.minimum, expected.box.minimum ) );
    CHECK( cypher::math::Vec3d_EqualsExact( object->bounds.box.maximum, expected.box.maximum ) );
    CHECK( object->nPoints == 8 ); CHECK( object->nLines == 12 );
}
}

TEST_CASE( "Brush conversion publishes one selected mesh edit and preserves root and face identity", "[map][gui][mesh-edit][conversion][persistence]" )
{
    session_t session; auto &ws = session.workspace;
    const auto mode = GENERATE( map_element_mode_t::OBJECTS, map_element_mode_t::MESHES );
    MapWorkspace_SetElementMode( &ws, mode ); REQUIRE( ws.elementMode == mode );
    const u64 first = AddBox( ws, -128 ), second = AddBox( ws, 96 );
    const u64 ids[]{ second, first }; MapWorkspace_SetSelection( &ws, ids, 2 );
    const auto selected = Selection( ws );
    const usize steps = EditorHistory_StepCount( &ws.history );
    const u64 next = ws.pDocument->nextId;
    std::set<u64> oldFaceIds;
    for ( u64 id : selected ) {
        const auto *brush = geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { id } ); REQUIRE( brush != nullptr );
        for ( usize i = 0; i < brush->sides.nCount; ++i ) { oldFaceIds.insert( brush->sides.pData[i].sourceId.value ); }
    }
    REQUIRE( Enabled( session, "map.brush.to_mesh" ) ); CHECK_FALSE( Enabled( session, "map.mesh.flip_normals" ) );
    REQUIRE( Execute( session, "map.brush.to_mesh" ) == command_result_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( ws.pDocument->geometry.meshes.nCount == 2 );
    CHECK( Selection( ws ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    CheckMeshWire( ws, first, Box( -128 ) ); CheckMeshWire( ws, second, Box( 96 ) );
    CHECK_FALSE( Enabled( session, "map.brush.to_mesh" ) ); CHECK( Enabled( session, "map.mesh.flip_normals" ) );
    std::set<u64> meshFaceIds, vertexIds;
    for ( u64 id : selected ) {
        description_t mesh( *ws.pDocument, id ); CHECK( mesh.value.sourceId.value == id );
        for ( usize i = 0; i < mesh.value.faces.nCount; ++i ) { meshFaceIds.insert( mesh.value.faces.pData[i].sourceId.value ); }
        for ( usize i = 0; i < mesh.value.vertices.nCount; ++i ) {
            const u64 vertex = mesh.value.vertices.pData[i].sourceId.value;
            CHECK( vertex >= next ); CHECK( vertexIds.insert( vertex ).second );
        }
    }
    CHECK( meshFaceIds == oldFaceIds );
    description_t beforeFirst( *ws.pDocument, first ), beforeSecond( *ws.pDocument, second );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 2 ); CHECK( ws.pDocument->geometry.meshes.nCount == 0 );
    CHECK( Selection( ws ) == selected ); CHECK( Enabled( session, "map.brush.to_mesh" ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CHECK( Selection( ws ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    description_t afterFirst( *ws.pDocument, first ), afterSecond( *ws.pDocument, second );
    CHECK( geo::MeshSourceDescription_Equal( &beforeFirst.value, &afterFirst.value ) );
    CHECK( geo::MeshSourceDescription_Equal( &beforeSecond.value, &afterSecond.value ) );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "converted.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    CHECK( loaded.geometry.brushes.nCount == 0 ); CHECK( loaded.geometry.meshes.nCount == 2 );
    description_t loadedFirst( loaded, first ), loadedSecond( loaded, second );
    CHECK( geo::MeshSourceDescription_Equal( &beforeFirst.value, &loadedFirst.value ) );
    CHECK( geo::MeshSourceDescription_Equal( &beforeSecond.value, &loadedSecond.value ) );
}

TEST_CASE( "Flip normals reverses authored winding without losing source identity or corner surfacing", "[map][gui][mesh-edit][normals][persistence]" )
{
    session_t session; auto &ws = session.workspace;
    const auto mode = GENERATE( map_element_mode_t::OBJECTS, map_element_mode_t::MESHES );
    MapWorkspace_SetElementMode( &ws, mode ); REQUIRE( ws.elementMode == mode );
    const u64 id = AddBox( ws, -32 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); description_t original( *ws.pDocument, id );
    const auto selected = Selection( ws ); const u64 next = ws.pDocument->nextId;
    const usize steps = EditorHistory_StepCount( &ws.history );
    REQUIRE( Execute( session, "map.mesh.flip_normals" ) == command_result_t::OK );
    description_t flipped( *ws.pDocument, id );
    REQUIRE( flipped.value.vertices.nCount == original.value.vertices.nCount );
    REQUIRE( flipped.value.faces.nCount == original.value.faces.nCount );
    REQUIRE( flipped.value.corners.nCount == original.value.corners.nCount );
    CHECK_FALSE( geo::MeshSourceDescription_Equal( &original.value, &flipped.value ) );
    CHECK( flipped.value.sourceId.value == id ); CHECK( ws.pDocument->nextId == next ); CHECK( Selection( ws ) == selected );
    for ( usize i = 0; i < original.value.vertices.nCount; ++i ) {
        CHECK( flipped.value.vertices.pData[i].sourceId.value == original.value.vertices.pData[i].sourceId.value );
        CHECK( cypher::math::Vec3d_EqualsExact( flipped.value.vertices.pData[i].position, original.value.vertices.pData[i].position ) );
    }
    for ( usize i = 0; i < original.value.faces.nCount; ++i ) {
        const auto &before = original.value.faces.pData[i], &after = flipped.value.faces.pData[i];
        CHECK( after.sourceId.value == before.sourceId.value ); REQUIRE( after.cCorners == before.cCorners );
        CHECK( after.attributes.material.value == before.attributes.material.value );
        CHECK( after.attributes.smoothingGroups == before.attributes.smoothingGroups );
        for ( u32 corner = 0; corner < before.cCorners; ++corner ) {
            const auto &a = flipped.value.corners.pData[after.iFirstCorner + corner];
            const auto &b = original.value.corners.pData[before.iFirstCorner + ( corner == 0 ? 0 : before.cCorners - corner )];
            CHECK( a.iVertex == b.iVertex ); CHECK( a.attributes.uv0.x == b.attributes.uv0.x ); CHECK( a.attributes.uv0.y == b.attributes.uv0.y );
            CHECK( a.attributes.uv1.x == b.attributes.uv1.x ); CHECK( a.attributes.uv1.y == b.attributes.uv1.y );
            CHECK( a.attributes.colorRgba == b.attributes.colorRgba );
        }
    }
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); description_t undone( *ws.pDocument, id );
    CHECK( geo::MeshSourceDescription_Equal( &original.value, &undone.value ) ); CHECK( Selection( ws ) == selected );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); description_t redone( *ws.pDocument, id );
    CHECK( geo::MeshSourceDescription_Equal( &flipped.value, &redone.value ) );
    REQUIRE( MapWorkspace_FlipMeshNormals( &ws ) ); description_t restored( *ws.pDocument, id );
    CHECK( geo::MeshSourceDescription_Equal( &original.value, &restored.value ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 2 ); CHECK( ws.pDocument->nextId == next );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "normals.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    description_t persisted( loaded, id ); CHECK( geo::MeshSourceDescription_Equal( &original.value, &persisted.value ) );
}

TEST_CASE( "Triangulate mesh publishes actual triangle topology with inherited corner data and one history entry", "[map][gui][mesh-edit][triangulate][persistence]" )
{
    session_t session; auto &ws = session.workspace;
    const auto mode = GENERATE( map_element_mode_t::OBJECTS, map_element_mode_t::MESHES );
    MapWorkspace_SetElementMode( &ws, mode ); REQUIRE( ws.elementMode == mode );
    const u64 id = AddBox( ws, -32 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); description_t original( *ws.pDocument, id );
    REQUIRE( original.value.faces.nCount == 6 ); const u64 next = ws.pDocument->nextId;
    const usize steps = EditorHistory_StepCount( &ws.history ); const auto selected = Selection( ws );
    REQUIRE( MapWorkspace_CanTriangulateMeshSelection( &ws ) ); REQUIRE( Enabled( session, "map.mesh.triangulate" ) );
    REQUIRE( Execute( session, "map.mesh.triangulate" ) == command_result_t::OK );
    description_t triangulated( *ws.pDocument, id ); CHECK( triangulated.value.sourceId.value == id );
    REQUIRE( triangulated.value.faces.nCount == 12 ); REQUIRE( triangulated.value.corners.nCount == 36 );
    REQUIRE( triangulated.value.vertices.nCount == original.value.vertices.nCount );
    CHECK( Selection( ws ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    std::set<u64> oldFaces, newFaces;
    for ( usize i = 0; i < original.value.faces.nCount; ++i ) { oldFaces.insert( original.value.faces.pData[i].sourceId.value ); }
    for ( usize i = 0; i < original.value.vertices.nCount; ++i ) {
        CHECK( triangulated.value.vertices.pData[i].sourceId.value == original.value.vertices.pData[i].sourceId.value );
        CHECK( cypher::math::Vec3d_EqualsExact( triangulated.value.vertices.pData[i].position, original.value.vertices.pData[i].position ) );
    }
    usize retained = 0;
    for ( usize i = 0; i < triangulated.value.faces.nCount; ++i ) {
        const auto &triangle = triangulated.value.faces.pData[i]; REQUIRE( triangle.cCorners == 3 );
        CHECK( newFaces.insert( triangle.sourceId.value ).second );
        if ( oldFaces.contains( triangle.sourceId.value ) ) { ++retained; }
        else { CHECK( triangle.sourceId.value >= next ); }
        // A box triangle has exactly one source quad containing all three
        // corners. Check its original surfacing by vertex, not by pool slot.
        const geo::mesh_source_face_t *source = nullptr;
        for ( usize q = 0; q < original.value.faces.nCount; ++q ) {
            const auto &quad = original.value.faces.pData[q]; bool containsAll = true;
            for ( u32 t = 0; t < 3; ++t ) {
                const u32 vertex = triangulated.value.corners.pData[triangle.iFirstCorner + t].iVertex; bool found = false;
                for ( u32 c = 0; c < quad.cCorners; ++c ) { found |= original.value.corners.pData[quad.iFirstCorner + c].iVertex == vertex; }
                containsAll &= found;
            }
            if ( containsAll ) { REQUIRE( source == nullptr ); source = &quad; }
        }
        REQUIRE( source != nullptr ); CHECK( triangle.attributes.material.value == source->attributes.material.value );
        CHECK( triangle.attributes.smoothingGroups == source->attributes.smoothingGroups );
        for ( u32 t = 0; t < 3; ++t ) {
            const auto &corner = triangulated.value.corners.pData[triangle.iFirstCorner + t];
            const geo::mesh_source_corner_t *sourceCorner = nullptr;
            for ( u32 c = 0; c < source->cCorners; ++c ) {
                const auto &candidate = original.value.corners.pData[source->iFirstCorner + c];
                if ( candidate.iVertex == corner.iVertex ) { sourceCorner = &candidate; }
            }
            REQUIRE( sourceCorner != nullptr ); CHECK( corner.attributes.uv0.x == sourceCorner->attributes.uv0.x );
            CHECK( corner.attributes.uv0.y == sourceCorner->attributes.uv0.y ); CHECK( corner.attributes.uv1.x == sourceCorner->attributes.uv1.x );
            CHECK( corner.attributes.uv1.y == sourceCorner->attributes.uv1.y ); CHECK( corner.attributes.colorRgba == sourceCorner->attributes.colorRgba );
        }
    }
    CHECK( retained == oldFaces.size() );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); description_t undone( *ws.pDocument, id );
    CHECK( geo::MeshSourceDescription_Equal( &original.value, &undone.value ) ); CHECK( Selection( ws ) == selected );
    CHECK( MapWorkspace_CanTriangulateMeshSelection( &ws ) ); CHECK( Enabled( session, "map.mesh.triangulate" ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); description_t redone( *ws.pDocument, id );
    CHECK( geo::MeshSourceDescription_Equal( &triangulated.value, &redone.value ) ); CHECK( Selection( ws ) == selected );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo ); const u64 triangulatedNext = ws.pDocument->nextId;
    CHECK_FALSE( MapWorkspace_CanTriangulateMeshSelection( &ws ) ); CHECK_FALSE( Enabled( session, "map.mesh.triangulate" ) );
    CHECK( Execute( session, "map.mesh.triangulate" ) == command_result_t::DISABLED );
    CHECK_FALSE( MapWorkspace_TriangulateMeshSelection( &ws ) ); CHECK( ws.pDocument == document );
    CHECK( ws.pDocument->nextId == triangulatedNext ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    description_t noOp( *ws.pDocument, id ); CHECK( geo::MeshSourceDescription_Equal( &triangulated.value, &noOp.value ) );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "triangulated.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    description_t persisted( loaded, id ); CHECK( geo::MeshSourceDescription_Equal( &triangulated.value, &persisted.value ) );
}

TEST_CASE( "Triangulation remains eligible for selected polygons alongside completed triangle meshes", "[map][gui][mesh-edit][triangulate][commands]" )
{
    session_t session; auto &ws = session.workspace; const u64 triangles = AddBox( ws, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); REQUIRE( MapWorkspace_TriangulateMeshSelection( &ws ) );
    description_t untouched( *ws.pDocument, triangles ); const u64 quads = AddBox( ws, 128 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    const u64 pair[]{ triangles, quads }; MapWorkspace_SetSelection( &ws, pair, 2 ); const auto selected = Selection( ws );
    const usize steps = EditorHistory_StepCount( &ws.history );
    REQUIRE( MapWorkspace_CanTriangulateMeshSelection( &ws ) ); REQUIRE( Enabled( session, "map.mesh.triangulate" ) );
    REQUIRE( Execute( session, "map.mesh.triangulate" ) == command_result_t::OK );
    description_t afterTriangles( *ws.pDocument, triangles ), afterQuads( *ws.pDocument, quads );
    CHECK( geo::MeshSourceDescription_Equal( &untouched.value, &afterTriangles.value ) );
    CHECK( afterQuads.value.faces.nCount == 12 ); CHECK( Selection( ws ) == selected );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 ); CHECK_FALSE( MapWorkspace_CanTriangulateMeshSelection( &ws ) );
    CHECK_FALSE( Enabled( session, "map.mesh.triangulate" ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( MapWorkspace_CanTriangulateMeshSelection( &ws ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK_FALSE( MapWorkspace_CanTriangulateMeshSelection( &ws ) );
}

TEST_CASE( "Mesh command eligibility respects root modes visibility read-only state and private gestures", "[map][gui][mesh-edit][commands][atomic]" )
{
    for ( bool mesh : { false, true } ) {
        CAPTURE( mesh ); session_t session; auto &ws = session.workspace;
        const u64 id = AddBox( ws, 0 ); if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        const char *command = mesh ? "map.mesh.flip_normals" : "map.brush.to_mesh";
        const auto eligible = [&]() { return mesh ? MapWorkspace_CanEditMeshSelection( &ws ) : MapWorkspace_CanConvertBrushSelection( &ws ); };
        const auto attempt = [&]() { return mesh ? MapWorkspace_FlipMeshNormals( &ws ) : MapWorkspace_ConvertBrushSelection( &ws ); };
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
        const auto selected = Selection( ws ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
        const auto reject = [&]() {
            CHECK_FALSE( eligible() ); CHECK_FALSE( Enabled( session, command ) );
            CHECK( Execute( session, command ) == command_result_t::DISABLED ); CHECK_FALSE( attempt() );
            CHECK_FALSE( MapWorkspace_CanTriangulateMeshSelection( &ws ) );
            CHECK_FALSE( Enabled( session, "map.mesh.triangulate" ) );
            CHECK( Execute( session, "map.mesh.triangulate" ) == command_result_t::DISABLED ); CHECK_FALSE( MapWorkspace_TriangulateMeshSelection( &ws ) );
            CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( Selection( ws ) == selected );
            CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        };
        REQUIRE( eligible() ); REQUIRE( Enabled( session, command ) );
        for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES, map_element_mode_t::FACES,
                                 map_element_mode_t::NAVIGATION } ) {
            CAPTURE( static_cast<int>( mode ) );
            if ( MapWorkspace_IsElementModeAvailable( mode ) ) {
                REQUIRE( MapWorkspace_IsElementModeAvailable( mode ) ); MapWorkspace_SetElementMode( &ws, mode );
            } else {
                // Navigation-mesh selection remains unavailable. Inject its
                // state to exercise defensive command gating independently.
                REQUIRE_FALSE( MapWorkspace_IsElementModeAvailable( mode ) );
                ws.elementMode = mode; MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
            }
            REQUIRE( ws.elementMode == mode ); reject();
        }
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
        CHECK( eligible() ); CHECK( Enabled( session, command ) );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS ); CHECK( eligible() ); CHECK( Enabled( session, command ) );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        ws.pDocument->bReadOnly = CY_TRUE; reject(); ws.pDocument->bReadOnly = CY_FALSE;
        REQUIRE( EditorSelection_Apply( &ws.hidden, id, EDITOR_SELECT_ADD ) ); reject(); ( void )EditorSelection_Clear( &ws.hidden );
        const auto group = mesh ? map_visgroup_t::MESHES : map_visgroup_t::BRUSHES;
        MapWorkspace_SetVisgroupHidden( &ws, group, CY_TRUE ); reject(); MapWorkspace_SetVisgroupHidden( &ws, group, CY_FALSE );
        REQUIRE( EditorHistory_Begin( &ws.history, StringView_FromCString( "Unfinished gesture" ) ) == editor_history_status_t::OK );
        reject(); EditorHistory_Cancel( &ws.history );
        map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::TRANSLATE; transform.delta = { 16, 0, 0 };
        MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive ); reject();
        CHECK( ws.editPreview.bActive ); MapWorkspace_ClearEditPreview( &ws );
        MapWorkspace_SetTool( &ws, map_tool_t::BLOCK ); MapWorkspace_SetEditPreview( &ws, Box( 192 ) );
        REQUIRE( MapWorkspace_StageBlockPreview( &ws ) ); reject(); CHECK( MapWorkspace_HasBlockPreview( &ws ) );
        MapWorkspace_ClearEditPreview( &ws ); CHECK( eligible() );
    }
}

TEST_CASE( "Mixed brush and mesh roots disable both mesh commands without partial publication", "[map][gui][mesh-edit][commands][atomic]" )
{
    session_t session; auto &ws = session.workspace; const u64 mesh = AddBox( ws, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); const u64 brush = AddBox( ws, 128 );
    const u64 pair[]{ mesh, brush }; MapWorkspace_SetSelection( &ws, pair, 2 );
    const auto selected = Selection( ws ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    CHECK_FALSE( MapWorkspace_CanConvertBrushSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanEditMeshSelection( &ws ) );
    CHECK_FALSE( Enabled( session, "map.brush.to_mesh" ) ); CHECK_FALSE( Enabled( session, "map.mesh.flip_normals" ) );
    CHECK_FALSE( Enabled( session, "map.mesh.triangulate" ) ); CHECK_FALSE( MapWorkspace_TriangulateMeshSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_ConvertBrushSelection( &ws ) ); CHECK_FALSE( MapWorkspace_FlipMeshNormals( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( Selection( ws ) == selected );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.pDocument->geometry.meshes.nCount == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
}

TEST_CASE( "Meshes clipboard addresses only geometry roots and leaves mixed bundle paste in Objects", "[map][gui][mesh-edit][selection-modes][clipboard][guards]" )
{
    clipboard_scope_t clipboard; session_t session; auto &ws = session.workspace;
    const u64 brush = AddBox( ws, 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
    REQUIRE( ws.elementMode == map_element_mode_t::MESHES );
    CHECK( MapWorkspace_CanEditSelection( &ws ) ); CHECK( MapWorkspace_CanMoveSelection( &ws ) );
    REQUIRE( MapWorkspace_CanCopySelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    REQUIRE( MapWorkspace_CanCutSelection( &ws ) );
    const QByteArray bundle = QApplication::clipboard()->mimeData()->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) );
    REQUIRE_FALSE( bundle.isEmpty() );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const auto selected = Selection( ws ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    CHECK_FALSE( MapWorkspace_CanPaste( &ws ) ); CHECK_FALSE( MapWorkspace_Paste( &ws ) );
    CHECK_FALSE( MapWorkspace_Paste( &ws, true ) );
    ws.pDocument->bReadOnly = CY_TRUE;
    CHECK_FALSE( MapWorkspace_CanEditSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanConvertBrushSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) );
    CHECK( MapWorkspace_CanCopySelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    ws.pDocument->bReadOnly = CY_FALSE;
    CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( Selection( ws ) == selected );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    CHECK( QApplication::clipboard()->mimeData()->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) == bundle );

    REQUIRE( MapWorkspace_CutSelection( &ws ) ); CHECK( MapWireframe_FindObject( ws.wire, brush ) == nullptr );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == selected );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.selection.ids.nCount == 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    REQUIRE( MapWorkspace_CanPaste( &ws ) ); REQUIRE( MapWorkspace_Paste( &ws, true ) );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] != brush );
    const auto *pasted = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[0] );
    REQUIRE( pasted != nullptr ); CHECK( pasted->kind == map_wire_kind_t::BRUSH );
}

TEST_CASE( "Meshes rejects retained foreign missing and mixed root selections before publication", "[map][gui][mesh-edit][selection-modes][guards][atomic]" )
{
    clipboard_scope_t clipboard; session_t session; auto &ws = session.workspace;
    const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
    REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES ); REQUIRE( ws.elementMode == map_element_mode_t::MESHES );
    const u64 brush = 1000; REQUIRE( MapWireframe_FindObject( ws.wire, brush ) != nullptr );
    std::vector<u64> foreignIds;
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
        const auto &object = ws.wire.objects.pData[i];
        if ( object.kind != map_wire_kind_t::BRUSH && object.kind != map_wire_kind_t::MESH ) { foreignIds.push_back( object.id ); }
    }
    // Originless logic records are not wire objects, but are valid general
    // Objects clipboard roots. Meshes must reject these as well as stale IDs.
    foreignIds.push_back( 170u ); foreignIds.push_back( CY_U64_MAX );
    bool foundPatch = false, foundTerrain = false, foundEntity = false;
    for ( const u64 foreign : foreignIds ) {
        CAPTURE( foreign );
        const auto *object = MapWireframe_FindObject( ws.wire, foreign );
        if ( object != nullptr ) {
            foundPatch |= object->kind == map_wire_kind_t::PATCH;
            foundTerrain |= object->kind == map_wire_kind_t::TERRAIN;
            foundEntity |= object->kind == map_wire_kind_t::ENTITY;
        }
        for ( const bool mixed : { false, true } ) {
            CAPTURE( mixed );
            const u64 ids[]{ brush, foreign };
            // Inject obsolete selection state deliberately: mutation guards
            // must still protect callers outside the viewport's category filter.
            REQUIRE( EditorSelection_Set( &ws.selection, mixed ? ids : ids + 1, mixed ? 2u : 1u ) );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
            const auto *points = ws.wire.points.pData; const auto selected = Selection( ws );
            const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
            CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanEditSelection( &ws ) );
            CHECK_FALSE( MapWorkspace_CanCopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) );
            CHECK_FALSE( MapWorkspace_CopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) );
            CHECK_FALSE( MapWorkspace_TranslateSelection( &ws, { 16, 0, 0 } ) );
            CHECK_FALSE( MapWorkspace_RotateSelection( &ws, { 0, 0, 15 }, {} ) );
            CHECK_FALSE( MapWorkspace_ScaleSelection( &ws, { 2, 1, 1 }, {} ) );
            CHECK_FALSE( MapWorkspace_DuplicateSelection( &ws, { 16, 0, 0 } ) ); CHECK_FALSE( MapWorkspace_DeleteSelection( &ws ) );
            for ( const char *command : { "map.brush.to_mesh", "map.mesh.flip_normals", "map.mesh.triangulate", "map.brush.hollow" } ) {
                CAPTURE( command ); CHECK_FALSE( Enabled( session, command ) ); CHECK( Execute( session, command ) == command_result_t::DISABLED );
            }
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
            CHECK( Selection( ws ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            const auto *mime = QApplication::clipboard()->mimeData();
            CHECK( ( mime == nullptr || !mime->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) ) );
        }
    }
    CHECK( foundPatch ); CHECK( foundTerrain ); CHECK( foundEntity );
}

TEST_CASE( "Unwired vertex and edge categories cannot mutate retained parent geometry", "[map][gui][mesh-edit][selection-modes][guards][atomic]" )
{
    clipboard_scope_t clipboard;
    for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES } ) {
        CAPTURE( static_cast<int>( mode ) ); session_t session; auto &ws = session.workspace;
        const u64 root = AddBox( ws, 0 );
        MapWorkspace_SetElementMode( &ws, mode ); REQUIRE( ws.elementMode == mode );
        REQUIRE( ws.selection.ids.nCount == 1 ); REQUIRE( ws.selection.ids.pData[0] == root );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const auto *points = ws.wire.points.pData; const auto selected = Selection( ws );
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
        CHECK_FALSE( MapWorkspace_CanEditSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) );
        CHECK_FALSE( MapWorkspace_CanEditBrushSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanConvertBrushSelection( &ws ) );
        CHECK_FALSE( MapWorkspace_CanCopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) );
        CHECK_FALSE( MapWorkspace_TranslateSelection( &ws, { 16, 0, 0 } ) );
        CHECK_FALSE( MapWorkspace_TranslateSelection( &ws, { 16, 0, 0 }, true ) );
        CHECK_FALSE( MapWorkspace_RotateSelection( &ws, { 0, 0, 15 }, {} ) );
        CHECK_FALSE( MapWorkspace_ScaleSelection( &ws, { 2, 1, 1 }, {} ) );
        CHECK_FALSE( MapWorkspace_ResizeSelection( &ws, { 1, 0, 0 }, { 16, 0, 0 }, false ) );
        CHECK_FALSE( MapWorkspace_DuplicateSelection( &ws, { 16, 0, 0 } ) ); CHECK_FALSE( MapWorkspace_DeleteSelection( &ws ) );
        CHECK_FALSE( MapWorkspace_ConvertBrushSelection( &ws ) ); CHECK_FALSE( MapWorkspace_HollowSelection( &ws, 8 ) );
        CHECK_FALSE( MapWorkspace_CopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) );
        for ( const char *command : { "map.brush.to_mesh", "map.mesh.flip_normals", "map.mesh.triangulate", "map.brush.hollow", "map.brush.merge" } ) {
            CAPTURE( command ); CHECK_FALSE( Enabled( session, command ) ); CHECK( Execute( session, command ) == command_result_t::DISABLED );
        }
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( Selection( ws ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Select properties expose mesh operations with current selection mode shortcut help", "[map][gui][mesh-edit][toolpanels][help]" )
{
    session_t session; auto &ws = session.workspace; AddBox( ws, 0 );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QString::fromUtf8( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mesh_help" bindings = {
    "map.selection.objects" = { "map.mesh.flip_normals" = [ "F" ] "map.mesh.triangulate" = [ "T" ] }
    "map.selection.groups" = { "map.mesh.flip_normals" = [ "F" ] "map.mesh.triangulate" = [ "Shift+T" ] }
    "map.selection.meshes" = { "map.mesh.flip_normals" = [ "Shift+F" ] "map.mesh.triangulate" = [ "Alt+T" ] }
} }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "mesh_help" ) ) == gui::editor_gui_status_t::OK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    const auto button = [&]( const char *id ) {
        auto *result = panel->findChild<QToolButton *>( QString::fromUtf8( id ) ); REQUIRE( result != nullptr ); return result;
    };
    const auto flipRows = [&]() {
        QStringList rows;
        for ( const auto &row : MapToolProperties_KeyRows( panel.get() ) ) {
            if ( row.contains( QStringLiteral( "Mesh objects: reverse all faces" ) ) ) { rows.append( row ); }
        }
        return rows;
    };
    const auto triangleRows = [&]() {
        QStringList rows;
        for ( const auto &row : MapToolProperties_KeyRows( panel.get() ) ) {
            if ( row.contains( QStringLiteral( "Mesh objects: triangulate all polygons" ) ) ) { rows.append( row ); }
        }
        return rows;
    };
    const QPointer<QToolButton> conversionButton = button( "map.brush.to_mesh" );
    const QPointer<QToolButton> flipButton = button( "map.mesh.flip_normals" );
    const QPointer<QToolButton> triangulationButton = button( "map.mesh.triangulate" );
    const auto retainedButtons = [&]() {
        // A selection-mode or keymap change can originate inside a control's
        // own signal handler. Refreshing state must not destroy that control.
        REQUIRE_FALSE( conversionButton.isNull() ); REQUIRE_FALSE( flipButton.isNull() ); REQUIRE_FALSE( triangulationButton.isNull() );
        CHECK( button( "map.brush.to_mesh" ) == conversionButton.data() );
        CHECK( button( "map.mesh.flip_normals" ) == flipButton.data() );
        CHECK( button( "map.mesh.triangulate" ) == triangulationButton.data() );
        CHECK_FALSE( conversionButton->isChecked() ); CHECK_FALSE( flipButton->isChecked() ); CHECK_FALSE( triangulationButton->isChecked() );
    };
    REQUIRE( button( "map.brush.to_mesh" )->isEnabled() ); CHECK_FALSE( button( "map.mesh.flip_normals" )->isEnabled() );
    CHECK_FALSE( button( "map.mesh.triangulate" )->isEnabled() );
    CHECK( flipRows() == QStringList{ QStringLiteral( "[F] Mesh objects: reverse all faces (Objects/Groups/Meshes)" ) } );
    CHECK( triangleRows() == QStringList{ QStringLiteral( "[T] Mesh objects: triangulate all polygons (Objects/Groups/Meshes)" ) } );
    button( "map.brush.to_mesh" )->click();
    retainedButtons();
    REQUIRE( ws.pDocument->geometry.meshes.nCount == 1 ); CHECK_FALSE( button( "map.brush.to_mesh" )->isEnabled() );
    CHECK( button( "map.mesh.flip_normals" )->isEnabled() );
    CHECK( button( "map.mesh.triangulate" )->isEnabled() );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    retainedButtons();
    CHECK_FALSE( button( "map.brush.to_mesh" )->isEnabled() ); CHECK_FALSE( button( "map.mesh.flip_normals" )->isEnabled() );
    CHECK_FALSE( button( "map.mesh.triangulate" )->isEnabled() );
    CHECK( flipRows().isEmpty() );
    CHECK( triangleRows().isEmpty() );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS );
    retainedButtons();
    CHECK( button( "map.mesh.flip_normals" )->isEnabled() );
    CHECK( button( "map.mesh.triangulate" )->isEnabled() );
    CHECK( flipRows() == QStringList{ QStringLiteral( "[F] Mesh objects: reverse all faces (Objects/Groups/Meshes)" ) } );
    CHECK( triangleRows() == QStringList{ QStringLiteral( "[Shift+T] Mesh objects: triangulate all polygons (Objects/Groups/Meshes)" ) } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
    retainedButtons();
    CHECK( button( "map.mesh.flip_normals" )->isEnabled() ); CHECK( button( "map.mesh.triangulate" )->isEnabled() );
    CHECK( flipRows() == QStringList{ QStringLiteral( "[Shift+F] Mesh objects: reverse all faces (Objects/Groups/Meshes)" ) } );
    CHECK( triangleRows() == QStringList{ QStringLiteral( "[Alt+T] Mesh objects: triangulate all polygons (Objects/Groups/Meshes)" ) } );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS );
    const usize steps = EditorHistory_StepCount( &ws.history ); button( "map.mesh.flip_normals" )->click();
    retainedButtons();
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); CHECK( flipRows().size() == 1 );
    retainedButtons();
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QString::fromUtf8( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mesh_help_remap" base = "mesh_help" bindings = {
    "map.selection.objects" = { "map.mesh.triangulate" = [ "J" ] }
} }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "mesh_help_remap" ) ) == gui::editor_gui_status_t::OK );
    retainedButtons();
    CHECK( triangleRows() == QStringList{ QStringLiteral( "[J] Mesh objects: triangulate all polygons (Objects/Groups/Meshes)" ) } );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    retainedButtons();
    CHECK( button( "map.brush.to_mesh" )->isEnabled() ); CHECK_FALSE( button( "map.mesh.flip_normals" )->isEnabled() );
}

TEST_CASE( "Mesh edit publication allocation failures leave selection wireframe history and document retryable", "[map][gui][mesh-edit][allocation][atomic]" )
{
    struct action_t { const char *name; bool needsMesh; bool ( *apply )( map_workspace_t * ) noexcept; };
    const action_t actions[]{
        { "Convert", false, MapWorkspace_ConvertBrushSelection },
        { "Flip normals", true, MapWorkspace_FlipMeshNormals },
        { "Triangulate", true, MapWorkspace_TriangulateMeshSelection }
    };
    for ( const auto &action : actions ) {
        CAPTURE( action.name ); allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
        const auto prepare = [&]( session_t &session ) {
            AddBox( session.workspace, 0 ); if ( action.needsMesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &session.workspace ) ); }
        };
        const auto edit = [&]( map_workspace_t &ws ) { return action.apply( &ws ); };
        usize successfulCalls{};
        {
            session_t session; prepare( session ); const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool changed = edit( session.workspace ); session.gui.pAllocator = original;
            REQUIRE( changed ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
        }
        REQUIRE( audit.live == 0 );
        for ( usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
            CAPTURE( failOn ); session_t session; prepare( session ); auto &ws = session.workspace;
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
            const auto *points = ws.wire.points.pData; const auto *selection = ws.selection.ids.pData;
            const auto selected = Selection( ws ); const auto selectionRevision = ws.selection.revision;
            const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
            audit.calls = 0; audit.failOn = failOn; const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool changed = edit( ws ); session.gui.pAllocator = original;
            CHECK_FALSE( changed ); CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
            CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision ); CHECK( Selection( ws ) == selected );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            audit.calls = 0; audit.failOn = 0; REQUIRE( edit( ws ) ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == selected );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == selected );
        }
        CHECK( audit.live == 0 );
    }
}
