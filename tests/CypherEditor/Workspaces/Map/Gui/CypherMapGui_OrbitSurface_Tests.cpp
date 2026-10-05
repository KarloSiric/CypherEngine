//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_OrbitSurface_Tests.cpp
//  Purpose: Verifies physical brush, mesh and terrain navigation pivots
//           independently of selection categories and incomplete queries.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Views.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_RaycastQueries.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>

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
f64 Distance( math::vec3d_t a, math::vec3d_t b )
{
    const f64 x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return std::sqrt( x * x + y * y + z * z );
}
math::vec3d_t Along( math::vec3d_t a, math::vec3d_t b, f64 fraction )
{
    return { a.x + ( b.x - a.x ) * fraction, a.y + ( b.y - a.y ) * fraction, a.z + ( b.z - a.z ) * fraction };
}
void CheckPoint( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( std::abs( actual.x - expected.x ) < 1e-6 ); CHECK( std::abs( actual.y - expected.y ) < 1e-6 ); CHECK( std::abs( actual.z - expected.z ) < 1e-6 );
}
QPointF Project( QWidget *view, math::vec3d_t point )
{
    QPointF screen; REQUIRE( MapCameraView_WorldToView( view, point, &screen ) );
    REQUIRE( std::isfinite( screen.x() ) ); REQUIRE( std::isfinite( screen.y() ) ); return screen;
}
void Mouse( QWidget *view, QEvent::Type type, QPointF position, Qt::MouseButton button = Qt::LeftButton,
            Qt::KeyboardModifiers modifiers = Qt::AltModifier )
{
    QMouseEvent event( type, position, view->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons( button ), modifiers );
    QCoreApplication::sendEvent( view, &event );
}
void Wheel( QWidget *view, QPointF position )
{
    QWheelEvent event( position, view->mapToGlobal( position ), QPoint(), QPoint( 0, 120 ), Qt::LeftButton,
                       Qt::AltModifier, Qt::NoScrollPhase, false );
    event.ignore(); QCoreApplication::sendEvent( view, &event ); REQUIRE( event.isAccepted() );
}

struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    settings_document_t settings{};
    u64 target{}, distant{};
    math::vec3d_t surface{ -16, 16, 96 };
    std::unique_ptr<QWidget> camera;
    explicit session_t( bool terrain = false, bool mesh = true ) {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) ); REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
        Real( "editor.camera.look_sensitivity", 1.0 ); Real( "editor.camera.slow_multiplier", 1.0 );
        Real( "editor.camera.zoom_sensitivity", 1.0 ); Bool( "editor.camera.zoom_to_cursor", false ); Bool( "editor.camera.invert_wheel", false );
        if ( terrain ) {
            geo::heightfield_t field{};
            const auto id = geo::GeometrySourceIdAllocator_Allocate( &ws.pDocument->geometry.sourceIds.allocator );
            REQUIRE( id.status == geo::geometry_status_t::OK ); target = id.id.value;
            REQUIRE( geo::HeightField_TryInit( &field, ws.pDocument->pAllocator, { -64, -64, 96 }, 32.0, 4u, 4u, 2u,
                id.id, &ws.pDocument->geometry.sourceIds.allocator ) == geo::geometry_status_t::OK );
            REQUIRE( geo::GeometryDocument_TryAddHeightField( &ws.pDocument->geometry, &field ) == geo::geometry_status_t::OK );
            geo::HeightField_Shutdown( &field );
            REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, target, StringView_FromCString( "default" ) ) == map_status_t::OK );
            MapWorkspace_DocumentChanged( &ws );
        } else { target = Box( { -64, -64, 0 }, { 64, 64, 96 }, mesh ); }
        distant = Box( { 512, 384, -64 }, { 640, 512, 64 }, false ); MapWorkspace_Select( &ws, target, MAP_SELECT_REPLACE );
        camera.reset( MapCameraView_Create( nullptr, &ws ) ); REQUIRE( camera != nullptr );
        camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); MapWorkspace_Select( &ws, 0u, MAP_SELECT_REPLACE );
        const auto screen = Project( camera.get(), surface ); REQUIRE( camera->rect().contains( screen.toPoint() ) );
        REQUIRE( QLineF( screen, QPointF( 400, 300 ) ).length() > 5.0 );
        REQUIRE( MapCameraView_Pick( camera.get(), screen ) == target );
    }
    ~session_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); }
    u64 Box( math::vec3d_t minimum, math::vec3d_t maximum, bool mesh ) {
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, minimum ); MapBounds_AddPoint( bounds, maximum );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); } return id;
    }
    void Write( const char *path, const setting_value_t &value ) {
        const auto *descriptor = EditorSettings_Find( &gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor != nullptr );
        REQUIRE( EditorSettings_Write( &gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    void Real( const char *path, f64 number ) {
        setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = number; Write( path, value );
    }
    void Bool( const char *path, bool enabled ) {
        setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = enabled; Write( path, value );
    }
};

struct snapshot_t {
    const map_document_t *document{};
    u64 revision{}, selectionRevision{};
    usize steps{}, applied{};
    undo_state_token_t token{};
    bool modified{};
    map_element_mode_t mode{};
    std::vector<u64> selection;
    std::vector<math::vec3d_t> points;
    explicit snapshot_t( const map_workspace_t &ws ) : document( ws.pDocument ), revision( document->geometry.revision ),
        selectionRevision( ws.selection.revision ), steps( EditorHistory_StepCount( &ws.history ) ), applied( EditorHistory_AppliedStepCount( &ws.history ) ),
        token( UndoRedo_StateToken( ws.history.pUndo ) ), modified( MapWorkspace_IsModified( &ws ) ), mode( ws.elementMode ) {
        for ( usize i = 0u; i < EditorSelection_Count( &ws.selection ); ++i ) { selection.push_back( EditorSelection_At( &ws.selection, i ) ); }
        for ( usize i = 0u; i < ws.wire.points.nCount; ++i ) { points.push_back( ws.wire.points.pData[i] ); }
    }
    void Check( const map_workspace_t &ws ) const {
        REQUIRE( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.elementMode == mode );
        CHECK( ws.selection.revision == selectionRevision ); REQUIRE( EditorSelection_Count( &ws.selection ) == selection.size() );
        for ( usize i = 0u; i < selection.size(); ++i ) { CHECK( EditorSelection_At( &ws.selection, i ) == selection[i] ); }
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
        REQUIRE( ws.wire.points.nCount == points.size() ); for ( usize i = 0u; i < points.size(); ++i ) { CheckPoint( ws.wire.points.pData[i], points[i] ); }
        CHECK_FALSE( ws.editPreview.bActive );
    }
};

void Orbit( QWidget *camera, QPointF press, math::vec3d_t pivot )
{
    const auto projection = Project( camera, pivot ); const f64 radius = Distance( MapCameraView_Position( camera ), pivot );
    const auto direction = MapCameraView_Forward( camera ); const QPointF moved = press + QPointF( 18, 11 ), continued = press + QPointF( 27, 16 );
    Mouse( camera, QEvent::MouseButtonPress, press ); Mouse( camera, QEvent::MouseMove, moved );
    CHECK( Distance( MapCameraView_Forward( camera ), direction ) > 0.01 );
    CHECK( Distance( MapCameraView_Position( camera ), pivot ) == Catch::Approx( radius ).margin( 1e-6 ) );
    CHECK( QLineF( Project( camera, pivot ), projection ).length() < 1e-6 );
    Wheel( camera, moved ); const f64 newRadius = Distance( MapCameraView_Position( camera ), pivot );
    CHECK( newRadius == Catch::Approx( radius - 64.0 ).margin( 1e-6 ) );
    CHECK( QLineF( Project( camera, pivot ), projection ).length() < 1e-6 );
    Mouse( camera, QEvent::MouseMove, continued );
    CHECK( Distance( MapCameraView_Position( camera ), pivot ) == Catch::Approx( newRadius ).margin( 1e-6 ) );
    CHECK( QLineF( Project( camera, pivot ), projection ).length() < 1e-6 );
    const auto position = MapCameraView_Position( camera ); Mouse( camera, QEvent::MouseButtonRelease, continued ); CheckPoint( MapCameraView_Position( camera ), position );
}

// An independent library query supplies the actual nearest point on a newly
// inserted occluder; the test never asks the GUI for its private pivot.
math::vec3d_t Hit( const map_workspace_t &ws, u64 object, math::vec3d_t origin, math::vec3d_t toward )
{
    const f64 length = Distance( origin, toward ); REQUIRE( length > 0.0 );
    const geo::geometry_raycast_ray_t ray{ origin, { ( toward.x - origin.x ) / length, ( toward.y - origin.y ) / length, ( toward.z - origin.z ) / length } };
    if ( const auto *mesh = geo::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ) ) {
        usize bytes{}; REQUIRE( geo::MeshQueries_TryGetRaycastScratchSize( &mesh->mesh, ws.pDocument->geometryPolicy, &bytes ) == geo::geometry_status_t::OK );
        geo::geometry_scratch_t scratch{}; geo::geometry_scratch_desc_t desc{}; desc.pFallbackAllocator = Allocator_GetSystem(); desc.cbCapacity = bytes; desc.cbBudget = 64u * CY_MIB;
        REQUIRE( geo::GeometryScratch_Acquire( &scratch, desc ) == geo::geometry_status_t::OK ); geo::mesh_raycast_hit_t hit{};
        const auto status = geo::MeshQueries_TryRaycast( &mesh->mesh, mesh->sourceId, ray, {}, ws.pDocument->geometryPolicy, &scratch, &hit );
        REQUIRE( geo::GeometryScratch_Release( &scratch ) == geo::geometry_status_t::OK ); REQUIRE( status == geo::geometry_status_t::OK ); REQUIRE( hit.bHit ); return hit.position;
    }
    const auto *brush = geo::GeometryDocument_FindBrush( &ws.pDocument->geometry, { object } ); REQUIRE( brush != nullptr );
    geo::brush_boundary_t boundary{}; REQUIRE( geo::BrushBoundary_Init( &boundary, Allocator_GetSystem() ) == geo::geometry_status_t::OK );
    const auto reconstructed = geo::BrushBoundary_TryReconstruct( &boundary, brush, ws.pDocument->geometryPolicy );
    geo::brush_raycast_hit_t hit{}; const auto queried = reconstructed == geo::geometry_status_t::OK
        ? geo::BrushQueries_TryRaycast( brush, &boundary, ray, {}, ws.pDocument->geometryPolicy, &hit ) : reconstructed;
    geo::BrushBoundary_Shutdown( &boundary ); REQUIRE( queried == geo::geometry_status_t::OK ); REQUIRE( hit.bHit ); return hit.position;
}

struct allocation_t {
    const allocator_t *base{ Allocator_GetSystem() };
    usize failSize{}, rejected{}, live{};
};
void *Allocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<allocation_t *>( context ); if ( bytes == audit.failSize ) { ++audit.rejected; return nullptr; }
    void *memory = Allocator_Allocate( audit.base, bytes, alignment ); if ( memory != nullptr ) { ++audit.live; } return memory;
}
void Free( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<allocation_t *>( context ); if ( memory != nullptr ) { --audit.live; } Allocator_Free( audit.base, memory, bytes, alignment );
}
struct allocator_scope_t {
    map_document_t *map{};
    const allocator_t *previous{};
    allocation_t audit{};
    allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    allocator_scope_t( map_document_t *document, usize failBytes ) : map( document ), previous( document->pAllocator ) {
        audit.base = previous; audit.failSize = failBytes; map->pAllocator = &allocator;
    }
    ~allocator_scope_t() { map->pAllocator = previous; }
};
}

