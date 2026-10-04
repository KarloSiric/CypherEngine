//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Shared staged Block construction, refinement, failure, and publication.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Views.h"
#include "CypherEditor_Keymap.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QMenu>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    session_t()
    {
        REQUIRE( gui::EditorGui_Init( &gui, qobject_cast<QApplication *>( QCoreApplication::instance() ), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        MapWorkspace_SetTool( &workspace, map_tool_t::BLOCK );
        MapWorkspace_SetGridSize( &workspace, 16 );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};
struct settings_t {
    settings_registry_t *registry;
    settings_document_t store{};
    explicit settings_t( settings_registry_t *settings ) : registry( settings )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Text( const char *path, const char *text )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
    void Real( const char *path, f64 number )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = number;
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
};
map_bounds_t Box( f64 maxX = 64, f64 maxY = 64, f64 maxZ = 64 )
{
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE; bounds.box = { { -64, -64, 0 }, { maxX, maxY, maxZ } }; return bounds;
}

void CheckPoint( cypher::math::vec3d_t actual, cypher::math::vec3d_t expected )
{
    CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-6 ) );
    CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-6 ) );
    CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-6 ) );
}
void Stage( map_workspace_t &ws, const map_bounds_t &bounds = Box(), u32 depthAxis = 2u )
{
    MapWorkspace_SetEditPreview( &ws, bounds );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws, depthAxis ) );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
}
void Show( QWidget *view )
{
    view->resize( 800, 600 ); view->show(); QCoreApplication::processEvents();
}
void Mouse( QWidget *view, QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const auto held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event( type, point, view->mapToGlobal( point ), button, held, modifiers );
    QCoreApplication::sendEvent( view, &event );
}
void Key( QWidget *view, int key )
{
    QKeyEvent event( QEvent::KeyPress, key, Qt::NoModifier ); QCoreApplication::sendEvent( view, &event );
}
void Wheel( QWidget *view, int angle, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier )
{
    const QPointF point( view->rect().center() );
    QWheelEvent event( point, view->mapToGlobal( point ), {}, QPoint( 0, angle ), Qt::NoButton, modifiers, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( view, &event );
}
void CheckMaterial( const map_document_t &document, u64 id, const char *material, f64 uvUnits )
{
    namespace geo = cypher::editor::geometry;
    const auto *brush = geo::GeometryDocument_FindBrush( &document.geometry, { id } );
    const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &document.geometry, { id } );
    REQUIRE( brush != nullptr ); REQUIRE( attributes != nullptr );
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        const auto &surface = attributes->records.pData[brush->sides.pData[i].iAttributeIndex];
        CHECK( StringView_Equals( MapMaterials_Path( &document.materials, surface.material.value ), StringView_FromCString( material ) ) );
        CHECK( surface.uvProjection.worldUnitsPerUv.x == Catch::Approx( uvUnits ) );
        CHECK( surface.uvProjection.worldUnitsPerUv.y == Catch::Approx( uvUnits ) );
    }
}
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
}

TEST_CASE( "Released Block construction stays private across panes until one confirmation", "[map][gui][block][staged-block][persistence]" )
{
    session_t session;
    auto &ws = session.workspace;
    auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    std::unique_ptr<QWidget> front( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
    Show( top.get() ); Show( front.get() );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -64, -64 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 64, 64 } );
    Mouse( top.get(), QEvent::MouseButtonPress, start );
    Mouse( top.get(), QEvent::MouseMove, end );
    Mouse( top.get(), QEvent::MouseButtonRelease, end );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( ws.editPreview.blockDepthAxis == 2 );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( ws.selection.ids.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    CheckPoint( ws.editPreview.bounds.box.minimum, { -64, -64, 0 } );
    CheckPoint( ws.editPreview.bounds.box.maximum, { 64, 64, 64 } );
    // A different projection can change height while preserving the footprint.
    const QPointF handle = MapOrthoView_WorldToView( front.get(), { 0, 64 } );
    const QPointF resized = MapOrthoView_WorldToView( front.get(), { 0, 103 } );
    Mouse( front.get(), QEvent::MouseButtonPress, handle );
    Mouse( front.get(), QEvent::MouseMove, resized );
    Mouse( front.get(), QEvent::MouseButtonRelease, resized );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CheckPoint( ws.editPreview.bounds.box.minimum, { -64, -64, 0 } );
    CheckPoint( ws.editPreview.bounds.box.maximum, { 64, 64, 96 } );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    Key( top.get(), Qt::Key_Return );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 1 );
    REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 id = ws.selection.ids.pData[0];
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    Key( front.get(), Qt::Key_Return );
    Mouse( top.get(), QEvent::MouseButtonRelease, end );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( ws.selection.ids.nCount == 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
    CheckPoint( object->bounds.box.minimum, { -64, -64, 0 } ); CheckPoint( object->bounds.box.maximum, { 64, 64, 96 } );
    QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "staged.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK );
    const auto *saved = MapWireframe_FindObject( wire, id ); REQUIRE( saved != nullptr );
    CheckPoint( saved->bounds.box.maximum, { 64, 64, 96 } );
}

