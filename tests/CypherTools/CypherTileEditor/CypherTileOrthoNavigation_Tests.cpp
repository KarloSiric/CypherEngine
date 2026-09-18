//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies orthographic navigation, rendering, and view preferences.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileOrthoView.h"
#include "CypherTileCanvas.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>

using namespace cypher::tools::tile_editor;

namespace {
QApplication &OrthoApplication()
{
    if ( QApplication::instance() ) return *static_cast<QApplication *>( QApplication::instance() );
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileOrthoNavigationTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

void SendMouse( QWidget &view, QEvent::Type type, const QPointF &point,
                Qt::MouseButton button, Qt::MouseButtons held )
{
    QMouseEvent event( type, point, point, button, held, Qt::NoModifier );
    QApplication::sendEvent( &view, &event );
}

void SendKey( QWidget &view, QEvent::Type type, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent event( type, key, modifiers );
    QApplication::sendEvent( &view, &event );
}

int SelectionPixelCount( QWidget &view, const QColor &selectionColor )
{
    QImage image( view.size(), QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );
    view.render( &image );
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ) == selectionColor ) ++count;
        }
    }
    return count;
}

struct ortho_camera_settings_t
{
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;
    QString configRoot;
    QString cacheRoot;

    explicit ortho_camera_settings_t( bool linked = true )
    {
        REQUIRE( directory.isValid() );
        const QSettings probe(
            QSettings::IniFormat, QSettings::UserScope,
            QStringLiteral( "CypherLinkedCameraProbe" ),
            QStringLiteral( "Path" ) );
        previousIniRoot = QFileInfo(
            QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
        QCoreApplication::setApplicationName(
            QStringLiteral( "TileLinkedCameras-" ) +
            QFileInfo( directory.path() ).fileName() );
        configRoot = QFileInfo( TileEditorConfig_DefaultPath() ).absolutePath();
        cacheRoot = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
        tile_editor_preferences_t preferences;
        preferences.startMaximized = false;
        preferences.activateViewOnHover = false;
        preferences.linkOrthographicCameras = linked;
        QSettings settings;
        TileEditorPreferences_Save( settings, preferences );
        QString error;
        REQUIRE( TileEditorConfig_Save(
            TileEditorConfig_DefaultPath(), preferences, error ) );
    }

    ~ortho_camera_settings_t()
    {
        if ( !configRoot.isEmpty() ) QDir( configRoot ).removeRecursively();
        if ( !cacheRoot.isEmpty() ) QDir( cacheRoot ).removeRecursively();
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};
}

TEST_CASE( "Linked 2D cameras share scale and common world axes only",
           "[TileEditor][Ortho][LinkedCamera]" )
{
    OrthoApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 32u, 24u, 2.0f, 3.0f }, &error ) );

    CypherTileCanvas top;
    CypherTileCanvas duplicateTop;
    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    CypherTileOrthoView side( tile_editor_ortho_plane_t::SIDE );
    top.resize( 800, 600 );
    duplicateTop.resize( 640, 480 );
    front.resize( 720, 420 );
    side.resize( 680, 400 );
    for ( auto *view : { &top, &duplicateTop } ) view->setDocumentBridge( &document );
    front.setDocumentBridge( &document );
    side.setDocumentBridge( &document );

    tile_ortho_camera_state_t topState;
    topState.pixelsPerWorldUnit = 6.0;
    topState.centerX = 10.0;
    topState.centerY = 20.0;
    topState.axisMask = TILE_ORTHO_CAMERA_AXIS_X | TILE_ORTHO_CAMERA_AXIS_Y;
    top.synchronizeOrthographicCamera( topState );
    tile_ortho_camera_state_t frontInitial;
    frontInitial.pixelsPerWorldUnit = 3.0;
    frontInitial.centerX = -4.0;
    frontInitial.centerZ = 30.0;
    frontInitial.axisMask = TILE_ORTHO_CAMERA_AXIS_X | TILE_ORTHO_CAMERA_AXIS_Z;
    front.synchronizeOrthographicCamera( frontInitial );
    tile_ortho_camera_state_t sideInitial;
    sideInitial.pixelsPerWorldUnit = 4.0;
    sideInitial.centerY = -8.0;
    sideInitial.centerZ = 42.0;
    sideInitial.axisMask = TILE_ORTHO_CAMERA_AXIS_Y | TILE_ORTHO_CAMERA_AXIS_Z;
    side.synchronizeOrthographicCamera( sideInitial );

    duplicateTop.synchronizeOrthographicCamera( top.orthographicCameraState() );
    front.synchronizeOrthographicCamera( top.orthographicCameraState() );
    side.synchronizeOrthographicCamera( top.orthographicCameraState() );
    auto duplicateState = duplicateTop.orthographicCameraState();
    auto frontState = front.orthographicCameraState();
    auto sideState = side.orthographicCameraState();
    CHECK( duplicateState.pixelsPerWorldUnit == Catch::Approx( 6.0 ) );
    CHECK( duplicateState.centerX == Catch::Approx( 10.0 ) );
    CHECK( duplicateState.centerY == Catch::Approx( 20.0 ) );
    CHECK( frontState.pixelsPerWorldUnit == Catch::Approx( 6.0 ) );
    CHECK( frontState.centerX == Catch::Approx( 10.0 ) );
    CHECK( frontState.centerZ == Catch::Approx( 30.0 ) );
    CHECK( sideState.pixelsPerWorldUnit == Catch::Approx( 6.0 ) );
    CHECK( sideState.centerY == Catch::Approx( 20.0 ) );
    CHECK( sideState.centerZ == Catch::Approx( 42.0 ) );

    tile_ortho_camera_state_t movedFront = frontState;
    movedFront.pixelsPerWorldUnit = 5.0;
    movedFront.centerX = 77.0;
    movedFront.centerZ = 9.0;
    front.synchronizeOrthographicCamera( movedFront );
    top.synchronizeOrthographicCamera( front.orthographicCameraState() );
    duplicateTop.synchronizeOrthographicCamera( front.orthographicCameraState() );
    side.synchronizeOrthographicCamera( front.orthographicCameraState() );
    const auto linkedTop = top.orthographicCameraState();
    const auto linkedDuplicate = duplicateTop.orthographicCameraState();
    const auto linkedSide = side.orthographicCameraState();
    CHECK( linkedTop.pixelsPerWorldUnit == Catch::Approx( 5.0 ) );
    CHECK( linkedTop.centerX == Catch::Approx( 77.0 ) );
    CHECK( linkedTop.centerY == Catch::Approx( 20.0 ) );
    CHECK( linkedDuplicate.centerX == Catch::Approx( 77.0 ) );
    CHECK( linkedDuplicate.centerY == Catch::Approx( 20.0 ) );
    CHECK( linkedSide.centerY == Catch::Approx( 20.0 ) );
    CHECK( linkedSide.centerZ == Catch::Approx( 9.0 ) );
}

