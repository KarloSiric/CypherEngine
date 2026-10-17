//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_UvEditor_Tests.cpp
//  Purpose: Tests the UV Editor: describing a face's texture coordinates,
//           the texture-space edits (move, turn, scale, shear), and the
//           panel's drags, each one undo step.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_UvEditor.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    // The top face's brush side (or mesh face) ID.
    u64 Top( u64 object, bool mesh )
    {
        for ( usize f = 0; f < ws.wire.faces.nCount; ++f ) {
            const auto &face = ws.wire.faces.pData[f];
            if ( face.id == object && face.normal.z > 0.9 ) { return mesh ? face.faceId : face.sideId; }
        }
        FAIL( "no top face" );
        return 0;
    }
    map_uv_face_t Describe( map_surface_ref_t surface )
    {
        map_uv_face_t face{};
        REQUIRE( MapTextureEdit_Describe( ws.pDocument, surface, &face ) == map_status_t::OK );
        return face;
    }
    usize Steps() { return EditorHistory_AppliedStepCount( &ws.history ); }
};

void Drag( QWidget *canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent press( QEvent::MouseButtonPress, from, canvas->mapToGlobal( from ), Qt::LeftButton, Qt::LeftButton, modifiers );
    QCoreApplication::sendEvent( canvas, &press );
    for ( int i = 1; i <= 4; ++i ) {
        const QPointF at = from + ( to - from ) * ( i / 4.0 );
        QMouseEvent move( QEvent::MouseMove, at, canvas->mapToGlobal( at ), Qt::NoButton, Qt::LeftButton, modifiers );
        QCoreApplication::sendEvent( canvas, &move );
    }
    QMouseEvent release( QEvent::MouseButtonRelease, to, canvas->mapToGlobal( to ), Qt::LeftButton, Qt::NoButton, modifiers );
    QCoreApplication::sendEvent( canvas, &release );
    QCoreApplication::processEvents();
}
} // namespace

TEST_CASE( "Texture-space edits move, turn, scale and shear face texture coordinates", "[map][gui][uv-editor]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 128, 128, 64 } );
    const map_surface_ref_t top{ box, s.Top( box, false ) };
    const map_uv_face_t before = s.Describe( top );
    REQUIRE( before.uvs.size() == 4u );
    CHECK( before.bBrush );
    CHECK_FALSE( before.material.empty() );
    // A move adds the delta to every corner.
    map_uv_edit_t edit{};
    edit.kind = map_uv_edit_kind_t::SHIFT;
    edit.delta = { 0.25, -0.5 };
    REQUIRE( MapTextureEdit_EditUv( s.ws.pDocument, { &top, 1 }, edit ) == map_status_t::OK );
    map_uv_face_t after = s.Describe( top );
    for ( usize i = 0; i < 4; ++i ) {
        CHECK( after.uvs[i].x == Catch::Approx( before.uvs[i].x + 0.25 ) );
        CHECK( after.uvs[i].y == Catch::Approx( before.uvs[i].y - 0.5 ) );
    }
    // A scale about a corner leaves that corner where it is.
    edit = {};
    edit.kind = map_uv_edit_kind_t::SCALE;
    edit.pivot = after.uvs[0];
    edit.factor = { 2.0, 2.0 };
    REQUIRE( MapTextureEdit_EditUv( s.ws.pDocument, { &top, 1 }, edit ) == map_status_t::OK );
    map_uv_face_t scaled = s.Describe( top );
    CHECK( scaled.uvs[0].x == Catch::Approx( after.uvs[0].x ) );
    CHECK( scaled.uvs[0].y == Catch::Approx( after.uvs[0].y ) );
    const f64 span = std::hypot( after.uvs[2].x - after.uvs[0].x, after.uvs[2].y - after.uvs[0].y );
    CHECK( std::hypot( scaled.uvs[2].x - scaled.uvs[0].x, scaled.uvs[2].y - scaled.uvs[0].y ) == Catch::Approx( span * 2.0 ) );
    // A quarter turn about the same corner keeps it and the distances.
    edit.kind = map_uv_edit_kind_t::ROTATE;
    edit.pivot = scaled.uvs[0];
    edit.radians = 3.14159265358979323846 / 2.0;
    REQUIRE( MapTextureEdit_EditUv( s.ws.pDocument, { &top, 1 }, edit ) == map_status_t::OK );
    const map_uv_face_t turned = s.Describe( top );
    CHECK( turned.uvs[0].x == Catch::Approx( scaled.uvs[0].x ) );
    const f64 dx = scaled.uvs[1].x - scaled.uvs[0].x, dy = scaled.uvs[1].y - scaled.uvs[0].y;
    CHECK( turned.uvs[1].x - turned.uvs[0].x == Catch::Approx( -dy ).margin( 1e-9 ) );
    CHECK( turned.uvs[1].y - turned.uvs[0].y == Catch::Approx( dx ).margin( 1e-9 ) );
    // A shear: u gains half of v (from the pivot), on a brush side and a mesh face alike.
    edit.kind = map_uv_edit_kind_t::SHEAR;
    edit.shear = { 0.5, 0.0 };
    edit.pivot = turned.uvs[2];
    REQUIRE( MapTextureEdit_EditUv( s.ws.pDocument, { &top, 1 }, edit ) == map_status_t::OK );
    const map_uv_face_t skewed = s.Describe( top );
    for ( usize i = 0; i < 4; ++i ) {
        CHECK( skewed.uvs[i].x == Catch::Approx( turned.uvs[i].x + 0.5 * ( turned.uvs[i].y - edit.pivot.y ) ).margin( 1e-9 ) );
        CHECK( skewed.uvs[i].y == Catch::Approx( turned.uvs[i].y ).margin( 1e-9 ) );
    }
    MapWorkspace_SetSelection( &s.ws, &box, 1 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.ws ) );
    const map_surface_ref_t face{ box, s.Top( box, true ) };
    const map_uv_face_t mesh = s.Describe( face );
    CHECK_FALSE( mesh.bBrush );
    edit.pivot = mesh.uvs[0];
    REQUIRE( MapTextureEdit_EditUv( s.ws.pDocument, { &face, 1 }, edit ) == map_status_t::OK );
    const map_uv_face_t sheared = s.Describe( face );
    for ( usize i = 0; i < sheared.uvs.size(); ++i ) {
        CHECK( sheared.uvs[i].x == Catch::Approx( mesh.uvs[i].x + 0.5 * ( mesh.uvs[i].y - mesh.uvs[0].y ) ) );
        CHECK( sheared.uvs[i].y == Catch::Approx( mesh.uvs[i].y ) );
    }
}

