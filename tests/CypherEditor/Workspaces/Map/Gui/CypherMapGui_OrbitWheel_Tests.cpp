//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_OrbitWheel_Tests.cpp
//  Purpose: Verifies wheel dolly during a captured orbit, including gesture
//           continuity, off-center pivots, keymap ownership, and staged edits.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Input.h"
#include "CypherMapGui_Views.h"
#include "CypherEditor_Keymap.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
f64 Distance( math::vec3d_t a, math::vec3d_t b )
{
    const math::vec3d_t d{ a.x - b.x, a.y - b.y, a.z - b.z };
    return std::sqrt( d.x * d.x + d.y * d.y + d.z * d.z );
}

void CheckPoint( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( std::abs( actual.x - expected.x ) < 1e-6 );
    CHECK( std::abs( actual.y - expected.y ) < 1e-6 );
    CHECK( std::abs( actual.z - expected.z ) < 1e-6 );
}

void Mouse( QWidget *view, QEvent::Type type, QPointF position,
            Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = Qt::AltModifier )
{
    QMouseEvent event( type, position, view->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons( button ), modifiers );
    QCoreApplication::sendEvent( view, &event );
}

bool Wheel( QWidget *view, QPointF position, int angle, int pixels = 0,
            Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = Qt::AltModifier )
{
    QWheelEvent event( position, view->mapToGlobal( position ), QPoint( 0, pixels ), QPoint( 0, angle ), button,
                       modifiers, Qt::NoScrollPhase, false );
    event.ignore(); QCoreApplication::sendEvent( view, &event ); return event.isAccepted();
}

QPointF Project( QWidget *view, math::vec3d_t point )
{
    QPointF screen;
    REQUIRE( MapCameraView_WorldToView( view, point, &screen ) );
    REQUIRE( std::isfinite( screen.x() ) ); REQUIRE( std::isfinite( screen.y() ) );
    return screen;
}

struct settings_t {
    settings_registry_t *registry{};
    settings_document_t store{};
    explicit settings_t( settings_registry_t *value ) : registry( value ) {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
        Real( "editor.camera.look_sensitivity", 1.0 );
        Real( "editor.camera.zoom_sensitivity", 1.0 );
        Real( "editor.camera.fast_multiplier", 1.0 );
        Real( "editor.camera.slow_multiplier", 1.0 );
        Bool( "editor.camera.invert_y", false ); Bool( "editor.camera.invert_wheel", false );
    }
    ~settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Write( const char *path, const setting_value_t &value ) {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    void Real( const char *path, f64 number ) {
        setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = number; Write( path, value );
    }
    void Bool( const char *path, bool enabled ) {
        setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = enabled; Write( path, value );
    }
};

struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    session_t() {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_New( &workspace ) == map_status_t::OK );
    }
};

struct snapshot_t {
    const map_document_t *document{};
    u64 revision{}, selectionRevision{}, selected{};
    usize steps{}, applied{};
    undo_state_token_t token{};
    bool modified{};
    map_tool_t tool{};
    std::vector<math::vec3d_t> points;
    explicit snapshot_t( const map_workspace_t &ws ) : document( ws.pDocument ), revision( document->geometry.revision ),
        selectionRevision( ws.selection.revision ), selected( EditorSelection_At( &ws.selection, 0 ) ),
        steps( EditorHistory_StepCount( &ws.history ) ), applied( EditorHistory_AppliedStepCount( &ws.history ) ),
        token( UndoRedo_StateToken( ws.history.pUndo ) ), modified( MapWorkspace_IsModified( &ws ) ), tool( ws.tool ) {
        for ( usize i = 0; i < ws.wire.points.nCount; ++i ) { points.push_back( ws.wire.points.pData[i] ); }
    }
    void Check( const map_workspace_t &ws ) const {
        REQUIRE( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.revision == selectionRevision ); REQUIRE( EditorSelection_Count( &ws.selection ) == 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == selected ); CHECK( ws.tool == tool );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( MapWorkspace_IsModified( &ws ) == modified ); REQUIRE( ws.wire.points.nCount == points.size() );
        for ( usize i = 0; i < points.size(); ++i ) { CheckPoint( ws.wire.points.pData[i], points[i] ); }
    }
};