TEST_CASE( "Staged Block RGB camera bounds handles use an exact axis plane and bypass grid snapping", "[map][gui][block][staged-block][resize]" )
{
    session_t session;
    auto &ws = session.workspace;
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    Show( camera.get() ); MapWorkspace_Frame( &ws, CY_FALSE );
    Stage( ws );
    // Empty-map framing intentionally shows a 1024-unit cube; this small
    // primitive's handle falls within the 14-pixel suppression radius there.
    // Frame the private stage before testing a visible, eligible RGB control.
    MapWorkspace_Frame( &ws, CY_TRUE, map_frame_target_t::PERSPECTIVE );
    QPointF handle, moved, center;
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 64, 0, 32 }, &handle ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 101, 0, 32 }, &moved ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 32 }, &center ) );
    REQUIRE( QRectF( camera->rect() ).adjusted( 5, 5, -5, -5 ).contains( handle ) );
    REQUIRE( QLineF( handle, center ).length() >= 14 );
    REQUIRE( QLineF( handle, moved ).length() >= 3 );
    Mouse( camera.get(), QEvent::MouseButtonPress, handle );
    Mouse( camera.get(), QEvent::MouseMove, moved, Qt::ControlModifier );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CheckPoint( ws.editPreview.bounds.box.minimum, { -64, -64, 0 } );
    CheckPoint( ws.editPreview.bounds.box.maximum, { 101, 64, 64 } );
    Mouse( camera.get(), QEvent::MouseButtonRelease, moved, Qt::ControlModifier );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) );
    CheckPoint( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 101, 64, 64 } );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Quad footprints create one authored mesh in every 2D plane and the 3D workplane", "[map][gui][block][staged-block][quad][gesture][persistence]" )
{
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        session_t session; auto &ws = session.workspace; settings_t settings( &session.gui.settings );
        settings.Text( "editor.map.new_brush_shape", "quad" );
        // The numeric normal default must not override the hovered pane's
        // captured footprint plane. Camera construction currently uses XY.
        settings.Text( "editor.map.primitive_axis", "X" );
        settings.Text( "editor.map.default_material", "materials/dev/quad_stage.cymat" );
        const bool perspective = projection == 3;
        const u32 normal = perspective ? 2u : static_cast<u32>( projection );
        const auto axes = normal == 0u ? map_ortho_axes_t::FRONT : normal == 1u ? map_ortho_axes_t::SIDE : map_ortho_axes_t::TOP;
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        Show( view.get() ); MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
        QPointF start, end;
        if ( perspective ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), { -64, -32, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 96, 48, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -64, -32 } ); end = MapOrthoView_WorldToView( view.get(), { 96, 48 } );
        }
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto nextId = document->nextId;
        Mouse( view.get(), QEvent::MouseButtonPress, start ); Mouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        CHECK( ws.editPreview.primitive.kind == map_primitive_kind_t::QUAD ); CHECK( ws.editPreview.primitive.axis == normal );
        map_bounds_t expected{}; expected.bHas = CY_TRUE;
        expected.box = normal == 0u ? cypher::math::aabbd_t{ { 0, -64, -32 }, { 0, 96, 48 } } :
            normal == 1u ? cypher::math::aabbd_t{ { -64, 0, -32 }, { 96, 0, 48 } } :
                           cypher::math::aabbd_t{ { -64, -32, 0 }, { 96, 48, 0 } };
        CheckPoint( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPoint( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        REQUIRE( ws.editPreviewWire.objects.nCount == 1 ); CHECK( ws.editPreviewWire.objects.pData[0].kind == map_wire_kind_t::MESH );
        CHECK( ws.editPreviewWire.points.nCount == 4 ); CHECK( ws.editPreviewWire.faces.nCount == 1 );
        Wheel( view.get(), 240 ); CheckPoint( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        Mouse( view.get(), QEvent::MouseButtonRelease, end ); REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
        CHECK( ws.editPreview.blockDepthAxis == normal );
        Wheel( view.get(), -120 ); CheckPoint( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPoint( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
        CHECK( ws.pDocument->geometry.meshes.nCount == 0 ); CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
        Key( view.get(), Qt::Key_Return ); REQUIRE( ws.pDocument->geometry.meshes.nCount == 1 ); CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
        REQUIRE( EditorSelection_Count( &ws.selection ) == 1 ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto *quad = MapWireframe_FindObject( ws.wire, id ); REQUIRE( quad != nullptr ); CHECK( quad->kind == map_wire_kind_t::MESH );
        CheckPoint( quad->bounds.box.minimum, expected.box.minimum ); CheckPoint( quad->bounds.box.maximum, expected.box.maximum );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 ); CHECK_FALSE( ws.editPreview.bActive );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.meshes.nCount == 0 );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWireframe_FindObject( ws.wire, id ) != nullptr );
        QTemporaryDir directory; REQUIRE( directory.isValid() );
        const auto path = directory.filePath( QStringLiteral( "quad.cymap" ) ); REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
        map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
        const auto *authored = cypher::editor::geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { id } );
        const auto *restored = cypher::editor::geometry::GeometryDocument_FindMesh( &loaded.geometry, { id } ); REQUIRE( authored != nullptr ); REQUIRE( restored != nullptr );
        namespace geo = cypher::editor::geometry;
        geo::mesh_source_description_t before{}, after{};
        REQUIRE( geo::MeshSourceDescription_Init( &before, ws.pDocument->pAllocator, { id } ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSourceDescription_Init( &after, loaded.pAllocator, { id } ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSource_TryDescribe( authored, &before ) == geo::geometry_status_t::OK ); REQUIRE( geo::MeshSource_TryDescribe( restored, &after ) == geo::geometry_status_t::OK );
        CHECK( geo::MeshSourceDescription_Equal( &before, &after ) );
        REQUIRE( after.faces.nCount == 1 ); CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, after.faces.pData[0].attributes.material.value ), StringView_FromCString( "materials/dev/quad_stage.cymat" ) ) );
        geo::MeshSourceDescription_Shutdown( &after ); geo::MeshSourceDescription_Shutdown( &before );
    }
}