TEST_CASE( "Linked camera notifications preserve source anchoring and cannot loop",
           "[TileEditor][Ortho][LinkedCamera][Callbacks]" )
{
    OrthoApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 16u, 16u, 4.0f, 2.0f }, &error ) );
    CypherTileCanvas top;
    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    top.resize( 640, 480 );
    front.resize( 640, 480 );
    top.setDocumentBridge( &document );
    front.setDocumentBridge( &document );
    int sourceNotifications = 0;
    int targetNotifications = 0;
    front.setNavigationChangedCallback(
        [&]( const tile_ortho_camera_state_t &state ) {
            ++targetNotifications;
            top.synchronizeOrthographicCamera( state );
        } );
    top.setNavigationChangedCallback(
        [&]( const tile_ortho_camera_state_t &state ) {
            ++sourceNotifications;
            front.synchronizeOrthographicCamera( state );
        } );

    const QPointF pointer( 173.0, 119.0 );
    const QPointF originBefore = top.viewOrigin();
    const qreal zoomBefore = top.zoomFactor();
    const QPointF worldBefore =
        ( pointer - originBefore ) * document.document()->nCellSize / zoomBefore;
    QWheelEvent zoom(
        pointer, pointer, {}, QPoint( 0, 120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &top, &zoom );
    const QPointF worldAfter =
        ( pointer - top.viewOrigin() ) * document.document()->nCellSize /
        top.zoomFactor();
    CHECK( worldAfter.x() == Catch::Approx( worldBefore.x() ) );
    CHECK( worldAfter.y() == Catch::Approx( worldBefore.y() ) );
    CHECK( sourceNotifications == 1 );
    CHECK( targetNotifications == 0 );
    CHECK( front.pixelsPerUnit() == Catch::Approx(
        top.zoomFactor() / document.document()->nCellSize ) );

    SendMouse( top, QEvent::MouseButtonPress,
               { 50, 60 }, Qt::MiddleButton, Qt::MiddleButton );
    SendMouse( top, QEvent::MouseMove,
               { 72, 48 }, Qt::NoButton, Qt::MiddleButton );
    SendMouse( top, QEvent::MouseButtonRelease,
               { 72, 48 }, Qt::MiddleButton, Qt::NoButton );
    CHECK( sourceNotifications == 2 );
    CHECK( targetNotifications == 0 );
    top.fitToView();
    CHECK( sourceNotifications == 3 );
    CHECK( targetNotifications == 0 );

    tile_editor_preferences_t linked;
    linked.linkOrthographicCameras = true;
    front.setPreferences( linked );
    QWheelEvent extreme(
        QPointF( 100, 100 ), QPointF( 100, 100 ), {}, QPoint( 0, 12000 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &front, &extreme );
    const auto commonRange = TileOrthoCamera_CommonScaleRange(
        document.document()->nCellSize );
    CHECK( front.pixelsPerUnit() == Catch::Approx( commonRange.maximum ) );
}

TEST_CASE( "View command disables linked camera propagation and persists the choice",
           "[TileEditor][Ortho][LinkedCamera][Workspace]" )
{
    OrthoApplication();
    ortho_camera_settings_t settingsScope;
    CypherTileEditorMainWindow window;
    auto *action = window.findChild<QAction *>(
        QStringLiteral( "view.linkOrthographicCameras" ) );
    auto *top = static_cast<CypherTileCanvas *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileCanvas" ) ) );
    auto *front = static_cast<CypherTileOrthoView *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileFrontView" ) ) );
    REQUIRE( action != nullptr );
    REQUIRE( top != nullptr );
    REQUIRE( front != nullptr );
    CHECK( action->isCheckable() );
    CHECK( action->isChecked() );

    tile_ortho_camera_state_t duplicateSeed =
        top->orthographicCameraState();
    duplicateSeed.pixelsPerWorldUnit = 2.5;
    duplicateSeed.centerX = 8.0;
    duplicateSeed.centerY = 12.0;
    top->synchronizeOrthographicCamera( duplicateSeed );
    auto *workspace = static_cast<CypherTileViewWorkspace *>(
        window.findChild<QWidget *>(
            QStringLiteral( "TileEditorFourViews" ) ) );
    REQUIRE( workspace != nullptr );
    REQUIRE( workspace->setPaneView( 2, tile_editor_view_t::TOP ) );
    auto *duplicateTop = static_cast<CypherTileCanvas *>(
        window.findChild<QWidget *>(
            QStringLiteral( "CypherTileCanvasExtra1" ) ) );
    REQUIRE( duplicateTop != nullptr );
    const auto duplicateState = duplicateTop->orthographicCameraState();
    CHECK( duplicateState.pixelsPerWorldUnit == Catch::Approx( 2.5 ) );
    CHECK( duplicateState.centerX == Catch::Approx( 8.0 ) );
    CHECK( duplicateState.centerY == Catch::Approx( 12.0 ) );
    const qreal frontHeightBeforeDuplicateZoom =
        front->orthographicCameraState().centerZ;
    QWheelEvent duplicateZoom(
        QPointF( 96, 72 ), QPointF( 96, 72 ), {}, QPoint( 0, 120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( duplicateTop, &duplicateZoom );
    const auto duplicateZoomed = duplicateTop->orthographicCameraState();
    const auto topAfterDuplicateZoom = top->orthographicCameraState();
    const auto frontAfterDuplicateZoom = front->orthographicCameraState();
    CHECK( topAfterDuplicateZoom.pixelsPerWorldUnit == Catch::Approx(
        duplicateZoomed.pixelsPerWorldUnit ) );
    CHECK( topAfterDuplicateZoom.centerX == Catch::Approx(
        duplicateZoomed.centerX ) );
    CHECK( topAfterDuplicateZoom.centerY == Catch::Approx(
        duplicateZoomed.centerY ) );
    CHECK( frontAfterDuplicateZoom.centerX == Catch::Approx(
        duplicateZoomed.centerX ) );
    CHECK( frontAfterDuplicateZoom.centerZ == Catch::Approx(
        frontHeightBeforeDuplicateZoom ) );

    action->setChecked( false );
    QSettings disabledSettings;
    CHECK_FALSE( TileEditorPreferences_Load(
        disabledSettings ).linkOrthographicCameras );
    const qreal independentScale = front->pixelsPerUnit();
    QWheelEvent independentZoom(
        QPointF( 80, 60 ), QPointF( 80, 60 ), {}, QPoint( 0, 120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( top, &independentZoom );
    CHECK( front->pixelsPerUnit() == Catch::Approx( independentScale ) );

    action->setChecked( true );
    CHECK( front->pixelsPerUnit() == Catch::Approx(
        top->orthographicCameraState().pixelsPerWorldUnit ) );
    QWheelEvent linkedZoom(
        QPointF( 80, 60 ), QPointF( 80, 60 ), {}, QPoint( 0, 120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( top, &linkedZoom );
    CHECK( front->pixelsPerUnit() == Catch::Approx(
        top->orthographicCameraState().pixelsPerWorldUnit ) );
    QSettings persisted;
    CHECK( TileEditorPreferences_Load( persisted ).linkOrthographicCameras );

    tile_ortho_camera_state_t displacedTop =
        top->orthographicCameraState();
    displacedTop.pixelsPerWorldUnit = 3.0;
    displacedTop.centerX = 4.0;
    displacedTop.centerY = 7.0;
    top->synchronizeOrthographicCamera( displacedTop );
    tile_ortho_camera_state_t displacedFront =
        front->orthographicCameraState();
    displacedFront.pixelsPerWorldUnit = 9.0;
    displacedFront.centerX = 30.0;
    displacedFront.centerZ = 11.0;
    front->synchronizeOrthographicCamera( displacedFront );
    auto *fitAll = window.findChild<QAction *>( QStringLiteral( "view.fitAll" ) );
    REQUIRE( fitAll != nullptr );
    fitAll->trigger();
    const auto firstTopFrame = top->orthographicCameraState();
    const auto firstFrontFrame = front->orthographicCameraState();
    CHECK( firstTopFrame.pixelsPerWorldUnit == Catch::Approx(
        firstFrontFrame.pixelsPerWorldUnit ) );
    top->synchronizeOrthographicCamera( displacedTop );
    front->synchronizeOrthographicCamera( displacedFront );
    fitAll->trigger();
    const auto secondTopFrame = top->orthographicCameraState();
    const auto secondFrontFrame = front->orthographicCameraState();
    CHECK( secondTopFrame.pixelsPerWorldUnit == Catch::Approx(
        firstTopFrame.pixelsPerWorldUnit ) );
    CHECK( secondTopFrame.centerX == Catch::Approx( firstTopFrame.centerX ) );
    CHECK( secondTopFrame.centerY == Catch::Approx( firstTopFrame.centerY ) );
    CHECK( secondFrontFrame.pixelsPerWorldUnit == Catch::Approx(
        firstFrontFrame.pixelsPerWorldUnit ) );
    CHECK( secondFrontFrame.centerX == Catch::Approx( firstFrontFrame.centerX ) );
    CHECK( secondFrontFrame.centerZ == Catch::Approx( firstFrontFrame.centerZ ) );
}

TEST_CASE( "2D viewport pan and zoom remain independent when linking is disabled",
           "[TileEditor][Ortho][IndependentCamera][Workspace]" )
{
    OrthoApplication();
    ortho_camera_settings_t settingsScope( false );
    CypherTileEditorMainWindow window;
    window.resize( 1200, 800 );
    window.show();
    QApplication::processEvents();

    auto *action = window.findChild<QAction *>(
        QStringLiteral( "view.linkOrthographicCameras" ) );
    auto *top = static_cast<CypherTileCanvas *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileCanvas" ) ) );
    auto *front = static_cast<CypherTileOrthoView *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileFrontView" ) ) );
    auto *side = static_cast<CypherTileOrthoView *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileSideView" ) ) );
    REQUIRE( action != nullptr );
    REQUIRE( top != nullptr );
    REQUIRE( front != nullptr );
    REQUIRE( side != nullptr );
    CHECK_FALSE( action->isChecked() );

    const auto topBefore = top->orthographicCameraState();
    const auto frontBefore = front->orthographicCameraState();
    const auto sideBefore = side->orthographicCameraState();
    QWheelEvent zoom(
        QPointF( 96, 72 ), QPointF( 96, 72 ), {}, QPoint( 0, 120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( top, &zoom );
    const auto topAfterZoom = top->orthographicCameraState();
    CHECK( topAfterZoom.pixelsPerWorldUnit != Catch::Approx(
        topBefore.pixelsPerWorldUnit ) );
    const auto frontAfterZoom = front->orthographicCameraState();
    const auto sideAfterZoom = side->orthographicCameraState();
    CHECK( frontAfterZoom.pixelsPerWorldUnit == Catch::Approx( frontBefore.pixelsPerWorldUnit ) );
    CHECK( frontAfterZoom.centerX == Catch::Approx( frontBefore.centerX ) );
    CHECK( frontAfterZoom.centerZ == Catch::Approx( frontBefore.centerZ ) );
    CHECK( sideAfterZoom.pixelsPerWorldUnit == Catch::Approx( sideBefore.pixelsPerWorldUnit ) );
    CHECK( sideAfterZoom.centerY == Catch::Approx( sideBefore.centerY ) );
    CHECK( sideAfterZoom.centerZ == Catch::Approx( sideBefore.centerZ ) );

    SendMouse( *top, QEvent::MouseButtonPress, QPointF( 100, 100 ),
               Qt::MiddleButton, Qt::MiddleButton );
    SendMouse( *top, QEvent::MouseMove, QPointF( 160, 130 ),
               Qt::NoButton, Qt::MiddleButton );
    SendMouse( *top, QEvent::MouseButtonRelease, QPointF( 160, 130 ),
               Qt::MiddleButton, Qt::NoButton );
    const auto topAfterPan = top->orthographicCameraState();
    CHECK( ( topAfterPan.centerX != Catch::Approx( topAfterZoom.centerX ) ||
             topAfterPan.centerY != Catch::Approx( topAfterZoom.centerY ) ) );
    const auto frontAfterPan = front->orthographicCameraState();
    const auto sideAfterPan = side->orthographicCameraState();
    CHECK( frontAfterPan.pixelsPerWorldUnit == Catch::Approx( frontBefore.pixelsPerWorldUnit ) );
    CHECK( frontAfterPan.centerX == Catch::Approx( frontBefore.centerX ) );
    CHECK( frontAfterPan.centerZ == Catch::Approx( frontBefore.centerZ ) );
    CHECK( sideAfterPan.pixelsPerWorldUnit == Catch::Approx( sideBefore.pixelsPerWorldUnit ) );
    CHECK( sideAfterPan.centerY == Catch::Approx( sideBefore.centerY ) );
    CHECK( sideAfterPan.centerZ == Catch::Approx( sideBefore.centerZ ) );

    // Exercise configureOrthoView's callback too: navigating Front must leave
    // both Top and Side exactly where the user placed them.
    const auto topBeforeFrontNavigation = top->orthographicCameraState();
    const auto frontBeforeOwnNavigation = front->orthographicCameraState();
    const auto sideBeforeFrontNavigation = side->orthographicCameraState();
    QWheelEvent frontZoom(
        QPointF( 90, 66 ), QPointF( 90, 66 ), {}, QPoint( 0, -120 ),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( front, &frontZoom );
    const auto frontAfterOwnZoom = front->orthographicCameraState();
    CHECK( frontAfterOwnZoom.pixelsPerWorldUnit != Catch::Approx(
        frontBeforeOwnNavigation.pixelsPerWorldUnit ) );
    const auto topAfterFrontZoom = top->orthographicCameraState();
    const auto sideAfterFrontZoom = side->orthographicCameraState();
    CHECK( topAfterFrontZoom.pixelsPerWorldUnit == Catch::Approx(
        topBeforeFrontNavigation.pixelsPerWorldUnit ) );
    CHECK( topAfterFrontZoom.centerX == Catch::Approx( topBeforeFrontNavigation.centerX ) );
    CHECK( topAfterFrontZoom.centerY == Catch::Approx( topBeforeFrontNavigation.centerY ) );
    CHECK( sideAfterFrontZoom.pixelsPerWorldUnit == Catch::Approx(
        sideBeforeFrontNavigation.pixelsPerWorldUnit ) );
    CHECK( sideAfterFrontZoom.centerY == Catch::Approx( sideBeforeFrontNavigation.centerY ) );
    CHECK( sideAfterFrontZoom.centerZ == Catch::Approx( sideBeforeFrontNavigation.centerZ ) );

    SendMouse( *front, QEvent::MouseButtonPress, QPointF( 100, 100 ),
               Qt::MiddleButton, Qt::MiddleButton );
    SendMouse( *front, QEvent::MouseMove, QPointF( 145, 135 ),
               Qt::NoButton, Qt::MiddleButton );
    SendMouse( *front, QEvent::MouseButtonRelease, QPointF( 145, 135 ),
               Qt::MiddleButton, Qt::NoButton );
    const auto frontAfterOwnPan = front->orthographicCameraState();
    CHECK( ( frontAfterOwnPan.centerX != Catch::Approx( frontAfterOwnZoom.centerX ) ||
             frontAfterOwnPan.centerZ != Catch::Approx( frontAfterOwnZoom.centerZ ) ) );
    const auto topAfterFrontPan = top->orthographicCameraState();
    const auto sideAfterFrontPan = side->orthographicCameraState();
    CHECK( topAfterFrontPan.pixelsPerWorldUnit == Catch::Approx(
        topBeforeFrontNavigation.pixelsPerWorldUnit ) );
    CHECK( topAfterFrontPan.centerX == Catch::Approx( topBeforeFrontNavigation.centerX ) );
    CHECK( topAfterFrontPan.centerY == Catch::Approx( topBeforeFrontNavigation.centerY ) );
    CHECK( sideAfterFrontPan.pixelsPerWorldUnit == Catch::Approx(
        sideBeforeFrontNavigation.pixelsPerWorldUnit ) );
    CHECK( sideAfterFrontPan.centerY == Catch::Approx( sideBeforeFrontNavigation.centerY ) );
    CHECK( sideAfterFrontPan.centerZ == Catch::Approx( sideBeforeFrontNavigation.centerZ ) );
}

TEST_CASE( "Front and Side panning share all navigation gestures without selecting", "[TileEditor][Ortho]" )
{
    OrthoApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        CypherTileOrthoView view( plane );
        int selections = 0;
        view.setSelectionCallback( [&]( tile_map_grid_coord_t ) { ++selections; } );
        for ( auto button : { Qt::MiddleButton, Qt::RightButton, Qt::LeftButton } ) {
            if ( button == Qt::LeftButton ) SendKey( view, QEvent::KeyPress, Qt::Key_Space );
            const QPointF before = view.viewOrigin();
            SendMouse( view, QEvent::MouseButtonPress, { 40, 50 }, button, button );
            REQUIRE( view.isPanning() );
            SendMouse( view, QEvent::MouseMove, { 65, 41 }, Qt::NoButton, button );
            CHECK( view.viewOrigin() == before + QPointF( 25, -9 ) );
            // Another button's release must not terminate this drag.
            SendMouse( view, QEvent::MouseButtonRelease, { 65, 41 },
                       button == Qt::RightButton ? Qt::MiddleButton : Qt::RightButton, button );
            CHECK( view.isPanning() );
            SendMouse( view, QEvent::MouseButtonRelease, { 65, 41 }, button, Qt::NoButton );
            CHECK_FALSE( view.isPanning() );
            SendKey( view, QEvent::KeyRelease, Qt::Key_Space );
        }
        view.setPanToolEnabled( true );
        CHECK( view.cursor().shape() == Qt::OpenHandCursor );
        SendMouse( view, QEvent::MouseButtonPress, { 30, 30 }, Qt::LeftButton, Qt::LeftButton );
        CHECK( view.isPanning() );
        view.setPanToolEnabled( false );
        CHECK_FALSE( view.isPanning() );
        CHECK( selections == 0 );
    }
}

TEST_CASE( "Front and Side RMB wait for the platform drag threshold before panning",
           "[TileEditor][Ortho][ContextMenu]" )
{
    OrthoApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT,
                         tile_editor_ortho_plane_t::SIDE } ) {
        INFO( ( plane == tile_editor_ortho_plane_t::FRONT ? "Front" : "Side" ) );
        CypherTileOrthoView view( plane );
        int contextMenus = 0;
        int navigationChanges = 0;
        QPoint requestedPosition;
        view.setContextMenuCallback( [&]( const QPoint &globalPosition ) {
            ++contextMenus;
            requestedPosition = globalPosition;
        } );
        view.setNavigationChangedCallback(
            [&]( const tile_ortho_camera_state_t & ) { ++navigationChanges; } );

        const QPointF start( 40, 50 );
        const QPointF initialOrigin = view.viewOrigin();
        SendMouse( view, QEvent::MouseButtonPress,
                   start, Qt::RightButton, Qt::RightButton );
        REQUIRE( view.isPanning() );
        SendMouse( view, QEvent::MouseButtonRelease,
                   start, Qt::RightButton, Qt::NoButton );
        CHECK_FALSE( view.isPanning() );
        CHECK( view.viewOrigin() == initialOrigin );
        CHECK( contextMenus == 0 );
        QApplication::processEvents();
        CHECK( contextMenus == 1 );
        CHECK( requestedPosition == start.toPoint() );
        CHECK( navigationChanges == 0 );

        const qreal dragThreshold = QApplication::startDragDistance();
        REQUIRE( dragThreshold > 0.0 );
        const QPointF jitterDelta( dragThreshold * 0.5, 0.0 );
        SendMouse( view, QEvent::MouseButtonPress,
                   start, Qt::RightButton, Qt::RightButton );
        REQUIRE( view.isPanning() );
        SendMouse( view, QEvent::MouseMove,
                   start + jitterDelta, Qt::NoButton, Qt::RightButton );
        CHECK( view.isPanning() );
        CHECK( view.viewOrigin() == initialOrigin );
        CHECK( navigationChanges == 0 );
        SendMouse( view, QEvent::MouseButtonRelease,
                   start + jitterDelta, Qt::RightButton, Qt::NoButton );
        QApplication::processEvents();
        CHECK( contextMenus == 2 );

        const QPointF thresholdDelta( dragThreshold, 0.0 );
        const QPointF continuedDelta( dragThreshold + 7.0, -4.0 );
        SendMouse( view, QEvent::MouseButtonPress,
                   start, Qt::RightButton, Qt::RightButton );
        SendMouse( view, QEvent::MouseMove,
                   start + jitterDelta, Qt::NoButton, Qt::RightButton );
        CHECK( view.viewOrigin() == initialOrigin );
        SendMouse( view, QEvent::MouseMove,
                   start + thresholdDelta, Qt::NoButton, Qt::RightButton );
        CHECK( view.viewOrigin() == initialOrigin + thresholdDelta );
        CHECK( navigationChanges == 1 );
        SendMouse( view, QEvent::MouseMove,
                   start + continuedDelta, Qt::NoButton, Qt::RightButton );
        CHECK( view.viewOrigin() == initialOrigin + continuedDelta );
        CHECK( navigationChanges == 2 );
        SendMouse( view, QEvent::MouseButtonRelease,
                   start + continuedDelta, Qt::RightButton, Qt::NoButton );
        CHECK_FALSE( view.isPanning() );
        QApplication::processEvents();
        CHECK( contextMenus == 2 );
    }
}

TEST_CASE( "Orthographic navigation releases cleanly across focus and window changes", "[TileEditor][Ortho]" )
{
    OrthoApplication();
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    for ( auto type : { QEvent::FocusOut, QEvent::Hide, QEvent::WindowDeactivate } ) {
        SendKey( view, QEvent::KeyPress, Qt::Key_Space );
        SendMouse( view, QEvent::MouseButtonPress, { 30, 30 }, Qt::LeftButton, Qt::LeftButton );
        REQUIRE( view.isPanning() );
        if ( type == QEvent::FocusOut ) {
            QFocusEvent interruption( QEvent::FocusOut );
            QApplication::sendEvent( &view, &interruption );
        } else if ( type == QEvent::Hide ) {
            QHideEvent interruption;
            QApplication::sendEvent( &view, &interruption );
        } else {
            QEvent interruption( type );
            QApplication::sendEvent( &view, &interruption );
        }
        CHECK_FALSE( view.isPanning() );
        CHECK( view.cursor().shape() == Qt::ArrowCursor );
        SendMouse( view, QEvent::MouseButtonPress, { 30, 30 }, Qt::LeftButton, Qt::LeftButton );
        CHECK_FALSE( view.isPanning() );
    }
    SendMouse( view, QEvent::MouseButtonPress, { 30, 30 }, Qt::RightButton, Qt::RightButton );
    REQUIRE( view.isPanning() );
    SendKey( view, QEvent::KeyPress, Qt::Key_Escape );
    CHECK_FALSE( view.isPanning() );
    SendMouse( view, QEvent::MouseButtonPress, { 30, 30 }, Qt::MiddleButton, Qt::MiddleButton );
    const QPointF before = view.viewOrigin();
    SendMouse( view, QEvent::MouseMove, { 100, 100 }, Qt::NoButton, Qt::NoButton );
    CHECK_FALSE( view.isPanning() );
    CHECK( view.viewOrigin() == before );
}

TEST_CASE( "Space navigation leaves Shift Space available for maximize", "[TileEditor][Ortho]" )
{
    OrthoApplication();
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    QKeyEvent ordinary( QEvent::ShortcutOverride, Qt::Key_Space, Qt::NoModifier );
    ordinary.setAccepted( false );
    QApplication::sendEvent( &view, &ordinary );
    CHECK( ordinary.isAccepted() );
    QKeyEvent maximize( QEvent::ShortcutOverride, Qt::Key_Space, Qt::ShiftModifier );
    maximize.setAccepted( false );
    QApplication::sendEvent( &view, &maximize );
    CHECK_FALSE( maximize.isAccepted() );
}

TEST_CASE( "Orthographic zoom stays anchored beneath the pointer", "[TileEditor][Ortho]" )
{
    OrthoApplication();
    CypherTileOrthoView view( tile_editor_ortho_plane_t::SIDE );
    const QPointF pointer( 100, 80 );
    const QPointF before = view.viewOrigin();
    QWheelEvent zoom( pointer, pointer, {}, QPoint( 0, 120 ),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QApplication::sendEvent( &view, &zoom );
    const QPointF expected = pointer - ( pointer - before ) * 1.18;
    CHECK( qAbs( view.viewOrigin().x() - expected.x() ) < 0.00001 );
    CHECK( qAbs( view.viewOrigin().y() - expected.y() ) < 0.00001 );
}

TEST_CASE( "Front and Side frame selected geometry and fall back to the whole map",
    "[TileEditor][Ortho][Selection][Framing]" )
{
    OrthoApplication();
    for ( const auto plane : { tile_editor_ortho_plane_t::FRONT,
                               tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        tile_map_document_desc_t description{};
        description.nWidth = description.nHeight = 32;
        description.nCellSize = 2.0f;
        description.nLevelHeight = 3.0f;
        QString error;
        REQUIRE( document.newDocument( description, &error ) );
        REQUIRE( document.beginEdit( QStringLiteral( "Framing fixture" ), &error ) );
        const tile_map_grid_coord_t selected{ 2, 3 };
        const tile_map_grid_coord_t distant = front
            ? tile_map_grid_coord_t{ 27, 3 }
            : tile_map_grid_coord_t{ 2, 27 };
        REQUIRE( document.paintCell( selected, { 1, 2, 0 }, &error ) );
        REQUIRE( document.paintCell( distant, { 5, 2, 0 }, &error ) );
        REQUIRE( document.commitEdit( &error ) );

        CypherTileOrthoView view( plane );
        view.resize( 720, 420 );
        view.setDocumentBridge( &document );
        view.fitToView();
        const qreal mapScale = view.pixelsPerUnit();
        view.setSelection( true, selected );
        view.fitSelection();
        CHECK( view.pixelsPerUnit() > mapScale );
        const qreal selectedHorizontalCenter =
            ( front ? selected.x + 0.5 : selected.y + 0.5 ) *
            description.nCellSize;
        CHECK( qAbs( view.viewOrigin().x() +
                     selectedHorizontalCenter * view.pixelsPerUnit() -
                     view.width() * 0.5 ) < 0.001 );

        view.setSelection( false, {} );
        view.fitSelection();
        CHECK( view.pixelsPerUnit() == Catch::Approx( mapScale ) );
    }
}

TEST_CASE( "Orthographic region selection highlights all selected source cells", "[TileEditor][Ortho]" )
{
    OrthoApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 8;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Selection fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 1 }, &error ) );
    REQUIRE( document.paintCell( { 3, 1 }, { 0, 1, 1 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 600, 300 );
    view.setDocumentBridge( &document );
    view.setSelection( true, { 1, 1 } );
    const int single = SelectionPixelCount( view, tile_editor_preferences_t{}.selectionColor );
    REQUIRE( single > 0 );
    view.setSelectionRect( true, { 1, 1, 3, 1 } );
    const int multiple = SelectionPixelCount( view, tile_editor_preferences_t{}.selectionColor );
    CHECK( multiple > single * 1.8 );
    view.setSelectionRect( false, {} );
    CHECK( SelectionPixelCount( view, tile_editor_preferences_t{}.selectionColor ) == 0 );
}

TEST_CASE( "Wireframe preference persists while saved canvas colors remain intact", "[TileEditor][Settings]" )
{
    OrthoApplication();
    QTemporaryDir directory;
    QSettings settings( directory.filePath( QStringLiteral( "prefs.ini" ) ), QSettings::IniFormat );
    CHECK( TileEditorPreferences_Load( settings ).wireframeOrtho );
    tile_editor_preferences_t preferences{};
    preferences.wireframeOrtho = false;
    preferences.canvasColor = QColor( 80, 20, 60 );
    TileEditorPreferences_Save( settings, preferences );
    const auto loaded = TileEditorPreferences_Load( settings );
    CHECK_FALSE( loaded.wireframeOrtho );
    CHECK( loaded.canvasColor == preferences.canvasColor );
    CypherTileEditorSettingsDialog dialog( loaded );
    auto *pCheckbox = dialog.findChild<QCheckBox *>( QStringLiteral( "TileSettingsWireframeOrtho" ) );
    REQUIRE( pCheckbox != nullptr );
    CHECK_FALSE( pCheckbox->isChecked() );
    pCheckbox->setChecked( true );
    CHECK( dialog.preferences().wireframeOrtho );
}

TEST_CASE( "Default editor shortcuts have valid distinct portable bindings", "[TileEditor][Settings]" )
{
    OrthoApplication();
    QSet<QString> bindings;
    QSet<QString> optionalActions{
        "view.assets", "view.outliner", "view.properties", "view.toolRail",
        "app.openConfig", "app.reloadConfig", "app.importConfig", "app.exportConfig",
        "camera.fly", "camera.orbit", "camera.faster", "camera.slower", "camera.frameMap",
        "camera.settings", "camera.toggleMode", "camera.autoOrbit",
        "camera.viewPerspective", "camera.viewTop", "camera.viewBottom",
        "camera.viewFront", "camera.viewBack", "camera.viewLeft", "camera.viewRight",
        "camera.store1", "camera.store2", "camera.store3", "camera.store4",
        "camera.recall1", "camera.recall2", "camera.recall3", "camera.recall4",
        "view.cameraHints", "view.activeBorder", "map.properties",
        "view.orthoMaterials", "view.materialLabels"
    };
    for ( const auto &definition : TileEditorShortcutDefinitions() ) {
        INFO( definition.id.toStdString() );
        const QString portable = definition.defaultSequence.toString( QKeySequence::PortableText );
        if ( optionalActions.remove( definition.id ) ) {
            REQUIRE( portable.isEmpty() );
            continue;
        }
        REQUIRE_FALSE( portable.isEmpty() );
        REQUIRE_FALSE( bindings.contains( portable ) );
        bindings.insert( portable );
    }
    CHECK( optionalActions.isEmpty() );
}
