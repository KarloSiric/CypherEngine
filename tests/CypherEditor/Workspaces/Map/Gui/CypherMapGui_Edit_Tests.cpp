//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Immutable edit snapshots and atomic wireframe publication contracts.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherEditorGui_Section.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Mathlib/CypherMath_UV.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentBrushAttributes.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QPushButton>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>
#include <map>
#include <set>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
struct allocation_failure_t { usize calls{ 0 }, failOn{ 0 }, live{ 0 }; };
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
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};
struct settings_scope_tester_t {
    settings_registry_t *registry;
    settings_document_t store{};
    explicit settings_scope_tester_t( settings_registry_t *settings ) : registry( settings )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~settings_scope_tester_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Text( const char *path, const char *text )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = descriptor->type; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
    void Integer( const char *path, i64 number )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = setting_type_t::INTEGER; value.nValue = number;
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
map_bounds_t Box( f64 x )
{
    map_bounds_t bounds{};
    bounds.bHas = CY_TRUE;
    bounds.box = { { x, 0, 0 }, { x + 64, 64, 64 } };
    return bounds;
}
map_clip_guide_t ClipGuide( u32 axis, f64 offset = 32, f64 start = 0, f64 end = 64, f64 constructionCoordinate = 17 )
{
    map_clip_guide_t guide{}; guide.bHas = CY_TRUE; guide.extrusionAxis = axis;
    if ( axis == 0 ) { guide.points[0] = { constructionCoordinate, offset, start }; guide.points[1] = { constructionCoordinate, offset, end }; }
    else if ( axis == 1 ) { guide.points[0] = { offset, constructionCoordinate, start }; guide.points[1] = { offset, constructionCoordinate, end }; }
    else { guide.points[0] = { offset, start, constructionCoordinate }; guide.points[1] = { offset, end, constructionCoordinate }; }
    return guide;
}
void CheckSavedBrush( const QString &path, usize expectedCount, u64 id = 0, f64 expectedX = 0 )
{
    map_document_t loaded{};
    const auto result = MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() );
    REQUIRE( result.status == map_files_status_t::OK );
    REQUIRE( result.documentStatus == map_status_t::OK );
    REQUIRE( loaded.geometry.brushes.nCount == expectedCount );
    if ( expectedCount != 0 ) {
        map_wireframe_t wire{};
        REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
        REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK );
        const auto *object = MapWireframe_FindObject( wire, id );
        REQUIRE( object != nullptr );
        CHECK( object->bounds.box.minimum.x == Catch::Approx( expectedX ) );
    }
}
}

TEST_CASE( "Workspace creation defaults author every primitive with persistent identity and surface mapping", "[map][gui][geometry-edit][primitive][persistence]" )
{
    namespace geo = cypher::editor::geometry;
    struct shape_t { const char *name; map_primitive_kind_t kind; usize faces; };
    const shape_t shapes[]{
        { "box", map_primitive_kind_t::BOX, 6 }, { "wedge", map_primitive_kind_t::WEDGE, 5 },
        { "cylinder", map_primitive_kind_t::CYLINDER, 14 }, { "spike", map_primitive_kind_t::CONE, 13 },
        { "sphere", map_primitive_kind_t::SPHERE, 240 }
    };
    for ( const auto &shape : shapes ) {
        CAPTURE( shape.name );
        session_t session;
        auto &ws = session.workspace;
        settings_scope_tester_t settings( &session.gui.settings );
        settings.Text( "editor.map.new_brush_shape", shape.name );
        settings.Text( "editor.map.primitive_axis", "Y" );
        settings.Text( "editor.map.default_material", "materials/dev/primitive_test.cymat" );
        settings.Integer( "editor.map.cylinder_sides", 12 );
        settings.Integer( "editor.map.sphere_subdivisions", 1 );
        settings.Real( "editor.map.cone_top_radius", 0 );
        settings.Real( "editor.map.default_texture_scale", 0.5 );
        const auto envelope = Box( 128 );
        const auto desc = MapWorkspace_PrimitiveDefaults( &ws, envelope );
        CHECK( desc.kind == shape.kind ); CHECK( desc.axis == 1 );
        CHECK( desc.worldUnitsPerUv == 256 );
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, envelope ) );
        REQUIRE( ws.pDocument->geometry.brushes.nCount == 1 );
        REQUIRE( ws.selection.ids.nCount == 1 );
        const u64 id = ws.selection.ids.pData[0];
        const auto *brush = geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { id } );
        REQUIRE( brush != nullptr ); REQUIRE( brush->sides.nCount == shape.faces );
        std::vector<u64> sideIds;
        for ( usize side = 0; side < brush->sides.nCount; ++side ) { sideIds.push_back( brush->sides.pData[side].sourceId.value ); }
        const auto checkSurfaces = [&]( const map_document_t &document ) {
            const auto *solid = geo::GeometryDocument_FindBrush( &document.geometry, { id } );
            const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &document.geometry, { id } );
            REQUIRE( solid != nullptr ); REQUIRE( attributes != nullptr );
            REQUIRE( solid->sides.nCount == sideIds.size() );
            CHECK( geo::BrushAttributes_Covers( solid, attributes ) );
            for ( usize side = 0; side < solid->sides.nCount; ++side ) {
                const auto &face = solid->sides.pData[side];
                CHECK( face.sourceId.value == sideIds[side] );
                const auto &surface = attributes->records.pData[face.iAttributeIndex];
                CHECK( StringView_Equals( MapMaterials_Path( &document.materials, surface.material.value ),
                    StringView_FromCString( "materials/dev/primitive_test.cymat" ) ) );
                CHECK( surface.uvProjection.worldUnitsPerUv.x == Catch::Approx( 256 ) );
                CHECK( surface.uvProjection.worldUnitsPerUv.y == Catch::Approx( 256 ) );
            }
        };
        checkSurfaces( *ws.pDocument );
        REQUIRE( MapWireframe_FindObject( ws.wire, id ) != nullptr );
        CHECK( ws.wire.nBrokenBrushes == 0 );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        QTemporaryDir folder;
        REQUIRE( folder.isValid() );
        const QString path = folder.filePath( QString::fromUtf8( shape.name ) + QStringLiteral( ".cymap" ) );
        REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
        map_document_t loaded{};
        REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
        REQUIRE( loaded.geometry.brushes.nCount == 1 );
        checkSurfaces( loaded );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
        CHECK( ws.wire.objects.nCount == 0 ); CHECK( ws.selection.ids.nCount == 0 );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == id );
        checkSurfaces( *ws.pDocument );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    }
}

TEST_CASE( "Primitive defaults preserve construction options and explicit box creation remains a box", "[map][gui][geometry-edit][primitive]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session;
    auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    CHECK( MapWorkspace_PrimitiveDefaults( &ws, Box( 0 ) ).worldUnitsPerUv == 128 );
    settings.Text( "editor.map.new_brush_shape", "cylinder" );
    settings.Text( "editor.map.primitive_axis", "X" );
    settings.Integer( "editor.map.cylinder_sides", 24 );
    settings.Integer( "editor.map.sphere_subdivisions", 0 );
    settings.Real( "editor.map.cone_top_radius", 0.5 );
    const auto desc = MapWorkspace_PrimitiveDefaults( &ws, Box( 0 ) );
    CHECK( desc.kind == map_primitive_kind_t::CYLINDER ); CHECK( desc.axis == 0 );
    CHECK( desc.nSides == 24 ); CHECK( desc.sphereSubdivisions == 0 ); CHECK( desc.coneTopRadiusRatio == 0.5 );
    CHECK( desc.wedgeSlopeAxis != desc.wedgeCutAxis );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 boxId = ws.selection.ids.pData[0];
    REQUIRE( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { boxId } ) != nullptr );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { boxId } )->sides.nCount == 6 );
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, Box( 128 ) ) );
    const u64 cylinderId = ws.selection.ids.pData[0];
    CHECK( cylinderId != boxId );
    REQUIRE( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { cylinderId } ) != nullptr );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { cylinderId } )->sides.nCount == 26 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
}

TEST_CASE( "Invalid workspace primitive creation cannot replace the live document or advance history", "[map][gui][geometry-edit][primitive][atomic]" )
{
    session_t session;
    auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    settings.Text( "editor.map.new_brush_shape", "cylinder" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const auto *document = ws.pDocument;
    const auto *wirePoints = ws.wire.points.pData;
    const auto *selection = ws.selection.ids.pData;
    const u64 selected = ws.selection.ids.pData[0];
    const auto revision = document->geometry.revision;
    const auto selectionRevision = ws.selection.revision;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    for ( int invalid = 0; invalid < 4; ++invalid ) {
        CAPTURE( invalid );
        auto bounds = Box( 128 );
        if ( invalid == 0 ) { bounds.bHas = CY_FALSE; }
        else if ( invalid == 1 ) { bounds.box.maximum.z = bounds.box.minimum.z; }
        else if ( invalid == 2 ) { bounds.box.maximum.x = bounds.box.minimum.x - 1; }
        else { bounds.box.minimum.y = std::numeric_limits<f64>::quiet_NaN(); }
        CHECK_FALSE( MapWorkspace_CreatePrimitive( &ws, bounds ) );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.wire.nBrokenBrushes == 0 );
        CHECK( ws.wire.points.pData == wirePoints ); CHECK( ws.selection.ids.pData == selection );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.selection.ids.pData[0] == selected );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Workspace subtraction retains its cutter and publishes attributed fragments in one history step", "[map][gui][geometry-edit][subtract][persistence]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session;
    auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    auto outer = Box( 0 ); outer.box.maximum = { 128, 128, 128 };
    auto cutterBounds = Box( 48 ); cutterBounds.box.minimum = { 48, -32, -32 }; cutterBounds.box.maximum = { 80, 160, 160 };
    settings.Text( "editor.map.default_material", "materials/dev/target_stone.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, outer ) );
    const u64 targetId = ws.selection.ids.pData[0];
    settings.Text( "editor.map.default_material", "materials/dev/cutter_copper.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, cutterBounds ) );
    const u64 cutterId = ws.selection.ids.pData[0];
    const auto *cutter = geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { cutterId } );
    REQUIRE( cutter != nullptr );
    std::vector<u64> cutterSides;
    for ( usize side = 0; side < cutter->sides.nCount; ++side ) { cutterSides.push_back( cutter->sides.pData[side].sourceId.value ); }
    MapWorkspace_Select( &ws, targetId, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_SubtractBrush( &ws, cutterId ) );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 3 );
    REQUIRE( ws.selection.ids.nCount == 2 );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { targetId } ) == nullptr );
    CHECK_FALSE( EditorSelection_Contains( &ws.selection, cutterId ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
    const std::vector<u64> fragments{ ws.selection.ids.pData[0], ws.selection.ids.pData[1] };
    bool left = false, right = false;
    for ( const u64 id : fragments ) {
        CHECK( id != targetId ); CHECK( id != cutterId );
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
        if ( std::abs( object->bounds.box.minimum.x ) < 1e-6 ) {
            left = true; CHECK( object->bounds.box.maximum.x == Catch::Approx( 48 ) );
        } else {
            right = true; CHECK( object->bounds.box.minimum.x == Catch::Approx( 80 ) );
            CHECK( object->bounds.box.maximum.x == Catch::Approx( 128 ) );
        }
        CHECK( object->bounds.box.minimum.y == Catch::Approx( 0 ) ); CHECK( object->bounds.box.maximum.z == Catch::Approx( 128 ) );
    }
    CHECK( left ); CHECK( right );
    const auto checkAuthoredResult = [&]( const map_document_t &document ) {
        const auto *retained = geo::GeometryDocument_FindBrush( &document.geometry, { cutterId } );
        REQUIRE( retained != nullptr ); REQUIRE( retained->sides.nCount == cutterSides.size() );
        for ( usize side = 0; side < retained->sides.nCount; ++side ) { CHECK( retained->sides.pData[side].sourceId.value == cutterSides[side] ); }
        for ( const u64 id : fragments ) {
            const auto *brush = geo::GeometryDocument_FindBrush( &document.geometry, { id } );
            const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &document.geometry, { id } );
            REQUIRE( brush != nullptr ); REQUIRE( attributes != nullptr );
            REQUIRE( brush->sides.nCount == 6 ); REQUIRE( geo::BrushAttributes_Covers( brush, attributes ) );
            usize targetFaces = 0, cutterFaces = 0;
            for ( usize side = 0; side < brush->sides.nCount; ++side ) {
                const auto &face = brush->sides.pData[side];
                const auto &surface = attributes->records.pData[face.iAttributeIndex];
                const auto material = MapMaterials_Path( &document.materials, surface.material.value );
                targetFaces += StringView_Equals( material, StringView_FromCString( "materials/dev/target_stone.cymat" ) );
                cutterFaces += StringView_Equals( material, StringView_FromCString( "materials/dev/cutter_copper.cymat" ) );
            }
            CHECK( targetFaces == 5 ); CHECK( cutterFaces == 1 );
        }
    };
    checkAuthoredResult( *ws.pDocument );
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "subtract.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    REQUIRE( loaded.geometry.brushes.nCount == 3 ); checkAuthoredResult( loaded );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 2 );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == targetId );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { targetId } ) != nullptr );
    CHECK( geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { cutterId } ) != nullptr );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.selection.ids.nCount == fragments.size() );
    CHECK( ws.selection.ids.pData[0] == fragments[0] ); CHECK( ws.selection.ids.pData[1] == fragments[1] );
    checkAuthoredResult( *ws.pDocument );
    CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
}

TEST_CASE( "Nonoverlapping cutter brushes do not publish a new document or history step", "[map][gui][geometry-edit][subtract][atomic]" )
{
    for ( const f64 cutterX : { 64.0, 128.0 } ) {
        CAPTURE( cutterX );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 target = ws.selection.ids.pData[0];
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( cutterX ) ) ); const u64 cutter = ws.selection.ids.pData[0];
        MapWorkspace_Select( &ws, target, MAP_SELECT_REPLACE );
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
        const auto revision = document->geometry.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        CHECK_FALSE( MapWorkspace_SubtractBrush( &ws, cutter ) );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.wire.points.pData == points ); CHECK( ws.pDocument->geometry.brushes.nCount == 2 );
        REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == target );
        CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Subtraction rejects selected hidden missing or mixed-geometry cutters and targets", "[map][gui][geometry-edit][subtract][atomic]" )
{
    for ( int invalid = 0; invalid < 4; ++invalid ) {
        CAPTURE( invalid );
        session_t session; auto &ws = session.workspace;
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK );
        const u64 selectedCutter[]{ 1000, 1001 }, mixedTargets[]{ 1000, 1200 };
        u64 cutter = 1001;
        if ( invalid == 0 ) { MapWorkspace_SetSelection( &ws, selectedCutter, 2 ); }
        else if ( invalid == 1 ) { MapWorkspace_SetSelection( &ws, mixedTargets, 2 ); }
        else {
            MapWorkspace_Select( &ws, 1000, MAP_SELECT_REPLACE );
            if ( invalid == 2 ) { REQUIRE( EditorSelection_Apply( &ws.hidden, cutter, EDITOR_SELECT_ADD ) ); MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); }
            else { cutter = 0; }
        }
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
        const auto *selected = ws.selection.ids.pData; const auto revision = document->geometry.revision;
        const auto selectionRevision = ws.selection.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        CHECK_FALSE( MapWorkspace_SubtractBrush( &ws, cutter ) );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData == selected ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Primitive construction caches exact private geometry without changing the live map", "[map][gui][geometry-edit][primitive][preview]" )
{
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    settings.Text( "editor.map.new_brush_shape", "cylinder" ); settings.Integer( "editor.map.cylinder_sides", 12 );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const u64 selected = ws.selection.ids.pData[0]; const auto revision = document->geometry.revision;
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreviewWire.objects.nCount == 1 ); CHECK( ws.editPreviewWire.faces.nCount == 14 );
    CHECK( ws.editPreviewWire.points.nCount > 8 ); CHECK( ws.editPreviewWire.nBrokenBrushes == 0 );
    const auto *previewPoints = ws.editPreviewWire.points.pData;
    const auto *previewFaces = ws.editPreviewWire.faces.pData;
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
    CHECK( ws.editPreviewWire.points.pData == previewPoints ); CHECK( ws.editPreviewWire.faces.pData == previewFaces );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == selected );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    MapWorkspace_ClearEditPreview( &ws );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Failed construction preparation clears stale geometry and can be retried without a live edit", "[map][gui][geometry-edit][primitive][preview][allocation]" )
{
    allocation_failure_t failure;
    const allocator_t allocator{ &Allocate, nullptr, &Free, &failure };
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    settings.Text( "editor.map.new_brush_shape", "cylinder" ); settings.Integer( "editor.map.cylinder_sides", 12 );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const auto revision = document->geometry.revision; const u64 selected = ws.selection.ids.pData[0];
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto buildAudited = [&]( const map_bounds_t &bounds ) {
        // Only the construction document borrows this allocator. Restore the
        // live document before assertions or its eventual destruction.
        const auto *originalAllocator = ws.pDocument->pAllocator;
        ws.pDocument->pAllocator = &allocator;
        MapWorkspace_SetEditPreview( &ws, bounds );
        ws.pDocument->pAllocator = originalAllocator;
    };
    buildAudited( Box( 256 ) );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreviewWire.faces.nCount == 14 );
    const usize successfulCalls = failure.calls;
    REQUIRE( successfulCalls > 1 ); REQUIRE( failure.live > 0 );
    MapWorkspace_ClearEditPreview( &ws ); REQUIRE( failure.live == 0 );
    // The new failed build must not leave the previously valid shape visible.
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
    REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    usize failOn = 1;
    SECTION( "Construction document allocation" ) {}
    SECTION( "Final wireframe allocation" ) { failOn = successfulCalls; }
    failure.calls = 0; failure.failOn = failOn;
    buildAudited( Box( 256 ) );
    CHECK( failure.calls >= failOn ); CHECK( failure.live == 0 );
    CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.lines.nCount == 0 );
    CHECK( ws.editPreviewWire.objects.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.wire.points.pData == points ); CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == selected );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    failure.calls = 0; failure.failOn = 0;
    buildAudited( Box( 256 ) );
    CHECK( ws.editPreview.status == map_status_t::OK ); CHECK( ws.editPreviewWire.faces.nCount == 14 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    MapWorkspace_ClearEditPreview( &ws ); CHECK( failure.live == 0 );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
}

