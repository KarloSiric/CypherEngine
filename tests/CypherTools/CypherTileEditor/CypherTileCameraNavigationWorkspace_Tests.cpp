//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCameraNavigationWorkspace_Tests.cpp
//  Purpose: Verifies deterministic 3D view presets, level navigation, and
//           short-lived editor camera bookmarks without requiring OpenGL.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCamera.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileRenderViewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace cypher::tools::tile_editor
{
struct tile_render_viewport_test_access_t
{
    static void setResourcesReady(
        CypherTileRenderViewport &view, bool ready )
    {
        view.m_bResourcesReady = ready;
    }

    static QTimer *frameTimer( CypherTileRenderViewport &view )
    {
        return view.m_pFrameTimer;
    }

    static bool userNavigated( const CypherTileRenderViewport &view )
    {
        return view.m_bUserNavigated;
    }

    static bool advanceFlyNavigation( CypherTileRenderViewport &view,
        float deltaSeconds, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        return view.advanceFlyNavigation( deltaSeconds, modifiers );
    }

    static void setCamera(
        CypherTileRenderViewport &view, const tile_camera_t &camera )
    {
        view.m_camera = camera;
    }

    static void corruptBookmark(
        CypherTileRenderViewport &view, int slot )
    {
        auto &bookmark = *view.m_cameraBookmarks[static_cast<std::size_t>( slot )];
        bookmark.position.x = std::numeric_limits<float>::infinity();
        bookmark.orbitDistance = -1.0f;
    }

    static std::optional<math::vec3_t> orbitPivot(
        const CypherTileRenderViewport &view )
    {
        return view.m_orbitPivot;
    }

    static std::optional<math::vec3_t> navigationOrbitPivot(
        const CypherTileRenderViewport &view )
    {
        return view.m_navigationOrbitPivot;
    }
};
} // namespace cypher::tools::tile_editor

