//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileValidation_Tests.cpp
//  Purpose: Exercises live map diagnostics through real editor gestures.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorIcons.h"
#include "CypherTileEditorMainWindow.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

namespace
{

using namespace cypher::tools::tile_editor;

QApplication &EnsureApplication()
{
    if ( QApplication::instance() != nullptr ) {
        return *static_cast<QApplication *>( QApplication::instance() );
    }
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileValidationTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    static QTemporaryDir settingsDirectory;
    REQUIRE( settingsDirectory.isValid() );
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "TileValidation" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope,
                        settingsDirectory.path() );
    return application;
}

void WaitForLiveChecks()
{
    QEventLoop loop;
    QTimer::singleShot( 100, &loop, &QEventLoop::quit );
    loop.exec();
}

void SendMouseAtSelectedCell( CypherTileCanvas *pCanvas, QEvent::Type type )
{
    const QPointF center( pCanvas->width() * 0.5, pCanvas->height() * 0.5 );
    QMouseEvent event( type, center, center,
        Qt::LeftButton,
        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
        Qt::NoModifier );
    QApplication::sendEvent( pCanvas, &event );
}

void TriggerAction( CypherTileEditorMainWindow &window, const char *pName )
{
    auto *pAction = window.findChild<QAction *>( QString::fromLatin1( pName ) );
    REQUIRE( pAction != nullptr );
    pAction->trigger();
}

QListWidgetItem *FindDiagnostic( QListWidget *pList, const char *pCode )
{
    for ( int i = 0; i < pList->count(); ++i ) {
        QListWidgetItem *pItem = pList->item( i );
        if ( pItem->data( Qt::UserRole + 4 ).toString() ==
             QString::fromLatin1( pCode ) ) return pItem;
    }
    return nullptr;
}