TEST_CASE( "The UV Editor's drags move, turn and scale the texture of the picked face", "[map][gui][uv-editor]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 128, 128, 64 } );
    std::unique_ptr<QWidget> panel( MapUvEditor_Create( nullptr, &s.ws ) );
    panel->resize( 600, 600 );
    panel->show();
    QCoreApplication::processEvents();
    CHECK( MapUvEditor_Status( panel.get() ).contains( QStringLiteral( "Faces mode" ) ) ); // Nothing picked yet.
    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::FACES );
    const map_surface_ref_t top{ box, s.Top( box, false ) };
    MapWorkspace_SelectBrushFace( &s.ws, box, top.face );
    QCoreApplication::processEvents();
    CHECK( MapUvEditor_Status( panel.get() ).startsWith( QStringLiteral( "1 face" ) ) );
    QWidget *canvas = MapUvEditor_Canvas( panel.get() );
    REQUIRE( canvas != nullptr );

    // Drag the face itself, inside it but clear of the handles around the
    // pivot (its centre): the texture moves, by whole texels.
    const map_uv_face_t start = s.Describe( top );
    QPointF centre{}, corner{};
    for ( const auto &uv : start.uvs ) { centre += MapUvEditor_ToScreen( panel.get(), uv ) / 4.0; }
    for ( const auto &uv : start.uvs ) {
        const QPointF at = MapUvEditor_ToScreen( panel.get(), uv ) - centre;
        if ( -at.x() - at.y() > -corner.x() - corner.y() ) { corner = at; }
    }
    const QPointF grab = centre + corner * 0.8;
    REQUIRE( QLineF( grab, centre ).length() > 48.0 + 26.0 + 12.0 );
    usize steps = s.Steps();
    Drag( canvas, grab, grab + QPointF( 40, 0 ) );
    CHECK( s.Steps() == ++steps );
    const map_uv_face_t moved = s.Describe( top );
    CHECK( moved.uvs[0].x > start.uvs[0].x );
    CHECK( moved.uvs[0].y == Catch::Approx( start.uvs[0].y ) );
    CHECK( MapWorkspace_HasBrushFace( &s.ws ) ); // The face stays picked.

    // The ring: a quarter turn about the pivot.
    const math::vec2d_t pivot = MapUvEditor_Pivot( panel.get() );
    const QPointF p = MapUvEditor_ToScreen( panel.get(), pivot );
    Drag( canvas, p + QPointF( 48, 0 ), p + QPointF( 0, 48 ) );
    CHECK( s.Steps() == ++steps );
    const map_uv_face_t turned = s.Describe( top );
    const f64 before = std::hypot( moved.uvs[0].x - pivot.x, moved.uvs[0].y - pivot.y );
    CHECK( std::hypot( turned.uvs[0].x - pivot.x, turned.uvs[0].y - pivot.y ) == Catch::Approx( before ).epsilon( 1e-6 ) );
    CHECK( std::hypot( turned.uvs[0].x - moved.uvs[0].x, turned.uvs[0].y - moved.uvs[0].y ) > 1e-6 );

    // The U square: twice as far out doubles U about the pivot.
    const QPointF handle = p + QPointF( 48 + 26, 0 );
    Drag( canvas, handle, p + QPointF( 2 * ( 48 + 26 ), 0 ) );
    CHECK( s.Steps() == ++steps );
    const map_uv_face_t scaled = s.Describe( top );
    CHECK( scaled.uvs[1].x - pivot.x == Catch::Approx( 2.0 * ( turned.uvs[1].x - pivot.x ) ).margin( 1e-9 ) );
    CHECK( scaled.uvs[1].y == Catch::Approx( turned.uvs[1].y ).margin( 1e-9 ) );

    // Alt on the U square shears: V gains a share of U, U stays.
    Drag( canvas, handle, handle + QPointF( 0, 40 ), Qt::AltModifier );
    CHECK( s.Steps() == ++steps );
    const map_uv_face_t sheared = s.Describe( top );
    bool skewed = false;
    for ( usize i = 0; i < 4; ++i ) {
        CHECK( sheared.uvs[i].x == Catch::Approx( scaled.uvs[i].x ).margin( 1e-9 ) );
        skewed |= std::fabs( sheared.uvs[i].y - scaled.uvs[i].y ) > 1e-6;
    }
    CHECK( skewed );
    CHECK( MapWorkspace_HasBrushFace( &s.ws ) );

    // Undo takes the shear back and the panel shows it.
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    QCoreApplication::processEvents();
    CHECK( s.Describe( top ).uvs[1].y == Catch::Approx( scaled.uvs[1].y ) );
}

