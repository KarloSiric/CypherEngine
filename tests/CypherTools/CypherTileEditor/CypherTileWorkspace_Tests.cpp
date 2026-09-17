//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies quad pane geometry, focus, frame and maximize behavior.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileViewWorkspace.h"
#include "CypherTileCanvas.h"
#include "CypherTileGrid.h"
#include "Core/CypherTileMapMaterials.h"
#include "CypherTileEditorIcons.h"
#include "CypherTileEditorTheme.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QAction>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QMouseEvent>
#include <QFile>
#include <QEnterEvent>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QImage>

using namespace cypher::tools::tile_editor;

namespace {
QApplication &EnsureApplication()
{
    if ( QApplication::instance() ) return *static_cast<QApplication *>( QApplication::instance() );
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileWorkspaceTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    static QTemporaryDir settings;
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "TileWorkspace" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settings.path() );
    return application;
}

QImage IconImage( const QIcon &icon, QIcon::State state = QIcon::Off )
{
    return icon.pixmap( QSize( 32, 32 ), QIcon::Normal, state )
        .toImage().convertToFormat( QImage::Format_ARGB32 );
}

QColor OpaqueIconColor( const QIcon &icon, QIcon::State state = QIcon::Off )
{
    const QImage image = IconImage( icon, state );
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 samples = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        const auto *row = reinterpret_cast<const QRgb *>( image.constScanLine( y ) );
        for ( int x = 0; x < image.width(); ++x ) {
            if ( qAlpha( row[x] ) < 192 ) continue;
            red += qRed( row[x] );
            green += qGreen( row[x] );
            blue += qBlue( row[x] );
            ++samples;
        }
    }
    if ( samples == 0 ) return {};
    return QColor(
        static_cast<int>( red / samples ),
        static_cast<int>( green / samples ),
        static_cast<int>( blue / samples ) );
}

namespace {
void PointerEvent( CypherTileCanvas &canvas, QEvent::Type type, QPointF point )
{
    QMouseEvent event( type, point, point,
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( &canvas, &event );
}
QPointF CellCenter( CypherTileCanvas &canvas, tile_map_grid_coord_t cell )
{
    canvas.selectCell( cell, true );
    return { canvas.width() * 0.5, canvas.height() * 0.5 };
}
}

TEST_CASE( "Line paints connected cells as one undoable edit", "[TileEditor][Tools]" )
{
    EnsureApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 8;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    CypherTileCanvas canvas;
    canvas.resize( 500, 400 );
    canvas.setDocumentBridge( &document );
    canvas.setTool( tile_canvas_tool_t::LINE );
    const QPointF start = CellCenter( canvas, { 1, 1 } );
    const QPointF end = start + QPointF( canvas.zoomFactor() * 3, canvas.zoomFactor() * 3 );
    PointerEvent( canvas, QEvent::MouseButtonPress, start );
    PointerEvent( canvas, QEvent::MouseMove, end );
    PointerEvent( canvas, QEvent::MouseButtonRelease, end );
    for ( int i = 1; i <= 4; ++i ) CHECK( CypherTileMapDocument_CellHasFloor( document.document(), { i, i } ) );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { 1, 2 } ) );
    REQUIRE( document.undo( &error ) );
    for ( int i = 1; i <= 4; ++i ) CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { i, i } ) );
    REQUIRE( document.redo( &error ) );
    CHECK( CypherTileMapDocument_CellHasFloor( document.document(), { 4, 4 } ) );
}

