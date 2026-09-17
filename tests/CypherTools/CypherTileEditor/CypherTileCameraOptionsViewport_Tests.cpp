//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies configurable navigation and discoverable camera controls.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileRenderViewport.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>
#include <QWheelEvent>


using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace
{
void EnsureApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileCameraOptionsTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

void Pointer( QWidget &view, QEvent::Type type, QPointF point,
    Qt::MouseButton button, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, point, point, type == QEvent::MouseMove ? Qt::NoButton : button,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers );
    QApplication::sendEvent( &view, &event );
}

void Wheel( QWidget &view, int delta )
{
    QWheelEvent event( { 40.0, 40.0 }, { 40.0, 40.0 }, {}, { 0, delta },
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &view, &event );
}

void PixelWheel( QWidget &view, int delta )
{
    QWheelEvent event( { 40.0, 40.0 }, { 40.0, 40.0 }, { 0, delta }, {},
        Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false );
    QApplication::sendEvent( &view, &event );
}

int CountColor( const QImage &image, const QColor &color )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y )
        for ( int x = 0; x < image.width(); ++x )
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) ++count;
    return count;
}
}

TEST_CASE( "Middle drag pans and releases only from its initiating button",
    "[TileEditor][Camera][Options]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    tile_camera_settings_t settings;
    settings.panSensitivity = 2.0f;
    view.setCameraSettings( settings, true );
    auto expected = view.camera();
    CypherTileCamera_Pan( expected, 50, -20, view.height() );
    Pointer( view, QEvent::MouseButtonPress, { 40, 40 }, Qt::MiddleButton );
    REQUIRE( view.isNavigating() );
    Pointer( view, QEvent::MouseMove, { 90, 20 }, Qt::MiddleButton );
    CHECK( math::Vec3_NearlyEquals( view.camera().position, expected.position, 0.00001f, 0.00001f ) );
    CHECK( view.camera().yawRadians == expected.yawRadians );
    CHECK( view.camera().pitchRadians == expected.pitchRadians );
    Pointer( view, QEvent::MouseButtonRelease, { 90, 20 }, Qt::RightButton );
    CHECK( view.isNavigating() );
    Pointer( view, QEvent::MouseButtonRelease, { 90, 20 }, Qt::MiddleButton );
    CHECK_FALSE( view.isNavigating() );

    for ( const auto cancel : { QEvent::FocusOut, QEvent::KeyPress } ) {
        Pointer( view, QEvent::MouseButtonPress, { 40, 40 }, Qt::MiddleButton );
        if ( cancel == QEvent::FocusOut ) {
            QFocusEvent event( cancel );
            QApplication::sendEvent( &view, &event );
        } else {
            QKeyEvent event( cancel, Qt::Key_Escape, Qt::NoModifier );
            QApplication::sendEvent( &view, &event );
        }
        CHECK_FALSE( view.isNavigating() );
    }
}

TEST_CASE( "Camera user controls report preferences without moving the view or echoing settings",
    "[TileEditor][Camera][Options]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    int changes = 0;
    tile_camera_settings_t reported;
    bool fly = true;
    view.setCameraChangeCallback( [&]( const tile_camera_settings_t &settings, bool isFly ) {
        ++changes;
        reported = settings;
        fly = isFly;
    } );
    view.setCameraSettings( {}, true );
    CHECK( changes == 0 );
    const auto before = view.camera();
    view.setCameraMode( tile_camera_mode_t::ORBIT );
    REQUIRE( changes == 1 );
    CHECK_FALSE( fly );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( math::Vec3_EqualsExact( view.camera().position, before.position ) );
    CHECK( view.camera().yawRadians == before.yawRadians );
    view.setCameraMode( tile_camera_mode_t::FLY );
    view.setMoveSpeed( 20.0f );
    CHECK( fly );
    CHECK( reported.moveSpeed == 20.0f );
    const int beforeWheel = changes;
    Wheel( view, 120 );
    CHECK( changes == beforeWheel + 1 );
    CHECK( reported.moveSpeed > 20.0f );
    CHECK( reported.moveSpeed == view.camera().settings.moveSpeed );
    CHECK( math::Vec3_EqualsExact( view.camera().position, before.position ) );
    Pointer( view, QEvent::MouseButtonPress, { 40, 40 }, Qt::RightButton );
    REQUIRE( view.isNavigating() );
    view.setCameraSettings( reported, fly );
    CHECK( view.isNavigating() );
    CHECK( changes == beforeWheel + 1 );
}

TEST_CASE( "High-resolution trackpad scrolling changes orbit distance",
    "[TileEditor][Camera][Options][Trackpad]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setCameraSettings( {}, false );
    const float before = view.camera().orbitDistance;
    PixelWheel( view, 40 );
    CHECK( view.camera().orbitDistance < before );
}