TEST_CASE( "Alt and a square shear a mesh face's texture in the UV Editor", "[map][gui][uv-editor]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 128, 128, 64 } );
    MapWorkspace_SetSelection( &s.ws, &box, 1 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &s.ws ) );
    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::FACES );
    const u64 face = s.Top( box, true );
    MapWorkspace_SelectMeshFace( &s.ws, box, face );
    std::unique_ptr<QWidget> panel( MapUvEditor_Create( nullptr, &s.ws ) );
    panel->resize( 600, 600 );
    panel->show();
    QCoreApplication::processEvents();
    const map_uv_face_t start = s.Describe( { box, face } );
    const QPointF p = MapUvEditor_ToScreen( panel.get(), MapUvEditor_Pivot( panel.get() ) );
    const usize steps = s.Steps();
    Drag( MapUvEditor_Canvas( panel.get() ), p + QPointF( 48 + 26, 0 ), p + QPointF( 48 + 26, 37 ), Qt::AltModifier );
    CHECK( s.Steps() == steps + 1u );
    const map_uv_face_t sheared = s.Describe( { box, face } );
    bool changed = false;
    for ( usize i = 0; i < start.uvs.size(); ++i ) { changed |= std::fabs( sheared.uvs[i].y - start.uvs[i].y ) > 1e-6; }
    CHECK( changed );
    CHECK( MapWorkspace_MeshFaces( &s.ws ).nCount == 1u ); // The face stays picked.
}