TEST_CASE( "Numeric Quad creation uses its normal setting while CreateBox remains a solid command", "[map][gui][block][quad][primitive]" )
{
    session_t session; auto &ws = session.workspace; settings_t settings( &session.gui.settings );
    settings.Text( "editor.map.new_brush_shape", "quad" ); settings.Text( "editor.map.primitive_axis", "Y" );
    const auto envelope = Box();
    const auto defaults = MapWorkspace_PrimitiveDefaults( &ws, envelope ); CHECK( defaults.kind == map_primitive_kind_t::QUAD ); CHECK( defaults.axis == 1u );
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, envelope ) ); REQUIRE( ws.pDocument->geometry.meshes.nCount == 1 );
    const auto *quad = MapWireframe_FindObject( ws.wire, EditorSelection_At( &ws.selection, 0 ) ); REQUIRE( quad != nullptr );
    CheckPoint( quad->bounds.box.minimum, { -64, -64, 0 } ); CheckPoint( quad->bounds.box.maximum, { 64, -64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, envelope ) ); CHECK( ws.pDocument->geometry.meshes.nCount == 1 ); CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    const auto *solid = MapWireframe_FindObject( ws.wire, EditorSelection_At( &ws.selection, 0 ) ); REQUIRE( solid != nullptr );
    CheckPoint( solid->bounds.box.minimum, envelope.box.minimum ); CheckPoint( solid->bounds.box.maximum, envelope.box.maximum );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( ws.pDocument->geometry.meshes.nCount == 1 );
}