namespace
{

QApplication &EnsureCameraNavigationApplication()
{
    if ( auto *pApplication = qobject_cast<QApplication *>( QApplication::instance() ) )
        return *pApplication;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileCameraNavigationWorkspaceTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

struct camera_navigation_settings_t
{
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;
    QString configRoot;
    QString cacheRoot;

    camera_navigation_settings_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe(
            QSettings::IniFormat, QSettings::UserScope,
            QStringLiteral( "CypherCameraNavigationProbe" ),
            QStringLiteral( "Path" ) );
        previousIniRoot = QFileInfo(
            QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
        QCoreApplication::setApplicationName(
            QStringLiteral( "TileCameraNavigation-" ) +
            QFileInfo( directory.path() ).fileName() );
        configRoot = QFileInfo( TileEditorConfig_DefaultPath() ).absolutePath();
        cacheRoot = QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation );

        tile_editor_preferences_t preferences;
        preferences.startMaximized = false;
        preferences.activateViewOnHover = false;
        QSettings settings;
        TileEditorPreferences_Save( settings, preferences );
        QString error;
        REQUIRE( TileEditorConfig_Save(
            TileEditorConfig_DefaultPath(), preferences, error ) );
    }

    ~camera_navigation_settings_t()
    {
        // Both paths belong to this test's unique application profile.
        if ( !configRoot.isEmpty() ) QDir( configRoot ).removeRecursively();
        if ( !cacheRoot.isEmpty() ) QDir( cacheRoot ).removeRecursively();
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};

math::vec3_t OrbitPivot( const tile_camera_t &camera )
{
    return math::Vec3_Add( camera.position,
        math::Vec3_Scale(
            CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
}

void CheckPose( const tile_camera_t &actual, const tile_camera_t &expected )
{
    CHECK( math::Vec3_NearlyEquals(
        actual.position, expected.position, 0.00001f, 0.00001f ) );
    CHECK( actual.yawRadians == Catch::Approx( expected.yawRadians ) );
    CHECK( actual.pitchRadians == Catch::Approx( expected.pitchRadians ) );
    CHECK( actual.orbitDistance == Catch::Approx( expected.orbitDistance ) );
}

void PointerPress( CypherTileRenderViewport &view )
{
    const QPointF point{ 40.0, 40.0 };
    QMouseEvent event( QEvent::MouseButtonPress, point, point,
        Qt::RightButton, Qt::RightButton, Qt::NoModifier );
    QApplication::sendEvent( &view, &event );
}

void PointerEvent( CypherTileRenderViewport &view, QEvent::Type type,
    QPointF point, Qt::MouseButton button = Qt::RightButton,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, point, point,
        type == QEvent::MouseMove ? Qt::NoButton : button,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : button,
        modifiers );
    QApplication::sendEvent( &view, &event );
}

void WheelEvent( CypherTileRenderViewport &view, int delta )
{
    QWheelEvent event( { 40.0, 40.0 }, { 40.0, 40.0 }, {}, { 0, delta },
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &view, &event );
}

QAction *CameraAction(
    CypherTileEditorMainWindow &window, const char *pObjectName )
{
    auto *pAction = window.findChild<QAction *>(
        QString::fromLatin1( pObjectName ) );
    REQUIRE( pAction != nullptr );
    return pAction;
}

QAction *CameraAction(
    CypherTileEditorMainWindow &window, const QString &objectName )
{
    auto *pAction = window.findChild<QAction *>( objectName );
    REQUIRE( pAction != nullptr );
    return pAction;
}

CypherTileRenderViewport *CameraViewport(
    CypherTileEditorMainWindow &window )
{
    auto *pWidget = window.findChild<QWidget *>(
        QStringLiteral( "CypherTileRenderViewport" ) );
    REQUIRE( pWidget != nullptr );
    return static_cast<CypherTileRenderViewport *>( pWidget );
}

QString WriteCameraFixture( const QTemporaryDir &directory )
{
    REQUIRE( directory.isValid() );
    CypherTileDocumentBridge document;
    QString error;
    tile_map_document_desc_t description{};
    description.nWidth = 8;
    description.nHeight = 6;
    description.nCellSize = 2.0f;
    description.nLevelHeight = 3.5f;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit(
        QStringLiteral( "Camera navigation fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 2, 0 }, &error ) );
    REQUIRE( document.paintCell( { 6, 4 }, { 2, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath(
        QStringLiteral( "camera-navigation.cymap" ) );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

} // namespace

TEST_CASE( "Camera view presets retain the orbit target and produce valid vertical matrices",
    "[TileEditor][Camera][Presets]" )
{
    struct preset_case_t {
        tile_camera_view_preset_t preset;
        math::vec3_t forward;
    };
    const std::array cases{
        preset_case_t{ tile_camera_view_preset_t::TOP, { 0.0f, 0.0f, -1.0f } },
        preset_case_t{ tile_camera_view_preset_t::BOTTOM, { 0.0f, 0.0f, 1.0f } },
        preset_case_t{ tile_camera_view_preset_t::FRONT, { 0.0f, 1.0f, 0.0f } },
        preset_case_t{ tile_camera_view_preset_t::BACK, { 0.0f, -1.0f, 0.0f } },
        preset_case_t{ tile_camera_view_preset_t::LEFT, { 1.0f, 0.0f, 0.0f } },
        preset_case_t{ tile_camera_view_preset_t::RIGHT, { -1.0f, 0.0f, 0.0f } }
    };

    tile_camera_t original;
    original.position = { 12.0f, -7.0f, 9.0f };
    original.yawRadians = 0.37f;
    original.pitchRadians = -0.41f;
    original.orbitDistance = 23.0f;
    original.boundsCenter = { 9.0f, 4.0f, 2.0f };
    original.boundsRadius = 48.0f;
    original.settings.moveSpeed = 19.0f;
    original.settings.verticalFovDegrees = 73.0f;
    original.mode = tile_camera_mode_t::FLY;
    const auto pivot = OrbitPivot( original );

    for ( const auto &entry : cases ) {
        INFO( static_cast<int>( entry.preset ) );
        auto camera = original;
        CypherTileCamera_ApplyViewPreset( camera, entry.preset );
        CHECK( math::Vec3_NearlyEquals( CypherTileCamera_Forward( camera ),
            entry.forward, 0.00001f, 0.00001f ) );
        CHECK( math::Vec3_NearlyEquals(
            OrbitPivot( camera ), pivot, 0.00001f, 0.00001f ) );
        CHECK( camera.orbitDistance == original.orbitDistance );
        CHECK( camera.boundsCenter.x == original.boundsCenter.x );
        CHECK( camera.boundsCenter.y == original.boundsCenter.y );
        CHECK( camera.boundsCenter.z == original.boundsCenter.z );
        CHECK( camera.boundsRadius == original.boundsRadius );
        CHECK( camera.settings == original.settings );
        CHECK( camera.mode == original.mode );
        math::mat4_t view{}, projection{};
        // TOP and BOTTOM require an up vector derived from the camera basis;
        // fixed world +Z is collinear and makes a look-at matrix singular.
        CHECK( CypherTileCamera_BuildMatrices(
            camera, 16.0f / 9.0f, view, projection ) );
    }

    auto perspective = original;
    CypherTileCamera_ApplyViewPreset(
        perspective, tile_camera_view_preset_t::PERSPECTIVE );
    CHECK( perspective.yawRadians == Catch::Approx( 2.42159265f ) );
    CHECK( perspective.pitchRadians == Catch::Approx( -0.62f ) );
    CHECK( math::Vec3_NearlyEquals(
        OrbitPivot( perspective ), pivot, 0.00001f, 0.00001f ) );
}

TEST_CASE( "Viewport presets stop captured navigation and camera levels follow the map",
    "[TileEditor][Camera][Navigation]" )
{
    EnsureCameraNavigationApplication();
    CypherTileDocumentBridge document;
    QString error;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 8;
    description.nCellSize = 2.0f;
    description.nLevelHeight = 3.5f;
    REQUIRE( document.newDocument( description, &error ) );

    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    view.setCameraSettings( {}, true );
    view.setAutoOrbitEnabled( true );
    REQUIRE( view.isAutoOrbiting() );
    view.setCameraViewPreset( tile_camera_view_preset_t::TOP );
    CHECK_FALSE( view.isAutoOrbiting() );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    view.setCameraMode( tile_camera_mode_t::FLY );
    PointerPress( view );
    REQUIRE( view.isNavigating() );
    view.setCameraViewPreset( tile_camera_view_preset_t::TOP );
    CHECK_FALSE( view.isNavigating() );
    CHECK_FALSE( view.isAutoOrbiting() );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( math::Vec3_NearlyEquals( CypherTileCamera_Forward( view.camera() ),
        { 0.0f, 0.0f, -1.0f }, 0.00001f, 0.00001f ) );

    const auto before = view.camera();
    REQUIRE( view.moveCameraLevel( 2 ) );
    CHECK( view.camera().position.x == before.position.x );
    CHECK( view.camera().position.y == before.position.y );
    CHECK( view.camera().position.z ==
        Catch::Approx( before.position.z + 7.0f ) );
    CHECK( view.camera().yawRadians == before.yawRadians );
    CHECK( view.camera().pitchRadians == before.pitchRadians );
    CHECK( view.camera().orbitDistance == before.orbitDistance );
    CHECK( view.camera().mode == before.mode );
    REQUIRE( view.moveCameraLevel( -3 ) );
    CHECK( view.camera().position.z ==
        Catch::Approx( before.position.z - 3.5f ) );
    const auto unchanged = view.camera();
    CHECK_FALSE( view.moveCameraLevel( 0 ) );
    CheckPose( view.camera(), unchanged );

    CypherTileRenderViewport detached;
    const auto detachedBefore = detached.camera();
    CHECK_FALSE( detached.moveCameraLevel( 1 ) );
    CheckPose( detached.camera(), detachedBefore );

    CypherTileDocumentBridge extremeDocument;
    tile_map_document_desc_t extremeDescription{};
    extremeDescription.nWidth = extremeDescription.nHeight = 1;
    extremeDescription.nCellSize = 1.0f;
    extremeDescription.nLevelHeight = std::numeric_limits<float>::max();
    REQUIRE( extremeDocument.newDocument( extremeDescription, &error ) );
    CypherTileRenderViewport extremeView;
    extremeView.setDocumentBridge( &extremeDocument );
    const auto extremeBefore = extremeView.camera();
    // A finite offset can still overflow when added to the current eye.
    CHECK_FALSE( extremeView.moveCameraLevel( 1 ) );
    CheckPose( extremeView.camera(), extremeBefore );
}

TEST_CASE( "Captured fly navigation advances held keys continuously and releases them cleanly",
    "[TileEditor][Camera][Navigation][Input]" )
{
    EnsureCameraNavigationApplication();
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setCameraSettings( {}, true );
    const auto initial = view.camera();

    PointerPress( view );
    REQUIRE( view.isNavigating() );
    QKeyEvent forwardPress(
        QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QApplication::sendEvent( &view, &forwardPress );
    CHECK( forwardPress.isAccepted() );
    // Merely pressing a key is not navigation. The deterministic frame tick
    // owns movement and marks the inspection pose only after it actually moves.
    CHECK_FALSE( tile_render_viewport_test_access_t::userNavigated( view ) );
    REQUIRE( tile_render_viewport_test_access_t::advanceFlyNavigation(
        view, 0.1f ) );
    CHECK( tile_render_viewport_test_access_t::userNavigated( view ) );
    CHECK( math::Vec3_NearlyEquals(
        view.camera().position,
        math::Vec3_Add( initial.position,
            math::Vec3_Scale(
                CypherTileCamera_Forward( initial ),
                initial.settings.moveSpeed * 0.1f ) ),
        0.00001f, 0.00001f ) );

    const auto beforeFast = view.camera();
    REQUIRE( tile_render_viewport_test_access_t::advanceFlyNavigation(
        view, 0.1f, Qt::ShiftModifier ) );
    CHECK( math::Vec3_Distance(
        view.camera().position, beforeFast.position ) ==
        Catch::Approx( beforeFast.settings.moveSpeed *
            beforeFast.settings.fastMultiplier * 0.1f ) );

    QKeyEvent forwardRelease(
        QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier );
    QApplication::sendEvent( &view, &forwardRelease );
    CHECK_FALSE( tile_render_viewport_test_access_t::advanceFlyNavigation(
        view, 0.1f ) );
    const auto stopped = view.camera();
    PointerEvent( view, QEvent::MouseButtonRelease, { 40.0, 40.0 } );
    REQUIRE_FALSE( view.isNavigating() );
    CHECK_FALSE( tile_render_viewport_test_access_t::advanceFlyNavigation(
        view, 0.1f ) );
    CheckPose( view.camera(), stopped );
}

TEST_CASE( "Level camera respects navigation mode and cancelling auto orbit stops idle frames",
    "[TileEditor][Camera][Navigation][Timer]" )
{
    EnsureCameraNavigationApplication();

    CypherTileRenderViewport view;
    view.setAttribute( Qt::WA_DontShowOnScreen );
    view.resize( 640, 480 );
    view.show();
    QApplication::processEvents();

    CHECK_FALSE( tile_render_viewport_test_access_t::userNavigated( view ) );
    view.setAutoOrbitEnabled( false );
    CHECK_FALSE( tile_render_viewport_test_access_t::userNavigated( view ) );

    view.setCameraMode( tile_camera_mode_t::FLY );
    const auto flyBefore = view.camera();
    view.levelCamera();
    CHECK( math::Vec3_NearlyEquals(
        view.camera().position, flyBefore.position, 0.00001f, 0.00001f ) );
    CHECK( view.camera().yawRadians == flyBefore.yawRadians );
    CHECK( view.camera().pitchRadians == 0.0f );

    view.setCameraViewPreset( tile_camera_view_preset_t::PERSPECTIVE );
    const auto orbitPivot = OrbitPivot( view.camera() );
    view.levelCamera();
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( view.camera().pitchRadians == 0.0f );
    CHECK( math::Vec3_NearlyEquals(
        OrbitPivot( view.camera() ), orbitPivot, 0.00001f, 0.00001f ) );

    // The offscreen test platform does not initialize the embedded renderer,
    // so inject only the readiness precondition needed by timer policy.
    const bool ownedRenderer = view.findChild<QTimer *>(
        QStringLiteral( "TileRenderFrameTimer" ) ) != nullptr;
    REQUIRE( ownedRenderer );
    tile_render_viewport_test_access_t::setResourcesReady( view, true );
    view.setAutoOrbitEnabled( true );
    auto *pTimer = tile_render_viewport_test_access_t::frameTimer( view );
    REQUIRE( pTimer != nullptr );
    REQUIRE( pTimer->isActive() );
    const auto autoOrbitPivot = OrbitPivot( view.camera() );
    view.levelCamera();
    CHECK_FALSE( view.isAutoOrbiting() );
    CHECK( math::Vec3_NearlyEquals(
        OrbitPivot( view.camera() ), autoOrbitPivot, 0.00001f, 0.00001f ) );
    CHECK_FALSE( pTimer->isActive() );
    tile_render_viewport_test_access_t::setResourcesReady( view, false );
    view.hide();
}

TEST_CASE( "Persistent orbit uses selection fallback and retains its pivot for wheel zoom",
    "[TileEditor][Camera][Navigation][Pivot]" )
{
    EnsureCameraNavigationApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 16;
    description.nCellSize = 2.0f;
    description.nLevelHeight = 3.0f;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Orbit selection fixture" ), &error ) );
    REQUIRE( document.paintCell( { 2, 3 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );

    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    view.setSelection( true, { 2, 3 } );
    view.setCameraSettings( {}, false );

    const math::vec3_t selectionCenter{
        5.0f, 7.0f,
        ( description.nLevelHeight - TILE_MAP_DEFAULT_FLOOR_THICKNESS ) * 0.5f };
    // Deliver the event well outside the rendered map silhouette so picking
    // deterministically misses and the selected-geometry fallback is used.
    const QPointF pressPoint{ -100.0, -100.0 };
    PointerEvent( view, QEvent::MouseButtonPress, pressPoint );
    REQUIRE( view.isNavigating() );
    const auto navigationPivot =
        tile_render_viewport_test_access_t::navigationOrbitPivot( view );
    REQUIRE( navigationPivot.has_value() );
    CHECK( math::Vec3_NearlyEquals(
        *navigationPivot, selectionCenter, 0.00001f, 0.00001f ) );

    const auto beforeOrbit = view.camera();
    const int drag = std::max( 1, QApplication::startDragDistance() ) + 12;
    const QPointF dragPoint = pressPoint + QPointF( drag, 7.0 );
    PointerEvent( view, QEvent::MouseMove, dragPoint );
    CHECK_FALSE( math::Vec3_EqualsExact(
        view.camera().position, beforeOrbit.position ) );
    PointerEvent( view, QEvent::MouseButtonRelease, dragPoint );
    CHECK_FALSE( view.isNavigating() );
    REQUIRE( view.camera().mode == tile_camera_mode_t::ORBIT );
    const auto retainedPivot =
        tile_render_viewport_test_access_t::orbitPivot( view );
    REQUIRE( retainedPivot.has_value() );
    CHECK( math::Vec3_NearlyEquals(
        *retainedPivot, selectionCenter, 0.00001f, 0.00001f ) );

    const float radiusBeforeWheel = math::Vec3_Distance(
        view.camera().position, selectionCenter );
    WheelEvent( view, 120 );
    CHECK( math::Vec3_Distance(
        view.camera().position, selectionCenter ) < radiusBeforeWheel );
    const auto afterWheelPivot =
        tile_render_viewport_test_access_t::orbitPivot( view );
    REQUIRE( afterWheelPivot.has_value() );
    CHECK( math::Vec3_NearlyEquals(
        *afterWheelPivot, selectionCenter, 0.00001f, 0.00001f ) );
}

TEST_CASE( "Camera bookmarks restore only pose and reject invalid slots",
    "[TileEditor][Camera][Bookmarks]" )
{
    EnsureCameraNavigationApplication();
    CypherTileDocumentBridge document;
    QString error;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 16;
    description.nCellSize = 2.0f;
    description.nLevelHeight = 4.0f;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Initial bounds" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );

    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    view.setCameraViewPreset( tile_camera_view_preset_t::FRONT );
    REQUIRE( view.moveCameraLevel( 2 ) );
    const auto stored = view.camera();
    REQUIRE( view.storeCameraBookmark( 0 ) );
    CHECK( view.hasCameraBookmark( 0 ) );
    CHECK_FALSE( view.hasCameraBookmark( 1 ) );

    REQUIRE( document.beginEdit( QStringLiteral( "Extend bounds" ), &error ) );
    REQUIRE( document.paintCell( { 15, 15 }, { 3, 2, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    view.refreshDocument();
    tile_camera_settings_t currentSettings;
    currentSettings.moveSpeed = 31.0f;
    currentSettings.lookSensitivity = 0.009f;
    currentSettings.verticalFovDegrees = 81.0f;
    currentSettings.invertMouseY = true;
    view.setCameraSettings( currentSettings, true );
    view.setCameraViewPreset( tile_camera_view_preset_t::RIGHT );
    view.setCameraMode( tile_camera_mode_t::FLY );
    REQUIRE( view.moveCameraLevel( -1 ) );
    const auto current = view.camera();
    REQUIRE( current.boundsRadius > stored.boundsRadius );

    REQUIRE( view.recallCameraBookmark( 0 ) );
    CheckPose( view.camera(), stored );
    CHECK( view.camera().settings == current.settings );
    CHECK( view.camera().boundsCenter.x == current.boundsCenter.x );
    CHECK( view.camera().boundsCenter.y == current.boundsCenter.y );
    CHECK( view.camera().boundsCenter.z == current.boundsCenter.z );
    CHECK( view.camera().boundsRadius == current.boundsRadius );
    CHECK( view.camera().mode == tile_camera_mode_t::FLY );

    const auto beforeInvalid = view.camera();
    CHECK_FALSE( view.storeCameraBookmark( -1 ) );
    CHECK_FALSE( view.storeCameraBookmark( 4 ) );
    CHECK_FALSE( view.recallCameraBookmark( -1 ) );
    CHECK_FALSE( view.recallCameraBookmark( 4 ) );
    CHECK_FALSE( view.hasCameraBookmark( -1 ) );
    CHECK_FALSE( view.hasCameraBookmark( 4 ) );
    CheckPose( view.camera(), beforeInvalid );

    auto corruptCamera = view.camera();
    corruptCamera.position.x = std::numeric_limits<float>::infinity();
    tile_render_viewport_test_access_t::setCamera( view, corruptCamera );
    CHECK_FALSE( view.storeCameraBookmark( 1 ) );
    CHECK_FALSE( view.hasCameraBookmark( 1 ) );
    tile_render_viewport_test_access_t::setCamera( view, beforeInvalid );

    tile_render_viewport_test_access_t::corruptBookmark( view, 0 );
    CHECK_FALSE( view.recallCameraBookmark( 0 ) );
    CheckPose( view.camera(), beforeInvalid );

    view.clearCameraBookmarks();
    CHECK_FALSE( view.hasCameraBookmark( 0 ) );
    CHECK_FALSE( view.recallCameraBookmark( 0 ) );
    CheckPose( view.camera(), beforeInvalid );
}

TEST_CASE( "Player-spawn camera rejects an unrenderable extreme map pose transactionally",
    "[TileEditor][Camera][Spawn][Robustness]" )
{
    EnsureCameraNavigationApplication();
    CypherTileDocumentBridge document;
    QString error;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 1;
    description.nCellSize = std::numeric_limits<float>::max();
    description.nLevelHeight = 1.0f;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit(
        QStringLiteral( "Extreme spawn fixture" ), &error ) );
    REQUIRE( document.paintCell( { 0, 0 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.placePlayerSpawn( { 0, 0 }, 0.0f, &error ) );
    REQUIRE( document.commitEdit( &error ) );

    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    const auto before = view.camera();
    CHECK_FALSE( view.goToPlayerSpawn() );
    CheckPose( view.camera(), before );
}

TEST_CASE( "Camera menu exposes presets, levels and four document-scoped bookmarks",
    "[TileEditor][Camera][Workspace]" )
{
    EnsureCameraNavigationApplication();
    camera_navigation_settings_t settings;
    QTemporaryDir maps;
    const QString replacement = WriteCameraFixture( maps );
    CypherTileEditorMainWindow window;
    auto *pViewport = CameraViewport( window );
    auto *pPresetMenu = window.findChild<QMenu *>(
        QStringLiteral( "TileCameraViewPresetsMenu" ) );
    REQUIRE( pPresetMenu != nullptr );

    const std::array presetIds{
        "camera.viewPerspective", "camera.viewTop", "camera.viewBottom",
        "camera.viewFront", "camera.viewBack", "camera.viewLeft",
        "camera.viewRight"
    };
    auto *pCanvas = window.findChild<QWidget *>(
        QStringLiteral( "CypherTileCanvas" ) );
    auto *pFront = window.findChild<QWidget *>(
        QStringLiteral( "CypherTileFrontView" ) );
    auto *pSide = window.findChild<QWidget *>(
        QStringLiteral( "CypherTileSideView" ) );
    REQUIRE( pCanvas != nullptr );
    REQUIRE( pFront != nullptr );
    REQUIRE( pSide != nullptr );
    const auto checkCameraScope = [&]( QAction *pAction ) {
        REQUIRE( pAction != nullptr );
        CHECK( pAction->shortcutContext() == Qt::WidgetWithChildrenShortcut );
        CHECK( pViewport->actions().contains( pAction ) );
        CHECK_FALSE( pCanvas->actions().contains( pAction ) );
        CHECK_FALSE( pFront->actions().contains( pAction ) );
        CHECK_FALSE( pSide->actions().contains( pAction ) );
    };
    for ( const char *pId : presetIds ) {
        auto *pAction = CameraAction( window, pId );
        CHECK( pPresetMenu->actions().contains( pAction ) );
        checkCameraScope( pAction );
    }
    checkCameraScope( CameraAction( window, "camera.level" ) );
    checkCameraScope( CameraAction( window, "camera.levelUp" ) );
    checkCameraScope( CameraAction( window, "camera.levelDown" ) );
    auto *pAutoOrbit = CameraAction( window, "camera.autoOrbit" );
    CHECK( pAutoOrbit->isCheckable() );
    checkCameraScope( pAutoOrbit );

    const auto definitions = TileEditorShortcutDefinitions();
    for ( const char *pId : presetIds ) {
        CHECK( std::count_if( definitions.begin(), definitions.end(),
            [pId]( const tile_editor_shortcut_definition_t &definition ) {
                return definition.id == QString::fromLatin1( pId );
            } ) == 1 );
    }
    for ( const char *pId : {
            "camera.level", "camera.levelUp", "camera.levelDown",
            "camera.autoOrbit" } ) {
        CHECK( std::count_if( definitions.begin(), definitions.end(),
            [pId]( const tile_editor_shortcut_definition_t &definition ) {
                return definition.id == QString::fromLatin1( pId );
            } ) == 1 );
    }

    auto *pFly = CameraAction( window, "camera.fly" );
    auto *pTop = CameraAction( window, "camera.viewTop" );
    pTop->trigger();
    CHECK( pFly->isChecked() );
    CHECK( pViewport->camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( math::Vec3_NearlyEquals(
        CypherTileCamera_Forward( pViewport->camera() ),
        { 0.0f, 0.0f, -1.0f }, 0.00001f, 0.00001f ) );
    CameraAction( window, "camera.toggleMode" )->trigger();
    CHECK( pViewport->camera().mode == tile_camera_mode_t::FLY );
    CHECK( pFly->isChecked() );

    for ( int slot = 1; slot <= 4; ++slot ) {
        auto *pStore = CameraAction( window,
            QStringLiteral( "camera.store%1" ).arg( slot ) );
        auto *pRecall = CameraAction( window,
            QStringLiteral( "camera.recall%1" ).arg( slot ) );
        checkCameraScope( pStore );
        checkCameraScope( pRecall );
        CHECK( pStore->isEnabled() );
        CHECK_FALSE( pRecall->isEnabled() );
        CHECK( std::count_if( definitions.begin(), definitions.end(),
            [slot]( const tile_editor_shortcut_definition_t &definition ) {
                return definition.id == QStringLiteral( "camera.store%1" ).arg( slot );
            } ) == 1 );
        CHECK( std::count_if( definitions.begin(), definitions.end(),
            [slot]( const tile_editor_shortcut_definition_t &definition ) {
                return definition.id == QStringLiteral( "camera.recall%1" ).arg( slot );
            } ) == 1 );
    }

    auto *pStore1 = CameraAction( window, "camera.store1" );
    auto *pRecall1 = CameraAction( window, "camera.recall1" );
    const auto bookmarked = pViewport->camera();
    pStore1->trigger();
    CHECK( pRecall1->isEnabled() );
    CameraAction( window, "camera.viewRight" )->trigger();
    CHECK_FALSE( math::Vec3_NearlyEquals(
        CypherTileCamera_Forward( pViewport->camera() ),
        CypherTileCamera_Forward( bookmarked ), 0.00001f, 0.00001f ) );
    pRecall1->trigger();
    CheckPose( pViewport->camera(), bookmarked );

    REQUIRE( window.openFilePath( replacement, false ) );
    CHECK_FALSE( pViewport->hasCameraBookmark( 0 ) );
    CHECK_FALSE( pRecall1->isEnabled() );
}