TEST_CASE( "Shared transform descriptors replace construction geometry without allocating or changing the map", "[map][gui][geometry-edit][transform-preview][allocation]" )
{
    allocation_failure_t audit;
    const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const auto *lines = ws.wire.lines.pData; const auto *faces = ws.wire.faces.pData;
    const auto revision = document->geometry.revision, nextId = document->nextId;
    const auto selectionRevision = ws.selection.revision; const u64 selected = ws.selection.ids.pData[0];
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const std::vector<cypher::math::vec3d_t> originalPoints( points, points + ws.wire.points.nCount );
    const auto *originalAllocator = document->pAllocator;
    ws.pDocument->pAllocator = &allocator;
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
    ws.pDocument->pAllocator = originalAllocator;
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreviewWire.points.nCount != 0 ); REQUIRE( audit.live > 0 );

    map_transform_preview_t transforms[3]{};
    transforms[0].kind = map_transform_preview_kind_t::TRANSLATE; transforms[0].delta = { 16, -8, 24 }; transforms[0].bClone = CY_TRUE;
    transforms[1].kind = map_transform_preview_kind_t::SCALE; transforms[1].pivot = { 32, 32, 32 }; transforms[1].factors = { 2, 0.5, 1.5 };
    transforms[2].kind = map_transform_preview_kind_t::ROTATE; transforms[2].pivot = { 32, 32, 32 }; transforms[2].degrees = { 0, 0, 90 };
    const cypher::math::vec3d_t minimum[]{ { 16, -8, 24 }, { -32, 16, -16 }, { 0, 0, 0 } };
    const cypher::math::vec3d_t maximum[]{ { 80, 56, 88 }, { 96, 48, 80 }, { 64, 64, 64 } };
    const cypher::math::vec3d_t corner[]{ { 80, -8, 88 }, { 96, 16, 80 }, { 64, 64, 64 } };
    const auto checkPoint = []( cypher::math::vec3d_t actual, cypher::math::vec3d_t expected ) {
        CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-9 ) );
        CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-9 ) );
        CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-9 ) );
    };
    audit.calls = 0; audit.failOn = 1;
    for ( usize pass = 0; pass < 12; ++pass ) {
        for ( usize i = 0; i < 3; ++i ) {
            CAPTURE( pass, i );
            // Restore the borrowed allocator before assertions or destruction.
            ws.pDocument->pAllocator = &allocator;
            MapWorkspace_SetTransformPreview( &ws, transforms[i] );
            ws.pDocument->pAllocator = originalAllocator;
            REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.status == map_status_t::OK );
            CHECK( audit.calls == 0 ); CHECK( audit.live == 0 );
            CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.lines.nCount == 0 );
            CHECK( ws.editPreviewWire.faces.nCount == 0 ); CHECK( ws.editPreviewWire.objects.nCount == 0 );
            CHECK( ws.editPreview.transform.kind == transforms[i].kind ); CHECK( ws.editPreview.transform.bClone == transforms[i].bClone );
            CHECK( ws.editPreview.tool == ws.tool ); CHECK( ws.editPreview.mode == ws.elementMode );
            CHECK( ws.editPreview.documentRevision == revision ); CHECK( ws.editPreview.selectionRevision == selectionRevision );
            REQUIRE( ws.editPreview.bounds.bHas ); checkPoint( ws.editPreview.bounds.box.minimum, minimum[i] );
            checkPoint( ws.editPreview.bounds.box.maximum, maximum[i] );
            checkPoint( MapWorkspace_TransformPreviewPoint( ws.editPreview.transform, { 64, 0, 64 } ), corner[i] );
        }
    }
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.wire.lines.pData == lines ); CHECK( ws.wire.faces.pData == faces ); REQUIRE( ws.wire.points.nCount == originalPoints.size() );
    for ( usize i = 0; i < originalPoints.size(); ++i ) { CHECK( cypher::math::Vec3d_EqualsExact( ws.wire.points.pData[i], originalPoints[i] ) ); }
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == selected ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    MapWorkspace_ClearEditPreview( &ws ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( audit.calls == 0 ); CHECK( audit.live == 0 );
}

TEST_CASE( "Invalid transform presentation cannot leave a stale descriptor or overflowed bounds", "[map][gui][geometry-edit][transform-preview][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const auto revision = document->geometry.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    map_transform_preview_t valid{}; valid.kind = map_transform_preview_kind_t::TRANSLATE; valid.delta = { 16, 8, -4 };
    MapWorkspace_SetTransformPreview( &ws, valid ); REQUIRE( ws.editPreview.bActive );
    auto invalid = valid;
    SECTION( "Nonfinite displacement" ) { invalid.delta.x = std::numeric_limits<f64>::quiet_NaN(); }
    SECTION( "Nonfinite pivot" ) { invalid.pivot.y = std::numeric_limits<f64>::infinity(); }
    SECTION( "Nonfinite angle" ) { invalid.kind = map_transform_preview_kind_t::ROTATE; invalid.degrees.z = std::numeric_limits<f64>::infinity(); }
    SECTION( "Collapsed scale" ) { invalid.kind = map_transform_preview_kind_t::SCALE; invalid.factors.y = 0; }
    SECTION( "Reversed scale" ) { invalid.kind = map_transform_preview_kind_t::SCALE; invalid.factors.z = -1; }
    SECTION( "Scale does not clone" ) { invalid.kind = map_transform_preview_kind_t::SCALE; invalid.bClone = CY_TRUE; }
    SECTION( "Rotation does not clone" ) { invalid.kind = map_transform_preview_kind_t::ROTATE; invalid.bClone = CY_TRUE; }
    SECTION( "Translation cannot describe bounds resize" ) { invalid.bResize = CY_TRUE; }
    SECTION( "Rotation cannot describe bounds resize" ) { invalid.kind = map_transform_preview_kind_t::ROTATE; invalid.bResize = CY_TRUE; }
    SECTION( "Finite matrix whose points overflow" ) { invalid.kind = map_transform_preview_kind_t::SCALE; invalid.factors.x = std::numeric_limits<f64>::max(); }
    SECTION( "Finite pivot and scale whose matrix overflows" ) {
        invalid.kind = map_transform_preview_kind_t::SCALE; invalid.pivot.x = std::numeric_limits<f64>::max(); invalid.factors.x = 2;
    }
    SECTION( "Finite coordinates exceed document magnitude policy" ) { invalid.delta.x = document->geometryPolicy.numerical.fCoordinateMagnitudeLimit; }
    SECTION( "No transform" ) { invalid.kind = map_transform_preview_kind_t::NONE; }
    SECTION( "Unknown transform kind" ) { invalid.kind = static_cast<map_transform_preview_kind_t>( 255 ); }
    MapWorkspace_SetTransformPreview( &ws, invalid );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.bounds.bHas );
    CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    MapWorkspace_SetTransformPreview( &ws, valid ); REQUIRE( ws.editPreview.bActive );
    CHECK( ws.editPreview.bounds.box.minimum.x == 16 ); CHECK( ws.editPreview.bounds.box.maximum.x == 80 );
}

TEST_CASE( "Transform preview inclusion follows selected owners and point entities without widening selection", "[map][gui][geometry-edit][transform-preview][entity]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 brush = ws.selection.ids.pData[0];
    u64 owner{}, light{};
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), { 32, 32, 32 }, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "light" ), { 256, 0, 16 }, &light ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws );
    const auto *ownedBrush = MapWireframe_FindObject( ws.wire, brush ); const auto *pointEntity = MapWireframe_FindObject( ws.wire, light );
    REQUIRE( ownedBrush != nullptr ); REQUIRE( pointEntity != nullptr ); REQUIRE( pointEntity->nLines == 0 );
    MapWorkspace_Select( &ws, owner, MAP_SELECT_REPLACE );
    CHECK( MapWorkspace_IsTransformPreviewObject( &ws, *ownedBrush ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, brush ) );
    CHECK_FALSE( MapWorkspace_IsTransformPreviewObject( &ws, *pointEntity ) );
    map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::TRANSLATE; transform.delta = { 16, 8, -4 };
    MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive );
    CHECK( ws.editPreview.bounds.box.minimum.x == 16 ); CHECK( ws.editPreview.bounds.box.maximum.x == 80 );
    CHECK( ws.editPreview.bounds.box.minimum.y == 8 ); CHECK( ws.editPreview.bounds.box.maximum.y == 72 );
    CHECK( ws.editPreview.bounds.box.minimum.z == -4 ); CHECK( ws.editPreview.bounds.box.maximum.z == 60 );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == owner );
    MapWorkspace_Select( &ws, light, MAP_SELECT_REPLACE );
    MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive );
    CHECK_FALSE( MapWorkspace_IsTransformPreviewObject( &ws, *ownedBrush ) ); CHECK( MapWorkspace_IsTransformPreviewObject( &ws, *pointEntity ) );
    CHECK( ws.editPreview.bounds.box.minimum.x == 272 - MAP_WIRE_POINT_ENTITY_HALF );
    CHECK( ws.editPreview.bounds.box.maximum.x == 272 + MAP_WIRE_POINT_ENTITY_HALF );
    CHECK( ws.editPreview.bounds.box.minimum.z == 12 - MAP_WIRE_POINT_ENTITY_HALF );
    CHECK( ws.editPreview.bounds.box.maximum.z == 12 + MAP_WIRE_POINT_ENTITY_HALF );
    transform.kind = map_transform_preview_kind_t::SCALE; transform.factors = { 2, 2, 2 };
    MapWorkspace_SetTransformPreview( &ws, transform ); CHECK_FALSE( ws.editPreview.bActive );
    transform.kind = map_transform_preview_kind_t::ROTATE; transform.degrees = { 0, 0, 90 };
    MapWorkspace_SetTransformPreview( &ws, transform ); CHECK_FALSE( ws.editPreview.bActive );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == light ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Invalid construction geometry cannot keep a stale successful preview", "[map][gui][geometry-edit][primitive][preview][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    settings.Text( "editor.map.new_brush_shape", "cylinder" );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const auto *points = ws.wire.points.pData; const u64 selected = ws.selection.ids.pData[0];
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    auto collapsed = Box( 128 ); collapsed.box.maximum.y = collapsed.box.minimum.y;
    MapWorkspace_SetEditPreview( &ws, collapsed );
    CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.selection.ids.pData[0] == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    MapWorkspace_ClearEditPreview( &ws ); CHECK_FALSE( ws.editPreview.bActive );
    MapWorkspace_SetEditPreview( &ws, Box( 128 ) ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    MapWorkspace_SetEditPreview( &ws, {} );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Construction cache keys include every geometry and surface creation option", "[map][gui][geometry-edit][primitive][preview][settings]" )
{
    allocation_failure_t audit;
    const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const auto revision = document->geometry.revision; const u64 selected = ws.selection.ids.pData[0];
    const auto build = [&]( usize expectedFaces ) {
        const usize before = audit.calls;
        const auto *originalAllocator = ws.pDocument->pAllocator;
        ws.pDocument->pAllocator = &allocator;
        MapWorkspace_SetEditPreview( &ws, Box( 128 ) );
        ws.pDocument->pAllocator = originalAllocator;
        REQUIRE( ws.editPreview.status == map_status_t::OK );
        CHECK( ws.editPreviewWire.faces.nCount == expectedFaces ); CHECK( audit.calls > before );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData[0] == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        // With identical bounds and settings every pane can reuse the result.
        const usize builtCalls = audit.calls;
        MapWorkspace_SetEditPreview( &ws, Box( 128 ) ); CHECK( audit.calls == builtCalls );
    };
    settings.Text( "editor.map.new_brush_shape", "cylinder" ); settings.Integer( "editor.map.cylinder_sides", 6 );
    build( 8 );
    settings.Integer( "editor.map.cylinder_sides", 12 ); build( 14 );
    settings.Text( "editor.map.primitive_axis", "X" ); build( 14 );
    usize caps = 0;
    for ( usize face = 0; face < ws.editPreviewWire.faces.nCount; ++face ) {
        const auto &surface = ws.editPreviewWire.faces.pData[face];
        if ( surface.nIndices == 12 ) { ++caps; CHECK( std::abs( surface.normal.x ) == Catch::Approx( 1 ) ); }
    }
    CHECK( caps == 2 );
    settings.Real( "editor.map.default_texture_scale", 0.5 ); build( 14 );
    CHECK( ws.editPreview.primitive.worldUnitsPerUv == 256 );
    settings.Text( "editor.map.new_brush_shape", "spike" ); settings.Real( "editor.map.cone_top_radius", 0 ); build( 13 );
    settings.Real( "editor.map.cone_top_radius", 0.5 ); build( 14 );
    settings.Text( "editor.map.new_brush_shape", "sphere" ); settings.Integer( "editor.map.sphere_subdivisions", 0 ); build( 128 );
    settings.Integer( "editor.map.sphere_subdivisions", 1 ); build( 240 );
    MapWorkspace_ClearEditPreview( &ws ); CHECK( audit.live == 0 );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "A clipping preview commits its captured plane mode and cut surface as one edit", "[map][gui][geometry-edit][clip][preview][persistence]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    settings.Text( "editor.map.default_material", "materials/dev/clip_source.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 original = ws.selection.ids.pData[0];
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const auto *livePoints = ws.wire.points.pData;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    settings.Text( "editor.map.clip_mode", "both" );
    settings.Text( "editor.map.default_material", "materials/dev/clip_cut.cymat" );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const cypher::math::planed_t plane{ { 1, 0, 0 }, -32 };
    MapWorkspace_SetClipPreview( &ws, plane );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    CHECK( ws.editPreview.clipMode == map_brush_clip_mode_t::BOTH );
    REQUIRE( ws.editPreviewWire.objects.nCount == 2 );
    CHECK( ws.editPreviewWire.faces.nCount == 12 );
    CHECK( ws.editPreviewWire.bounds.box.minimum.x == 0 ); CHECK( ws.editPreviewWire.bounds.box.maximum.x == 64 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == livePoints );
    CHECK( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == original );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    settings.Text( "editor.map.default_material", "materials/dev/clip_later.cymat" );
    REQUIRE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 2 ); REQUIRE( ws.selection.ids.nCount == 2 );
    const u64 first = ws.selection.ids.pData[0], second = ws.selection.ids.pData[1];
    CHECK( first != original ); CHECK( second != original ); CHECK( first != second );
    CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    int cutFaces = 0;
    for ( usize i = 0; i < ws.pDocument->geometry.brushes.nCount; ++i ) {
        const auto *brush = ws.pDocument->geometry.brushes.pData[i];
        const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &ws.pDocument->geometry, brush->sourceId );
        REQUIRE( attributes != nullptr );
        for ( usize j = 0; j < brush->sides.nCount; ++j ) {
            const auto &side = brush->sides.pData[j];
            const auto &surface = attributes->records.pData[side.iAttributeIndex];
            const bool cut = std::abs( side.plane.normal.x ) > 0.99 && std::abs( std::abs( side.plane.d ) - 32 ) < 1e-6;
            cutFaces += cut ? 1 : 0;
            CHECK( StringView_Equals( MapMaterials_Path( &ws.pDocument->materials, surface.material.value ),
                StringView_FromCString( cut ? "materials/dev/clip_cut.cymat" : "materials/dev/clip_source.cymat" ) ) );
        }
    }
    CHECK( cutFaces == 2 );
    const auto committedToken = UndoRedo_StateToken( ws.history.pUndo );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK( UndoRedo_StateTokenEquals( committedToken, UndoRedo_StateToken( ws.history.pUndo ) ) );
    QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "clip_preview.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    const auto *savedFirst = MapWireframe_FindObject( ws.wire, first ); REQUIRE( savedFirst != nullptr );
    CheckSavedBrush( path, 2, first, savedFirst->bounds.box.minimum.x );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( ws.selection.ids.pData[0] == original ); CHECK( ws.wire.bounds.box.maximum.x == 64 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.selection.ids.nCount == 2 ); CHECK( ws.selection.ids.pData[0] == first ); CHECK( ws.selection.ids.pData[1] == second );
}