TEST_CASE( "Staged Quad keeps its captured plane and cache through invalid thickness allocation failure and retry", "[map][gui][block][staged-block][quad][failure]" )
{
    allocation_failure_t failure{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &failure };
    session_t session; auto &ws = session.workspace; settings_t settings( &session.gui.settings );
    settings.Text( "editor.map.new_brush_shape", "quad" );
    MapWorkspace_SetEditPreview( &ws, Box(), 1u ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws, 2u ) ); CHECK( ws.editPreview.blockDepthAxis == 1u );
    const auto original = ws.editPreview.bounds; const auto primitive = ws.editPreview.primitive;
    const auto *points = ws.editPreviewWire.points.pData; const auto count = ws.editPreviewWire.points.nCount;
    auto *document = ws.pDocument; const auto revision = document->geometry.revision, nextId = document->nextId;
    auto thick = original; thick.box.maximum.y += 16;
    CHECK_FALSE( MapWorkspace_SetBlockPreviewBounds( &ws, thick ) ); CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT );
    CHECK( ws.editPreviewWire.points.pData == points ); CheckPoint( ws.editPreview.bounds.box.minimum, original.box.minimum ); CheckPoint( ws.editPreview.bounds.box.maximum, original.box.maximum );
    CHECK_FALSE( MapWorkspace_CommitBlockPreview( &ws ) ); REQUIRE( MapWorkspace_SetBlockPreviewBounds( &ws, original ) );
    auto resized = original; resized.box.maximum.x += 32;
    const auto *savedAllocator = document->pAllocator; document->pAllocator = &allocator; failure.failOn = 1;
    const bool rebuilt = MapWorkspace_SetBlockPreviewBounds( &ws, resized ); document->pAllocator = savedAllocator;
    CHECK_FALSE( rebuilt ); CHECK( failure.calls >= 1 ); CHECK( failure.live == 0 ); CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY );
    CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.points.nCount == count );
    REQUIRE( MapWorkspace_SetBlockPreviewBounds( &ws, resized ) );
    settings.Text( "editor.map.new_brush_shape", "box" ); settings.Text( "editor.map.primitive_axis", "Z" );
    CHECK( ws.editPreview.primitive.kind == map_primitive_kind_t::QUAD ); CHECK( ws.editPreview.primitive.axis == primitive.axis );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( document->nextId == nextId );
    CHECK( document->geometry.meshes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    failure.calls = 0; failure.failOn = 1; document->pAllocator = &allocator;
    const bool committed = MapWorkspace_CommitBlockPreview( &ws ); document->pAllocator = savedAllocator;
    CHECK_FALSE( committed ); CHECK( failure.live == 0 ); CHECK( ws.editPreview.bBlockCommitFailed ); REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) ); REQUIRE( ws.pDocument->geometry.meshes.nCount == 1 );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CheckPoint( ws.wire.objects.pData[0].bounds.box.minimum, resized.box.minimum ); CheckPoint( ws.wire.objects.pData[0].bounds.box.maximum, resized.box.maximum );
}