TEST_CASE( "Fill respects connected surface boundaries and picker and pan do not edit", "[TileEditor][Tools]" )
{
    EnsureApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 8;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Fixture" ), &error ) );
    REQUIRE( document.paintRect( { 1, 1, 2, 2 }, { -2, 2, 1 }, &error ) );
    REQUIRE( document.paintCell( { 5, 5 }, { -2, 2, 1 }, &error ) );
    REQUIRE( document.paintCell( { 3, 1 }, { -2, 2, 2 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    document.markSaved();
    CypherTileCanvas canvas;
    canvas.resize( 500, 400 );
    canvas.setDocumentBridge( &document );
    canvas.setTool( tile_canvas_tool_t::FILL );
    canvas.setPaint( { 0, 1, 6 } );
    QPointF center = CellCenter( canvas, { 1, 1 } );
    PointerEvent( canvas, QEvent::MouseButtonPress, center );
    PointerEvent( canvas, QEvent::MouseButtonRelease, center );
    CHECK( CypherTileMapDocument_CellAt( document.document(), { 2, 2 } )->nMaterialSlot == 6 );
    CHECK( CypherTileMapDocument_CellAt( document.document(), { 5, 5 } )->nMaterialSlot == 1 );
    CHECK( CypherTileMapDocument_CellAt( document.document(), { 3, 1 } )->nMaterialSlot == 2 );
    REQUIRE( document.undo( &error ) );
    CHECK_FALSE( document.isDirty() );
    canvas.setTool( tile_canvas_tool_t::EYEDROPPER );
    bool picked = false;
    canvas.setPaintPickedCallback( [&]( const tile_map_paint_t &paint ) {
        picked = true;
        CHECK( paint.nMaterialSlot == 1 );
        CHECK( paint.nFloorLevel == -2 );
        CHECK( paint.nWallHeightLevels == 2 );
    } );
    PointerEvent( canvas, QEvent::MouseButtonPress, center );
    PointerEvent( canvas, QEvent::MouseButtonRelease, center );
    CHECK( picked );
    CHECK_FALSE( document.isDirty() );
    canvas.setTool( tile_canvas_tool_t::PAN );
    PointerEvent( canvas, QEvent::MouseButtonPress, center );
    PointerEvent( canvas, QEvent::MouseMove, center + QPointF( 30, 30 ) );
    PointerEvent( canvas, QEvent::MouseButtonRelease, center + QPointF( 30, 30 ) );
    CHECK_FALSE( document.isDirty() );
}

TEST_CASE( "Editor preferences persist and reject invalid numeric settings", "[TileEditor][Settings]" )
{
    EnsureApplication();
    QTemporaryDir directory;
    QSettings settings( directory.filePath( QStringLiteral( "prefs.ini" ) ), QSettings::IniFormat );
    tile_editor_preferences_t preferences{};
    preferences.showGrid = preferences.showMarkers = false;
    preferences.startMaximized = preferences.frameMapOnOpen = false;
    preferences.gridSpacingCells = 8;
    preferences.adaptiveGrid = false;
    preferences.gridMinimumPixels = 20;
    preferences.activateViewOnHover = false;
    preferences.shortcuts.insert( QStringLiteral( "tool.fill" ), QKeySequence( QStringLiteral( "Alt+G" ) ) );
    TileEditorPreferences_Save( settings, preferences );
    auto loaded = TileEditorPreferences_Load( settings );
    CHECK_FALSE( loaded.showGrid );
    CHECK_FALSE( loaded.showMarkers );
    CHECK_FALSE( loaded.startMaximized );
    CHECK_FALSE( loaded.frameMapOnOpen );
    CHECK( loaded.gridSpacingCells == 8 );
    CHECK_FALSE( loaded.adaptiveGrid );
    CHECK_FALSE( loaded.activateViewOnHover );
    CHECK( loaded.gridMinimumPixels == 20 );
    CHECK( loaded.shortcuts.value( QStringLiteral( "tool.fill" ) ) == QKeySequence( QStringLiteral( "Alt+G" ) ) );
    settings.setValue( QStringLiteral( "TileEditor/Preferences/gridSpacingCells" ), 0 );
    settings.setValue( QStringLiteral( "TileEditor/Preferences/defaultMapWidth" ), -5 );
    settings.setValue( QStringLiteral( "TileEditor/Preferences/showGrid" ), QStringLiteral( "invalid" ) );
    settings.setValue( QStringLiteral( "TileEditor/Preferences/gridMinimumPixels" ), 0 );
    loaded = TileEditorPreferences_Load( settings );
    CHECK( loaded.gridSpacingCells == 1 );
    CHECK( loaded.defaultMapWidth == 1 );
    CHECK( loaded.showGrid );
    CHECK( loaded.gridMinimumPixels == 4 );
}

TEST_CASE( "Bundled editor theme and every semantic SVG icon load offline", "[TileEditor][Assets]" )
{
    auto &app = EnsureApplication();
    CypherTileEditorTheme_Apply( app );
    CHECK( app.styleSheet().contains( QStringLiteral( "TileEditorToolRail" ) ) );
    for ( int i = 0; i <= static_cast<int>( tile_editor_icon_t::PLAY ); ++i ) {
        INFO( i );
        const QIcon icon = CypherTileEditorIcon_Create( static_cast<tile_editor_icon_t>( i ) );
        REQUIRE_FALSE( icon.isNull() );
        CHECK_FALSE( icon.pixmap( 22, 22 ).isNull() );
    }

    const QColor validate = OpaqueIconColor(
        CypherTileEditorIcon_Create( tile_editor_icon_t::VALIDATE ) );
    const QColor build = OpaqueIconColor(
        CypherTileEditorIcon_Create( tile_editor_icon_t::BUILD ) );
    const QColor run = OpaqueIconColor(
        CypherTileEditorIcon_Create( tile_editor_icon_t::PLAY ) );
    const QColor stop = OpaqueIconColor(
        CypherTileEditorIcon_Create( tile_editor_icon_t::STOP ) );
    REQUIRE( validate.isValid() );
    REQUIRE( build.isValid() );
    REQUIRE( run.isValid() );
    REQUIRE( stop.isValid() );
    CHECK( validate.blue() > validate.green() );
    CHECK( validate.blue() > validate.red() );
    CHECK( build.red() > build.green() );
    CHECK( build.green() > build.blue() );
    CHECK( run.green() > run.red() );
    CHECK( run.green() > run.blue() );
    CHECK( stop.red() > stop.green() );
    CHECK( stop.red() > stop.blue() );

    const QIcon materialPreview = CypherTileEditorIcon_Create(
        tile_editor_icon_t::MATERIAL );
    const QIcon materialLibrary = CypherTileEditorIcon_Create(
        tile_editor_icon_t::MATERIAL_LIBRARY );
    CHECK( IconImage( materialPreview ) != IconImage( materialLibrary ) );
    CHECK( IconImage( materialPreview, QIcon::Off ) !=
           IconImage( materialPreview, QIcon::On ) );
    CHECK( IconImage( materialLibrary, QIcon::Off ) !=
           IconImage( materialLibrary, QIcon::On ) );
}
}

TEST_CASE( "Four view workspace frames individual panes and restores their sizes", "[TileEditor][Workspace]" )
{
    auto &app = EnsureApplication();
    std::array<QWidget *, 4> views{};
    for ( QWidget *&pView : views ) {
        pView = new QWidget();
        pView->setMinimumSize( 160, 120 );
        pView->setFocusPolicy( Qt::StrongFocus );
    }
    CypherTileViewWorkspace workspace( views );
    workspace.resize( 960, 700 );
    workspace.show();
    app.processEvents();
    for ( QWidget *pView : views ) {
        CHECK( pView->isVisible() );
        CHECK( pView->width() > 350 );
        CHECK( pView->height() > 250 );
    }
    CHECK( views[1]->mapTo( &workspace, QPoint() ).x() < views[0]->mapTo( &workspace, QPoint() ).x() );
    CHECK( views[0]->mapTo( &workspace, QPoint() ).y() < views[2]->mapTo( &workspace, QPoint() ).y() );
    const QSize previousSize = views[2]->size();
    int frameCount = 0;
    workspace.setFrameCallback( [&]( tile_editor_view_t view ) {
        CHECK( view == tile_editor_view_t::FRONT );
        ++frameCount;
    } );
    auto *pFrame = workspace.findChild<QToolButton *>( QStringLiteral( "TileViewFrame2" ) );
    REQUIRE( pFrame );
    pFrame->click();
    CHECK( frameCount == 1 );
    CHECK( workspace.activeView() == tile_editor_view_t::FRONT );
    workspace.toggleMaximize();
    app.processEvents();
    CHECK( workspace.isMaximized() );
    CHECK( views[2]->isVisible() );
    CHECK_FALSE( views[0]->isVisible() );
    CHECK_FALSE( views[1]->isVisible() );
    CHECK_FALSE( views[3]->isVisible() );
    CHECK( views[2]->height() > previousSize.height() );
    workspace.showFourViews();
    app.processEvents();
    CHECK_FALSE( workspace.isMaximized() );
    for ( QWidget *pView : views ) CHECK( pView->isVisible() );
    CHECK( views[2]->size() == previousSize );
    workspace.toggleMaximize();
    workspace.focusView( tile_editor_view_t::PERSPECTIVE );
    CHECK_FALSE( workspace.isMaximized() );
    CHECK( workspace.activeView() == tile_editor_view_t::PERSPECTIVE );
}

TEST_CASE( "Pointer ownership activates and focuses a viewport without a preparatory click", "[TileEditor][Workspace]" )
{
    auto &app = EnsureApplication();
    QWidget host;
    auto *pLayout = new QVBoxLayout( &host );
    std::array<QWidget *, 4> views{};
    for ( QWidget *&pView : views ) {
        pView = new QWidget();
        pView->setMinimumSize( 160, 120 );
        pView->setFocusPolicy( Qt::StrongFocus );
    }
    auto *pWorkspace = new CypherTileViewWorkspace( views, &host );
    auto *pInput = new QLineEdit( &host );
    pLayout->addWidget( pWorkspace );
    pLayout->addWidget( pInput );
    host.resize( 800, 650 );
    host.show();
    host.activateWindow();
    app.processEvents();
    REQUIRE( host.isActiveWindow() );
    pWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
    auto enter = [&]( int index ) {
        QEnterEvent event( QPointF( 20, 20 ), QPointF( 20, 20 ), QPointF( 20, 20 ) );
        QApplication::sendEvent( views[index], &event );
    };
    enter( 2 );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::FRONT );
    CHECK( views[2]->hasFocus() );
    for ( int i = 0; i < 4; ++i ) {
        CHECK( views[i]->parentWidget()->property( "active" ).toBool() == ( i == 2 ) );
    }
    QAction viewportShortcut( &host );
    viewportShortcut.setShortcut( QKeySequence( Qt::Key_H ) );
    viewportShortcut.setShortcutContext( Qt::WidgetShortcut );
    views[2]->addAction( &viewportShortcut );
    int shortcutTriggers = 0;
    QObject::connect( &viewportShortcut, &QAction::triggered,
        [&shortcutTriggers] { ++shortcutTriggers; } );
    QKeyEvent shortcutPress( QEvent::KeyPress, Qt::Key_H, Qt::NoModifier );
    QApplication::sendEvent( views[2], &shortcutPress );
    CHECK( shortcutTriggers == 1 );
    pInput->setFocus();
    pInput->setText( QStringLiteral( "material 3" ) );
    enter( 3 );
    CHECK_FALSE( pInput->hasFocus() );
    CHECK( views[3]->hasFocus() );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::SIDE );
    CHECK( pInput->text() == QStringLiteral( "material 3" ) );

    // Mouse tracking keeps ownership correct after a popup/dialog or another
    // control temporarily took focus while the pointer stayed in this view.
    pInput->setFocus();
    QMouseEvent move( QEvent::MouseMove, QPointF( 25, 25 ), QPointF( 25, 25 ),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QApplication::sendEvent( views[2], &move );
    CHECK_FALSE( pInput->hasFocus() );
    CHECK( views[2]->hasFocus() );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::FRONT );

    // Crossing a pane frame/header has the same ownership semantics as its
    // view body, so the narrow chrome never creates a dead input strip.
    pInput->setFocus();
    QEnterEvent paneEnter( QPointF( 2, 2 ), QPointF( 2, 2 ), QPointF( 2, 2 ) );
    QApplication::sendEvent( views[0]->parentWidget(), &paneEnter );
    CHECK( views[0]->hasFocus() );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::TOP );

    // A button-held move belongs to the drag that started elsewhere and must
    // not steal focus merely because the drag crosses another viewport.
    pInput->setFocus();
    QMouseEvent dragMove( QEvent::MouseMove, QPointF( 30, 30 ), QPointF( 30, 30 ),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( views[3], &dragMove );
    CHECK( pInput->hasFocus() );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::TOP );

    pWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
    pWorkspace->setActivateOnHover( false );
    pInput->setFocus();
    enter( 0 );
    CHECK( pWorkspace->activeView() == tile_editor_view_t::PERSPECTIVE );
    CHECK( pInput->hasFocus() );
}