namespace
{
void UvMouse( QWidget *canvas, QEvent::Type kind, QPointF at, Qt::MouseButton button,
              Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( kind, at, canvas->mapToGlobal( at ), button, buttons, modifiers );
    QCoreApplication::sendEvent( canvas, &event );
}

void CheckUvUnchanged( const map_uv_face_t &actual, const map_uv_face_t &expected )
{
    REQUIRE( actual.uvs.size() == expected.uvs.size() );
    CHECK( actual.material == expected.material );
    for ( usize i = 0; i < actual.uvs.size(); ++i ) {
        CHECK( actual.uvs[i].x == Catch::Approx( expected.uvs[i].x ).margin( 1e-12 ) );
        CHECK( actual.uvs[i].y == Catch::Approx( expected.uvs[i].y ).margin( 1e-12 ) );
    }
}

map_surface_ref_t SelectUvTop( session_t &s, bool mesh )
{
    // New documents retain the current element mode; whole-object conversion
    // must be performed in an object mode before picking the new map's face.
    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::OBJECTS );
    const u64 box = s.Box( { 0, 0, 0 }, { 128, 128, 64 } );
    if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &s.ws ) ); }
    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::FACES );
    const map_surface_ref_t top{ box, s.Top( box, mesh ) };
    if ( mesh ) { MapWorkspace_SelectMeshFace( &s.ws, box, top.face ); }
    else { MapWorkspace_SelectBrushFace( &s.ws, box, top.face ); }
    return top;
}

std::unique_ptr<QWidget> ShowUvPanel( session_t &s )
{
    std::unique_ptr<QWidget> panel( MapUvEditor_Create( nullptr, &s.ws ) );
    panel->resize( 600, 600 );
    panel->show();
    QCoreApplication::processEvents();
    return panel;
}

void CheckUvSelected( session_t &s, map_surface_ref_t top, bool mesh )
{
    if ( mesh ) {
        REQUIRE( MapWorkspace_MeshFaces( &s.ws ).nCount == 1u );
        CHECK( s.ws.selectedMeshFaceObject == top.object );
        CHECK( MapWorkspace_MeshFaces( &s.ws ).pData[0] == top.face );
    } else {
        REQUIRE( MapWorkspace_HasBrushFace( &s.ws ) );
        CHECK( s.ws.selectedBrushFaceObject == top.object );
        CHECK( s.ws.selectedBrushFaceSide == top.face );
    }
}
} // namespace

TEST_CASE( "UV stationary clicks never replay a completed edit and move carries the pivot once", "[map][gui][uv-editor][uv-gesture-safety]" )
{
    for ( const bool mesh : { false, true } ) {
        CAPTURE( mesh );
        session_t s;
        const auto top = SelectUvTop( s, mesh );
        auto panel = ShowUvPanel( s );
        auto *canvas = MapUvEditor_Canvas( panel.get() );
        REQUIRE( canvas != nullptr );
        const auto before = s.Describe( top );
        const auto pivotBefore = MapUvEditor_Pivot( panel.get() );
        const QPointF grab = MapUvEditor_ToScreen( panel.get(), pivotBefore ) + QPointF( -110, -110 );
        const usize stepsBefore = s.Steps();
        Drag( canvas, grab, grab + QPointF( 32, 16 ) );
        REQUIRE( s.Steps() == stepsBefore + 1u );
        const auto moved = s.Describe( top );
        const auto pivot = MapUvEditor_Pivot( panel.get() );
        const f64 dx = moved.uvs[0].x - before.uvs[0].x, dy = moved.uvs[0].y - before.uvs[0].y;
        REQUIRE( std::fabs( dx ) > 1e-6 );
        CHECK( pivot.x == Catch::Approx( pivotBefore.x + dx ) );
        CHECK( pivot.y == Catch::Approx( pivotBefore.y + dy ) );
        const QPointF p = MapUvEditor_ToScreen( panel.get(), pivot );
        struct click_t { QPointF delta; Qt::KeyboardModifiers modifiers; };
        const click_t clicks[]{ { { -110, -110 }, Qt::NoModifier }, { { 48, 0 }, Qt::NoModifier },
            { { 74, 0 }, Qt::NoModifier }, { { 0, 74 }, Qt::NoModifier },
            { { 74, 0 }, Qt::AltModifier }, { { 0, 74 }, Qt::AltModifier }, { {}, Qt::NoModifier } };
        for ( usize i = 0; i < std::size( clicks ); ++i ) {
            CAPTURE( i );
            const auto &click = clicks[i];
            UvMouse( canvas, QEvent::MouseMove, p + click.delta, Qt::NoButton, Qt::NoButton );
            UvMouse( canvas, QEvent::MouseButtonPress, p + click.delta, Qt::LeftButton, Qt::LeftButton, click.modifiers );
            UvMouse( canvas, QEvent::MouseButtonRelease, p + click.delta, Qt::LeftButton, Qt::NoButton, click.modifiers );
            CHECK( s.Steps() == stepsBefore + 1u );
            CheckUvUnchanged( s.Describe( top ), moved );
            CHECK( MapUvEditor_Pivot( panel.get() ).x == Catch::Approx( pivot.x ) );
            CHECK( MapUvEditor_Pivot( panel.get() ).y == Catch::Approx( pivot.y ) );
            CheckUvSelected( s, top, mesh );
        }
        // Moving out and back, and sub-texel movement rounded to zero, are also no-ops.
        UvMouse( canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton );
        UvMouse( canvas, QEvent::MouseMove, grab + QPointF( 20, 0 ), Qt::NoButton, Qt::LeftButton );
        UvMouse( canvas, QEvent::MouseMove, grab, Qt::NoButton, Qt::LeftButton );
        UvMouse( canvas, QEvent::MouseButtonRelease, grab, Qt::LeftButton, Qt::NoButton );
        Drag( canvas, grab, grab + QPointF( 0.01, 0.01 ) );
        CHECK( s.Steps() == stepsBefore + 1u );
        CheckUvUnchanged( s.Describe( top ), moved );
    }
}

