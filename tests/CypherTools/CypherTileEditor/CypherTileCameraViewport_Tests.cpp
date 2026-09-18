//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies camera settings and the Qt input adapter without OpenGL.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileRenderViewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace
{

QApplication &EnsureCameraApplication()
{
    if ( QApplication::instance() ) return *static_cast<QApplication *>( QApplication::instance() );
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileCameraViewportTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    static QTemporaryDir settings;
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "TileCameraViewport" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settings.path() );
    return application;
}

void PointerEvent( CypherTileRenderViewport &view, QEvent::Type type,
    QPointF point = { 40.0, 40.0 }, Qt::MouseButton button = Qt::RightButton,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, point, point,
        type == QEvent::MouseMove ? Qt::NoButton : button,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers );
    QApplication::sendEvent( &view, &event );
}

void WheelEvent( CypherTileRenderViewport &view, int delta )
{
    QWheelEvent event( { 40.0, 40.0 }, { 40.0, 40.0 }, {}, { 0, delta },
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &view, &event );
}

void CheckSamePose( const tile_camera_t &actual, const tile_camera_t &expected )
{
    CHECK( actual.position.x == Catch::Approx( expected.position.x ) );
    CHECK( actual.position.y == Catch::Approx( expected.position.y ) );
    CHECK( actual.position.z == Catch::Approx( expected.position.z ) );
    CHECK( actual.yawRadians == Catch::Approx( expected.yawRadians ) );
    CHECK( actual.pitchRadians == Catch::Approx( expected.pitchRadians ) );
    CHECK( actual.orbitDistance == Catch::Approx( expected.orbitDistance ) );
    CHECK( actual.mode == expected.mode );
}

void CheckCameraDefaults( const tile_editor_preferences_t &preferences )
{
    CHECK( preferences.cameraMoveSpeed == Catch::Approx( 8.0 ) );
    CHECK( preferences.cameraLookSensitivity == Catch::Approx( 0.20 ) );
    CHECK( preferences.cameraFieldOfView == Catch::Approx( 60.0 ) );
    CHECK_FALSE( preferences.cameraInvertY );
    CHECK( preferences.cameraFlyMode );
}

} // namespace

TEST_CASE( "Camera preferences survive a settings file round trip", "[TileEditor][Camera][Settings]" )
{
    EnsureCameraApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "camera.ini" ) );
    {
        QSettings settings( path, QSettings::IniFormat );
        tile_editor_preferences_t preferences{};
        preferences.cameraMoveSpeed = 21.5;
        preferences.cameraLookSensitivity = 0.37;
        preferences.cameraFieldOfView = 84.0;
        preferences.cameraInvertY = true;
        preferences.cameraFlyMode = false;
        TileEditorPreferences_Save( settings, preferences );
        settings.sync();
        REQUIRE( settings.status() == QSettings::NoError );
    }
    QSettings reloadedFile( path, QSettings::IniFormat );
    const auto loaded = TileEditorPreferences_Load( reloadedFile );
    CHECK( loaded.cameraMoveSpeed == Catch::Approx( 21.5 ) );
    CHECK( loaded.cameraLookSensitivity == Catch::Approx( 0.37 ) );
    CHECK( loaded.cameraFieldOfView == Catch::Approx( 84.0 ) );
    CHECK( loaded.cameraInvertY );
    CHECK_FALSE( loaded.cameraFlyMode );
}

