//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_PanWheel_Tests.cpp
//  Purpose: Verifies continuous orthographic pan across wheel zoom, including
//           captured sensitivity, cancellation, and released edit ownership.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Views.h"
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
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
void CheckPoint( QPointF actual, QPointF expected )
{
    CHECK( std::abs( actual.x() - expected.x() ) < 1e-6 );
    CHECK( std::abs( actual.y() - expected.y() ) < 1e-6 );
}
void CheckPoint( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( std::abs( actual.x - expected.x ) < 1e-6 );
    CHECK( std::abs( actual.y - expected.y ) < 1e-6 );
    CHECK( std::abs( actual.z - expected.z ) < 1e-6 );
}

void Mouse( QWidget *view, QEvent::Type type, QPointF position, Qt::MouseButton button,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, position, view->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons( button ), modifiers );
    QCoreApplication::sendEvent( view, &event );
}
void Key( QWidget *view, QEvent::Type type, Qt::Key key )
{
    QKeyEvent event( type, key, Qt::NoModifier ); QCoreApplication::sendEvent( view, &event );
}
bool Wheel( QWidget *view, QPointF position, int angle, Qt::MouseButton button,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QWheelEvent event( position, view->mapToGlobal( position ), QPoint(), QPoint( 0, angle ), button,
                       modifiers, Qt::NoScrollPhase, false );
    event.ignore(); QCoreApplication::sendEvent( view, &event ); return event.isAccepted();
}
QPointF Center( QWidget *view ) { return MapOrthoView_ViewToWorld( view, QPointF( view->width() * 0.5, view->height() * 0.5 ) ); }

enum class pan_t { MIDDLE, SPACE, CAMERA, NEUTRAL };
Qt::MouseButton Button( pan_t pan ) { return pan == pan_t::MIDDLE ? Qt::MiddleButton : Qt::LeftButton; }
void Begin( map_workspace_t &ws, QWidget *view, pan_t pan, QPointF start )
{
    MapWorkspace_SetTool( &ws, pan == pan_t::CAMERA ? map_tool_t::CAMERA : pan == pan_t::NEUTRAL ? map_tool_t::NONE : map_tool_t::SELECT );
    if ( pan == pan_t::SPACE ) { Key( view, QEvent::KeyPress, Qt::Key_Space ); }
    Mouse( view, QEvent::MouseButtonPress, start, Button( pan ) );
}
void End( QWidget *view, pan_t pan, QPointF end )
{
    Mouse( view, QEvent::MouseButtonRelease, end, Button( pan ) );
    if ( pan == pan_t::SPACE ) { Key( view, QEvent::KeyRelease, Qt::Key_Space ); }
}

struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    settings_document_t settings{};
    map_bounds_t source{};
    std::unique_ptr<QWidget> view;
    explicit session_t( map_ortho_axes_t axes ) {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) ); REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
        Real( "editor.camera.pan_sensitivity", 1.75 ); Real( "editor.camera.zoom_sensitivity", 1.0 );
        Bool( "editor.camera.invert_wheel", false ); Bool( "editor.camera.link_2d_views", false );
        MapBounds_AddPoint( source, { -64, -64, 0 } ); MapBounds_AddPoint( source, { 64, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, source ) );
        view.reset( MapOrthoView_Create( nullptr, &ws, axes ) ); REQUIRE( view != nullptr );
        view->resize( 800, 600 ); view->show(); QCoreApplication::processEvents();
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    }
    ~session_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); }
    void Write( const char *path, const setting_value_t &value ) {
        const auto *descriptor = EditorSettings_Find( &gui.settings, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
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
        REQUIRE( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.tool == tool );
        CHECK( ws.selection.revision == selectionRevision ); REQUIRE( EditorSelection_Count( &ws.selection ) == 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); REQUIRE( ws.wire.points.nCount == points.size() );
        for ( usize i = 0; i < points.size(); ++i ) { CheckPoint( ws.wire.points.pData[i], points[i] ); }
    }
};
}