QString WriteMap( const QTemporaryDir &directory, bool bSpawn, bool bDoor )
{
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = 4;
    description.nHeight = 4;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Create fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, tile_map_paint_t{}, &error ) );
    if ( bSpawn ) REQUIRE( document.placePlayerSpawn( { 1, 1 }, 0.0f, &error ) );
    if ( bDoor ) REQUIRE( document.placeDoor(
        { 1, 1 }, tile_map_marker_side_t::EAST, nullptr, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath( QStringLiteral( "validation.cymap" ) );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

} // namespace

TEST_CASE( "Toolbar command and material actions use distinct stateful semantic icons",
           "[CypherTools][CypherTileEditor][Icons]" )
{
    EnsureApplication();
    CypherTileEditorMainWindow window;
    const auto action = [&]( const char *name ) {
        auto *found = window.findChild<QAction *>( QString::fromLatin1( name ) );
        REQUIRE( found != nullptr );
        REQUIRE_FALSE( found->icon().isNull() );
        return found;
    };
    const auto image = []( const QIcon &icon, QIcon::State state = QIcon::Off ) {
        return icon.pixmap( QSize( 24, 24 ), QIcon::Normal, state )
            .toImage().convertToFormat( QImage::Format_ARGB32 );
    };
    const auto matches = [&]( const char *name, tile_editor_icon_t semantic ) {
        return image( action( name )->icon() ) ==
               image( CypherTileEditorIcon_Create( semantic ) );
    };

    CHECK( matches( "map.validate", tile_editor_icon_t::VALIDATE ) );
    CHECK( matches( "map.build", tile_editor_icon_t::BUILD ) );
    CHECK( matches( "map.preview", tile_editor_icon_t::PLAY ) );
    CHECK( matches( "map.previewStop", tile_editor_icon_t::STOP ) );
    CHECK( matches( "view.orthoMaterials", tile_editor_icon_t::MATERIAL ) );
    CHECK( matches( "view.assets", tile_editor_icon_t::MATERIAL_LIBRARY ) );

    auto *preview = action( "view.orthoMaterials" );
    auto *library = action( "view.assets" );
    CHECK( preview->isCheckable() );
    CHECK( library->isCheckable() );
    CHECK( image( preview->icon() ) != image( library->icon() ) );
    CHECK( image( preview->icon(), QIcon::Off ) !=
           image( preview->icon(), QIcon::On ) );
    CHECK( image( library->icon(), QIcon::Off ) !=
           image( library->icon(), QIcon::On ) );
    CHECK( preview->toolTip().contains( QStringLiteral( "checked" ),
                                       Qt::CaseInsensitive ) );
    CHECK( library->toolTip().contains( QStringLiteral( "checked" ),
                                       Qt::CaseInsensitive ) );
}

TEST_CASE( "Live checks follow spawn edits and history without manual validation",
           "[CypherTools][CypherTileEditor][Validation]" )
{
    EnsureApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( WriteMap( directory, false, false ), false ) );
    // Leave the main window hidden: these tests exercise the production edit
    // callbacks and diagnostics without pretending offscreen Qt tests OpenGL.
    auto *pCanvas = static_cast<CypherTileCanvas *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileCanvas" ) ) );
    auto *pList = window.findChild<QListWidget *>( QStringLiteral( "TileValidationList" ) );
    auto *pSummary = window.findChild<QLabel *>( QStringLiteral( "TileValidationSummary" ) );
    auto *pTabs = window.findChild<QTabWidget *>( QStringLiteral( "TileInspectorTabs" ) );
    auto *pConsoleDock = window.findChild<QDockWidget *>(
        QStringLiteral( "TileEditorConsoleDock" ) );
    auto *pConsoleOutput = window.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    REQUIRE( pCanvas != nullptr );
    REQUIRE( pList != nullptr );
    REQUIRE( pSummary != nullptr );
    REQUIRE( pTabs != nullptr );
    REQUIRE( pConsoleDock != nullptr );
    REQUIRE( pConsoleOutput != nullptr );
    QListWidgetItem *pMissing = FindDiagnostic( pList, "MISSING_PLAYER_SPAWN" );
    REQUIRE( pMissing != nullptr );
    CHECK_FALSE( pMissing->data( Qt::UserRole ).toBool() );
    CHECK( pMissing->data( Qt::UserRole + 3 ).toInt() == 1 );
    CHECK( pSummary->text().contains( QStringLiteral( "1 warning" ) ) );
    CHECK( pTabs->tabText( pTabs->indexOf( pList->parentWidget() ) ).contains( QLatin1Char( '1' ) ) );

    pCanvas->setTool( tile_canvas_tool_t::PLAYER_SPAWN );
    pCanvas->selectCell( { 1, 1 }, true );
    SendMouseAtSelectedCell( pCanvas, QEvent::MouseButtonPress );
    SendMouseAtSelectedCell( pCanvas, QEvent::MouseButtonRelease );
    CHECK( FindDiagnostic( pList, "MISSING_PLAYER_SPAWN" ) == nullptr );
    CHECK( pSummary->text() == QStringLiteral( "Live checks: no issues" ) );
    TriggerAction( window, "edit.undo" );
    CHECK( FindDiagnostic( pList, "MISSING_PLAYER_SPAWN" ) != nullptr );
    TriggerAction( window, "edit.redo" );
    CHECK( FindDiagnostic( pList, "MISSING_PLAYER_SPAWN" ) == nullptr );
    pTabs->setCurrentIndex( 0 );
    const int iInspectorPage = pTabs->currentIndex();
    pConsoleDock->hide();
    pConsoleOutput->clear();
    TriggerAction( window, "map.validate" );
    CHECK_FALSE( pConsoleDock->isHidden() );
    CHECK( pTabs->currentIndex() == iInspectorPage );
    CHECK( pConsoleOutput->toPlainText().contains(
        QStringLiteral( "Validate Map: checking" ) ) );
    CHECK( pConsoleOutput->toPlainText().contains(
        QStringLiteral( "Validate Map completed" ) ) );
    CHECK( pList->item( 0 )->text().contains( QStringLiteral( "structure, spawn, and door" ) ) );
}

TEST_CASE( "Geometry build reports progress in the unified console without navigating Properties",
           "[CypherTools][CypherTileEditor][Validation][Console][Build]" )
{
    EnsureApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( WriteMap( directory, true, false ), false ) );

    auto *pTabs = window.findChild<QTabWidget *>(
        QStringLiteral( "TileInspectorTabs" ) );
    auto *pConsoleDock = window.findChild<QDockWidget *>(
        QStringLiteral( "TileEditorConsoleDock" ) );
    auto *pConsoleOutput = window.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    REQUIRE( pTabs != nullptr );
    REQUIRE( pConsoleDock != nullptr );
    REQUIRE( pConsoleOutput != nullptr );
    CHECK( window.findChild<QTabWidget *>(
        QStringLiteral( "TileConsoleTabs" ) ) == nullptr );

    pTabs->setCurrentIndex( 1 );
    const int iInspectorPage = pTabs->currentIndex();
    pConsoleDock->hide();
    pConsoleOutput->clear();
    TriggerAction( window, "map.build" );

    const QString transcript = pConsoleOutput->toPlainText();
    CHECK_FALSE( pConsoleDock->isHidden() );
    CHECK( pTabs->currentIndex() == iInspectorPage );
    CHECK( transcript.contains( QStringLiteral( "Build Geometry: started" ) ) );
    CHECK( transcript.contains( QStringLiteral( "Build Geometry [1/2]" ) ) );
    CHECK( transcript.contains( QStringLiteral( "Build Geometry [2/2]" ) ) );
    CHECK( transcript.contains( QStringLiteral( "Geometry build succeeded" ) ) );
    CHECK( transcript.contains(
        QStringLiteral( "Build Geometry: completed successfully" ) ) );
}