namespace
{
u64 FacePreviewSide( const map_workspace_t &ws, u64 id, u32 axis, int sign )
{
    const auto *brush = cypher::editor::geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { id } ); REQUIRE( brush != nullptr );
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        const auto normal = brush->sides.pData[i].plane.normal;
        if ( ( axis == 0 ? normal.x : axis == 1 ? normal.y : normal.z ) * sign > 0.99 ) { return brush->sides.pData[i].sourceId.value; }
    }
    FAIL( "Expected an authored box side in this direction" ); return 0;
}
std::vector<cypher::math::vec3d_t> FacePreviewPoints( const map_wireframe_t &wire )
{
    return { wire.points.pData, wire.points.pData + wire.points.nCount };
}
void CheckFacePreviewPoints( const map_wireframe_t &wire, const std::vector<cypher::math::vec3d_t> &points )
{
    REQUIRE( wire.points.nCount == points.size() );
    for ( usize i = 0; i < points.size(); ++i ) {
        CHECK( wire.points.pData[i].x == Catch::Approx( points[i].x ).margin( 1e-7 ) );
        CHECK( wire.points.pData[i].y == Catch::Approx( points[i].y ).margin( 1e-7 ) );
        CHECK( wire.points.pData[i].z == Catch::Approx( points[i].z ).margin( 1e-7 ) );
    }
}
}

TEST_CASE( "Brush face previews contain the complete exact solid in all signed axes before one atomic publication", "[map][gui][geometry-edit][face-volume-preview][persistence]" )
{
    for ( u32 axis = 0; axis < 3; ++axis ) { for ( int sign : { -1, 1 } ) { for ( f64 distance : { -16.0, 32.0 } ) {
        CAPTURE( axis, sign, distance ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 id = ws.selection.ids.pData[0];
        const u64 side = FacePreviewSide( ws, id, axis, sign ); MapWorkspace_SelectBrushFace( &ws, id, side );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto original = FacePreviewPoints( ws.wire );
        MapWorkspace_SetFacePreview( &ws, distance ); REQUIRE( MapWorkspace_HasFacePreview( &ws ) );
        REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.objects.nCount == 1 );
        CHECK( ws.editPreviewWire.objects.pData[0].id == id ); CHECK( ws.editPreviewWire.faces.nCount == 6 );
        CHECK( ws.editPreviewWire.points.nCount == 8 ); CHECK( ws.editPreviewWire.lines.nCount == 12 );
        auto expected = Box( 0 ).box;
        auto &changed = sign > 0 ? expected.maximum : expected.minimum;
        ( axis == 0 ? changed.x : axis == 1 ? changed.y : changed.z ) += sign * distance;
        CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.bounds.box.minimum, expected.minimum ) );
        CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.bounds.box.maximum, expected.maximum ) );
        const auto *brush = cypher::editor::geometry::GeometryDocument_FindBrush( &document->geometry, { id } ); REQUIRE( brush != nullptr );
        for ( usize i = 0; i < ws.editPreviewWire.faces.nCount; ++i ) {
            const auto &face = ws.editPreviewWire.faces.pData[i]; bool sourceSide = false;
            for ( usize j = 0; j < brush->sides.nCount; ++j ) { sourceSide |= face.sideId == brush->sides.pData[j].sourceId.value; }
            CHECK( sourceSide ); CHECK( face.nIndices == 4 );
        }
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CheckFacePreviewPoints( ws.wire, original );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        const auto candidate = FacePreviewPoints( ws.editPreviewWire ); const auto *cached = ws.editPreviewWire.points.pData;
        MapWorkspace_SetFacePreview( &ws, distance ); CHECK( ws.editPreviewWire.points.pData == cached );
        REQUIRE( MapWorkspace_CommitFacePreview( &ws ) ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
        CheckFacePreviewPoints( ws.wire, candidate ); CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == 2 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckFacePreviewPoints( ws.wire, original );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckFacePreviewPoints( ws.wire, candidate );
        if ( axis == 2 && sign == 1 && distance == 32 ) {
            QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "face_volume.cymap" ) );
            REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
            map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
            map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) ); REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK );
            CheckFacePreviewPoints( wire, candidate );
        }
    } } }
}

TEST_CASE( "A sloped brush face preview reconstructs its intersections instead of shifting AABB corners", "[map][gui][geometry-edit][face-volume-preview][sloped]" )
{
    session_t session; auto &ws = session.workspace; settings_scope_tester_t settings( &session.gui.settings );
    settings.Text( "editor.map.new_brush_shape", "wedge" );
    settings.Text( "editor.map.primitive_axis", "Z" );
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, Box( 0 ) ) ); const u64 id = ws.selection.ids.pData[0];
    const auto *brush = cypher::editor::geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { id } ); REQUIRE( brush != nullptr );
    u64 side = 0; cypher::math::planed_t plane{};
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        const auto candidate = brush->sides.pData[i].plane;
        if ( std::abs( candidate.normal.x ) > 0.1 && std::abs( candidate.normal.z ) > 0.1 ) { side = brush->sides.pData[i].sourceId.value; plane = candidate; }
    }
    REQUIRE( side != 0 ); MapWorkspace_SelectBrushFace( &ws, id, side );
    for ( f64 distance : { -8.0, 8.0 } ) {
        CAPTURE( distance ); MapWorkspace_SetFacePreview( &ws, distance ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        REQUIRE( ws.editPreviewWire.faces.nCount >= 4 );
        const map_wire_face_t *moved = nullptr;
        for ( usize i = 0; i < ws.editPreviewWire.faces.nCount; ++i ) { if ( ws.editPreviewWire.faces.pData[i].sideId == side ) { moved = &ws.editPreviewWire.faces.pData[i]; } }
        REQUIRE( moved != nullptr ); REQUIRE( moved->nIndices >= 3 );
        for ( u32 i = 0; i < moved->nIndices; ++i ) {
            const auto point = ws.editPreviewWire.points.pData[ws.editPreviewWire.faceIndices.pData[moved->iFirstIndex + i]];
            CHECK( cypher::math::Vec3d_Dot( plane.normal, point ) + plane.d == Catch::Approx( distance ).margin( 1e-6 ) );
        }
        const auto candidate = FacePreviewPoints( ws.editPreviewWire ); REQUIRE( MapWorkspace_CommitFacePreview( &ws ) );
        CheckFacePreviewPoints( ws.wire, candidate ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    }
}

TEST_CASE( "A private owned brush preview retains surface identities while publication preserves authored metadata", "[map][gui][geometry-edit][face-volume-preview][metadata]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session; auto &ws = session.workspace; settings_scope_tester_t settings( &session.gui.settings );
    settings.Text( "editor.map.default_material", "materials/dev/owned_preview.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 id = ws.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 256 ) ) ); const u64 other = ws.selection.ids.pData[0];
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
    u64 owner{}; REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "func_detail" ), { 32, 32, 32 }, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, id, StringView_FromCString( "detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, id, owner ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "owned_face.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_layer_store_t *chunk{}; auto *record = MapDocument_FindObject( ws.pDocument, id, &chunk ); REQUIRE( record != nullptr ); REQUIRE( chunk != nullptr );
    auto *extra = KeyValue_ObjectInsert( chunk->store.pDocument, record, StringView_FromCString( "editor_custom_annotation" ), key_value_type_t::STRING );
    REQUIRE( extra != nullptr ); REQUIRE( KeyValue_SetString( chunk->store.pDocument, extra, StringView_FromCString( "quality-pass" ) ) );
    auto *attributes = geo::GeometryDocument_FindBrushAttributesMutable( &ws.pDocument->geometry, { id } ); REQUIRE( attributes != nullptr );
    for ( usize i = 0; i < attributes->records.nCount; ++i ) { attributes->records.pData[i].uvProjection.offset.x = 0.125 * static_cast<f64>( i + 1 ); }
    ++ws.pDocument->geometry.revision; MapWorkspace_DocumentChanged( &ws );
    const u64 side = FacePreviewSide( ws, id, 2, 1 ); MapWorkspace_SelectBrushFace( &ws, id, side );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    MapWorkspace_SetFacePreview( &ws, 16 ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreviewWire.objects.nCount == 1 ); CHECK( ws.editPreviewWire.entities.nCount == 0 );
    CHECK( ws.editPreviewWire.objects.pData[0].id == id ); CHECK( ws.pDocument == document );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    const auto *brush = geo::GeometryDocument_FindBrush( &document->geometry, { id } ); REQUIRE( brush != nullptr );
    for ( usize i = 0; i < ws.editPreviewWire.faces.nCount; ++i ) {
        const auto &face = ws.editPreviewWire.faces.pData[i]; bool matched = false;
        for ( usize j = 0; j < brush->sides.nCount; ++j ) { if ( face.sideId == brush->sides.pData[j].sourceId.value ) {
            CHECK( face.material == attributes->records.pData[brush->sides.pData[j].iAttributeIndex].material.value ); matched = true;
        } }
        CHECK( matched );
    }
    REQUIRE( MapWorkspace_CommitFacePreview( &ws ) );
    const auto checkMetadata = [&]( map_document_t &map ) {
        const auto *placement = MapWireframe_FindObject( ws.wire, id ); REQUIRE( placement != nullptr ); CHECK( placement->owner == owner );
        const auto *store = geo::GeometryDocument_FindBrushAttributes( &map.geometry, { id } ); REQUIRE( store != nullptr );
        for ( usize i = 0; i < store->records.nCount; ++i ) { CHECK( store->records.pData[i].uvProjection.offset.x == Catch::Approx( 0.125 * static_cast<f64>( i + 1 ) ) ); }
        bool placed = false;
        for ( usize i = 0; i < map.geometryRecords.nCount; ++i ) { if ( map.geometryRecords.pData[i].id == id ) {
            CHECK( map.geometryRecords.pData[i].owner == owner ); CHECK( map.geometryRecords.pData[i].iLayer == 1 ); placed = true;
        } }
        CHECK( placed ); string_view_t annotation{};
        REQUIRE( KeyValue_GetString( KeyValue_Find( MapDocument_FindObject( &map, id, nullptr ), StringView_FromCString( "editor_custom_annotation" ) ), &annotation ) );
        CHECK( StringView_Equals( annotation, StringView_FromCString( "quality-pass" ) ) );
        CHECK( map.geometry.brushes.nCount == 2 ); CHECK( geo::GeometryDocument_FindBrush( &map.geometry, { other } ) != nullptr );
    };
    checkMetadata( *ws.pDocument );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK ); checkMetadata( loaded );
}

TEST_CASE( "Invalid brush face previews clear stale solids and cannot publish the last valid distance", "[map][gui][geometry-edit][face-volume-preview][invalid]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0]; MapWorkspace_SelectBrushFace( &ws, id, FacePreviewSide( ws, id, 2, 1 ) );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto original = FacePreviewPoints( ws.wire );
    for ( f64 invalid : { -64.0, -128.0, 1e12, std::numeric_limits<f64>::infinity() } ) {
        CAPTURE( invalid ); MapWorkspace_SetFacePreview( &ws, 16 ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
        MapWorkspace_SetFacePreview( &ws, invalid ); REQUIRE( ws.editPreview.bFacePushPull ); CHECK( ws.editPreview.status != map_status_t::OK );
        CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitFacePreview( &ws ) );
        CHECK( ws.pDocument == document ); CheckFacePreviewPoints( ws.wire, original ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
    MapWorkspace_SetFacePreview( &ws, 0 ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
}

TEST_CASE( "Every private face preview allocation failure preserves the live document and permits a clean retry", "[map][gui][geometry-edit][face-volume-preview][allocation]" )
{
    allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0]; const u64 side = FacePreviewSide( ws, id, 2, 1 ); MapWorkspace_SelectBrushFace( &ws, id, side );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto original = FacePreviewPoints( ws.wire );
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto prepare = [&]() {
        const auto *originalAllocator = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &allocator;
        MapWorkspace_SetFacePreview( &ws, 16 ); ws.pDocument->pAllocator = originalAllocator;
    };
    prepare(); REQUIRE( ws.editPreview.status == map_status_t::OK ); const usize successfulCalls = audit.calls; REQUIRE( successfulCalls > 1 );
    MapWorkspace_ClearEditPreview( &ws ); REQUIRE( audit.live == 0 );
    for ( usize fail = 1; fail <= successfulCalls; ++fail ) {
        CAPTURE( fail ); MapWorkspace_SetFacePreview( &ws, 32 ); REQUIRE( ws.editPreviewWire.faces.nCount != 0 );
        audit.calls = 0; audit.failOn = fail; prepare(); CHECK( audit.calls >= fail ); CHECK( audit.live == 0 );
        CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY ); CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitFacePreview( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CheckFacePreviewPoints( ws.wire, original );
        CHECK( ws.selection.ids.nCount == 1 ); CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        audit.calls = 0; audit.failOn = 0; prepare(); REQUIRE( ws.editPreview.status == map_status_t::OK );
        MapWorkspace_ClearEditPreview( &ws ); CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Shared face previews invalidate when authored or interaction context changes", "[map][gui][geometry-edit][face-volume-preview][context]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0]; const u64 side = FacePreviewSide( ws, id, 2, 1 ); MapWorkspace_SelectBrushFace( &ws, id, side );
    MapWorkspace_SetFacePreview( &ws, 16 ); REQUIRE( MapWorkspace_HasFacePreview( &ws ) );
    SECTION( "Tool change" ) { MapWorkspace_SetTool( &ws, map_tool_t::NONE ); }
    SECTION( "Mode change" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); }
    SECTION( "Selected face change" ) { MapWorkspace_SelectBrushFace( &ws, id, FacePreviewSide( ws, id, 0, 1 ) ); }
    SECTION( "Visibility change" ) { MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE ); }
    SECTION( "Document replacement" ) { REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); }
    SECTION( "Live authored edit" ) { REQUIRE( MapWorkspace_PushPullFace( &ws, 8 ) ); }
    SECTION( "Read only transition" ) { ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); }
    CHECK_FALSE( MapWorkspace_HasFacePreview( &ws ) ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitFacePreview( &ws ) );
}

TEST_CASE( "Failed clipping preview allocation clears staged geometry without publishing the cut", "[map][gui][geometry-edit][clip][preview][allocation]" )
{
    allocation_failure_t failure;
    const allocator_t allocator{ &Allocate, nullptr, &Free, &failure };
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    settings.Text( "editor.map.clip_mode", "back" );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const auto *points = ws.wire.points.pData; const u64 selected = ws.selection.ids.pData[0];
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const cypher::math::planed_t plane{ { 1, 0, 0 }, -32 };
    const auto prepare = [&]() {
        const auto *originalAllocator = ws.pDocument->pAllocator;
        ws.pDocument->pAllocator = &allocator;
        MapWorkspace_SetClipPreview( &ws, plane );
        ws.pDocument->pAllocator = originalAllocator;
    };
    prepare();
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.faces.nCount == 6 );
    const usize successfulCalls = failure.calls;
    REQUIRE( successfulCalls > 1 ); REQUIRE( failure.live > 0 );
    MapWorkspace_ClearEditPreview( &ws ); REQUIRE( failure.live == 0 );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -16 } );
    REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    usize failOn = 1;
    SECTION( "Private preview document allocation" ) {}
    SECTION( "Final retained wireframe allocation" ) { failOn = successfulCalls; }
    failure.calls = 0; failure.failOn = failOn;
    prepare();
    CHECK( failure.calls >= failOn ); CHECK( failure.live == 0 );
    CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.bClip );
    CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == selected );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    failure.calls = 0; failure.failOn = 0;
    prepare();
    CHECK( ws.editPreview.status == map_status_t::OK ); CHECK( ws.editPreviewWire.objects.nCount == 1 );
    CHECK( ws.editPreviewWire.bounds.box.maximum.x == 32 );
    MapWorkspace_ClearEditPreview( &ws ); CHECK( failure.live == 0 );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
}