TEST_CASE( "Camera preference loading and saving sanitize invalid numbers", "[TileEditor][Camera][Settings]" )
{
    EnsureCameraApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings( directory.filePath( QStringLiteral( "invalid.ini" ) ), QSettings::IniFormat );
    const QString prefix = QStringLiteral( "TileEditor/Preferences/" );
    for ( const QVariant &invalid : {
              QVariant( std::numeric_limits<double>::quiet_NaN() ),
              QVariant( std::numeric_limits<double>::infinity() ),
              QVariant( -std::numeric_limits<double>::infinity() ),
              QVariant( QStringLiteral( "not a number" ) ) } ) {
        INFO( invalid.toString().toStdString() );
        settings.setValue( prefix + QStringLiteral( "cameraMoveSpeed" ), invalid );
        settings.setValue( prefix + QStringLiteral( "cameraLookSensitivity" ), invalid );
        settings.setValue( prefix + QStringLiteral( "cameraFieldOfView" ), invalid );
        CheckCameraDefaults( TileEditorPreferences_Load( settings ) );
    }
    settings.setValue( prefix + QStringLiteral( "cameraMoveSpeed" ), -5.0 );
    settings.setValue( prefix + QStringLiteral( "cameraLookSensitivity" ), 25.0 );
    settings.setValue( prefix + QStringLiteral( "cameraFieldOfView" ), 1.0 );
    settings.setValue( prefix + QStringLiteral( "cameraInvertY" ), QStringLiteral( "invalid" ) );
    settings.setValue( prefix + QStringLiteral( "cameraFlyMode" ), QStringLiteral( "invalid" ) );
    const auto clamped = TileEditorPreferences_Load( settings );
    CHECK( clamped.cameraMoveSpeed == Catch::Approx( 0.1 ) );
    CHECK( clamped.cameraLookSensitivity == Catch::Approx( 2.0 ) );
    CHECK( clamped.cameraFieldOfView == Catch::Approx( 30.0 ) );
    CHECK_FALSE( clamped.cameraInvertY );
    CHECK( clamped.cameraFlyMode );

    tile_editor_preferences_t invalid{};
    invalid.cameraMoveSpeed = std::numeric_limits<double>::infinity();
    invalid.cameraLookSensitivity = -1.0;
    invalid.cameraFieldOfView = 500.0;
    TileEditorPreferences_Save( settings, invalid );
    // Inspect the stored values directly so load-time repair cannot mask a bad save.
    CHECK( settings.value( prefix + QStringLiteral( "cameraMoveSpeed" ) ).toDouble() == Catch::Approx( 8.0 ) );
    CHECK( settings.value( prefix + QStringLiteral( "cameraLookSensitivity" ) ).toDouble() == Catch::Approx( 0.01 ) );
    CHECK( settings.value( prefix + QStringLiteral( "cameraFieldOfView" ) ).toDouble() == Catch::Approx( 100.0 ) );
}

TEST_CASE( "Camera settings controls extract edits and restore defaults", "[TileEditor][Camera][Settings]" )
{
    EnsureCameraApplication();
    CypherTileEditorSettingsDialog dialog( tile_editor_preferences_t{} );
    auto *speed = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "TileSettingsCameraSpeed" ) );
    auto *sensitivity = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "TileSettingsCameraSensitivity" ) );
    auto *fov = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "TileSettingsCameraFov" ) );
    auto *invert = dialog.findChild<QCheckBox *>( QStringLiteral( "TileSettingsCameraInvertY" ) );
    auto *fly = dialog.findChild<QCheckBox *>( QStringLiteral( "TileSettingsCameraFlyMode" ) );
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    REQUIRE( speed );
    REQUIRE( sensitivity );
    REQUIRE( fov );
    REQUIRE( invert );
    REQUIRE( fly );
    REQUIRE( buttons );
    CheckCameraDefaults( dialog.preferences() );

    speed->setValue( 32.5 );
    sensitivity->setValue( 0.45 );
    fov->setValue( 78.0 );
    invert->setChecked( true );
    fly->setChecked( false );
    const auto edited = dialog.preferences();
    CHECK( edited.cameraMoveSpeed == Catch::Approx( 32.5 ) );
    CHECK( edited.cameraLookSensitivity == Catch::Approx( 0.45 ) );
    CHECK( edited.cameraFieldOfView == Catch::Approx( 78.0 ) );
    CHECK( edited.cameraInvertY );
    CHECK_FALSE( edited.cameraFlyMode );

    buttons->button( QDialogButtonBox::RestoreDefaults )->click();
    CheckCameraDefaults( dialog.preferences() );
    buttons->button( QDialogButtonBox::Ok )->click();
    CHECK( dialog.result() == QDialog::Accepted );
}