TEST_CASE( "Adaptive grid settings expose their actual enabled state", "[TileEditor][Settings]" )
{
    EnsureApplication();
    tile_editor_preferences_t preferences{};
    preferences.adaptiveGrid = false;
    preferences.gridMinimumPixels = 20;
    CypherTileEditorSettingsDialog dialog( preferences );
    auto *pAdaptive = dialog.findChild<QCheckBox *>( QStringLiteral( "TileSettingsAdaptiveGrid" ) );
    auto *pSpacing = dialog.findChild<QSpinBox *>( QStringLiteral( "TileSettingsGridMinimumPixels" ) );
    auto *pHover = dialog.findChild<QCheckBox *>( QStringLiteral( "TileSettingsActivateViewOnHover" ) );
    REQUIRE( pAdaptive );
    REQUIRE( pSpacing );
    REQUIRE( pHover );
    CHECK_FALSE( pSpacing->isEnabled() );
    pAdaptive->setChecked( true );
    CHECK( pSpacing->isEnabled() );
    pSpacing->setValue( 24 );
    pHover->setChecked( false );
    CHECK( dialog.preferences().adaptiveGrid );
    CHECK( dialog.preferences().gridMinimumPixels == 24 );
    CHECK_FALSE( dialog.preferences().activateViewOnHover );
}