TEST_CASE( "Invalid clipping planes cannot reuse a previous successful retained solid", "[map][gui][geometry-edit][clip][preview][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
    const u64 selected = ws.selection.ids.pData[0];
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -32 } );
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    MapWorkspace_SetClipPreview( &ws, { { 0, 0, 0 }, 0 } );
    CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.objects.nCount == 0 );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.selection.ids.pData[0] == selected ); CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Programmatic clipping previews cancel when their targets become hidden or selection changes", "[map][gui][geometry-edit][clip][preview][visibility]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 target = ws.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 128 ) ) );
    const u64 other = ws.selection.ids.pData[0];
    MapWorkspace_Select( &ws, target, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    const auto *points = ws.wire.points.pData;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -32 } );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreviewWire.points.nCount != 0 );

    SECTION( "Hiding Brushes invalidates a preview even though its selection is unchanged" ) {
        const auto selectionRevision = ws.selection.revision;
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
        CHECK( ws.selection.revision == selectionRevision );
        CHECK( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == target );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
        CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_FALSE );
    }
    SECTION( "Selecting another visible brush invalidates a preview without a viewport owner" ) {
        MapWorkspace_Select( &ws, other, MAP_SELECT_REPLACE );
        CHECK( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == other );
    }
    CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.objects.nCount == 0 );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.wire.points.pData == points ); CHECK( ws.pDocument->geometry.brushes.nCount == 2 );
    CHECK( MapWireframe_FindObject( ws.wire, target )->bounds.box.maximum.x == 64 );
    CHECK( MapWireframe_FindObject( ws.wire, other )->bounds.box.maximum.x == 192 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
}

TEST_CASE( "Clip guides construct normalized planes along all extrusion axes and publish anchors before notifications", "[map][gui][geometry-edit][clip][guide]" )
{
    CHECK( MapWorkspace_ClipAxis( nullptr ) == 2 );
    for ( u32 axis = 0; axis < 3; ++axis ) {
        CAPTURE( axis ); session_t session; auto &ws = session.workspace; settings_scope_tester_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); settings.Text( "editor.map.clip_mode", "back" );
        settings.Text( "editor.map.clip_axis", axis == 0 ? "x" : axis == 1 ? "y" : "z" ); CHECK( MapWorkspace_ClipAxis( &ws ) == axis );
        MapWorkspace_SetTool( &ws, map_tool_t::CLIP ); const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        const auto guide = ClipGuide( axis );
        struct observation_t { map_workspace_t *ws{}; usize views{}; map_edit_preview_t preview{}; } observation{ &ws };
        const map_listener_fn listener = []( void *context, u32 changes ) {
            auto &seen = *static_cast<observation_t *>( context );
            if ( changes & MAP_CHANGE_VIEW ) { ++seen.views; seen.preview = seen.ws->editPreview; }
        };
        REQUIRE( MapWorkspace_AddListener( &ws, listener, &observation ) );
        MapWorkspace_SetClipGuide( &ws, guide ); MapWorkspace_RemoveListener( &ws, listener, &observation );
        REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreview.clipGuide.bHas );
        CHECK( observation.views == 1 ); CHECK( observation.preview.status == map_status_t::OK ); CHECK( observation.preview.clipGuide.bHas );
        CHECK( observation.preview.clipGuide.extrusionAxis == axis );
        CHECK( cypher::math::Vec3d_EqualsExact( observation.preview.clipGuide.points[0], guide.points[0] ) );
        CHECK( cypher::math::Vec3d_EqualsExact( observation.preview.clipGuide.points[1], guide.points[1] ) );
        const auto plane = ws.editPreview.clipPlane;
        CHECK( cypher::math::Planed_IsNormalized( plane, 1e-8 ) ); CHECK( plane.d == -32 );
        CHECK( plane.normal.x == ( axis == 0 ? 0.0 : 1.0 ) ); CHECK( plane.normal.y == ( axis == 0 ? 1.0 : 0.0 ) ); CHECK( plane.normal.z == 0 );
        CHECK( cypher::math::Vec3d_Dot( plane.normal, guide.points[0] ) + plane.d == Catch::Approx( 0 ).margin( 1e-8 ) );
        CHECK( cypher::math::Vec3d_Dot( plane.normal, guide.points[1] ) + plane.d == Catch::Approx( 0 ).margin( 1e-8 ) );
        REQUIRE( ws.editPreviewWire.objects.nCount == 1 ); CHECK( ws.editPreviewWire.bounds.box.maximum.x == ( axis == 0 ? 64.0 : 32.0 ) );
        CHECK( ws.editPreviewWire.bounds.box.maximum.y == ( axis == 0 ? 32.0 : 64.0 ) );
        CHECK( ws.pDocument == document ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
    // An oblique guide also produces the plane through both anchors, rather
    // than approximating it with a world-aligned cut.
    session_t session; auto &ws = session.workspace; settings_scope_tester_t settings( &session.gui.settings ); settings.Text( "editor.map.clip_mode", "both" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const map_clip_guide_t diagonal{ CY_TRUE, { { 16, 16, 9 }, { 48, 48, 9 } }, 2 };
    MapWorkspace_SetClipGuide( &ws, diagonal ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    CHECK( ws.editPreview.clipPlane.normal.x == Catch::Approx( std::sqrt( 0.5 ) ) ); CHECK( ws.editPreview.clipPlane.normal.y == Catch::Approx( -std::sqrt( 0.5 ) ) );
    CHECK( ws.editPreview.clipPlane.d == Catch::Approx( 0 ).margin( 1e-8 ) ); CHECK( ws.editPreviewWire.objects.nCount == 2 );
}

TEST_CASE( "Clip guide metadata follows mode and material rebuilds while numeric plane changes invalidate anchors", "[map][gui][geometry-edit][clip][guide]" )
{
    session_t session; auto &ws = session.workspace; settings_scope_tester_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP ); const auto guide = ClipGuide( 2 );
    settings.Text( "editor.map.clip_mode", "back" ); MapWorkspace_SetClipGuide( &ws, guide ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    const auto plane = ws.editPreview.clipPlane;
    for ( const char *mode : { "front", "both", "back" } ) {
        settings.Text( "editor.map.clip_mode", mode ); MapWorkspace_SetClipPreview( &ws, plane );
        REQUIRE( ws.editPreview.status == map_status_t::OK ); CHECK( ws.editPreview.clipGuide.bHas );
        CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.clipGuide.points[0], guide.points[0] ) );
        CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.clipGuide.points[1], guide.points[1] ) );
        CHECK( ws.editPreview.clipMode == MapWorkspace_ClipMode( &ws ) );
        CHECK( ws.editPreviewWire.objects.nCount == ( StringView_Equals( StringView_FromCString( mode ), StringView_FromCString( "both" ) ) ? 2u : 1u ) );
    }
    settings.Text( "editor.map.default_material", "materials/dev/guide_cut.cymat" ); MapWorkspace_SetClipPreview( &ws, plane );
    REQUIRE( ws.editPreview.status == map_status_t::OK ); CHECK( ws.editPreview.clipGuide.bHas );
    CHECK( StringView_Equals( StringView_FromCString( ws.editPreview.clipMaterial ), StringView_FromCString( "materials/dev/guide_cut.cymat" ) ) );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -16 } ); REQUIRE( ws.editPreview.status == map_status_t::OK ); CHECK_FALSE( ws.editPreview.clipGuide.bHas );
    CHECK( ws.editPreviewWire.bounds.box.maximum.x == 16 );
    MapWorkspace_SetClipGuide( &ws, guide ); REQUIRE( ws.editPreview.clipGuide.bHas );
    MapWorkspace_SetClipPreview( &ws, { { 0, 1, 0 }, -32 } ); REQUIRE( ws.editPreview.status == map_status_t::OK ); CHECK_FALSE( ws.editPreview.clipGuide.bHas );
    CHECK( ws.editPreviewWire.bounds.box.maximum.y == 32 );
}

TEST_CASE( "Moving clip anchors along an unchanged plane only refreshes presentation metadata", "[map][gui][geometry-edit][clip][guide][cache]" )
{
    allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *originalAllocator = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &allocator;
    MapWorkspace_SetClipGuide( &ws, ClipGuide( 2 ) ); ws.pDocument->pAllocator = originalAllocator;
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( audit.live > 0 );
    const auto *points = ws.editPreviewWire.points.pData; const auto *faces = ws.editPreviewWire.faces.pData; const auto *objects = ws.editPreviewWire.objects.pData;
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    struct observation_t { map_workspace_t *ws{}; usize views{}; map_clip_guide_t latest{}; } observation{ &ws };
    const map_listener_fn listener = []( void *context, u32 changes ) {
        auto &seen = *static_cast<observation_t *>( context ); if ( changes & MAP_CHANGE_VIEW ) { ++seen.views; seen.latest = seen.ws->editPreview.clipGuide; }
    };
    REQUIRE( MapWorkspace_AddListener( &ws, listener, &observation ) );
    const auto moved = ClipGuide( 2, 32, 16, 96, 23 ); audit.calls = 0; audit.failOn = 1; ws.pDocument->pAllocator = &allocator;
    MapWorkspace_SetClipGuide( &ws, moved ); ws.pDocument->pAllocator = originalAllocator;
    CHECK( audit.calls == 0 ); CHECK( ws.editPreview.status == map_status_t::OK ); CHECK( observation.views == 1 );
    CHECK( cypher::math::Vec3d_EqualsExact( observation.latest.points[0], moved.points[0] ) ); CHECK( cypher::math::Vec3d_EqualsExact( observation.latest.points[1], moved.points[1] ) );
    CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.faces.pData == faces ); CHECK( ws.editPreviewWire.objects.pData == objects );
    MapWorkspace_SetClipGuide( &ws, moved ); MapWorkspace_SetClipPreview( &ws, ws.editPreview.clipPlane ); CHECK( observation.views == 1 );
    CHECK( ws.editPreview.clipGuide.bHas ); CHECK( ws.pDocument == document ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    MapWorkspace_RemoveListener( &ws, listener, &observation ); audit.failOn = 0; MapWorkspace_ClearEditPreview( &ws ); CHECK( audit.live == 0 );
}

TEST_CASE( "Invalid clip guides clear stale ghost geometry and cannot masquerade as a canonical plane", "[map][gui][geometry-edit][clip][guide][atomic]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument; const auto *livePoints = ws.wire.points.pData; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    std::vector<map_clip_guide_t> invalid;
    auto guide = ClipGuide( 2 ); guide.bHas = CY_FALSE; invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.extrusionAxis = 3; invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.points[1].z += 1; invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.points[1] = guide.points[0]; invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.points[0].x = std::numeric_limits<double>::quiet_NaN(); invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.points[1].y = std::numeric_limits<double>::infinity(); invalid.push_back( guide );
    guide = ClipGuide( 2 ); guide.points[0].y = -std::numeric_limits<double>::max(); guide.points[1].y = std::numeric_limits<double>::max(); invalid.push_back( guide );
    for ( const auto &bad : invalid ) {
        MapWorkspace_SetClipGuide( &ws, ClipGuide( 2 ) ); REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
        MapWorkspace_SetClipGuide( &ws, bad );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip ); CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT ); CHECK_FALSE( ws.editPreview.clipGuide.bHas );
        CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.clipPlane.normal, {} ) ); CHECK( ws.editPreview.clipPlane.d == 0 );
        CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == livePoints ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Failed guide preparation clears ghost geometry but retains real anchors for an atomic retry", "[map][gui][geometry-edit][clip][guide][allocation]" )
{
    allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto *originalAllocator = ws.pDocument->pAllocator;
    const auto prepare = [&]( const map_clip_guide_t &guide ) {
        ws.pDocument->pAllocator = &allocator; MapWorkspace_SetClipGuide( &ws, guide ); ws.pDocument->pAllocator = originalAllocator;
    };
    const auto guide = ClipGuide( 2 ); prepare( guide ); REQUIRE( ws.editPreview.status == map_status_t::OK ); const usize successfulCalls = audit.calls;
    REQUIRE( successfulCalls > 1 ); MapWorkspace_ClearEditPreview( &ws ); REQUIRE( audit.live == 0 );
    for ( const usize failOn : { usize{ 1 }, successfulCalls } ) {
        CAPTURE( failOn ); MapWorkspace_SetClipGuide( &ws, ClipGuide( 2, 16 ) ); REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
        audit.calls = 0; audit.failOn = failOn; prepare( guide );
        CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 ); CHECK( ws.editPreview.status == map_status_t::OUT_OF_MEMORY );
        CHECK( ws.editPreview.clipGuide.bHas ); CHECK( cypher::math::Vec3d_EqualsExact( ws.editPreview.clipGuide.points[0], guide.points[0] ) );
        CHECK( ws.editPreview.clipPlane.normal.x == 1 ); CHECK( ws.editPreview.clipPlane.normal.z == 0 ); CHECK( ws.editPreview.clipPlane.d == -32 );
        CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK( ws.editPreviewWire.faces.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        audit.calls = 0; audit.failOn = 0; prepare( guide ); REQUIRE( ws.editPreview.status == map_status_t::OK ); CHECK( ws.editPreview.clipGuide.bHas );
        MapWorkspace_ClearEditPreview( &ws ); CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Workspace brush merge retains source materials and publishes one persistent undo step", "[map][gui][geometry-edit][merge][persistence]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    settings.Text( "editor.map.default_material", "materials/dev/merge_stone.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 a = ws.selection.ids.pData[0];
    settings.Text( "editor.map.default_material", "materials/dev/merge_copper.cymat" );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 64 ) ) ); const u64 b = ws.selection.ids.pData[0];
    const u64 operands[]{ b, a }; MapWorkspace_SetSelection( &ws, operands, 2 );
    REQUIRE( MapWorkspace_CanMergeBrushSelection( &ws ) );
    const auto beforeToken = UndoRedo_StateToken( ws.history.pUndo );
    REQUIRE( MapWorkspace_MergeSelection( &ws ) );
    REQUIRE( ws.selection.ids.nCount == 1 ); const u64 merged = ws.selection.ids.pData[0];
    CHECK( merged != a ); CHECK( merged != b ); CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
    CHECK( StringView_Equals( EditorHistory_UndoLabel( &ws.history ), StringView_FromCString( "Merge Brushes" ) ) );
    CHECK_FALSE( UndoRedo_StateTokenEquals( beforeToken, UndoRedo_StateToken( ws.history.pUndo ) ) );
    const auto checkResult = [&]( const map_document_t &document ) {
        const auto *brush = geo::GeometryDocument_FindBrush( &document.geometry, { merged } );
        const auto *attributes = geo::GeometryDocument_FindBrushAttributes( &document.geometry, { merged } );
        REQUIRE( brush != nullptr ); REQUIRE( attributes != nullptr ); REQUIRE( brush->sides.nCount == 6 );
        REQUIRE( geo::BrushAttributes_Covers( brush, attributes ) );
        CHECK( geo::GeometryDocument_FindBrush( &document.geometry, { a } ) == nullptr );
        CHECK( geo::GeometryDocument_FindBrush( &document.geometry, { b } ) == nullptr );
        usize stone = 0, copper = 0;
        for ( usize i = 0; i < brush->sides.nCount; ++i ) {
            const auto &side = brush->sides.pData[i];
            const auto path = MapMaterials_Path( &document.materials, attributes->records.pData[side.iAttributeIndex].material.value );
            stone += StringView_Equals( path, StringView_FromCString( "materials/dev/merge_stone.cymat" ) );
            copper += StringView_Equals( path, StringView_FromCString( "materials/dev/merge_copper.cymat" ) );
        }
        CHECK( stone == 5 ); CHECK( copper == 1 );
    };
    checkResult( *ws.pDocument );
    const auto *object = MapWireframe_FindObject( ws.wire, merged ); REQUIRE( object != nullptr );
    CHECK( object->bounds.box.minimum.x == 0 ); CHECK( object->bounds.box.maximum.x == 128 );
    QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "merged.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    REQUIRE( loaded.geometry.brushes.nCount == 1 ); checkResult( loaded );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.selection.ids.nCount == 2 ); CHECK( ws.selection.ids.pData[0] == a ); CHECK( ws.selection.ids.pData[1] == b );
    CHECK( ws.pDocument->geometry.brushes.nCount == 2 );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 2, a, 0 ); CheckSavedBrush( path, 2, b, 64 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == merged ); checkResult( *ws.pDocument );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, merged, 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
}