TEST_CASE( "Fly navigation owns movement shortcuts only while captured", "[TileEditor][Camera][Input]" )
{
    EnsureCameraApplication();
    // Keep the widget hidden: this tests Qt input routing, not context creation.
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    REQUIRE_FALSE( view.isNavigating() );
    PointerEvent( view, QEvent::MouseButtonPress );
    REQUIRE( view.isNavigating() );
    CHECK( view.camera().mode == tile_camera_mode_t::FLY );
    const auto beforeLook = view.camera();
    PointerEvent( view, QEvent::MouseMove, { 70.0, 50.0 } );
    CHECK( view.camera().yawRadians != beforeLook.yawRadians );
    CHECK( view.camera().pitchRadians != beforeLook.pitchRadians );
    CHECK( view.camera().position.x == beforeLook.position.x );
    CHECK( view.camera().position.y == beforeLook.position.y );
    CHECK( view.camera().position.z == beforeLook.position.z );

    for ( const int key : { Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_Q, Qt::Key_E } ) {
        for ( const auto modifiers : { Qt::NoModifier, Qt::ShiftModifier, Qt::ControlModifier } ) {
            INFO( "key=" << key << " modifiers=" << static_cast<int>( modifiers ) );
            QKeyEvent shortcut( QEvent::ShortcutOverride, key, modifiers );
            shortcut.ignore();
            QApplication::sendEvent( &view, &shortcut );
            // Qt suppresses QAction/QShortcut dispatch when this override is accepted.
            CHECK( shortcut.isAccepted() );
        }
    }
    PointerEvent( view, QEvent::MouseButtonRelease, { 70.0, 50.0 } );
    REQUIRE_FALSE( view.isNavigating() );
    const auto afterRelease = view.camera();
    PointerEvent( view, QEvent::MouseMove, { 100.0, 90.0 } );
    CheckSamePose( view.camera(), afterRelease );
    QKeyEvent toolShortcut( QEvent::ShortcutOverride, Qt::Key_D, Qt::NoModifier );
    toolShortcut.ignore();
    QApplication::sendEvent( &view, &toolShortcut );
    CHECK_FALSE( toolShortcut.isAccepted() );
}