struct scene_t {
    session_t session;
    settings_t settings{ &session.gui.settings };
    math::vec3d_t pivot{ 0, 0, 48 };
    map_bounds_t source{};
    std::unique_ptr<QWidget> camera;
    explicit scene_t( bool entity = false, bool zoomToCursor = false ) {
        auto &ws = session.workspace;
        settings.Bool( "editor.camera.zoom_to_cursor", zoomToCursor );
        MapBounds_AddPoint( source, { -64, -64, 0 } ); MapBounds_AddPoint( source, { 64, 64, 96 } );
        u64 selected{};
        if ( entity ) {
            REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ),
                StringView_FromCString( "info_player_start" ), pivot, &selected ) == map_status_t::OK );
            MapWorkspace_DocumentChanged( &ws );
        } else {
            REQUIRE( MapWorkspace_CreateBox( &ws, source ) ); selected = EditorSelection_At( &ws.selection, 0 );
        }
        map_bounds_t distant{}; MapBounds_AddPoint( distant, { 320, -64, 0 } ); MapBounds_AddPoint( distant, { 448, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, distant ) ); MapWorkspace_Select( &ws, selected, MAP_SELECT_REPLACE );
        camera.reset( MapCameraView_Create( nullptr, &ws ) ); REQUIRE( camera != nullptr );
        camera->resize( 800, 600 ); camera->show(); QCoreApplication::processEvents();
        // Frame the map, leaving the selected pivot away from the center.
        // This distinguishes orbit dolly from camera-forward/cursor dolly.
        MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
        REQUIRE( QLineF( Project( camera.get(), pivot ), QPointF( 400, 300 ) ).length() > 10.0 );
    }
};

struct keys_t {
    gui::editor_gui_t *gui{};
    settings_document_t base{}, profile{};
    keys_t( gui::editor_gui_t *target, const char *overrideText ) : gui( target ) {
        REQUIRE( SettingsDocument_Init( &base, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Init( &profile, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &base, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "orbit_wheel_base" mouse = { "map.viewport.3d" = {
    "map.camera.orbit" = [ "MiddleDrag" ] "map.camera.dolly" = [ "Wheel" ] } } })cykv" ) ).status == settings_document_status_t::OK );
        const std::string text = std::string( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n" ) + overrideText;
        REQUIRE( SettingsDocument_Load( &profile, { text.data(), text.size() } ).status == settings_document_status_t::OK );
        gui->keymapChain[0] = SettingsDocument_Root( &profile ); gui->keymapChain[1] = SettingsDocument_Root( &base ); gui->nKeymapChain = 2u;
    }
    ~keys_t() { gui->nKeymapChain = 0u; gui->keymapChain[0] = nullptr; gui->keymapChain[1] = nullptr; }
};
}

TEST_CASE( "Orbit wheel keeps its new radius through stationary movement rotation and release", "[map][gui][views][camera-orbit-wheel]" )
{
    for ( const bool entity : { false, true } ) {
        for ( const bool zoomToCursor : { false, true } ) {
            CAPTURE( entity, zoomToCursor ); scene_t scene( entity, zoomToCursor ); auto *camera = scene.camera.get();
            const auto &ws = scene.session.workspace; snapshot_t original( ws );
            const QPointF start( 300, 220 ), moved( 337, 236 ), continued( 359, 250 );
            Mouse( camera, QEvent::MouseButtonPress, start ); Mouse( camera, QEvent::MouseMove, moved );
            const auto before = MapCameraView_Position( camera ), direction = MapCameraView_Forward( camera );
            const auto projected = Project( camera, scene.pivot ); const f64 radius = Distance( before, scene.pivot );
            REQUIRE( radius > 100.0 );
            // The wheel pointer is far from both the selected pivot and the
            // drag pointer. Orbit must retain its pivot even with cursor zoom.
            REQUIRE( Wheel( camera, QPointF( 700, 60 ), 120 ) );
            const auto after = MapCameraView_Position( camera ); const f64 changedRadius = Distance( after, scene.pivot );
            CHECK( changedRadius == Catch::Approx( radius - 64.0 ).margin( 1e-6 ) ); CheckPoint( MapCameraView_Forward( camera ), direction );
            CHECK( QLineF( Project( camera, scene.pivot ), projected ).length() < 1e-6 );
            Mouse( camera, QEvent::MouseMove, moved ); CheckPoint( MapCameraView_Position( camera ), after );
            Mouse( camera, QEvent::MouseMove, continued );
            CHECK( Distance( MapCameraView_Forward( camera ), direction ) > 0.01 );
            CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( changedRadius ).margin( 1e-6 ) );
            CHECK( QLineF( Project( camera, scene.pivot ), projected ).length() < 1e-6 );
            const auto rotated = MapCameraView_Position( camera ), facing = MapCameraView_Forward( camera );
            Mouse( camera, QEvent::MouseButtonRelease, continued );
            CheckPoint( MapCameraView_Position( camera ), rotated ); CheckPoint( MapCameraView_Forward( camera ), facing ); original.Check( ws );
        }
    }
}