TEST_CASE( "Rejected brush merge preserves the live map selection preview and history", "[map][gui][geometry-edit][merge][atomic]" )
{
    for ( bool concave : { false, true } ) {
        CAPTURE( concave ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 a = ws.selection.ids.pData[0];
        auto second = Box( 160 );
        if ( concave ) { second.box = { { 32, 32, 0 }, { 96, 96, 64 } }; }
        REQUIRE( MapWorkspace_CreateBox( &ws, second ) ); const u64 b = ws.selection.ids.pData[0];
        const u64 operands[]{ a, b }; MapWorkspace_SetSelection( &ws, operands, 2 );
        MapWorkspace_SetTool( &ws, map_tool_t::CLIP ); MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -48 } );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
        const auto *selection = ws.selection.ids.pData; const auto *ghost = ws.editPreviewWire.points.pData;
        const auto revision = ws.pDocument->geometry.revision; const auto selectionRevision = ws.selection.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo );
        REQUIRE( MapWorkspace_CanMergeBrushSelection( &ws ) ); CHECK_FALSE( MapWorkspace_MergeSelection( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.pData == ghost );
        CHECK( EditorHistory_StepCount( &ws.history ) == 2 ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Merge preparation allocation failures never publish a partial document or history step", "[map][gui][geometry-edit][merge][allocation][atomic]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    usize successfulCalls = 0;
    const auto prepare = [&]( session_t &session ) {
        auto &ws = session.workspace;
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); const u64 a = ws.selection.ids.pData[0];
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 64 ) ) ); const u64 b = ws.selection.ids.pData[0];
        const u64 ids[]{ a, b }; MapWorkspace_SetSelection( &ws, ids, 2 );
    };
    {
        session_t session; prepare( session );
        const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        const bool merged = MapWorkspace_MergeSelection( &session.workspace ); session.gui.pAllocator = original;
        REQUIRE( merged ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
    }
    REQUIRE( audit.live == 0 );
    for ( usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
        CAPTURE( failOn ); session_t session; prepare( session ); auto &ws = session.workspace;
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData;
        const auto *selection = ws.selection.ids.pData; const auto revision = document->geometry.revision;
        const auto selectionRevision = ws.selection.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        audit.calls = 0; audit.failOn = failOn;
        const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        const bool merged = MapWorkspace_MergeSelection( &ws ); session.gui.pAllocator = original;
        CHECK_FALSE( merged ); CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( ws.pDocument->geometry.brushes.nCount == 2 ); CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        audit.calls = 0; audit.failOn = 0; REQUIRE( MapWorkspace_MergeSelection( &ws ) ); CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
    }
}

TEST_CASE( "Anchored resize publication failures preserve geometry selection and history for retry", "[map][gui][geometry-edit][select-resize][allocation][atomic]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    const cypher::math::vec3d_t factors{ 1.5, 0.75, 1 }, pivot{ 0, 64, 32 };
    usize successfulCalls = 0;
    {
        session_t session; REQUIRE( MapWorkspace_CreateBox( &session.workspace, Box( 0 ) ) );
        const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        const bool resized = MapWorkspace_ScaleSelection( &session.workspace, factors, pivot ); session.gui.pAllocator = original;
        REQUIRE( resized ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
    }
    REQUIRE( audit.live == 0 );
    for ( usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
        CAPTURE( failOn ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
        map_transform_preview_t resize{}; resize.kind = map_transform_preview_kind_t::SCALE;
        resize.bResize = CY_TRUE; resize.factors = factors; resize.pivot = pivot;
        MapWorkspace_SetTransformPreview( &ws, resize ); REQUIRE( ws.editPreview.bActive );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const auto *points = ws.wire.points.pData; const auto *selection = ws.selection.ids.pData;
        const auto selectionRevision = ws.selection.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        const auto preview = ws.editPreview.bounds;
        audit.calls = 0; audit.failOn = failOn;
        const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        const bool resized = MapWorkspace_ScaleSelection( &ws, factors, pivot ); session.gui.pAllocator = original;
        CHECK_FALSE( resized ); CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResize );
        audit.calls = 0; audit.failOn = 0; REQUIRE( MapWorkspace_ScaleSelection( &ws, factors, pivot ) );
        CHECK( EditorHistory_StepCount( &ws.history ) == 2 ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( std::abs( ws.wire.bounds.box.maximum.x - preview.box.maximum.x ) < 1e-6 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( ws.wire.bounds.box.maximum.x == 64 );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        CHECK( std::abs( ws.wire.bounds.box.maximum.x - preview.box.maximum.x ) < 1e-6 );
    }
}

TEST_CASE( "Geometry history snapshots survive saving and unrelated selection changes", "[map][gui][geometry-edit][snapshot]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 first = ws.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 128 ) ) );
    const u64 second = ws.selection.ids.pData[0];
    const auto *brush = cypher::editor::geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { first } );
    REQUIRE( brush != nullptr );
    const u64 side = brush->sides.pData[0].sourceId.value;
    MapWorkspace_SelectBrushFace( &ws, first, side );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 8, 0, 0 } ) );
    MapWorkspace_Select( &ws, second, MAP_SELECT_REPLACE );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "snapshot.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 );
    CHECK( ws.selection.ids.pData[0] == first );
    CHECK( ws.selectedBrushFaceObject == first );
    CHECK( ws.selectedBrushFaceSide == side );
    REQUIRE( MapWireframe_FindObject( ws.wire, first ) != nullptr );
    CHECK( MapWireframe_FindObject( ws.wire, first )->bounds.box.minimum.x == 0 );
    const u64 all[]{ first, second };
    MapWorkspace_SetSelection( &ws, all, 2 );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( ws.selection.ids.nCount == 1 );
    CHECK( ws.selection.ids.pData[0] == first );
    CHECK( ws.selectedBrushFaceObject == first );
    CHECK( ws.selectedBrushFaceSide == side );
    CHECK( MapWireframe_FindObject( ws.wire, first )->bounds.box.minimum.x == 8 );
    // Clearing owned snapshots must preserve the separately owned live map.
    const auto *live = ws.pDocument;
    EditorHistory_Clear( &ws.history );
    CHECK( ws.pDocument == live );
    CHECK( ws.pDocument->geometry.brushes.nCount == 2 );
    CHECK( MapWireframe_FindObject( ws.wire, first ) != nullptr );
}

TEST_CASE( "Mesh source identities survive workspace publication undo redo and reload", "[map][gui][geometry-edit][mesh-components][snapshot][persistence]" )
{
    namespace geo = cypher::editor::geometry;
    session_t session; auto &ws = session.workspace;
    const auto example = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
    REQUIRE( MapWorkspace_Open( &ws, example ).status == map_files_status_t::OK );
    REQUIRE( ws.pDocument->geometry.meshes.nCount != 0u );
    const u64 meshId = ws.pDocument->geometry.meshes.pData[0]->sourceId.value;
    geo::mesh_source_description_t desc{};
    REQUIRE( geo::MeshSourceDescription_Init( &desc, Allocator_GetSystem(), { meshId } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( ws.pDocument->geometry.meshes.pData[0], &desc ) == geo::geometry_status_t::OK );
    std::map<u64, cypher::math::vec3d_t> vertices;
    std::set<u64> faces;
    std::set<std::pair<u64, u64>> edges;
    for ( usize i = 0u; i < desc.vertices.nCount; ++i ) { vertices.emplace( desc.vertices.pData[i].sourceId.value, desc.vertices.pData[i].position ); }
    for ( usize i = 0u; i < desc.faces.nCount; ++i ) {
        const auto &face = desc.faces.pData[i]; faces.insert( face.sourceId.value );
        for ( u32 c = 0u; c < face.cCorners; ++c ) {
            const u64 a = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + c].iVertex].sourceId.value;
            const u64 b = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + ( c + 1u ) % face.cCorners].iVertex].sourceId.value;
            edges.emplace( std::min( a, b ), std::max( a, b ) );
        }
    }
    geo::MeshSourceDescription_Shutdown( &desc );
    REQUIRE( vertices.size() == 4u ); REQUIRE( faces.size() == 1u ); REQUIRE( edges.size() == 4u );
    const auto checkCache = [&]( const map_wireframe_t &wire, cypher::math::vec3d_t delta ) {
        const auto *object = MapWireframe_FindObject( wire, meshId ); REQUIRE( object != nullptr );
        REQUIRE( wire.pointSourceIds.nCount == wire.points.nCount );
        // The example mesh carries a mirror modifier: its evaluated surface
        // adds points with no authored identity (0), faces naming no cage
        // face (copies) and outlines between those points. The authored cage
        // must still come through exactly once, at the moved positions.
        std::set<u64> actualVertices, actualFaces;
        std::set<std::pair<u64, u64>> actualEdges;
        for ( u32 i = 0u; i < object->nPoints; ++i ) {
            const u32 point = object->iFirstPoint + i; const u64 id = wire.pointSourceIds.pData[point];
            if ( id == 0u ) { continue; }
            REQUIRE( vertices.contains( id ) ); CHECK( actualVertices.insert( id ).second );
            const auto original = vertices.at( id ), position = wire.points.pData[point];
            CHECK( position.x == Catch::Approx( original.x + delta.x ) );
            CHECK( position.y == Catch::Approx( original.y + delta.y ) );
            CHECK( position.z == Catch::Approx( original.z + delta.z ) );
        }
        for ( usize i = 0u; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            if ( face.id == meshId && face.faceId != 0u ) { CHECK( face.sideId == 0u ); CHECK( actualFaces.insert( face.faceId ).second ); }
        }
        for ( u32 i = 0u; i < object->nLines; ++i ) {
            const auto &line = wire.lines.pData[object->iFirstLine + i];
            const u64 a = wire.pointSourceIds.pData[line.iA], b = wire.pointSourceIds.pData[line.iB];
            if ( a == 0u && b == 0u ) { continue; }
            CHECK( a < b ); CHECK( actualEdges.emplace( a, b ).second );
        }
        CHECK( actualVertices.size() == vertices.size() ); CHECK( actualFaces == faces ); CHECK( actualEdges == edges );
    };
    checkCache( ws.wire, {} );
    MapWorkspace_Select( &ws, meshId, MAP_SELECT_REPLACE );
    const cypher::math::vec3d_t delta{ 32.0, -16.0, 8.0 };
    REQUIRE( MapWorkspace_TranslateSelection( &ws, delta ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1u ); checkCache( ws.wire, delta );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); checkCache( ws.wire, {} );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkCache( ws.wire, delta );
    QTemporaryDir folder; REQUIRE( folder.isValid() );
    const auto path = folder.filePath( QStringLiteral( "mesh-identities.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); checkCache( ws.wire, delta );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    map_wireframe_t loadedWire{}; REQUIRE( MapWireframe_Init( &loadedWire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &loadedWire, loaded ) == map_status_t::OK ); checkCache( loadedWire, delta );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); checkCache( ws.wire, {} );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkCache( ws.wire, delta );
}

TEST_CASE( "Failed undo and redo preparation preserve the published document and wireframe", "[map][gui][geometry-edit][snapshot][allocation]" )
{
    // This allocator is used only by restored selection/wireframe preparation.
    // Snapshot document clones retain their original allocator, making the
    // second failure a wireframe allocation after the selection was copied.
    allocation_failure_t failure;
    const allocator_t allocator{ &Allocate, nullptr, &Free, &failure };
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 16, 0, 0 } ) );
    bool redo = false;
    usize failOn = 1;
    SECTION( "Undo selection allocation fails" ) {}
    SECTION( "Undo wireframe allocation fails" ) { failOn = 2; }
    SECTION( "Redo selection allocation fails" ) { redo = true; }
    SECTION( "Redo wireframe allocation fails" ) { redo = true; failOn = 2; }
    if ( redo ) { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); }
    const auto *document = ws.pDocument;
    const auto *points = ws.wire.points.pData;
    const auto *objects = ws.wire.objects.pData;
    const auto *selected = ws.selection.ids.pData;
    const auto revision = ws.pDocument->geometry.revision;
    const auto selectionRevision = ws.selection.revision;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const auto x = ws.wire.objects.pData[0].bounds.box.minimum.x;
    failure.failOn = failOn;
    session.gui.pAllocator = &allocator;
    const auto status = redo ? MapWorkspace_Redo( &ws ) : MapWorkspace_Undo( &ws );
    session.gui.pAllocator = Allocator_GetSystem();
    REQUIRE( status == editor_history_status_t::APPLY_FAILED );
    CHECK( failure.calls >= failOn );
    CHECK( failure.live == 0 );
    CHECK( ws.pDocument == document );
    CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.wire.points.pData == points );
    CHECK( ws.wire.objects.pData == objects );
    CHECK( ws.wire.objects.pData[0].bounds.box.minimum.x == x );
    CHECK( ws.selection.ids.pData == selected );
    CHECK( ws.selection.revision == selectionRevision );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    // The same history step remains available and can be retried.
    REQUIRE( ( redo ? MapWorkspace_Redo( &ws ) : MapWorkspace_Undo( &ws ) ) == editor_history_status_t::OK );
    CHECK( ws.wire.objects.pData[0].bounds.box.minimum.x == ( redo ? 16 : 0 ) );
}

TEST_CASE( "Undoing a saved creation removes it from the saved map file", "[map][gui][geometry-edit][snapshot][persistence]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0];
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "undo.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 0 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 0 );
    // The whole level is the one file: nothing is written beside it.
    CHECK( QDir( folder.path() ).entryList( QDir::AllEntries | QDir::NoDotAndDotDot ) == QStringList{ QStringLiteral( "undo.cymap" ) } );
}

TEST_CASE( "Undo and redo across a far move rewrite the one map file without duplicates", "[map][gui][geometry-edit][snapshot][persistence]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0];
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "move.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 9000, 0, 0 } ) );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 9000 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 9000 );
}