TEST_CASE( "Stationary RMB opens 3D context options while RMB drag keeps navigating",
           "[TileEditor][Camera][Input][ContextMenu]" )
{
    EnsureCameraApplication();
    // This remains hidden so the input adapter is exercised without creating
    // an OpenGL context or initializing CypherRender.
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    int contextMenus = 0;
    QPoint requestedPosition;
    view.setContextMenuCallback( [&]( const QPoint &globalPosition ) {
        ++contextMenus;
        requestedPosition = globalPosition;
    } );

    const QPointF start( 40.0, 40.0 );
    const auto initialPose = view.camera();
    PointerEvent( view, QEvent::MouseButtonPress, start );
    REQUIRE( view.isNavigating() );
    PointerEvent( view, QEvent::MouseButtonRelease, start );
    CHECK_FALSE( view.isNavigating() );
    CheckSamePose( view.camera(), initialPose );
    CHECK( contextMenus == 0 );
    QApplication::processEvents();
    CHECK( contextMenus == 1 );
    CHECK( requestedPosition == start.toPoint() );

    // Hand jitter below the platform's drag threshold remains a context click
    // and must not rotate the camera.
    const int dragThreshold = std::max( 1, QApplication::startDragDistance() );
    const QPointF jittered = start + QPointF( std::max( 0, dragThreshold - 1 ), 0.0 );
    PointerEvent( view, QEvent::MouseButtonPress, start );
    PointerEvent( view, QEvent::MouseMove, jittered );
    CheckSamePose( view.camera(), initialPose );
    PointerEvent( view, QEvent::MouseButtonRelease, jittered );
    QApplication::processEvents();
    CHECK( contextMenus == 2 );

    // Reaching the threshold applies the complete accumulated delta once.
    const QPointF dragged = start + QPointF( dragThreshold, 0.0 );
    PointerEvent( view, QEvent::MouseButtonPress, start );
    REQUIRE( view.isNavigating() );
    PointerEvent( view, QEvent::MouseMove, dragged );
    CHECK( view.isNavigating() );
    CHECK( view.camera().yawRadians != initialPose.yawRadians );
    CHECK( view.camera().pitchRadians == initialPose.pitchRadians );
    PointerEvent( view, QEvent::MouseButtonRelease, dragged );
    CHECK_FALSE( view.isNavigating() );
    QApplication::processEvents();
    CHECK( contextMenus == 2 );

    const float speedBefore = view.camera().settings.moveSpeed;
    PointerEvent( view, QEvent::MouseButtonPress, start );
    WheelEvent( view, 120 );
    CHECK( view.camera().settings.moveSpeed > speedBefore );
    PointerEvent( view, QEvent::MouseButtonRelease, start );
    QApplication::processEvents();
    CHECK( contextMenus == 2 );
}