TEST_CASE( "Interrupted UV previews cannot commit on a late release and accept a fresh drag", "[map][gui][uv-editor][uv-gesture-safety]" )
{
    enum class interruption_t { ESCAPE, FOCUS, HIDE, DEACTIVATE, UNGRAB, SELECTION, DOCUMENT, WHEEL, LOST_BUTTON, RESIZE };
    for ( const bool mesh : { false, true } ) {
        for ( const auto reason : { interruption_t::ESCAPE, interruption_t::FOCUS, interruption_t::HIDE,
                 interruption_t::DEACTIVATE, interruption_t::UNGRAB, interruption_t::SELECTION,
                 interruption_t::DOCUMENT, interruption_t::WHEEL, interruption_t::LOST_BUTTON, interruption_t::RESIZE } ) {
            CAPTURE( mesh, static_cast<int>( reason ) );
            session_t s;
            auto top = SelectUvTop( s, mesh );
            auto panel = ShowUvPanel( s );
            auto *canvas = MapUvEditor_Canvas( panel.get() );
            REQUIRE( canvas != nullptr );
            // Commit a custom pivot before the interrupted edit; cancellation keeps it.
            QPointF p = MapUvEditor_ToScreen( panel.get(), MapUvEditor_Pivot( panel.get() ) );
            Drag( canvas, p, p + QPointF( 26, 18 ), Qt::ControlModifier );
            const auto pivot = MapUvEditor_Pivot( panel.get() );
            QPointF grab = MapUvEditor_ToScreen( panel.get(), pivot ) + QPointF( -110, -110 );
            auto before = s.Describe( top );
            usize steps = s.Steps();
            UvMouse( canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton );
            UvMouse( canvas, QEvent::MouseMove, grab + QPointF( 40, 15 ), Qt::NoButton, Qt::LeftButton );
            CHECK( s.Steps() == steps );
            CheckUvUnchanged( s.Describe( top ), before );
            switch ( reason ) {
                case interruption_t::ESCAPE: {
                    QKeyEvent override( QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier );
                    override.ignore();
                    QCoreApplication::sendEvent( canvas, &override );
                    CHECK( override.isAccepted() );
                    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
                    QCoreApplication::sendEvent( canvas, &escape );
                    break;
                }
                case interruption_t::FOCUS: {
                    QFocusEvent event( QEvent::FocusOut ); QCoreApplication::sendEvent( canvas, &event ); break;
                }
                case interruption_t::HIDE: canvas->hide(); canvas->show(); break;
                case interruption_t::DEACTIVATE: {
                    QEvent event( QEvent::WindowDeactivate ); QCoreApplication::sendEvent( canvas, &event ); break;
                }
                case interruption_t::UNGRAB: {
                    QEvent event( QEvent::UngrabMouse ); QCoreApplication::sendEvent( canvas, &event ); break;
                }
                case interruption_t::SELECTION:
                    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::OBJECTS );
                    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::FACES );
                    if ( mesh ) { MapWorkspace_SelectMeshFace( &s.ws, top.object, top.face ); }
                    else { MapWorkspace_SelectBrushFace( &s.ws, top.object, top.face ); }
                    break;
                case interruption_t::DOCUMENT:
                    REQUIRE( MapWorkspace_New( &s.ws ) == map_status_t::OK );
                    top = SelectUvTop( s, mesh ); // The new map may reuse both IDs.
                    before = s.Describe( top );
                    steps = s.Steps();
                    break;
                case interruption_t::WHEEL: {
                    QWheelEvent wheel( grab, canvas->mapToGlobal( grab ), {}, QPoint( 0, 120 ), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false );
                    QCoreApplication::sendEvent( canvas, &wheel );
                    break;
                }
                case interruption_t::LOST_BUTTON:
                    UvMouse( canvas, QEvent::MouseMove, grab + QPointF( 45, 15 ), Qt::NoButton, Qt::NoButton );
                    break;
                case interruption_t::RESIZE:
                    panel->resize( 640, 620 );
                    QCoreApplication::processEvents();
                    break;
            }
            UvMouse( canvas, QEvent::MouseMove, grab + QPointF( 60, 25 ), Qt::NoButton, Qt::LeftButton );
            UvMouse( canvas, QEvent::MouseButtonRelease, grab + QPointF( 60, 25 ), Qt::LeftButton, Qt::NoButton );
            CHECK( s.Steps() == steps );
            CheckUvUnchanged( s.Describe( top ), before );
            CheckUvSelected( s, top, mesh );
            if ( reason != interruption_t::DOCUMENT && reason != interruption_t::SELECTION ) {
                CHECK( MapUvEditor_Pivot( panel.get() ).x == Catch::Approx( pivot.x ) );
                CHECK( MapUvEditor_Pivot( panel.get() ).y == Catch::Approx( pivot.y ) );
            }
            // The cancelled transaction cannot poison the next one.
            grab = MapUvEditor_ToScreen( panel.get(), MapUvEditor_Pivot( panel.get() ) ) + QPointF( -110, -110 );
            Drag( canvas, grab, grab + QPointF( 24, 0 ) );
            CHECK( s.Steps() == steps + 1u );
            const auto moved = s.Describe( top );
            CHECK( std::fabs( moved.uvs[0].x - before.uvs[0].x ) > 1e-6 );
            REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
            CheckUvUnchanged( s.Describe( top ), before );
            REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK );
            CheckUvUnchanged( s.Describe( top ), moved );
        }
    }
}