TEST_CASE( "Every orthographic pan gesture retains wheel zoom and continues from its current pointer", "[map][gui][views][ortho-pan-wheel]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const auto pan : { pan_t::MIDDLE, pan_t::SPACE, pan_t::CAMERA, pan_t::NEUTRAL } ) {
            for ( const bool cursorAnchor : { false, true } ) {
                CAPTURE( static_cast<int>( axes ), static_cast<int>( pan ), cursorAnchor ); session_t session( axes ); auto *view = session.view.get();
                session.Bool( "editor.camera.zoom_to_cursor", cursorAnchor );
                const QPointF start( 235, 175 ), moved = start + QPointF( 45, 26 ), step( 20, -12 ), wheelPoint( 650, 150 );
                const auto initial = Center( view ); Begin( session.ws, view, pan, start ); snapshot_t original( session.ws );
                Mouse( view, QEvent::MouseMove, moved, Button( pan ) ); REQUIRE( QLineF( initial, Center( view ) ).length() > 0.1 );
                const f64 oldZoom = MapOrthoView_Zoom( view ); const QPointF anchor = cursorAnchor ? wheelPoint : QPointF( 400, 300 );
                const QPointF anchorWorld = MapOrthoView_ViewToWorld( view, anchor ); REQUIRE( Wheel( view, wheelPoint, 120, Button( pan ) ) );
                const f64 zoom = MapOrthoView_Zoom( view ); REQUIRE( zoom > oldZoom );
                CheckPoint( MapOrthoView_ViewToWorld( view, anchor ), anchorWorld );
                const QPointF afterWheel = Center( view ); Mouse( view, QEvent::MouseMove, moved, Button( pan ) );
                CheckPoint( Center( view ), afterWheel ); CHECK( MapOrthoView_Zoom( view ) == zoom );
                Mouse( view, QEvent::MouseMove, moved + step, Button( pan ) );
                CheckPoint( Center( view ) - afterWheel, QPointF( -step.x() * 1.75 / zoom, step.y() * 1.75 / zoom ) );
                const QPointF continued = Center( view ); End( view, pan, moved + step );
                CheckPoint( Center( view ), continued ); CHECK( MapOrthoView_Zoom( view ) == zoom ); original.Check( session.ws );
            }
        }
    }
}

TEST_CASE( "Wheel rebasing retains captured pan sensitivity until a fresh press", "[map][gui][views][ortho-pan-wheel][settings]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const bool cursorAnchor : { false, true } ) {
            CAPTURE( static_cast<int>( axes ), cursorAnchor ); session_t session( axes ); auto *view = session.view.get();
            session.Bool( "editor.camera.zoom_to_cursor", cursorAnchor );
            const QPointF start( 260, 190 ), moved = start + QPointF( 40, 20 ), step( 24, -16 );
            Begin( session.ws, view, pan_t::MIDDLE, start ); snapshot_t original( session.ws );
            Mouse( view, QEvent::MouseMove, moved, Qt::MiddleButton ); const auto beforeSetting = Center( view );
            session.Real( "editor.camera.pan_sensitivity", 0.5 ); CheckPoint( Center( view ), beforeSetting );
            REQUIRE( Wheel( view, QPointF( 640, 160 ), -120, Qt::MiddleButton ) );
            const f64 zoom = MapOrthoView_Zoom( view ); const auto afterWheel = Center( view );
            Mouse( view, QEvent::MouseMove, moved, Qt::MiddleButton ); CheckPoint( Center( view ), afterWheel );
            Mouse( view, QEvent::MouseMove, moved + step, Qt::MiddleButton );
            CheckPoint( Center( view ) - afterWheel, QPointF( -step.x() * 1.75 / zoom, step.y() * 1.75 / zoom ) ); End( view, pan_t::MIDDLE, moved + step );
            const auto freshCenter = Center( view ); Begin( session.ws, view, pan_t::MIDDLE, start );
            Mouse( view, QEvent::MouseMove, start + step, Qt::MiddleButton );
            CheckPoint( Center( view ) - freshCenter, QPointF( -step.x() * 0.5 / zoom, step.y() * 0.5 / zoom ) ); End( view, pan_t::MIDDLE, start + step );
            original.Check( session.ws );
        }
    }
}