TEST_CASE( "Save As writes each destination as its own complete map file", "[map][gui][geometry-edit][snapshot][persistence]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0];
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString a = folder.filePath( QStringLiteral( "a.cymap" ) );
    const QString b = folder.filePath( QStringLiteral( "b.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, a ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 9000, 0, 0 } ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, b ).status == map_files_status_t::OK );
    CHECK( ws.path == b );
    CheckSavedBrush( a, 1, id, 0 );
    CheckSavedBrush( b, 1, id, 9000 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( a, 1, id, 0 );
    CheckSavedBrush( b, 1, id, 0 );
}

TEST_CASE( "A failed save leaves the map on disk unchanged and a retry succeeds", "[map][gui][geometry-edit][snapshot][persistence][save-failure]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0];
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "partial.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 9000, 0, 0 } ) );
    // A directory at the map's path lets the `.tmp` be written but stops the
    // replace, the last step of a save.
    REQUIRE( QFile::remove( path ) );
    REQUIRE( QDir().mkdir( path ) );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::WRITE_FAILED );
    CHECK( QFileInfo( path ).isDir() );
    CHECK_FALSE( QFileInfo::exists( path + QStringLiteral( ".tmp" ) ) );
    CHECK( MapWorkspace_IsModified( &ws ) );
    REQUIRE( QDir().rmdir( path ) );
    REQUIRE( MapWorkspace_Save( &ws ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, id, 9000 );
}

TEST_CASE( "Brush scale uses texture scale lock independently from translation lock", "[map][gui][geometry-edit][texture-lock]" )
{
    session_t session;
    auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings );
    bool scaleLock = false;
    SECTION( "Unlocked scaling retains world texture density" ) {}
    SECTION( "Locked scaling stretches the texture with geometry" ) { scaleLock = true; }
    const auto *descriptor = EditorSettings_Find( &session.gui.settings, StringView_FromCString( "editor.map.texture_scale_lock" ) );
    REQUIRE( descriptor != nullptr );
    setting_value_t value{};
    value.type = setting_type_t::BOOL;
    value.bValue = scaleLock;
    REQUIRE( EditorSettings_Write( &session.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    const u64 id = ws.selection.ids.pData[0];
    const auto *attributes = cypher::editor::geometry::GeometryDocument_FindBrushAttributes( &ws.pDocument->geometry, { id } );
    REQUIRE( attributes != nullptr );
    const auto before = attributes->records.pData[0].uvProjection;
    const cypher::math::vec3d_t point{ before.origin.x + before.uAxis.x * 64, before.origin.y + before.uAxis.y * 64, before.origin.z + before.uAxis.z * 64 };
    cypher::math::vec2d_t uvBefore{};
    REQUIRE( cypher::math::Uvd_TryProjectPlanarPoint( before, point, 0, &uvBefore ) );
    ws.bTextureLock = !scaleLock;
    REQUIRE( MapWorkspace_ScaleSelection( &ws, { 2, 2, 2 }, {} ) );
    attributes = cypher::editor::geometry::GeometryDocument_FindBrushAttributes( &ws.pDocument->geometry, { id } );
    REQUIRE( attributes != nullptr );
    const auto after = attributes->records.pData[0].uvProjection;
    cypher::math::vec2d_t uvAfter{};
    REQUIRE( cypher::math::Uvd_TryProjectPlanarPoint( after, { point.x * 2, point.y * 2, point.z * 2 }, 0, &uvAfter ) );
    CHECK( uvAfter.x == Catch::Approx( scaleLock ? uvBefore.x : uvBefore.x * 2 ) );
    CHECK( after.worldUnitsPerUv.x == Catch::Approx( scaleLock ? before.worldUnitsPerUv.x * 2 : before.worldUnitsPerUv.x ) );
}

TEST_CASE( "Save As through a directory alias writes the same map file", "[map][gui][geometry-edit][snapshot][persistence]" )
{
    session_t session;
    auto &ws = session.workspace;
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString real = folder.filePath( QStringLiteral( "real" ) );
    const QString alias = folder.filePath( QStringLiteral( "alias" ) );
    REQUIRE( QDir().mkpath( real ) );
    std::error_code error;
    std::filesystem::create_directory_symlink( real.toStdString(), alias.toStdString(), error );
    if ( error ) { SKIP( "Directory symlinks are unavailable on this host" ); }
    QString relative = QStringLiteral( "map.cymap" );
    SECTION( "Existing parent directories" ) {}
    SECTION( "Saving creates nested parent directories" ) { relative = QStringLiteral( "new/nested/map.cymap" ); }
    const QString path = QDir( real ).filePath( relative );
    const QString aliasedPath = QDir( alias ).filePath( relative );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 1, ws.selection.ids.pData[0], 0 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_SaveAs( &ws, aliasedPath ).status == map_files_status_t::OK );
    CheckSavedBrush( path, 0 );
}

TEST_CASE( "Four unequal brush and mesh roots resize by equal travel with exact independent previews", "[map][gui][geometry-edit][individual-resize][transform-preview][persistence]" )
{
    for ( int axis = 0; axis < 3; ++axis ) { for ( const f64 side : { -1.0, 1.0 } ) { for ( const bool centered : { false, true } ) {
        CAPTURE( axis, side, centered ); session_t session; auto &ws = session.workspace;
        u64 ids[4]{}; map_bounds_t originals[4]{};
        for ( int i = 0; i < 4; ++i ) {
            originals[i].bHas = CY_TRUE;
            originals[i].box = { { 7.0 + i * 192, 13.0 + i * 144, 19.0 + i * 32 },
                { 71.0 + i * 224, 93.0 + i * 160, 115.0 + i * 48 } };
            REQUIRE( MapWorkspace_CreateBox( &ws, originals[i] ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
            if ( i >= 2 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        }
        MapWorkspace_SetSelection( &ws, ids, 4 );
        const auto coordinate = []( cypher::math::vec3d_t p, int a ) { return a == 0 ? p.x : a == 1 ? p.y : p.z; };
        const auto setCoordinate = []( cypher::math::vec3d_t &p, int a, f64 value ) { if ( a == 0 ) { p.x = value; } else if ( a == 1 ) { p.y = value; } else { p.z = value; } };
        map_transform_preview_t resize{}; resize.kind = map_transform_preview_kind_t::SCALE;
        resize.bResize = resize.bResizeIndividually = CY_TRUE; resize.bResizeFromCenter = centered ? CY_TRUE : CY_FALSE;
        setCoordinate( resize.resizeSides, axis, side ); setCoordinate( resize.delta, axis, side * 64 );
        const auto *document = ws.pDocument; const u64 revision = document->geometry.revision;
        const auto *points = ws.wire.points.pData; const auto *selection = ws.selection.ids.pData;
        const usize steps = EditorHistory_AppliedStepCount( &ws.history );
        allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
        const auto *oldAllocator = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        MapWorkspace_SetTransformPreview( &ws, resize ); session.gui.pAllocator = oldAllocator;
        REQUIRE( ws.editPreview.bActive ); CHECK( audit.calls == 0 ); CHECK( audit.live == 0 );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.wire.points.pData == points ); CHECK( ws.selection.ids.pData == selection );
        std::vector<cypher::math::vec3d_t> expected[4]; map_bounds_t combined{};
        for ( int i = 0; i < 4; ++i ) {
            const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
            const auto affine = MapWorkspace_TransformPreviewObjectAffine( resize, *object );
            map_bounds_t bounds{};
            for ( u32 p = 0; p < object->nPoints; ++p ) {
                const auto point = cypher::math::Affine3d_TransformPoint( affine, ws.wire.points.pData[object->iFirstPoint + p] );
                expected[i].push_back( point ); MapBounds_AddPoint( bounds, point );
            }
            for ( int a = 0; a < 3; ++a ) {
                const f64 lower = coordinate( originals[i].box.minimum, a ), upper = coordinate( originals[i].box.maximum, a );
                const f64 dmin = a == axis && ( centered || side < 0 ) ? -64.0 : 0.0;
                const f64 dmax = a == axis && ( centered || side > 0 ) ? 64.0 : 0.0;
                CHECK( coordinate( bounds.box.minimum, a ) == Catch::Approx( lower + dmin ).margin( 1e-8 ) );
                CHECK( coordinate( bounds.box.maximum, a ) == Catch::Approx( upper + dmax ).margin( 1e-8 ) );
            }
            MapBounds_AddBounds( combined, bounds );
        }
        CHECK( ws.editPreview.bounds.box.minimum.x == Catch::Approx( combined.box.minimum.x ) );
        CHECK( ws.editPreview.bounds.box.maximum.y == Catch::Approx( combined.box.maximum.y ) );
        REQUIRE( MapWorkspace_ResizeSelection( &ws, resize.resizeSides, resize.delta, centered ) );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); CHECK_FALSE( ws.editPreview.bActive );
        const auto verify = [&]() {
            for ( int i = 0; i < 4; ++i ) {
                const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
                REQUIRE( object->nPoints == expected[i].size() );
                for ( u32 p = 0; p < object->nPoints; ++p ) {
                    const auto actual = ws.wire.points.pData[object->iFirstPoint + p], target = expected[i][p];
                    CHECK( actual.x == Catch::Approx( target.x ).margin( 1e-8 ) );
                    CHECK( actual.y == Catch::Approx( target.y ).margin( 1e-8 ) );
                    CHECK( actual.z == Catch::Approx( target.z ).margin( 1e-8 ) );
                }
                CHECK( MapWorkspace_IsSelected( &ws, ids[i] ) );
            }
        };
        verify(); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        for ( int i = 0; i < 4; ++i ) {
            const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
            CHECK( object->bounds.box.minimum.x == Catch::Approx( originals[i].box.minimum.x ) );
            CHECK( object->bounds.box.maximum.z == Catch::Approx( originals[i].box.maximum.z ) );
        }
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); verify();
        if ( axis == 0 && side > 0 && !centered ) {
            QTemporaryDir temporary; REQUIRE( temporary.isValid() ); const QString path = temporary.path() + QStringLiteral( "/four-walls.cymap" );
            REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
            session_t restored; REQUIRE( MapWorkspace_Open( &restored.workspace, path ).status == map_files_status_t::OK );
            for ( int i = 0; i < 4; ++i ) {
                const auto *object = MapWireframe_FindObject( restored.workspace.wire, ids[i] ); REQUIRE( object != nullptr );
                CHECK( object->bounds.box.minimum.x == Catch::Approx( originals[i].box.minimum.x ) );
                CHECK( object->bounds.box.maximum.x == Catch::Approx( originals[i].box.maximum.x + 64 ) );
            }
        }
    } } }
}

TEST_CASE( "Individual resize preparation failures retain every root and the shared preview for retry", "[map][gui][geometry-edit][individual-resize][allocation][atomic]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    const auto setup = []( map_workspace_t &ws ) {
        u64 ids[4]{};
        for ( int i = 0; i < 4; ++i ) { REQUIRE( MapWorkspace_CreateBox( &ws, Box( i * 128 ) ) ); ids[i] = EditorSelection_At( &ws.selection, 0 ); }
        MapWorkspace_SetSelection( &ws, ids, 4 );
    };
    usize successfulCalls = 0;
    { session_t session; setup( session.workspace ); const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
      const bool success = MapWorkspace_ResizeSelection( &session.workspace, { 1, 0, 0 }, { 32, 0, 0 }, false );
      session.gui.pAllocator = original; REQUIRE( success ); successfulCalls = audit.calls; }
    REQUIRE( audit.live == 0 ); REQUIRE( successfulCalls > 2 );
    for ( const usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
        CAPTURE( failOn ); session_t session; auto &ws = session.workspace; setup( ws );
        map_transform_preview_t resize{}; resize.kind = map_transform_preview_kind_t::SCALE;
        resize.bResize = resize.bResizeIndividually = CY_TRUE; resize.resizeSides = { 1, 0, 0 }; resize.delta = { 32, 0, 0 };
        MapWorkspace_SetTransformPreview( &ws, resize ); REQUIRE( ws.editPreview.bActive );
        const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData; const auto *selection = ws.selection.ids.pData;
        const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo );
        audit.calls = 0; audit.failOn = failOn; const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
        const bool success = MapWorkspace_ResizeSelection( &ws, { 1, 0, 0 }, { 32, 0, 0 }, false ); session.gui.pAllocator = original;
        CHECK_FALSE( success ); CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.wire.points.pData == points );
        CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResizeIndividually );
        audit.calls = 0; audit.failOn = 0; REQUIRE( MapWorkspace_ResizeSelection( &ws, { 1, 0, 0 }, { 32, 0, 0 }, false ) );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == 5 ); CHECK_FALSE( ws.editPreview.bActive );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    }
}


namespace
{
namespace history_geo = cypher::editor::geometry;
u64 HistoryQuad( map_workspace_t &ws )
{
    auto desc = MapWorkspace_PrimitiveDefaults( &ws, Box( 0 ) );
    desc.kind = map_primitive_kind_t::QUAD; desc.axis = 2;
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, desc ) );
    REQUIRE( ws.selection.ids.nCount == 1 );
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    return ws.selection.ids.pData[0];
}
std::vector<history_geo::mesh_edge_ref_t> HistoryEdges( const map_workspace_t &ws, u64 id )
{
    std::vector<history_geo::mesh_edge_ref_t> edges;
    const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
    for ( u32 i = 0; i < object->nLines; ++i ) {
        const auto &line = ws.wire.lines.pData[object->iFirstLine + i];
        edges.push_back( history_geo::MeshEdgeRef_Make( { ws.wire.pointSourceIds.pData[line.iA] }, { ws.wire.pointSourceIds.pData[line.iB] } ) );
    }
    REQUIRE( !edges.empty() );
    return edges;
}
std::vector<history_geo::geometry_source_id_t> HistoryVertices( const map_workspace_t &ws, u64 id )
{
    std::vector<history_geo::geometry_source_id_t> vertices;
    const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
    for ( u32 i = 0; i < object->nPoints; ++i ) { vertices.push_back( { ws.wire.pointSourceIds.pData[object->iFirstPoint + i] } ); }
    std::sort( vertices.begin(), vertices.end(), []( auto a, auto b ) { return a.value < b.value; } );
    return vertices;
}
void HistoryCheckEdges( const map_workspace_t &ws, u64 mesh, const std::vector<history_geo::mesh_edge_ref_t> &expected,
    history_geo::mesh_edge_ref_t seed )
{
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == mesh );
    CHECK( ws.meshSelection.meshId.value == mesh ); REQUIRE( ws.meshSelection.edges.nCount == expected.size() );
    for ( const auto edge : expected ) { CHECK( history_geo::MeshSelection_HasEdge( &ws.meshSelection, edge ) ); }
    CHECK( ws.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( ws.selectedMeshEdgeSeed.b.value == seed.b.value );
    CHECK( ws.meshSelection.vertices.nCount == 0 ); CHECK( ws.selectedMeshFaceObject == 0 );
    CHECK( MapWorkspace_HasMeshEdges( &ws ) );
}
struct history_extend_t {
    u64 mesh{};
    history_geo::mesh_edge_ref_t edge{};
    history_geo::mesh_selection_t next{};
    explicit history_extend_t( u64 id, history_geo::mesh_edge_ref_t source ) : mesh( id ), edge( source )
    {
        REQUIRE( history_geo::MeshSelection_Init( &next, Allocator_GetSystem(), { mesh } ) == history_geo::geometry_status_t::OK );
    }
    static map_status_t Apply( void *context, map_document_t *copy, vector_t<u64> *roots )
    {
        auto &self = *static_cast<history_extend_t *>( context );
        if ( !Vector_PushBack( roots, self.mesh ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapTopologyEdit_ExtendEdgesBy( copy, self.mesh, { &self.edge, 1 }, { 0, 0, 32 }, &self.next.edges );
    }
};
struct history_move_t {
    u64 mesh{}; span_t<const history_geo::geometry_source_id_t> vertices{};
    static map_status_t Apply( void *context, map_document_t *copy, vector_t<u64> *roots )
    {
        const auto &self = *static_cast<const history_move_t *>( context );
        if ( !Vector_PushBack( roots, self.mesh ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapTopologyEdit_MoveVertices( copy, self.mesh, self.vertices, { 16, 0, 0 } );
    }
};
struct history_published_t {
    const map_document_t *document;
    const cypher::math::vec3d_t *wire;
    const u64 *roots;
    u64 revision, selectionRevision, nextId;
    undo_state_token_t token;
    usize applied, steps;
    explicit history_published_t( const map_workspace_t &ws ) : document( ws.pDocument ), wire( ws.wire.points.pData ),
        roots( ws.selection.ids.pData ), revision( ws.pDocument->geometry.revision ), selectionRevision( ws.selection.revision ),
        nextId( ws.pDocument->nextId ), token( UndoRedo_StateToken( ws.history.pUndo ) ),
        applied( EditorHistory_AppliedStepCount( &ws.history ) ), steps( EditorHistory_StepCount( &ws.history ) ) {}
    void Check( const map_workspace_t &ws ) const
    {
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == wire ); CHECK( ws.selection.ids.pData == roots );
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( ws.pDocument->nextId == nextId ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
};
}

TEST_CASE( "History owns old boundary and new extension rim before publication", "[map][gui][component-history][snapshot]" )
{
    session_t session; auto &ws = session.workspace;
    const u64 id = HistoryQuad( ws ); const auto old = HistoryEdges( ws, id ).front();
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, old, MAP_SELECT_REPLACE ) );
    history_extend_t edit( id, old );
    map_prepared_component_selection_t result{ &edit.next };
    const usize steps = EditorHistory_AppliedStepCount( &ws.history );
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Extend boundary", &history_extend_t::Apply, &edit, &result ) );
    REQUIRE( edit.next.edges.nCount == 1 ); const auto rim = edit.next.edges.pData[0];
    CHECK( rim.a.value != old.a.value ); CHECK( rim.b.value != old.b.value );
    HistoryCheckEdges( ws, id, { rim }, rim );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 );
    // Mutate/destroy the borrowed tool output; history and live selection own
    // independent storage, including the after-rim produced inside the callback.
    history_geo::MeshSelection_Shutdown( &edit.next );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); HistoryCheckEdges( ws, id, { old }, old );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); HistoryCheckEdges( ws, id, { rim }, rim );
    history_extend_t continuation( id, ws.meshSelection.edges.pData[0] );
    map_prepared_component_selection_t next{ &continuation.next };
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Continue boundary", &history_extend_t::Apply, &continuation, &next ) );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 2 );
    REQUIRE( continuation.next.edges.nCount == 1 );
    HistoryCheckEdges( ws, id, { continuation.next.edges.pData[0] }, continuation.next.edges.pData[0] );
}