TEST_CASE( "Staged Block bounds preserve captured shape UVs and material through failed creation and retry", "[map][gui][block][staged-block][failure][primitive]" )
{
    session_t session;
    auto &ws = session.workspace;
    settings_t settings( &session.gui.settings );
    settings.Text( "editor.map.new_brush_shape", "wedge" );
    settings.Text( "editor.map.primitive_axis", "Y" );
    settings.Real( "editor.map.default_texture_scale", 0.5 );
    settings.Text( "editor.map.default_material", "materials/dev/staged_material.cymat" );
    Stage( ws );
    const auto primitive = ws.editPreview.primitive;
    REQUIRE( primitive.kind == map_primitive_kind_t::WEDGE ); CHECK( primitive.axis == 1 );
    auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    REQUIRE( EditorHistory_Begin( &ws.history, StringView_FromCString( "Unrelated transaction" ) ) == editor_history_status_t::OK );
    CHECK_FALSE( MapWorkspace_CommitBlockPreview( &ws ) );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( ws.editPreview.bBlockCommitFailed ); CHECK( ws.editPreview.status == map_status_t::OK );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( ws.selection.ids.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    settings.Text( "editor.map.new_brush_shape", "sphere" );
    settings.Text( "editor.map.primitive_axis", "Z" );
    settings.Real( "editor.map.default_texture_scale", 0.125 );
    settings.Text( "editor.map.default_material", "materials/dev/later_material.cymat" );
    REQUIRE( MapWorkspace_SetBlockPreviewBounds( &ws, Box( 96, 80, 112 ) ) );
    CHECK( ws.editPreview.primitive.kind == primitive.kind ); CHECK( ws.editPreview.primitive.axis == primitive.axis );
    CHECK( ws.editPreview.primitive.wedgeCutAxis == primitive.wedgeCutAxis );
    CHECK( ws.editPreview.primitive.wedgeSlopeAxis == primitive.wedgeSlopeAxis );
    CHECK( ws.editPreview.primitive.worldUnitsPerUv == 256 );
    CHECK( StringView_Equals( StringView_FromCString( ws.editPreview.blockMaterial ), StringView_FromCString( "materials/dev/staged_material.cymat" ) ) );
    EditorHistory_Cancel( &ws.history );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 1 ); REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 id = ws.selection.ids.pData[0];
    CHECK( ws.pDocument->geometry.brushes.pData[0]->sides.nCount == 5 );
    CheckMaterial( *ws.pDocument, id, "materials/dev/staged_material.cymat", 256 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CheckMaterial( *ws.pDocument, id, "materials/dev/staged_material.cymat", 256 );
}

TEST_CASE( "Failed staged Block rebuild and publication keep last good geometry without history or leaks", "[map][gui][block][staged-block][failure]" )
{
    allocation_failure_t failure{};
    const allocator_t allocator{ &Allocate, nullptr, &Free, &failure };
    session_t session;
    auto &ws = session.workspace;
    Stage( ws );
    const auto original = ws.editPreview.bounds;
    const auto points = ws.editPreviewWire.points.pData;
    const auto count = ws.editPreviewWire.points.nCount;
    auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    const auto nextId = document->nextId;
    SECTION( "Invalid numeric bounds never replace the valid preview" ) {
        for ( f64 value : { std::numeric_limits<f64>::quiet_NaN(), std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::max(), -65.0 } ) {
            CAPTURE( value );
            auto invalid = original; invalid.box.maximum.x = value;
            CHECK_FALSE( MapWorkspace_SetBlockPreviewBounds( &ws, invalid ) );
            CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.editPreview.status != map_status_t::OK );
            CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.points.nCount == count );
            CheckPoint( ws.editPreview.bounds.box.maximum, original.box.maximum );
            CHECK_FALSE( MapWorkspace_CommitBlockPreview( &ws ) );
            REQUIRE( MapWorkspace_SetBlockPreviewBounds( &ws, original ) );
            CHECK( ws.editPreview.status == map_status_t::OK );
        }
    }
    SECTION( "Failed private construction allocation retains the old cache and can retry" ) {
        const auto *savedAllocator = document->pAllocator;
        document->pAllocator = &allocator; failure.failOn = 1;
        const bool rebuilt = MapWorkspace_SetBlockPreviewBounds( &ws, Box( 96 ) );
        document->pAllocator = savedAllocator;
        CHECK_FALSE( rebuilt ); CHECK( failure.calls >= 1 ); CHECK( failure.live == 0 );
        CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY ); CHECK( MapWorkspace_HasBlockPreview( &ws ) );
        CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.points.nCount == count );
        CheckPoint( ws.editPreview.bounds.box.maximum, original.box.maximum );
        REQUIRE( MapWorkspace_SetBlockPreviewBounds( &ws, original ) );
    }
    SECTION( "Failed live document clone keeps the same confirmed descriptor for another Confirm" ) {
        const auto *savedAllocator = document->pAllocator;
        document->pAllocator = &allocator; failure.failOn = 1;
        const bool committed = MapWorkspace_CommitBlockPreview( &ws );
        document->pAllocator = savedAllocator;
        CHECK_FALSE( committed ); CHECK( failure.calls >= 1 ); CHECK( failure.live == 0 );
        CHECK( ws.editPreview.bBlockCommitFailed ); CHECK( ws.editPreview.status == map_status_t::OK );
        CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.points.nCount == count );
    }
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( document->nextId == nextId );
    CHECK( ws.selection.ids.nCount == 0 ); CHECK( document->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK_FALSE( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( failure.live == 0 );
}

TEST_CASE( "Staged Block depth remains attached to its original projection and Cancel follows the idle hierarchy", "[map][gui][block][staged-block][depth][cancel]" )
{
    for ( u32 depthAxis = 0; depthAxis < 3; ++depthAxis ) {
        CAPTURE( depthAxis );
        session_t session;
        auto &ws = session.workspace;
        const auto axes = depthAxis == 0 ? map_ortho_axes_t::FRONT : depthAxis == 1 ? map_ortho_axes_t::SIDE : map_ortho_axes_t::TOP;
        std::unique_ptr<QWidget> original( MapOrthoView_Create( nullptr, &ws, axes ) );
        std::unique_ptr<QWidget> other( MapOrthoView_Create( nullptr, &ws, depthAxis == 2 ? map_ortho_axes_t::FRONT : map_ortho_axes_t::TOP ) );
        Show( original.get() ); Show( other.get() );
        const QPointF start = MapOrthoView_WorldToView( original.get(), { -64, -64 } );
        const QPointF end = MapOrthoView_WorldToView( original.get(), { 64, 64 } );
        Mouse( original.get(), QEvent::MouseButtonPress, start );
        Mouse( original.get(), QEvent::MouseMove, end );
        Mouse( original.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.editPreview.blockDepthAxis == depthAxis );
        const auto before = ws.editPreview.bounds;
        const f64 zoom = MapOrthoView_Zoom( other.get() );
        Wheel( other.get(), 240 ); Wheel( other.get(), 120, Qt::ControlModifier | Qt::ShiftModifier );
        auto expected = before.box.maximum;
        if ( depthAxis == 0 ) { expected.x += 33; }
        else if ( depthAxis == 1 ) { expected.y += 33; }
        else { expected.z += 33; }
        CheckPoint( ws.editPreview.bounds.box.minimum, before.box.minimum ); CheckPoint( ws.editPreview.bounds.box.maximum, expected );
        CHECK( MapOrthoView_Zoom( other.get() ) == zoom );
        CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
        // Ordinary camera/2D navigation and idle focus transfer retain the stage.
        Wheel( other.get(), 120, Qt::NoModifier );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
        QFocusEvent focus( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( original.get(), &focus );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
        Key( other.get(), Qt::Key_Escape );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == map_tool_t::BLOCK );
        CHECK( ws.editPreviewWire.points.nCount == 0 );
        Key( other.get(), Qt::Key_Escape ); CHECK( ws.tool == map_tool_t::NONE );
        CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
        CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    }
}

TEST_CASE( "A staged Block corner resize preserves its fixed opposite sides and clamps crossing", "[map][gui][block][staged-block][resize]" )
{
    session_t session;
    auto &ws = session.workspace;
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    Show( top.get() ); Stage( ws );
    const auto corner = MapOrthoView_WorldToView( top.get(), { 64, 64 } );
    const auto moved = MapOrthoView_WorldToView( top.get(), { 99, 119 } );
    Mouse( top.get(), QEvent::MouseButtonPress, corner );
    Mouse( top.get(), QEvent::MouseMove, moved );
    Mouse( top.get(), QEvent::MouseButtonRelease, moved );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CheckPoint( ws.editPreview.bounds.box.minimum, { -64, -64, 0 } );
    CheckPoint( ws.editPreview.bounds.box.maximum, { 96, 112, 64 } );
    const auto side = MapOrthoView_WorldToView( top.get(), { -64, 24 } );
    const auto across = MapOrthoView_WorldToView( top.get(), { 256, 24 } );
    Mouse( top.get(), QEvent::MouseButtonPress, side );
    Mouse( top.get(), QEvent::MouseMove, across );
    Mouse( top.get(), QEvent::MouseButtonRelease, across );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CheckPoint( ws.editPreview.bounds.box.minimum, { 95, -64, 0 } );
    CheckPoint( ws.editPreview.bounds.box.maximum, { 96, 112, 64 } );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) );
    CheckPoint( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { 95, -64, 0 } );
}