TEST_CASE( "Empty-selection orbit pivots on the exact mesh or terrain surface independently of selection category", "[map][gui][views][camera-orbit-surface]" )
{
    for ( const bool terrain : { false, true } ) {
        for ( const auto mode : { map_element_mode_t::OBJECTS, map_element_mode_t::MESHES, map_element_mode_t::VERTICES, map_element_mode_t::EDGES, map_element_mode_t::FACES } ) {
            CAPTURE( terrain, static_cast<int>( mode ) ); session_t session( terrain );
            MapWorkspace_SetElementMode( &session.ws, mode ); REQUIRE( EditorSelection_Count( &session.ws.selection ) == 0u ); snapshot_t original( session.ws );
            Orbit( session.camera.get(), Project( session.camera.get(), session.surface ), session.surface ); original.Check( session.ws );
        }
    }
}

TEST_CASE( "Surface navigation takes the nearest visible brush or mesh rather than a deeper hit", "[map][gui][views][camera-orbit-surface][occlusion]" )
{
    for ( const bool nearMesh : { false, true } ) {
        for ( const bool hidden : { false, true } ) {
            CAPTURE( nearMesh, hidden ); session_t session( false, !nearMesh ); const auto origin = MapCameraView_Position( session.camera.get() );
            const auto center = Along( origin, session.surface, 0.5 );
            const u64 near = session.Box( { center.x - 24, center.y - 24, center.z - 24 }, { center.x + 24, center.y + 24, center.z + 24 }, nearMesh );
            MapWorkspace_Select( &session.ws, 0u, MAP_SELECT_REPLACE ); const auto nearPoint = Hit( session.ws, near, origin, session.surface );
            const auto press = Project( session.camera.get(), session.surface ); REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == near );
            REQUIRE( Distance( nearPoint, session.surface ) > 24.0 );
            if ( hidden ) {
                REQUIRE( EditorSelection_Apply( &session.ws.hidden, near, EDITOR_SELECT_ADD ) ); MapWorkspace_Notify( &session.ws, MAP_CHANGE_VIEW );
                REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == session.target );
            }
            snapshot_t original( session.ws ); Orbit( session.camera.get(), press, hidden ? session.surface : nearPoint ); original.Check( session.ws );
            CHECK( EditorSelection_Contains( &session.ws.hidden, near ) == hidden );
        }
    }
}

