//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies orthographic navigation, rendering, and view preferences.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileOrthoView.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSet>
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

TEST_CASE( "Stationary RMB opens Front and Side context options while drags keep panning",
           "[TileEditor][Ortho][ContextMenu]" )
{
    OrthoApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT,
                         tile_editor_ortho_plane_t::SIDE } ) {
        INFO( ( plane == tile_editor_ortho_plane_t::FRONT ? "Front" : "Side" ) );
        CypherTileOrthoView view( plane );
        int contextMenus = 0;
        QPoint requestedPosition;
        view.setContextMenuCallback( [&]( const QPoint &globalPosition ) {
            ++contextMenus;
            requestedPosition = globalPosition;
        } );

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

        const QPointF dragDelta(
            QApplication::startDragDistance() + 12,
            -( QApplication::startDragDistance() + 4 ) );
        SendMouse( view, QEvent::MouseButtonPress,
                   start, Qt::RightButton, Qt::RightButton );
        REQUIRE( view.isPanning() );
        SendMouse( view, QEvent::MouseMove,
                   start + dragDelta, Qt::NoButton, Qt::RightButton );
        CHECK( view.isPanning() );
        CHECK( view.viewOrigin() == initialOrigin + dragDelta );
        SendMouse( view, QEvent::MouseButtonRelease,
                   start + dragDelta, Qt::RightButton, Qt::NoButton );
        CHECK_FALSE( view.isPanning() );
        QApplication::processEvents();
        CHECK( contextMenus == 1 );
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