TEST_CASE( "Staged Block confirmation and cancellation resolve effective bindings in either pane family", "[map][gui][block][staged-block][input]" )
{
    for ( bool perspective : { false, true } ) {
        for ( bool confirm : { false, true } ) {
            CAPTURE( perspective, confirm );
            session_t session;
            auto &ws = session.workspace;
            settings_document_t keys{};
            REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
            REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "staged_block" bindings = { "map.tool.block" = {
    "map.tool.confirm" = [ "V" ]
    "map.tool.cancel" = [ "C" ]
} } })cykv" ) ).status == settings_document_status_t::OK );
            session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1;
            std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
            Show( view.get() ); Stage( ws );
            Key( view.get(), Qt::Key_Return ); Key( view.get(), Qt::Key_Escape );
            CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
            const int key = confirm ? Qt::Key_V : Qt::Key_C;
            QKeyEvent preflight( QEvent::ShortcutOverride, key, Qt::NoModifier ); preflight.ignore();
            QCoreApplication::sendEvent( view.get(), &preflight );
            CHECK( preflight.isAccepted() ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
            Key( view.get(), key );
            CHECK_FALSE( ws.editPreview.bActive );
            CHECK( ws.pDocument->geometry.brushes.nCount == ( confirm ? 1u : 0u ) );
            CHECK( EditorHistory_StepCount( &ws.history ) == ( confirm ? 1u : 0u ) );
            CHECK( ws.tool == map_tool_t::BLOCK );
            session.gui.nKeymapChain = 0;
        }
    }
}