TEST_CASE( "Unified console help documents local shell controls",
           "[CypherTools][CypherTileEditor][Console][Shell][Help]" )
{
    EnsureApplication();
    CypherTileEditorMainWindow window;
    auto *pConsoleInput = window.findChild<QLineEdit *>(
        QStringLiteral( "TileConsoleInput" ) );
    auto *pConsoleOutput = window.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    REQUIRE( pConsoleInput != nullptr );
    REQUIRE( pConsoleOutput != nullptr );

    pConsoleOutput->clear();
    pConsoleInput->setText( QStringLiteral( "help" ) );
    REQUIRE( QMetaObject::invokeMethod(
        pConsoleInput, "returnPressed", Qt::DirectConnection ) );
    const QString commandList = pConsoleOutput->toPlainText();
    CHECK( commandList.contains( QStringLiteral( "shell_stop" ) ) );
    CHECK( commandList.contains( QStringLiteral( "shell_restart" ) ) );

    pConsoleOutput->clear();
    pConsoleInput->setText( QStringLiteral( "help shell_stop" ) );
    REQUIRE( QMetaObject::invokeMethod(
        pConsoleInput, "returnPressed", Qt::DirectConnection ) );
    CHECK( pConsoleOutput->toPlainText().contains(
        QStringLiteral( "usage: shell_stop" ) ) );
}

TEST_CASE( "Live checks preview and cancel drag errors and locate committed issues",
           "[CypherTools][CypherTileEditor][Validation]" )
{
    EnsureApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( WriteMap( directory, true, true ), false ) );
    auto *pCanvas = static_cast<CypherTileCanvas *>( window.findChild<QWidget *>(
        QStringLiteral( "CypherTileCanvas" ) ) );
    auto *pList = window.findChild<QListWidget *>( QStringLiteral( "TileValidationList" ) );
    auto *pSummary = window.findChild<QLabel *>( QStringLiteral( "TileValidationSummary" ) );
    REQUIRE( pCanvas != nullptr );
    REQUIRE( pList != nullptr );
    REQUIRE( pSummary != nullptr );
    CHECK( FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" ) == nullptr );

    // Painting beside the east door makes it an invalid interior edge. The
    // result must arrive before mouse release, then disappear on cancellation.
    pCanvas->setTool( tile_canvas_tool_t::PAINT );
    pCanvas->selectCell( { 2, 1 }, true );
    SendMouseAtSelectedCell( pCanvas, QEvent::MouseButtonPress );
    WaitForLiveChecks();
    REQUIRE( FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" ) != nullptr );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QApplication::sendEvent( pCanvas, &escape );
    WaitForLiveChecks();
    CHECK( FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" ) == nullptr );

    SendMouseAtSelectedCell( pCanvas, QEvent::MouseButtonPress );
    SendMouseAtSelectedCell( pCanvas, QEvent::MouseButtonRelease );
    QListWidgetItem *pIssue = FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" );
    REQUIRE( pIssue != nullptr );
    CHECK( pIssue->data( Qt::UserRole ).toBool() );
    CHECK( pIssue->data( Qt::UserRole + 1 ).toInt() == 1 );
    CHECK( pIssue->data( Qt::UserRole + 2 ).toInt() == 1 );
    CHECK( pIssue->data( Qt::UserRole + 3 ).toInt() == 2 );
    CHECK( pSummary->text().contains( QStringLiteral( "1 error" ) ) );
    REQUIRE( QMetaObject::invokeMethod( pList, "itemActivated", Qt::DirectConnection,
        Q_ARG( QListWidgetItem *, pIssue ) ) );
    CHECK( pCanvas->selectedCell().x == 1 );
    CHECK( pCanvas->selectedCell().y == 1 );
    TriggerAction( window, "edit.undo" );
    CHECK( FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" ) == nullptr );
    TriggerAction( window, "edit.redo" );
    CHECK( FindDiagnostic( pList, "DOOR_NOT_ON_BOUNDARY" ) != nullptr );
}