TEST_CASE( "Component history retains full sets seeds active faces and projected vertex columns", "[map][gui][component-history][snapshot]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    const u64 id = ws.selection.ids.pData[0]; const auto vertices = HistoryVertices( ws, id );
    history_move_t edit{ id, { vertices.data(), vertices.size() } };
    SECTION( "Selected edges and nonfirst loop seed" ) {
        const auto edges = HistoryEdges( ws, id ); REQUIRE( edges.size() > 2 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[0], MAP_SELECT_REPLACE ) );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[2], MAP_SELECT_ADD ) );
        REQUIRE( MapWorkspace_ApplyEdit( &ws, "Move selected edges", &history_move_t::Apply, &edit ) );
        HistoryCheckEdges( ws, id, { edges[0], edges[2] }, edges[2] );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[1], MAP_SELECT_REPLACE ) );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); HistoryCheckEdges( ws, id, { edges[0], edges[2] }, edges[2] );
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[1], MAP_SELECT_REPLACE ) );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); HistoryCheckEdges( ws, id, { edges[0], edges[2] }, edges[2] );
    }
    SECTION( "Vertex set and column axis are restamped with the new selection revision" ) {
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
        REQUIRE( MapWorkspace_SetMeshComponents( &ws, id, { vertices.data(), 3 }, {} ) );
        ws.meshVertexColumnAxis = 2; ws.meshVertexColumnRevision = ws.selection.revision;
        const auto check = [&] {
            REQUIRE( ws.meshSelection.vertices.nCount == 3 ); CHECK( ws.meshSelection.meshId.value == id );
            for ( usize i = 0; i < 3; ++i ) { CHECK( ws.meshSelection.vertices.pData[i].value == vertices[i].value ); }
            CHECK( ws.meshVertexColumnAxis == 2 ); CHECK( ws.meshVertexColumnRevision == ws.selection.revision );
            CHECK( MapWorkspace_HasMeshVertices( &ws ) );
        };
        REQUIRE( MapWorkspace_ApplyEdit( &ws, "Move vertex column", &history_move_t::Apply, &edit ) ); check();
        REQUIRE( MapWorkspace_SelectMeshVertex( &ws, id, vertices.back(), MAP_SELECT_REPLACE ) );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); check();
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); check();
    }
    SECTION( "Complete face set and active face replace unrelated later highlights" ) {
        std::vector<u64> faces;
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { if ( ws.wire.faces.pData[i].id == id ) { faces.push_back( ws.wire.faces.pData[i].faceId ); } }
        REQUIRE( faces.size() == 6 ); std::sort( faces.begin(), faces.end() );
        REQUIRE( MapWorkspace_SetMeshFaces( &ws, id, { faces.data(), 3 }, faces[1] ) );
        const auto check = [&] {
            CHECK( ws.selectedMeshFaceObject == id ); CHECK( ws.selectedMeshFaceId == faces[1] );
            const auto selected = MapWorkspace_MeshFaces( &ws ); REQUIRE( selected.nCount == 3 );
            for ( usize i = 0; i < 3; ++i ) { CHECK( selected.pData[i] == faces[i] ); }
            CHECK( ws.meshSelection.vertices.nCount == 0 ); CHECK( ws.meshSelection.edges.nCount == 0 );
        };
        REQUIRE( MapWorkspace_ApplyEdit( &ws, "Move selected faces", &history_move_t::Apply, &edit ) ); check();
        REQUIRE( MapWorkspace_SetMeshFaces( &ws, id, { faces.data() + 3, 3 }, faces[5] ) );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); check();
        REQUIRE( MapWorkspace_SetMeshFaces( &ws, id, { faces.data() + 3, 3 }, faces[4] ) );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); check();
    }
}

TEST_CASE( "Component history respects the current mode and session layer locks", "[map][gui][component-history][snapshot]" )
{
    session_t session; auto &ws = session.workspace; const u64 id = HistoryQuad( ws );
    const auto vertices = HistoryVertices( ws, id ); const auto edges = HistoryEdges( ws, id );
    REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "history" ), StringView_FromCString( "History" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, id, StringView_FromCString( "history" ) ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[0], MAP_SELECT_REPLACE ) );
    history_move_t edit{ id, { vertices.data(), vertices.size() } };
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Move edge context", &history_move_t::Apply, &edit ) );
    bool locked = false;
    SECTION( "Objects mode stays Objects with no component highlights" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); }
    SECTION( "Vertices mode cannot revive an old edge selection" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); }
    SECTION( "Current locked layer clears the complete restored root and components" ) { locked = true; REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "history" ), true ) ); }
    const auto mode = ws.elementMode; const auto tool = ws.tool;
    for ( const bool redo : { false, true } ) {
        REQUIRE( ( redo ? MapWorkspace_Redo( &ws ) : MapWorkspace_Undo( &ws ) ) == editor_history_status_t::OK );
        CHECK( ws.elementMode == mode ); CHECK( ws.tool == tool );
        CHECK( ws.meshSelection.edges.nCount == 0 ); CHECK( ws.meshSelection.vertices.nCount == 0 );
        CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.meshFaceSet.nCount == 0 );
        CHECK( ws.selection.ids.nCount == ( locked ? 0 : 1 ) );
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
        CHECK( object->bounds.box.minimum.x == Catch::Approx( redo ? 16 : 0 ) );
    }
    // Snapshots retain their original IDs even when a replay was incompatible.
    if ( locked ) { REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "history" ), false ) ); }
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); HistoryCheckEdges( ws, id, { edges[0] }, edges[0] );
}

TEST_CASE( "Invalid prepared component results refuse the authored edit before history publication", "[map][gui][component-history][atomic]" )
{
    session_t session; auto &ws = session.workspace; const u64 id = HistoryQuad( ws );
    const auto vertices = HistoryVertices( ws, id ); const auto edges = HistoryEdges( ws, id );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, edges[0], MAP_SELECT_REPLACE ) );
    history_move_t edit{ id, { vertices.data(), vertices.size() } };
    history_geo::mesh_selection_t next{};
    REQUIRE( history_geo::MeshSelection_Init( &next, Allocator_GetSystem(), { id } ) == history_geo::geometry_status_t::OK );
    REQUIRE( history_geo::MeshSelection_TryAddEdge( &next, edges[0] ) == history_geo::geometry_status_t::OK );
    map_prepared_component_selection_t result{ &next };
    SECTION( "Missing persistent endpoint" ) { next.edges.pData[0].b.value = CY_U64_MAX - 1; }
    SECTION( "Different root" ) { next.meshId.value = CY_U64_MAX - 1; }
    SECTION( "Wrong component category" ) {
        Vector_Clear( &next.edges ); REQUIRE( history_geo::MeshSelection_TryAddVertex( &next, vertices[0] ) == history_geo::geometry_status_t::OK );
    }
    SECTION( "Noncanonical repeated edge" ) { REQUIRE( Vector_PushBack( &next.edges, edges[0] ) ); }
    SECTION( "Missing borrowed output" ) { result.pMeshSelection = nullptr; }
    const history_published_t before( ws );
    CHECK_FALSE( MapWorkspace_ApplyEdit( &ws, "Invalid selection", &history_move_t::Apply, &edit, &result ) );
    before.Check( ws ); HistoryCheckEdges( ws, id, { edges[0] }, edges[0] );
    CHECK( ws.lastEdit.status == map_status_t::INVALID_ARGUMENT );
    result.pMeshSelection = nullptr;
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Retry valid selection", &history_move_t::Apply, &edit ) );
    HistoryCheckEdges( ws, id, { edges[0] }, edges[0] );
}

TEST_CASE( "Selection snapshot allocation failures leave map components and history exact for retry", "[map][gui][component-history][allocation][atomic]" )
{
    usize allocationCount = 0;
    {
        allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
        {
            session_t session; auto &ws = session.workspace; const u64 id = HistoryQuad( ws );
            const auto vertices = HistoryVertices( ws, id ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
            REQUIRE( MapWorkspace_SetMeshComponents( &ws, id, { vertices.data(), 2 }, {} ) );
            ws.meshVertexColumnAxis = 2; ws.meshVertexColumnRevision = ws.selection.revision;
            history_move_t edit{ id, { vertices.data(), vertices.size() } };
            session.gui.pAllocator = &allocator;
            REQUIRE( MapWorkspace_ApplyEdit( &ws, "Probe context allocations", &history_move_t::Apply, &edit ) );
            allocationCount = audit.calls;
        }
        CHECK( audit.live == 0 );
    }
    REQUIRE( allocationCount >= 3 );
    // Fail the final after-root copy and the before/after vertex-set copies,
    // after the candidate wire and its live component selection are ready.
    for ( usize failOn = allocationCount - 2; failOn <= allocationCount; ++failOn ) {
        CAPTURE( failOn, allocationCount ); allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
        {
            session_t session; auto &ws = session.workspace; const u64 id = HistoryQuad( ws );
            const auto vertices = HistoryVertices( ws, id ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
            REQUIRE( MapWorkspace_SetMeshComponents( &ws, id, { vertices.data(), 2 }, {} ) );
            ws.meshVertexColumnAxis = 2; ws.meshVertexColumnRevision = ws.selection.revision;
            history_move_t edit{ id, { vertices.data(), vertices.size() } }; const history_published_t before( ws );
            const auto *components = ws.meshSelection.vertices.pData;
            session.gui.pAllocator = &allocator; audit.failOn = failOn;
            CHECK_FALSE( MapWorkspace_ApplyEdit( &ws, "Fail context allocation", &history_move_t::Apply, &edit ) );
            CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 ); before.Check( ws );
            CHECK( ws.meshSelection.vertices.pData == components ); CHECK( ws.meshSelection.vertices.nCount == 2 );
            CHECK( ws.meshVertexColumnAxis == 2 ); CHECK( ws.meshVertexColumnRevision == ws.selection.revision );
            audit.failOn = 0;
            REQUIRE( MapWorkspace_ApplyEdit( &ws, "Retry context allocation", &history_move_t::Apply, &edit ) );
            CHECK( ws.meshSelection.vertices.nCount == 2 );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            CHECK( ws.meshSelection.vertices.nCount == 2 );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Undo and redo component allocation refusal preserves live continuation state", "[map][gui][component-history][allocation][snapshot]" )
{
    allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    {
        session_t session; auto &ws = session.workspace; const u64 id = HistoryQuad( ws ); const auto old = HistoryEdges( ws, id )[0];
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, id, old, MAP_SELECT_REPLACE ) );
        history_extend_t edit( id, old ); map_prepared_component_selection_t result{ &edit.next };
        REQUIRE( MapWorkspace_ApplyEdit( &ws, "Extend for restore refusal", &history_extend_t::Apply, &edit, &result ) );
        const auto rim = edit.next.edges.pData[0];
        bool redo = false;
        SECTION( "Undo context clone" ) {}
        SECTION( "Redo context clone" ) { redo = true; }
        session.gui.pAllocator = &allocator;
        // Measure a complete restore, return to the same state, then fail its
        // final component copy. All successful transfers use this outer audit.
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        const usize undoCalls = audit.calls; audit.calls = 0;
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        const usize redoCalls = audit.calls;
        if ( redo ) { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); }
        const usize live = audit.live; const history_published_t before( ws ); const auto *components = ws.meshSelection.edges.pData;
        audit.calls = 0; audit.failOn = redo ? redoCalls : undoCalls;
        CHECK( ( redo ? MapWorkspace_Redo( &ws ) : MapWorkspace_Undo( &ws ) ) == editor_history_status_t::APPLY_FAILED );
        CHECK( audit.calls >= audit.failOn ); CHECK( audit.live == live ); before.Check( ws );
        CHECK( ws.meshSelection.edges.pData == components ); HistoryCheckEdges( ws, id, { redo ? old : rim }, redo ? old : rim );
        audit.failOn = 0;
        REQUIRE( ( redo ? MapWorkspace_Redo( &ws ) : MapWorkspace_Undo( &ws ) ) == editor_history_status_t::OK );
        HistoryCheckEdges( ws, id, { redo ? rim : old }, redo ? rim : old );
    }
    CHECK( audit.live == 0 );
}

namespace
{
struct region_description_t {
    history_geo::mesh_source_description_t value{};
    region_description_t( const map_workspace_t &ws, u64 mesh )
    {
        REQUIRE( history_geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), { mesh } ) == history_geo::geometry_status_t::OK );
        const auto *source = history_geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { mesh } ); REQUIRE( source != nullptr );
        REQUIRE( history_geo::MeshSource_TryDescribe( source, &value ) == history_geo::geometry_status_t::OK );
    }
    ~region_description_t() { history_geo::MeshSourceDescription_Shutdown( &value ); }
};
u64 RegionTwoQuads( map_workspace_t &ws, std::vector<u64> &faces )
{
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &ws.pGui->commands ) == command_registry_status_t::OK );
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { 0, 0, 0 }, { 128, 64, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); REQUIRE( ws.selection.ids.nCount == 1 );
    const u64 mesh = ws.selection.ids.pData[0];
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    u64 face = 0;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { if ( ws.wire.faces.pData[i].id == mesh ) { face = ws.wire.faces.pData[i].faceId; } }
    REQUIRE( face != 0 ); MapWorkspace_SelectMeshFace( &ws, mesh, face );
    REQUIRE( MapWorkspace_QuadSliceMeshFace( &ws, 2, 1 ) );
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { if ( ws.wire.faces.pData[i].id == mesh ) { faces.push_back( ws.wire.faces.pData[i].faceId ); } }
    std::sort( faces.begin(), faces.end() ); REQUIRE( faces.size() == 2 );
    REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    return mesh;
}
void RegionCheckSelection( const map_workspace_t &ws, u64 mesh, const std::vector<u64> &faces )
{
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == mesh );
    CHECK( ws.selectedMeshFaceObject == mesh ); CHECK( ws.selectedMeshFaceId == faces[1] );
    const auto selected = MapWorkspace_MeshFaces( &ws ); REQUIRE( selected.nCount == faces.size() );
    for ( usize i = 0; i < selected.nCount; ++i ) { CHECK( selected.pData[i] == faces[i] ); }
}
}

TEST_CASE( "Numeric Extrude raises the full connected face region with one history step", "[map][gui][geometry-edit][region-extrude][component-history]" )
{
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings ); settings.Real( "editor.map.mesh_extrude_distance", 16 );
    std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
    region_description_t before( ws, mesh ); REQUIRE( before.value.faces.nCount == 2 ); REQUIRE( before.value.vertices.nCount == 6 );
    const usize steps = EditorHistory_AppliedStepCount( &ws.history );
    REQUIRE( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.mesh.extrude" ) ) & COMMAND_STATE_ENABLED ) != 0 );
    REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.extrude" ), {} ) == command_result_t::OK );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); RegionCheckSelection( ws, mesh, faces );
    region_description_t after( ws, mesh ); REQUIRE( after.value.faces.nCount == 8 ); REQUIRE( after.value.vertices.nCount == 12 );
    usize caps = 0, walls = 0;
    for ( usize f = 0; f < after.value.faces.nCount; ++f ) {
        const auto &face = after.value.faces.pData[f]; REQUIRE( face.cCorners == 4 );
        const auto old = std::find_if( before.value.faces.pData, before.value.faces.pData + before.value.faces.nCount,
            [&]( const auto &source ) { return source.sourceId.value == face.sourceId.value; } );
        if ( old != before.value.faces.pData + before.value.faces.nCount ) {
            ++caps; CHECK( face.attributes.material.value == old->attributes.material.value );
            for ( u32 c = 0; c < face.cCorners; ++c ) {
                const auto &corner = after.value.corners.pData[face.iFirstCorner + c];
                const auto &p = after.value.vertices.pData[corner.iVertex].position;
                CHECK( p.z == Catch::Approx( 16 ) );
                usize matching = 0;
                for ( u32 k = 0; k < old->cCorners; ++k ) {
                    const auto &sourceCorner = before.value.corners.pData[old->iFirstCorner + k];
                    const auto &sourcePosition = before.value.vertices.pData[sourceCorner.iVertex].position;
                    if ( std::fabs( p.x - sourcePosition.x ) > 1e-6 || std::fabs( p.y - sourcePosition.y ) > 1e-6 ) { continue; }
                    ++matching;
                    CHECK( corner.attributes.uv0.x == sourceCorner.attributes.uv0.x ); CHECK( corner.attributes.uv0.y == sourceCorner.attributes.uv0.y );
                    CHECK( corner.attributes.uv1.x == sourceCorner.attributes.uv1.x ); CHECK( corner.attributes.uv1.y == sourceCorner.attributes.uv1.y );
                    CHECK( corner.attributes.colorRgba == sourceCorner.attributes.colorRgba );
                }
                CHECK( matching == 1 );
            }
        } else {
            ++walls;
            bool allOnInternalSeam = true; usize bottom = 0, top = 0;
            for ( u32 c = 0; c < face.cCorners; ++c ) {
                const auto &p = after.value.vertices.pData[after.value.corners.pData[face.iFirstCorner + c].iVertex].position;
                allOnInternalSeam = allOnInternalSeam && std::fabs( p.x - 64 ) < 1e-6;
                bottom += std::fabs( p.z ) < 1e-6; top += std::fabs( p.z - 16 ) < 1e-6;
            }
            CHECK_FALSE( allOnInternalSeam ); CHECK( bottom == 2 ); CHECK( top == 2 );
        }
    }
    CHECK( caps == 2 ); CHECK( walls == 6 );
    // Undo/Redo must carry both caps and the non-first active face, even if
    // the user picks a different face after the operation.
    MapWorkspace_SelectMeshFace( &ws, mesh, faces[0] );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); RegionCheckSelection( ws, mesh, faces );
    { region_description_t restored( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &before.value, &restored.value ) ); }
    MapWorkspace_SelectMeshFace( &ws, mesh, faces[0] );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); RegionCheckSelection( ws, mesh, faces );
    { region_description_t restored( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &after.value, &restored.value ) ); }
}