TEST_CASE( "Staged Block cannot survive stale document selection tool mode or visibility context", "[map][gui][block][staged-block][stale]" )
{
    for ( int change = 0; change < 7; ++change ) {
        CAPTURE( change );
        session_t session;
        auto &ws = session.workspace;
        Stage( ws );
        const auto count = EditorHistory_StepCount( &ws.history );
        if ( change == 0 ) { ++ws.pDocument->geometry.revision; }
        else if ( change == 1 ) { ++ws.selection.revision; }
        else if ( change == 2 ) { ws.tool = map_tool_t::SELECT; }
        else if ( change == 3 ) { ws.elementMode = map_element_mode_t::FACES; }
        else if ( change == 4 ) { ++ws.hidden.revision; }
        else if ( change == 5 ) { ws.hiddenVisgroups ^= 1u; }
        else { ws.bCordonActive = CY_TRUE; ws.cordon = Box( 32 ); }
        CHECK_FALSE( MapWorkspace_HasBlockPreview( &ws ) );
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
        CHECK_FALSE( MapWorkspace_CommitBlockPreview( &ws ) );
        CHECK( ws.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == count );
    }
    SECTION( "Document identity is checked even when the revision matches" ) {
        session_t session;
        auto &ws = session.workspace;
        Stage( ws );
        auto *original = ws.pDocument;
        map_document_t replacement{};
        REQUIRE( MapDocument_Create( &replacement, Allocator_GetSystem(),
            { StringView_FromCString( "Replacement" ), StringView_FromCString( MAP_WORKSPACE_DEFAULT_GAME ), {} } ) == map_status_t::OK );
        replacement.geometry.revision = original->geometry.revision;
        ws.pDocument = &replacement;
        const bool current = MapWorkspace_HasBlockPreview( &ws );
        const bool committed = MapWorkspace_CommitBlockPreview( &ws );
        ws.pDocument = original;
        CHECK_FALSE( current ); CHECK_FALSE( committed ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( original->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    }
}

TEST_CASE( "Staged Block material capture accepts the full path capacity without truncation", "[map][gui][block][staged-block][material]" )
{
    session_t session;
    auto &ws = session.workspace;
    settings_t settings( &session.gui.settings );
    const std::string material( MAP_MATERIAL_PATH_MAX, 'm' );
    settings.Text( "editor.map.default_material", material.c_str() );
    Stage( ws );
    CHECK( StringView_FromCString( ws.editPreview.blockMaterial ).cchLength == MAP_MATERIAL_PATH_MAX );
    CHECK( StringView_Equals( StringView_FromCString( ws.editPreview.blockMaterial ), StringView_FromCString( material.c_str() ) ) );
    REQUIRE( MapWorkspace_CommitBlockPreview( &ws ) );
    REQUIRE( ws.selection.ids.nCount == 1 );
    CheckMaterial( *ws.pDocument, ws.selection.ids.pData[0], material.c_str(), 128 );
}

TEST_CASE( "Active staged Block resize focus loss discards construction before delayed release or confirmation", "[map][gui][block][staged-block][resize][focus][cancel]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session;
        auto &ws = session.workspace;
        auto authored = Box( 320, 160, 96 ); authored.box.minimum = { 192, 32, 0 };
        REQUIRE( MapWorkspace_CreateBox( &ws, authored ) );
        REQUIRE( ws.selection.ids.nCount == 1 );
        const u64 selected = ws.selection.ids.pData[0];
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        Show( view.get() ); Stage( ws );
        QPointF handle, moved;
        if ( perspective ) {
            MapWorkspace_Frame( &ws, CY_TRUE, map_frame_target_t::PERSPECTIVE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 64, 0, 32 }, &handle ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 101, 0, 32 }, &moved ) );
        } else {
            handle = MapOrthoView_WorldToView( view.get(), { 64, 0 } );
            moved = MapOrthoView_WorldToView( view.get(), { 101, 0 } );
        }
        auto *document = ws.pDocument;
        const auto revision = document->geometry.revision;
        const auto selectionRevision = ws.selection.revision;
        const auto historySteps = EditorHistory_StepCount( &ws.history );
        const auto points = ws.wire.points.pData;
        const bool modified = MapWorkspace_IsModified( &ws );
        Mouse( view.get(), QEvent::MouseButtonPress, handle );
        Mouse( view.get(), QEvent::MouseMove, moved, Qt::ControlModifier );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
        CheckPoint( ws.editPreview.bounds.box.maximum, { 101, 64, 64 } );
        REQUIRE( ws.editPreviewWire.points.nCount != 0 );
        QFocusEvent focus( QEvent::FocusOut, Qt::OtherFocusReason );
        QCoreApplication::sendEvent( view.get(), &focus );
        const auto checkCancelled = [&]() {
            CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( MapWorkspace_HasBlockPreview( &ws ) );
            CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.pointSourceIds.nCount == 0 );
            CHECK( ws.editPreviewWire.lines.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
            CHECK( ws.editPreviewWire.objects.nCount == 0 );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
            CHECK( document->geometry.brushes.nCount == 1 ); CHECK( ws.wire.points.pData == points );
            REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == selected );
            CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorHistory_StepCount( &ws.history ) == historySteps );
            CHECK( MapWorkspace_IsModified( &ws ) == modified ); CHECK( ws.tool == map_tool_t::BLOCK );
        };
        checkCancelled();
        // Focus loss cancels the active resize and its private stage. Late
        // input from the old gesture must never publish a replacement brush.
        Mouse( view.get(), QEvent::MouseButtonRelease, moved, Qt::ControlModifier );
        Key( view.get(), Qt::Key_Return );
        CHECK_FALSE( MapWorkspace_CommitBlockPreview( &ws ) );
        checkCancelled();
    }
}

TEST_CASE( "Staged Block viewport double-click cannot open an unrelated object or view dialog", "[map][gui][block][staged-block][double-click]" )
{
    session_t session;
    auto &ws = session.workspace;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) );
    MapViews_SetArrangement( views.get(), map_view_arrangement_t::FOUR );
    Show( views.get() ); Stage( ws );
    auto *document = ws.pDocument;
    for ( int pane : { 0, 1 } ) {
        QWidget *view = MapViews_PaneView( views.get(), pane ); REQUIRE( view != nullptr );
        Mouse( view, QEvent::MouseButtonDblClick, view->rect().center() );
        QCoreApplication::processEvents();
        CHECK( QApplication::activePopupWidget() == nullptr );
        for ( auto *menu : views->findChildren<QMenu *>() ) { CHECK_FALSE( menu->isVisible() ); }
        CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.pDocument == document );
        CHECK( EditorHistory_StepCount( &ws.history ) == 0 ); CHECK( ws.selection.ids.nCount == 0 );
    }
}