TEST_CASE( "Viewport cancellation always ends camera navigation", "[TileEditor][Camera][Input]" )
{
    EnsureCameraApplication();
    CypherTileRenderViewport view;
    PointerEvent( view, QEvent::MouseButtonPress );
    REQUIRE( view.isNavigating() );
    QKeyEvent heldForward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QApplication::sendEvent( &view, &heldForward );

    SECTION( "Escape" ) {
        QKeyEvent event( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
        QApplication::sendEvent( &view, &event );
    }
    SECTION( "Focus lost" ) {
        QFocusEvent event( QEvent::FocusOut, Qt::OtherFocusReason );
        QApplication::sendEvent( &view, &event );
    }
    SECTION( "Viewport hidden" ) {
        QHideEvent event;
        QApplication::sendEvent( &view, &event );
    }
    SECTION( "Application window deactivated" ) {
        QEvent event( QEvent::WindowDeactivate );
        QApplication::sendEvent( &view, &event );
    }
    SECTION( "Pointer capture lost" ) {
        QEvent event( QEvent::UngrabMouse );
        QApplication::sendEvent( &view, &event );
    }
    SECTION( "Document replaced" ) {
        view.setDocumentBridge( nullptr );
    }
    CHECK_FALSE( view.isNavigating() );
    const auto stopped = view.camera();
    PointerEvent( view, QEvent::MouseMove, { 100.0, 100.0 } );
    CheckSamePose( view.camera(), stopped );
}

TEST_CASE( "Camera adapter selects orbit and pan without moving on mode changes", "[TileEditor][Camera][Input]" )
{
    EnsureCameraApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    const auto before = view.camera();
    PointerEvent( view, QEvent::MouseButtonPress, { 40.0, 40.0 }, Qt::RightButton, Qt::AltModifier );
    REQUIRE( view.isNavigating() );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( view.camera().position.x == before.position.x );
    CHECK( view.camera().position.y == before.position.y );
    CHECK( view.camera().position.z == before.position.z );
    PointerEvent( view, QEvent::MouseButtonRelease );
    CHECK( view.camera().mode == before.mode );
    CheckSamePose( view.camera(), before );

    view.setCameraSettings( tile_camera_settings_t{}, false );
    PointerEvent( view, QEvent::MouseButtonPress );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    PointerEvent( view, QEvent::MouseButtonRelease );
    const auto beforePan = view.camera();
    PointerEvent( view, QEvent::MouseButtonPress, { 40.0, 40.0 }, Qt::MiddleButton );
    PointerEvent( view, QEvent::MouseMove, { 80.0, 40.0 }, Qt::MiddleButton );
    CHECK( math::Vec3_Distance( view.camera().position, beforePan.position ) > 0.01f );
    CHECK( view.camera().yawRadians == beforePan.yawRadians );
    CHECK( view.camera().pitchRadians == beforePan.pitchRadians );
    PointerEvent( view, QEvent::MouseButtonRelease, { 80.0, 40.0 }, Qt::MiddleButton );
    CHECK_FALSE( view.isNavigating() );
}

TEST_CASE( "Selection framing targets its geometry and map edits preserve inspection pose", "[TileEditor][Camera][Framing]" )
{
    EnsureCameraApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 16;
    description.nCellSize = 4.0f;
    description.nLevelHeight = 2.0f;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Camera fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 2 }, { 2, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 8, 8 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    const float wholeMapDistance = view.camera().orbitDistance;
    view.setSelection( true, { 1, 2 } );
    const auto mapBoundsCenter = view.camera().boundsCenter;
    auto expected = view.camera();
    // The selected tile includes its floor slab and boundary walls. Verify
    // that the adapter supplies these exact bounds to core framing, which
    // centers the projected silhouette rather than the geometric AABB center.
    CypherTileCamera_FrameBounds( expected,
        { 4.0f, 8.0f, 4.0f - TILE_MAP_DEFAULT_FLOOR_THICKNESS },
        { 8.0f, 12.0f, 6.0f }, 640.0f / 480.0f );
    view.frameSelection();
    const auto framed = view.camera();
    CheckSamePose( framed, expected );
    CHECK( math::Vec3_EqualsExact( framed.boundsCenter, mapBoundsCenter ) );
    CHECK( framed.orbitDistance < wholeMapDistance );

    REQUIRE( document.beginEdit( QStringLiteral( "Extend distant geometry" ), &error ) );
    REQUIRE( document.paintCell( { 15, 15 }, { 4, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    view.refreshDocument();
    CheckSamePose( view.camera(), framed );
    CHECK( view.camera().boundsRadius > framed.boundsRadius );

    view.setSelection( false, {} );
    view.frameSelection();
    CHECK( view.camera().orbitDistance > wholeMapDistance );
}

TEST_CASE( "Auto orbit returns to configured navigation and settings stop animation", "[TileEditor][Camera][Input]" )
{
    EnsureCameraApplication();
    CypherTileRenderViewport view;
    tile_camera_settings_t settings{};
    view.setCameraSettings( settings, true );
    const auto toggleOrbit = [&] {
        view.setAutoOrbitEnabled( !view.isAutoOrbiting() );
    };

    SECTION( "Stopping the animation restores fly mode" ) {
        const auto before = view.camera();
        toggleOrbit();
        REQUIRE( view.camera().mode == tile_camera_mode_t::ORBIT );
        toggleOrbit();
        CheckSamePose( view.camera(), before );
    }
    SECTION( "New camera settings cancel an active animation" ) {
        toggleOrbit();
        REQUIRE( view.camera().mode == tile_camera_mode_t::ORBIT );
        settings.moveSpeed = 16.0f;
        view.setCameraSettings( settings, true );
        CHECK( view.camera().mode == tile_camera_mode_t::FLY );
        CHECK( view.camera().settings.moveSpeed == Catch::Approx( 16.0f ) );
        // A new command starts an orbit. If the old animation survived settings,
        // the toggle would instead stop it and remain in fly mode.
        toggleOrbit();
        CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
        toggleOrbit();
        CHECK( view.camera().mode == tile_camera_mode_t::FLY );
    }
    SECTION( "Stopping animation preserves the orbit navigation preference" ) {
        view.setCameraSettings( settings, false );
        toggleOrbit();
        toggleOrbit();
        CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    }
}