TEST_CASE( "Noncoplanar numeric region Extrude refuses the whole selection", "[map][gui][geometry-edit][region-extrude][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
    REQUIRE( MapWorkspace_CreateBox( &ws, Box( 0 ) ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    const u64 mesh = ws.selection.ids.pData[0]; MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    std::vector<u64> faces;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &face = ws.wire.faces.pData[i];
        if ( face.id == mesh && ( face.normal.z > 0.99 || face.normal.x > 0.99 ) ) { faces.push_back( face.faceId ); }
    }
    std::sort( faces.begin(), faces.end() ); REQUIRE( faces.size() == 2 );
    REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    const history_published_t published( ws ); region_description_t before( ws, mesh ); const auto *set = ws.meshFaceSet.pData;
    CHECK( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.extrude" ), {} ) == command_result_t::FAILED );
    published.Check( ws ); RegionCheckSelection( ws, mesh, faces ); CHECK( ws.meshFaceSet.pData == set );
    { region_description_t unchanged( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
}

TEST_CASE( "Numeric Corner Inset disables multiple faces in API command and panel", "[map][gui][geometry-edit][region-extrude][inset][tool-properties]" )
{
    session_t session; auto &ws = session.workspace; std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); REQUIRE( panel != nullptr );
    auto *controls = panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) ); REQUIRE( controls != nullptr );
    auto *inset = controls->findChild<QPushButton *>( QStringLiteral( "map.mesh.inset" ) ); REQUIRE( inset != nullptr );
    auto *extrude = controls->findChild<QPushButton *>( QStringLiteral( "map.mesh.extrude" ) ); REQUIRE( extrude != nullptr );
    CHECK_FALSE( inset->isEnabled() ); CHECK( extrude->isEnabled() );
    const history_published_t published( ws );
    CHECK( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.mesh.inset" ) ) & COMMAND_STATE_ENABLED ) == 0 );
    CHECK( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.inset" ), {} ) == command_result_t::DISABLED );
    CHECK_FALSE( MapWorkspace_InsetMeshFace( &ws, 8 ) ); published.Check( ws ); RegionCheckSelection( ws, mesh, faces );
    MapWorkspace_SelectMeshFace( &ws, mesh, faces[0] ); QCoreApplication::processEvents();
    // Single-face inset retains its existing command and control workflow.
    CHECK( inset->isEnabled() );
    REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.inset" ), {} ) == command_result_t::OK );
    CHECK( ws.selectedMeshFaceId == faces[0] ); CHECK( MapWorkspace_MeshFaces( &ws ).nCount == 1 );
}

TEST_CASE( "Numeric region face snapshot allocation refusal preserves all selection and history", "[map][gui][geometry-edit][region-extrude][allocation]" )
{
    allocation_failure_t audit; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    {
        session_t session; auto &ws = session.workspace; std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
        const history_published_t published( ws ); region_description_t before( ws, mesh ); const auto *set = ws.meshFaceSet.pData;
        session.gui.pAllocator = &allocator; audit.failOn = 1;
        CHECK( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.extrude" ), {} ) == command_result_t::FAILED );
        CHECK( audit.calls == 1 ); CHECK( audit.live == 0 ); CHECK( ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
        published.Check( ws ); RegionCheckSelection( ws, mesh, faces ); CHECK( ws.meshFaceSet.pData == set );
        { region_description_t unchanged( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) ); }
        audit.failOn = 0;
        REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.extrude" ), {} ) == command_result_t::OK );
        RegionCheckSelection( ws, mesh, faces );
        region_description_t after( ws, mesh ); CHECK( after.value.faces.nCount == 8 );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Prepared face selection is validated before history and publishes its non-first active face", "[map][gui][component-history][atomic-brush-face-adapter][prepared-face-selection]" )
{
    session_t session; auto &ws = session.workspace; std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 32, 0, 0 } ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    history_geo::mesh_selection_t next{};
    REQUIRE( history_geo::MeshSelection_Init( &next, Allocator_GetSystem(), { mesh } ) == history_geo::geometry_status_t::OK );
    for ( const u64 face : faces ) { REQUIRE( Vector_PushBack( &next.faces, history_geo::geometry_source_id_t{ face } ) ); }
    map_prepared_component_selection_t result{ &next, {}, -1, faces[1] };
    SECTION( "Missing active face" ) { result.activeMeshFaceId = 0; }
    SECTION( "Active face outside the prepared set" ) { result.activeMeshFaceId = mesh; }
    SECTION( "Duplicate face IDs" ) { next.faces.pData[1] = next.faces.pData[0]; }
    SECTION( "Unsorted face IDs" ) { std::swap( next.faces.pData[0], next.faces.pData[1] ); }
    SECTION( "A vertex cannot be mixed into a face result" ) { REQUIRE( Vector_PushBack( &next.vertices, HistoryVertices( ws, mesh )[0] ) ); }
    SECTION( "Wrong result root" ) { next.meshId.value = faces[0]; }
    SECTION( "Face result in Objects mode" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); }
    const history_published_t before( ws ); region_description_t original( ws, mesh );
    const auto *oldSet = ws.meshFaceSet.pData; const u64 oldObject = ws.selectedMeshFaceObject, oldActive = ws.selectedMeshFaceId;
    const auto apply = []( void *context, map_document_t *copy, vector_t<u64> *roots ) -> map_status_t {
        const u64 id = *static_cast<u64 *>( context );
        if ( !Vector_PushBack( roots, id ) ) { return map_status_t::OUT_OF_MEMORY; }
        return MapEdit_Translate( copy, { &id, 1 }, { 0, 0, 16 } );
    };
    u64 target = mesh;
    CHECK_FALSE( MapWorkspace_ApplyEdit( &ws, "Reject invalid prepared faces", apply, &target, &result ) );
    CHECK( ws.lastEdit.status == map_status_t::INVALID_ARGUMENT ); before.Check( ws );
    CHECK( ws.meshFaceSet.pData == oldSet ); CHECK( ws.selectedMeshFaceObject == oldObject ); CHECK( ws.selectedMeshFaceId == oldActive );
    { region_description_t unchanged( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
    REQUIRE( EditorHistory_CanRedo( &ws.history ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    // The valid retry explicitly chooses the second face as active. History
    // must retain that choice as well as the full set, rather than repairing
    // selection after a separately published geometry step.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    next.meshId.value = mesh; next.faces.pData[0].value = faces[0]; next.faces.pData[1].value = faces[1]; next.vertices.nCount = 0;
    result.activeMeshFaceId = faces[1];
    REQUIRE( MapWorkspace_ApplyEdit( &ws, "Publish prepared faces", apply, &target, &result ) ); RegionCheckSelection( ws, mesh, faces );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); CHECK_FALSE( EditorHistory_CanRedo( &ws.history ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); RegionCheckSelection( ws, mesh, faces );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); RegionCheckSelection( ws, mesh, faces );
}

namespace
{
void CheckInsetRegion( const map_workspace_t &ws, u64 mesh, const std::vector<u64> &faces,
    const history_geo::mesh_source_description_t &original )
{
    region_description_t result( ws, mesh );
    REQUIRE( result.value.vertices.nCount == 12 ); REQUIRE( result.value.faces.nCount == 8 );
    usize caps = 0, strips = 0;
    for ( usize v = 0; v < original.vertices.nCount; ++v ) {
        const auto &old = original.vertices.pData[v];
        const auto *retained = std::find_if( result.value.vertices.pData, result.value.vertices.pData + result.value.vertices.nCount,
            [&]( const auto &vertex ) { return vertex.sourceId.value == old.sourceId.value; } );
        REQUIRE( retained != result.value.vertices.pData + result.value.vertices.nCount );
        CHECK( retained->position.x == old.position.x ); CHECK( retained->position.y == old.position.y ); CHECK( retained->position.z == old.position.z );
    }
    for ( usize i = 0; i < result.value.faces.nCount; ++i ) {
        const auto &face = result.value.faces.pData[i]; REQUIRE( face.cCorners == 4 );
        const bool cap = std::find( faces.begin(), faces.end(), face.sourceId.value ) != faces.end();
        cap ? ++caps : ++strips;
        bool allOnInternalSeam = true;
        for ( u32 c = 0; c < face.cCorners; ++c ) {
            const auto &point = result.value.vertices.pData[result.value.corners.pData[face.iFirstCorner + c].iVertex].position;
            CHECK( point.z == Catch::Approx( 0 ).margin( 1e-8 ) );
            allOnInternalSeam = allOnInternalSeam && std::fabs( point.x - 64 ) < 1e-8;
            if ( cap ) {
                CHECK( ( std::fabs( point.x - 8 ) < 1e-8 || std::fabs( point.x - 64 ) < 1e-8 || std::fabs( point.x - 120 ) < 1e-8 ) );
                CHECK( ( std::fabs( point.y - 8 ) < 1e-8 || std::fabs( point.y - 56 ) < 1e-8 ) );
            }
        }
        if ( !cap ) { CHECK_FALSE( allOnInternalSeam ); }
    }
    CHECK( caps == 2 ); CHECK( strips == 6 ); RegionCheckSelection( ws, mesh, faces );
}
void RegionInsetRedo( map_workspace_t &ws, u64 mesh, const std::vector<u64> &faces )
{
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 0, 0, 32 } ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    REQUIRE( EditorHistory_CanRedo( &ws.history ) );
}
}

TEST_CASE( "Region width Inset uses one outer border through Tool Properties and continues with region Extrude", "[map][gui][geometry-edit][region-inset-width][widget-events][component-history]" )
{
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings ); settings.Real( "editor.map.mesh_inset_width", 8 );
    std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
    region_description_t original( ws, mesh ); const usize steps = EditorHistory_AppliedStepCount( &ws.history );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); REQUIRE( panel );
    panel->resize( 520, 1200 ); panel->show(); QCoreApplication::processEvents();
    auto *body = panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) ); REQUIRE( body );
    gui::EditorSection_SetExpanded( body->parentWidget(), true ); QCoreApplication::processEvents();
    auto *button = body->findChild<QPushButton *>( QStringLiteral( "map.mesh.inset_width" ) ); REQUIRE( button );
    REQUIRE( button->isEnabled() ); REQUIRE( button->isVisible() );
    const QPointF point = button->rect().center(), global = button->mapToGlobal( point );
    QMouseEvent press( QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QMouseEvent release( QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( button, &press ); QCoreApplication::sendEvent( button, &release );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); CheckInsetRegion( ws, mesh, faces, original.value );
    region_description_t inset( ws, mesh );
    MapWorkspace_SelectMeshFace( &ws, mesh, faces[0] );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); RegionCheckSelection( ws, mesh, faces );
    { region_description_t undone( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &original.value, &undone.value ) ); }
    MapWorkspace_SelectMeshFace( &ws, mesh, faces[0] );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckInsetRegion( ws, mesh, faces, original.value );
    QTemporaryDir directory; REQUIRE( directory.isValid() ); const auto path = directory.filePath( QStringLiteral( "region-inset.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); session_t reopened; auto &restored = reopened.workspace;
    REQUIRE( MapWorkspace_Open( &restored, path ).status == map_files_status_t::OK );
    { region_description_t loaded( restored, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &inset.value, &loaded.value ) ); }
    MapWorkspace_SetTool( &restored, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &restored, map_element_mode_t::FACES );
    REQUIRE( MapWorkspace_SetMeshFaces( &restored, mesh, { faces.data(), faces.size() }, faces[1] ) );
    REQUIRE( MapWorkspace_ExtrudeMeshFace( &restored, 16 ) ); RegionCheckSelection( restored, mesh, faces );
    region_description_t extruded( restored, mesh ); CHECK( extruded.value.faces.nCount == 14 ); CHECK( extruded.value.vertices.nCount == 18 );
}

TEST_CASE( "Collapsed region width Inset preserves the full face set and Redo before retry", "[map][gui][geometry-edit][region-inset-width][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    settings_scope_tester_t settings( &session.gui.settings ); settings.Real( "editor.map.mesh_inset_width", 32 );
    std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces ); RegionInsetRedo( ws, mesh, faces );
    const history_published_t before( ws ); region_description_t original( ws, mesh ); const auto *faceStorage = ws.meshFaceSet.pData;
    CHECK( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.inset_width" ), {} ) == command_result_t::FAILED );
    before.Check( ws ); CHECK( ws.meshFaceSet.pData == faceStorage ); RegionCheckSelection( ws, mesh, faces );
    { region_description_t unchanged( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
    REQUIRE( EditorHistory_CanRedo( &ws.history ) ); REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); REQUIRE( MapWorkspace_SetMeshFaces( &ws, mesh, { faces.data(), faces.size() }, faces[1] ) );
    settings.Real( "editor.map.mesh_inset_width", 8 );
    REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.mesh.inset_width" ), {} ) == command_result_t::OK );
    CheckInsetRegion( ws, mesh, faces, original.value ); CHECK_FALSE( EditorHistory_CanRedo( &ws.history ) );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 );
}

TEST_CASE( "Region width Inset allocation refusal retains all caps and history and allows retry", "[map][gui][geometry-edit][region-inset-width][allocation]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit }; usize calls = 0;
    {
        session_t session( &allocator ); std::vector<u64> faces; const u64 mesh = RegionTwoQuads( session.workspace, faces );
        RegionInsetRedo( session.workspace, mesh, faces ); audit.calls = 0;
        REQUIRE( MapWorkspace_InsetMeshFaceByEdgeWidth( &session.workspace, 8 ) ); calls = audit.calls;
    }
    CHECK( audit.live == 0 ); REQUIRE( calls > 10 ); usize refused = 0;
    for ( const usize failOn : { usize{ 1 }, calls / 2, calls - 1 } ) {
        CAPTURE( failOn, calls );
        {
            session_t session( &allocator ); auto &ws = session.workspace; std::vector<u64> faces; const u64 mesh = RegionTwoQuads( ws, faces );
            RegionInsetRedo( ws, mesh, faces ); const history_published_t before( ws ); region_description_t original( ws, mesh );
            const auto *faceStorage = ws.meshFaceSet.pData; audit.calls = 0; audit.failOn = failOn;
            const bool done = MapWorkspace_InsetMeshFaceByEdgeWidth( &ws, 8 ); audit.failOn = 0;
            if ( !done ) {
                ++refused; before.Check( ws ); CHECK( ws.meshFaceSet.pData == faceStorage ); RegionCheckSelection( ws, mesh, faces );
                { region_description_t unchanged( ws, mesh ); CHECK( history_geo::MeshSourceDescription_Equal( &original.value, &unchanged.value ) ); }
                REQUIRE( EditorHistory_CanRedo( &ws.history ) ); REQUIRE( MapWorkspace_InsetMeshFaceByEdgeWidth( &ws, 8 ) );
            }
            CheckInsetRegion( ws, mesh, faces, original.value ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 );
        }
        CHECK( audit.live == 0 );
    }
    CHECK( refused >= 2 );
}