TEST_CASE( "Captured orbit wheel honors fractional pixel sensitivity inversion and modifier steps", "[map][gui][views][camera-orbit-wheel][settings]" )
{
    for ( const bool pixels : { false, true } ) {
        for ( const bool inverted : { false, true } ) {
            CAPTURE( pixels, inverted ); scene_t scene; auto *camera = scene.camera.get(); const auto &ws = scene.session.workspace;
            scene.settings.Real( "editor.camera.zoom_sensitivity", 1.5 ); scene.settings.Real( "editor.camera.fast_multiplier", 2.0 );
            scene.settings.Bool( "editor.camera.invert_wheel", inverted ); snapshot_t original( ws );
            const QPointF point( 300, 220 ); Mouse( camera, QEvent::MouseButtonPress, point );
            const f64 radius = Distance( MapCameraView_Position( camera ), scene.pivot ); const auto projected = Project( camera, scene.pivot );
            REQUIRE( Wheel( camera, point, pixels ? 0 : 30, pixels ? 10 : 0, Qt::LeftButton, Qt::ShiftModifier ) );
            // Quarter of a wheel step, sensitivity 1.5 and fast multiplier 2.
            const f64 expected = radius + ( inverted ? 48.0 : -48.0 );
            CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( expected ).margin( 1e-6 ) );
            CHECK( QLineF( Project( camera, scene.pivot ), projected ).length() < 1e-6 );
            Mouse( camera, QEvent::MouseMove, point + QPointF( 16, -7 ) );
            CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( expected ).margin( 1e-6 ) );
            Mouse( camera, QEvent::MouseButtonRelease, point + QPointF( 16, -7 ) ); original.Check( ws );
        }
    }
}