TEST_CASE( "Center 2D and 3D commands frame a private Block stage without enabling other selection actions", "[map][gui][block][staged-block][frame][commands]" )
{
    session_t session;
    auto &ws = session.workspace;
    auto &commands = session.gui.commands;
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &commands ) == command_registry_status_t::OK );
    const auto enabled = [&]( const char *id ) { return ( EditorCommands_State( &commands, StringView_FromCString( id ) ) & COMMAND_STATE_ENABLED ) != 0u; };
    CHECK_FALSE( enabled( "map.view.center_selection_2d" ) ); CHECK_FALSE( enabled( "map.view.center_selection_3d" ) );
    Stage( ws, Box( 80, 96, 112 ) );
    auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    const auto points = ws.editPreviewWire.points.pData;
    REQUIRE( ws.selection.ids.nCount == 0 ); REQUIRE( document->geometry.brushes.nCount == 0 );
    CHECK_FALSE( enabled( "map.hide.selected" ) ); CHECK_FALSE( enabled( "map.hide.unselected" ) );
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        const char *id = perspective ? "map.view.center_selection_3d" : "map.view.center_selection_2d";
        REQUIRE( enabled( id ) );
        REQUIRE( EditorCommands_ExecuteLine( &commands, StringView_FromCString( id ) ) == command_result_t::OK );
        CHECK( ws.frameTarget == ( perspective ? map_frame_target_t::PERSPECTIVE : map_frame_target_t::ORTHOGRAPHIC ) );
        CheckPoint( ws.frameBounds.box.minimum, { -64, -64, 0 } ); CheckPoint( ws.frameBounds.box.maximum, { 80, 96, 112 } );
        CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.editPreviewWire.points.pData == points );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.ids.nCount == 0 ); CHECK( document->geometry.brushes.nCount == 0 );
        CHECK( EditorHistory_StepCount( &ws.history ) == 0 ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    }
    SECTION( "A failed resize still permits framing the retained valid bounds" ) {
        auto invalid = ws.editPreview.bounds; invalid.box.maximum.x = std::numeric_limits<f64>::infinity();
        REQUIRE_FALSE( MapWorkspace_SetBlockPreviewBounds( &ws, invalid ) );
        REQUIRE( enabled( "map.view.center_selection_3d" ) );
        REQUIRE( EditorCommands_ExecuteLine( &commands, StringView_FromCString( "map.view.center_selection_3d" ) ) == command_result_t::OK );
        CheckPoint( ws.frameBounds.box.maximum, { 80, 96, 112 } );
        CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.editPreview.status != map_status_t::OK );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    }
    SECTION( "An invalidated stage does not keep center commands enabled" ) {
        ++ws.hidden.revision;
        CHECK_FALSE( enabled( "map.view.center_selection_2d" ) ); CHECK_FALSE( enabled( "map.view.center_selection_3d" ) );
        CHECK( EditorCommands_ExecuteLine( &commands, StringView_FromCString( "map.view.center_selection_3d" ) ) == command_result_t::DISABLED );
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); CHECK_FALSE( ws.editPreview.bActive );
    }
}

TEST_CASE( "Frame Map unions a staged primitive with the committed world", "[map][gui][block][staged-block][frame]" )
{
    session_t session;
    auto &ws = session.workspace;
    auto world = Box( 320, 160, 96 ); world.box.minimum = { 192, 32, 0 };
    REQUIRE( MapWorkspace_CreateBox( &ws, world ) );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    Stage( ws );
    auto *document = ws.pDocument;
    const auto steps = EditorHistory_StepCount( &ws.history );
    MapWorkspace_Frame( &ws, CY_FALSE );
    CheckPoint( ws.frameBounds.box.minimum, { -64, -64, 0 } ); CheckPoint( ws.frameBounds.box.maximum, { 320, 160, 96 } );
    CHECK( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.pDocument == document );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( document->geometry.brushes.nCount == 1 );
    // Center-selection prioritizes the construction even if an old authored
    // object remains selected when the Block tool starts its next primitive.
    MapWorkspace_ClearEditPreview( &ws );
    MapWorkspace_Select( &ws, ws.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    Stage( ws ); MapWorkspace_Frame( &ws, CY_TRUE );
    CheckPoint( ws.frameBounds.box.maximum, { 64, 64, 64 } ); CHECK( MapWorkspace_HasBlockPreview( &ws ) );
}