TEST_CASE( "UV pivot cancellation restores its prior location and an unrelated release cannot publish", "[map][gui][uv-editor][uv-gesture-safety]" )
{
    session_t s;
    const auto top = SelectUvTop( s, true );
    auto panel = ShowUvPanel( s );
    auto *canvas = MapUvEditor_Canvas( panel.get() );
    const auto before = s.Describe( top );
    const usize steps = s.Steps();
    const auto pivot = MapUvEditor_Pivot( panel.get() );
    const QPointF p = MapUvEditor_ToScreen( panel.get(), pivot );
    UvMouse( canvas, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton );
    UvMouse( canvas, QEvent::MouseMove, p + QPointF( 20, 30 ), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier );
    REQUIRE( std::fabs( MapUvEditor_Pivot( panel.get() ).x - pivot.x ) > 1e-6 );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( canvas, &escape );
    UvMouse( canvas, QEvent::MouseButtonRelease, p + QPointF( 20, 30 ), Qt::LeftButton, Qt::NoButton );
    CHECK( MapUvEditor_Pivot( panel.get() ).x == Catch::Approx( pivot.x ) );
    CHECK( MapUvEditor_Pivot( panel.get() ).y == Catch::Approx( pivot.y ) );
    const QPointF grab = p + QPointF( -110, -110 );
    UvMouse( canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton );
    UvMouse( canvas, QEvent::MouseMove, grab + QPointF( 30, 0 ), Qt::NoButton, Qt::LeftButton );
    UvMouse( canvas, QEvent::MouseButtonRelease, grab + QPointF( 30, 0 ), Qt::RightButton, Qt::LeftButton );
    CHECK( s.Steps() == steps );
    CheckUvUnchanged( s.Describe( top ), before );
    UvMouse( canvas, QEvent::MouseButtonRelease, grab + QPointF( 30, 0 ), Qt::LeftButton, Qt::NoButton );
    CHECK( s.Steps() == steps + 1u );
    CHECK( std::fabs( s.Describe( top ).uvs[0].x - before.uvs[0].x ) > 1e-6 );
}