TEST_CASE( "Wheel before the first pan movement rebases from the actual press location", "[map][gui][views][ortho-pan-wheel][pickup]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const bool cursorAnchor : { false, true } ) {
            CAPTURE( static_cast<int>( axes ), cursorAnchor ); session_t session( axes ); auto *view = session.view.get();
            session.Bool( "editor.camera.zoom_to_cursor", cursorAnchor ); const QPointF start( 240, 180 ), wheelPoint( 660, 150 ), step( 18, 11 );
            Begin( session.ws, view, pan_t::MIDDLE, start ); snapshot_t original( session.ws );
            const QPointF anchor = cursorAnchor ? wheelPoint : QPointF( 400, 300 );
            const QPointF anchorWorld = MapOrthoView_ViewToWorld( view, anchor ); REQUIRE( Wheel( view, wheelPoint, 120, Qt::MiddleButton ) );
            CheckPoint( MapOrthoView_ViewToWorld( view, anchor ), anchorWorld ); const auto afterWheel = Center( view );
            Mouse( view, QEvent::MouseMove, start, Qt::MiddleButton ); CheckPoint( Center( view ), afterWheel );
            const f64 zoom = MapOrthoView_Zoom( view ); Mouse( view, QEvent::MouseMove, start + step, Qt::MiddleButton );
            CheckPoint( Center( view ) - afterWheel, QPointF( -step.x() * 1.75 / zoom, step.y() * 1.75 / zoom ) );
            End( view, pan_t::MIDDLE, start + step ); original.Check( session.ws );
        }
    }
}

TEST_CASE( "Zoom limits during pan remain stable and Escape or focus loss ignores late movement", "[map][gui][views][ortho-pan-wheel][limits][focus]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const bool maximum : { false, true } ) {
            for ( const bool escape : { false, true } ) {
                CAPTURE( static_cast<int>( axes ), maximum, escape ); session_t session( axes ); auto *view = session.view.get();
                const QPointF start( 260, 180 ), moved = start + QPointF( 40, 25 ), wheelPoint( 670, 130 );
                Begin( session.ws, view, pan_t::SPACE, start ); snapshot_t original( session.ws );
                Mouse( view, QEvent::MouseMove, moved, Qt::LeftButton ); REQUIRE( Wheel( view, wheelPoint, maximum ? 120000 : -120000, Qt::LeftButton ) );
                const f64 zoom = maximum ? MAP_VIEW_ZOOM_MAX : MAP_VIEW_ZOOM_MIN; CHECK( MapOrthoView_Zoom( view ) == zoom );
                const auto saturated = Center( view ); REQUIRE( Wheel( view, wheelPoint, maximum ? 120 : -120, Qt::LeftButton ) );
                CheckPoint( Center( view ), saturated ); Mouse( view, QEvent::MouseMove, moved, Qt::LeftButton ); CheckPoint( Center( view ), saturated );
                if ( escape ) { Key( view, QEvent::KeyPress, Qt::Key_Escape ); }
                else { QFocusEvent event( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( view, &event ); }
                Mouse( view, QEvent::MouseMove, moved + QPointF( 70, 50 ), Qt::LeftButton ); End( view, pan_t::SPACE, moved + QPointF( 70, 50 ) );
                CheckPoint( Center( view ), saturated ); CHECK( MapOrthoView_Zoom( view ) == zoom ); original.Check( session.ws );
            }
        }
    }
}