TEST_CASE( "Wheel zooms the active auto-orbit view before restoring Fly preference",
    "[TileEditor][Camera][Options][AutoOrbit]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setCameraSettings( {}, true );
    const float speedBefore = view.camera().settings.moveSpeed;
    const float distanceBefore = view.camera().orbitDistance;
    view.setAutoOrbitEnabled( true );
    REQUIRE( view.isAutoOrbiting() );
    REQUIRE( view.camera().mode == tile_camera_mode_t::ORBIT );

    Wheel( view, 120 );

    CHECK_FALSE( view.isAutoOrbiting() );
    CHECK( view.camera().mode == tile_camera_mode_t::FLY );
    CHECK( view.camera().orbitDistance < distanceBefore );
    CHECK( view.camera().settings.moveSpeed == speedBefore );
}

TEST_CASE( "Editor speed bounds and optional camera hints match persisted controls",
    "[TileEditor][Camera][Options]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.setMoveSpeed( 2000 );
    CHECK( view.camera().settings.moveSpeed == 1000.0f );
    Wheel( view, 1200 );
    CHECK( view.camera().settings.moveSpeed == 1000.0f );
    view.setMoveSpeed( -5 );
    CHECK( view.camera().settings.moveSpeed == 0.1f );
    Wheel( view, -1200 );
    CHECK( view.camera().settings.moveSpeed == 0.1f );
    auto *overlay = view.findChild<QLabel *>( QStringLiteral( "TileRenderOverlay" ) );
    REQUIRE( overlay );
    view.setViewAppearance( QColor( "#101820" ), true );
    view.setCameraHintsVisible( true );
    CHECK( overlay->text().contains( QStringLiteral( "XYZ" ) ) );
    CHECK( overlay->text().contains( QStringLiteral( "RMB" ) ) );
    view.setCameraHintsVisible( false );
    CHECK( overlay->text().contains( QStringLiteral( "XYZ" ) ) );
    CHECK_FALSE( overlay->text().contains( QStringLiteral( "RMB" ) ) );
    view.setViewAppearance( QColor( "#101820" ), false );
    CHECK( overlay->isHidden() );
}

TEST_CASE( "3D orientation triad uses camera-relative RGB axes and obeys display visibility",
    "[TileEditor][Camera][Axes]" )
{
    EnsureApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    auto *triad = view.findChild<QWidget *>( QStringLiteral( "TileAxisTriad" ) );
    REQUIRE( triad );

    const QColor xColor( 241, 51, 61 );
    const QColor yColor( 47, 222, 99 );
    const QColor zColor( 59, 121, 249 );
    view.setAxisAppearance( true, xColor, yColor, zColor );
    CHECK_FALSE( triad->isHidden() );
    QImage perspective( triad->size(), QImage::Format_ARGB32_Premultiplied );
    perspective.fill( Qt::transparent );
    triad->render( &perspective );
    CHECK( CountColor( perspective, xColor ) > 0 );
    CHECK( CountColor( perspective, yColor ) > 0 );
    CHECK( CountColor( perspective, zColor ) > 0 );

    view.setCameraViewPreset( tile_camera_view_preset_t::TOP );
    QImage top( triad->size(), QImage::Format_ARGB32_Premultiplied );
    top.fill( Qt::transparent );
    triad->render( &top );
    CHECK( perspective != top );
    CHECK( CountColor( top, xColor ) > 0 );
    CHECK( CountColor( top, yColor ) > 0 );
    CHECK( CountColor( top, zColor ) > 0 );

    view.setAxisAppearance( false, xColor, yColor, zColor );
    CHECK( triad->isHidden() );
}

TEST_CASE( "Camera menu remains with the 3D pane through swaps and maximization",
    "[TileEditor][Camera][Options]" )
{
    EnsureApplication();
    std::array<QWidget *, 4> views{ new QWidget, new QWidget, new QWidget, new QWidget };
    CypherTileViewWorkspace workspace( views );
    QMenu menu;
    menu.addAction( QStringLiteral( "Camera settings" ) );
    workspace.setCameraMenu( &menu );
    auto *button = workspace.findChild<QToolButton *>( QStringLiteral( "TileViewCameraOptions" ) );
    REQUIRE( button );
    CHECK( button->menu() == &menu );
    CHECK_FALSE( button->isHidden() );
    QWidget *header = button->parentWidget();
    REQUIRE( workspace.swapViews( tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::SIDE ) );
    workspace.focusView( tile_editor_view_t::PERSPECTIVE );
    workspace.toggleMaximize();
    CHECK( button->parentWidget() == header );
    CHECK( header->parentWidget()->objectName() == QStringLiteral( "TileViewPane1" ) );
    CHECK_FALSE( header->parentWidget()->isHidden() );
    workspace.setCameraMenu( nullptr );
    CHECK( button->isHidden() );
}