TEST_CASE( "Orbit wheel inward limit retains a visible off-center pivot and can move outward again", "[map][gui][views][camera-orbit-wheel][limits]" )
{
    scene_t scene; auto *camera = scene.camera.get(); const auto &ws = scene.session.workspace; snapshot_t original( ws );
    const QPointF point( 300, 220 ); Mouse( camera, QEvent::MouseButtonPress, point );
    Mouse( camera, QEvent::MouseMove, point + QPointF( 20, 10 ) ); const auto projected = Project( camera, scene.pivot );
    REQUIRE( Wheel( camera, point, 12000000 ) );
    const auto limited = MapCameraView_Position( camera ); const auto forward = MapCameraView_Forward( camera );
    const math::vec3d_t delta{ scene.pivot.x - limited.x, scene.pivot.y - limited.y, scene.pivot.z - limited.z };
    CHECK( delta.x * forward.x + delta.y * forward.y + delta.z * forward.z == Catch::Approx( 2.0 ).margin( 1e-6 ) );
    CHECK( QLineF( Project( camera, scene.pivot ), projected ).length() < 1e-5 );
    REQUIRE( Wheel( camera, point, 120 ) ); CheckPoint( MapCameraView_Position( camera ), limited );
    Mouse( camera, QEvent::MouseMove, point + QPointF( 30, 15 ) );
    CHECK( Distance( MapCameraView_Forward( camera ), forward ) > 0.01 );
    CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( Distance( limited, scene.pivot ) ).margin( 1e-6 ) );
    REQUIRE( Wheel( camera, point, -120 ) );
    CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( Distance( limited, scene.pivot ) + 64.0 ).margin( 1e-6 ) );
    CHECK( QLineF( Project( camera, scene.pivot ), projected ).length() < 1e-5 );
    const auto outward = MapCameraView_Position( camera ); Mouse( camera, QEvent::MouseButtonRelease, point + QPointF( 30, 15 ) );
    CheckPoint( MapCameraView_Position( camera ), outward ); original.Check( ws );
}

TEST_CASE( "Remapped reserved and unbound orbit wheels preserve capture without inherited dolly", "[map][gui][views][camera-orbit-wheel][keymap]" )
{
    const char *profiles[]{
        R"cykv({ id = "remapped" mouse = { "map.viewport.3d" = { "map.camera.dolly" = [ "Shift+Wheel" ] } } })cykv",
        R"cykv({ id = "unbound" mouse = { "map.viewport.3d" = { "map.camera.dolly" = [] } } })cykv",
        R"cykv({ id = "reserved" mouse = { "map.viewport.3d" = { "future.custom" = [ "Wheel" ] } } })cykv"
    };
    for ( int variant = 0; variant < 3; ++variant ) {
        CAPTURE( variant ); scene_t scene; auto *camera = scene.camera.get(); const auto &ws = scene.session.workspace;
        keys_t keys( &scene.session.gui, profiles[variant] ); snapshot_t original( ws );
        const QPointF point( 300, 220 ), end( 330, 235 );
        Mouse( camera, QEvent::MouseButtonPress, point, Qt::MiddleButton, Qt::NoModifier );
        const auto position = MapCameraView_Position( camera ), direction = MapCameraView_Forward( camera );
        // A remap removes the old match; explicit unbinding reserves it.
        CHECK( Wheel( camera, point, 120, 0, Qt::MiddleButton, Qt::NoModifier ) == ( variant != 0 ) );
        CheckPoint( MapCameraView_Position( camera ), position ); CheckPoint( MapCameraView_Forward( camera ), direction );
        if ( variant == 0 ) {
            REQUIRE( Wheel( camera, point, 120, 0, Qt::MiddleButton, Qt::ShiftModifier ) );
            CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( Distance( position, scene.pivot ) - 64.0 ).margin( 1e-6 ) );
        }
        const f64 radius = Distance( MapCameraView_Position( camera ), scene.pivot );
        Mouse( camera, QEvent::MouseMove, end, Qt::MiddleButton, Qt::NoModifier );
        CHECK( Distance( MapCameraView_Forward( camera ), direction ) > 0.01 );
        CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( radius ).margin( 1e-6 ) );
        const auto rotated = MapCameraView_Position( camera ); Mouse( camera, QEvent::MouseButtonRelease, end, Qt::MiddleButton, Qt::NoModifier );
        CheckPoint( MapCameraView_Position( camera ), rotated ); original.Check( ws );
    }
}