TEST_CASE( "Orthographic pan owns Shift wheel over released Block and Clip previews", "[map][gui][views][ortho-pan-wheel][block][clip]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const bool clip : { false, true } ) {
            for ( const bool space : { false, true } ) {
                CAPTURE( static_cast<int>( axes ), clip, space ); session_t session( axes ); auto *view = session.view.get();
                MapWorkspace_SetTool( &session.ws, clip ? map_tool_t::CLIP : map_tool_t::BLOCK );
                if ( clip ) {
                    map_clip_guide_t guide{}; guide.bHas = CY_TRUE; guide.extrusionAxis = 2u;
                    guide.points[0] = { 16, -96, 0 }; guide.points[1] = { 16, 96, 0 }; MapWorkspace_SetClipGuide( &session.ws, guide );
                } else { MapWorkspace_SetEditPreview( &session.ws, session.source ); REQUIRE( MapWorkspace_StageBlockPreview( &session.ws ) ); }
                REQUIRE( session.ws.editPreview.bActive ); REQUIRE( session.ws.editPreview.status == map_status_t::OK );
                REQUIRE( session.ws.editPreviewWire.points.nCount != 0u ); snapshot_t original( session.ws ); const auto preview = session.ws.editPreview;
                const auto *points = session.ws.editPreviewWire.points.pData; const auto count = session.ws.editPreviewWire.points.nCount;
                const auto button = space ? Qt::LeftButton : Qt::MiddleButton; const QPointF start( 245, 180 ), moved = start + QPointF( 30, 20 );
                if ( space ) { Key( view, QEvent::KeyPress, Qt::Key_Space ); }
                Mouse( view, QEvent::MouseButtonPress, start, button ); Mouse( view, QEvent::MouseMove, moved, button );
                const f64 beforeZoom = MapOrthoView_Zoom( view ); REQUIRE( Wheel( view, QPointF( 640, 150 ), 120, button, Qt::ShiftModifier ) );
                CHECK( MapOrthoView_Zoom( view ) > beforeZoom ); const auto afterWheel = Center( view );
                Mouse( view, QEvent::MouseMove, moved, button ); CheckPoint( Center( view ), afterWheel );
                Mouse( view, QEvent::MouseButtonRelease, moved, button ); if ( space ) { Key( view, QEvent::KeyRelease, Qt::Key_Space ); }
                REQUIRE( session.ws.editPreview.bActive ); CHECK( session.ws.editPreview.status == map_status_t::OK );
                CHECK( session.ws.editPreviewWire.points.pData == points ); CHECK( session.ws.editPreviewWire.points.nCount == count );
                CheckPoint( session.ws.editPreview.bounds.box.minimum, preview.bounds.box.minimum ); CheckPoint( session.ws.editPreview.bounds.box.maximum, preview.bounds.box.maximum );
                if ( clip ) {
                    REQUIRE( session.ws.editPreview.clipGuide.bHas ); CheckPoint( session.ws.editPreview.clipGuide.points[0], preview.clipGuide.points[0] );
                    CheckPoint( session.ws.editPreview.clipGuide.points[1], preview.clipGuide.points[1] );
                    CheckPoint( session.ws.editPreview.clipPlane.normal, preview.clipPlane.normal ); CHECK( session.ws.editPreview.clipPlane.d == preview.clipPlane.d );
                } else { CHECK( MapWorkspace_HasBlockPreview( &session.ws ) ); }
                original.Check( session.ws );
                if ( !clip ) {
                    // Releasing navigation restores the ordinary staged-depth
                    // gesture instead of permanently shadowing it with zoom.
                    const f64 idleZoom = MapOrthoView_Zoom( view );
                    REQUIRE( Wheel( view, moved, 120, Qt::NoButton, Qt::ShiftModifier ) );
                    CHECK( MapOrthoView_Zoom( view ) == idleZoom ); REQUIRE( MapWorkspace_HasBlockPreview( &session.ws ) );
                    CheckPoint( session.ws.editPreview.bounds.box.minimum, preview.bounds.box.minimum );
                    auto expected = preview.bounds.box.maximum; expected.z += session.ws.gridSize;
                    CheckPoint( session.ws.editPreview.bounds.box.maximum, expected ); original.Check( session.ws );
                }
            }
        }
    }
}