TEST_CASE( "Grid density follows zoom without shifting authored coordinates", "[TileEditor][Grid]" )
{
    // Zoom thresholds double the displayed interval, including tiny zooms.
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 12.0, true, 12 ) == 1 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 11.9, true, 12 ) == 2 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 5.0, true, 12 ) == 4 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 0.125, true, 12 ) == 128 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 4, 100.0, true, 12 ) == 4 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 3, 0.125, false, 12 ) == 3 );
    // Side/front cell widths and level heights may represent different units.
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 64 * 0.1, true, 12 ) == 2 );
    CHECK( TileEditorGrid_EffectiveSpacingCells( 1, 16 * 0.1, true, 12 ) == 8 );
    for ( double coordinate : { -16.0, -8.0, 0.0, 8.0, 16.0 } )
        CHECK( TileEditorGrid_IsMajorCoordinate( coordinate, 8 ) );
    for ( double coordinate : { -4.0, -1.0, 1.0, 4.0 } )
        CHECK_FALSE( TileEditorGrid_IsMajorCoordinate( coordinate, 8 ) );
}

TEST_CASE( "Floor material remains visible at overview zoom", "[TileEditor][Grid]" )
{
    EnsureApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 64;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Filled room" ), &error ) );
    REQUIRE( document.paintRect( { 0, 0, 64, 64 }, { 0, 1, 2 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileCanvas canvas;
    canvas.resize( 400, 300 );
    tile_editor_preferences_t preferences{};
    preferences.showOrthoMaterials = false; // Legacy solid palette mode remains supported.
    preferences.showGrid = preferences.showMarkers = false;
    preferences.wireframeOrtho = false;
    canvas.setPreferences( preferences );
    canvas.setDocumentBridge( &document );
    canvas.show();
    QApplication::processEvents();
    const auto material = CypherTileMapMaterial_Resolve( 2 );
    const QColor expected = QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
    for ( qreal zoom : { 2.0, 1.0, 0.5 } ) {
        canvas.setZoomFactor( zoom );
        const QImage frame = canvas.grab().toImage();
        REQUIRE_FALSE( frame.isNull() );
        // Compare rasterized RGB, not QColor's floating-point storage format.
        const QColor actual = frame.pixelColor( frame.width() / 2, frame.height() / 2 );
        INFO( "zoom=" << zoom << " actual=" << actual.name().toStdString()
              << " expected=" << expected.name().toStdString() );
        CHECK( actual.rgb() == expected.rgb() );
    }
}