TEST_CASE( "Escape and focus loss end orbit wheel capture without late movement or authoring", "[map][gui][views][camera-orbit-wheel][focus]" )
{
    for ( const bool escape : { false, true } ) {
        CAPTURE( escape ); scene_t scene; auto *camera = scene.camera.get(); auto &ws = scene.session.workspace;
        MapWorkspace_SetTool( &ws, map_tool_t::ROTATE ); snapshot_t original( ws );
        const QPointF point( 300, 220 ), end( 330, 235 ); Mouse( camera, QEvent::MouseButtonPress, point );
        Mouse( camera, QEvent::MouseMove, end ); REQUIRE( Wheel( camera, end, 120 ) );
        const auto position = MapCameraView_Position( camera ), forward = MapCameraView_Forward( camera );
        if ( escape ) { QKeyEvent event( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &event ); }
        else { QFocusEvent event( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera, &event ); }
        Mouse( camera, QEvent::MouseMove, end + QPointF( 40, 20 ) ); Mouse( camera, QEvent::MouseButtonRelease, end + QPointF( 40, 20 ) );
        CheckPoint( MapCameraView_Position( camera ), position ); CheckPoint( MapCameraView_Forward( camera ), forward );
        CheckPoint( MapCameraView_NavigationVelocity( camera ), {} ); original.Check( ws );
    }
}

TEST_CASE( "Captured orbit owns Shift wheel without changing released block or clipping previews", "[map][gui][views][camera-orbit-wheel][block][clip]" )
{
    for ( const bool clip : { false, true } ) {
        CAPTURE( clip ); scene_t scene; auto *camera = scene.camera.get(); auto &ws = scene.session.workspace;
        MapWorkspace_SetTool( &ws, clip ? map_tool_t::CLIP : map_tool_t::BLOCK );
        if ( clip ) {
            map_clip_guide_t guide{}; guide.bHas = CY_TRUE; guide.extrusionAxis = 2u;
            guide.points[0] = { 16, -96, 0 }; guide.points[1] = { 16, 96, 0 }; MapWorkspace_SetClipGuide( &ws, guide );
        } else { MapWorkspace_SetEditPreview( &ws, scene.source ); REQUIRE( MapWorkspace_StageBlockPreview( &ws ) ); }
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0u );
        snapshot_t original( ws ); const auto preview = ws.editPreview;
        const auto *points = ws.editPreviewWire.points.pData; const auto pointCount = ws.editPreviewWire.points.nCount;
        const QPointF point( 300, 220 ), end( 320, 232 ); Mouse( camera, QEvent::MouseButtonPress, point );
        const f64 radius = Distance( MapCameraView_Position( camera ), scene.pivot );
        REQUIRE( Wheel( camera, point, 120, 0, Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier ) );
        CHECK( Distance( MapCameraView_Position( camera ), scene.pivot ) == Catch::Approx( radius - 64.0 ).margin( 1e-6 ) );
        Mouse( camera, QEvent::MouseMove, end ); Mouse( camera, QEvent::MouseButtonRelease, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.status == map_status_t::OK );
        CHECK( ws.editPreviewWire.points.pData == points ); CHECK( ws.editPreviewWire.points.nCount == pointCount );
        CheckPoint( ws.editPreview.bounds.box.minimum, preview.bounds.box.minimum ); CheckPoint( ws.editPreview.bounds.box.maximum, preview.bounds.box.maximum );
        if ( clip ) {
            REQUIRE( ws.editPreview.clipGuide.bHas ); CheckPoint( ws.editPreview.clipGuide.points[0], preview.clipGuide.points[0] );
            CheckPoint( ws.editPreview.clipGuide.points[1], preview.clipGuide.points[1] );
            CheckPoint( ws.editPreview.clipPlane.normal, preview.clipPlane.normal ); CHECK( ws.editPreview.clipPlane.d == preview.clipPlane.d );
        } else { CHECK( MapWorkspace_HasBlockPreview( &ws ) ); }
        original.Check( ws );
    }
}