TEST_CASE( "Selection has navigation priority and a true empty surface miss retains scene fallback", "[map][gui][views][camera-orbit-surface][fallback]" )
{
    for ( const bool selected : { false, true } ) {
        CAPTURE( selected ); session_t session; math::vec3d_t expected{}; QPointF press;
        if ( selected ) {
            MapWorkspace_Select( &session.ws, session.distant, MAP_SELECT_REPLACE );
            const auto *object = MapWireframe_FindObject( session.ws.wire, session.distant ); REQUIRE( object != nullptr ); expected = MapBounds_Center( object->bounds );
            press = Project( session.camera.get(), session.surface ); REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == session.target );
        } else {
            press = QPointF( 5, 5 ); REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == 0u ); expected = MapBounds_Center( session.ws.wire.bounds );
        }
        snapshot_t original( session.ws ); Orbit( session.camera.get(), press, expected ); original.Check( session.ws );
    }
}

TEST_CASE( "Captured mesh surface supplies the correct camera pan depth", "[map][gui][views][camera-orbit-surface][pan]" )
{
    session_t session; session.Real( "editor.camera.pan_sensitivity", 1.5 ); snapshot_t original( session.ws ); auto *camera = session.camera.get();
    const auto press = Project( camera, session.surface ); const auto forward = MapCameraView_Forward( camera ); const auto position = MapCameraView_Position( camera );
    const QPointF step( 40, 24 ); Mouse( camera, QEvent::MouseButtonPress, press, Qt::MiddleButton, Qt::NoModifier );
    Mouse( camera, QEvent::MouseMove, press + step, Qt::MiddleButton, Qt::NoModifier );
    CHECK( Distance( MapCameraView_Position( camera ), position ) > 1.0 ); CheckPoint( MapCameraView_Forward( camera ), forward );
    // With the physical point as pivot, its projection follows the pointer
    // times captured sensitivity. A deeper scene pivot moves it too far.
    CHECK( QLineF( Project( camera, session.surface ), press + step * 1.5 ).length() < 1e-6 );
    const auto moved = MapCameraView_Position( camera ); Mouse( camera, QEvent::MouseButtonRelease, press + step, Qt::MiddleButton, Qt::NoModifier );
    CheckPoint( MapCameraView_Position( camera ), moved ); original.Check( session.ws );
}

TEST_CASE( "A failed eligible mesh query uses scene fallback instead of a partially known deeper brush", "[map][gui][views][camera-orbit-surface][allocation][atomic]" )
{
    session_t session( false, false ); const auto origin = MapCameraView_Position( session.camera.get() ); const auto center = Along( origin, session.surface, 0.5 );
    const u64 near = session.Box( { center.x - 24, center.y - 24, center.z - 24 }, { center.x + 24, center.y + 24, center.z + 24 }, true );
    MapWorkspace_Select( &session.ws, 0u, MAP_SELECT_REPLACE ); const auto press = Project( session.camera.get(), session.surface );
    REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == near );
    const auto *mesh = geo::GeometryDocument_FindMesh( &session.ws.pDocument->geometry, { near } ); REQUIRE( mesh != nullptr );
    usize bytes{}; REQUIRE( geo::MeshQueries_TryGetRaycastScratchSize( &mesh->mesh, session.ws.pDocument->geometryPolicy, &bytes ) == geo::geometry_status_t::OK ); REQUIRE( bytes > 0u );
    snapshot_t original( session.ws ); const auto fallback = MapBounds_Center( session.ws.wire.bounds );
    {
        allocator_scope_t scope( session.ws.pDocument, bytes );
        // Ordinary picking can report the deeper brush after failing this
        // nearer mesh. Navigation requires a complete physical query first.
        REQUIRE( MapCameraView_Pick( session.camera.get(), press ) == session.target ); REQUIRE( scope.audit.rejected > 0u );
        Orbit( session.camera.get(), press, fallback ); CHECK( scope.audit.rejected >= 2u ); CHECK( scope.audit.live == 0u ); original.Check( session.ws );
    }
    CHECK( session.ws.pDocument->pAllocator == Allocator_GetSystem() );
}
