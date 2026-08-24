//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/Picasso/PicassoConsole_Tests.cpp
//  Purpose: Tests Picasso console submission, completion, and history behavior.
//  Details: Tests use Qt's offscreen platform so the same interaction contract can
//           execute in Windows, Linux, and macOS CI without a visible desktop.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoConsole.h"
#include "PicassoCanvas.h"
#include "PicassoIcons.h"
#include "PicassoMainWindow.h"
#include "PicassoTheme.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QToolBar>

// PicassoGui is a static library, so GUI test hosts explicitly retain and
// register the compiled Qt resource object just as the application entry point
// does. Without this call a linker may discard the icon bundle.
void InitializePicassoTestResources()
{
    Q_INIT_RESOURCE( PicassoResources );
}

namespace
{

QApplication &EnsureApplication()
{
    static const bool bResourcesInitialized = [] {
        InitializePicassoTestResources();
        return true;
    }();
    (void)bResourcesInitialized;

    if ( QApplication::instance() != nullptr ) {
        return *static_cast<QApplication *>( QApplication::instance() );
    }

    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char applicationName[] = "PicassoConsoleTests";
    static char *argv[]{ applicationName, nullptr };
    static QApplication application( argc, argv );
    return application;
}

} // namespace

TEST_CASE( "Picasso console submits commands and recalls history",
           "[CypherTools][Picasso][Console]" )
{
    QApplication &application = EnsureApplication();
    (void)application;
    cypher::tools::picasso::PicassoConsole console;

    QString submitted;
    int cSubmissions = 0;
    console.setExecuteCallback( [&]( const QString &line ) {
        submitted = line;
        ++cSubmissions;
    } );

    auto *pInput = console.findChild<QLineEdit *>(
        QStringLiteral( "PicassoConsoleInput" ) );
    auto *pOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "PicassoConsoleOutput" ) );
    REQUIRE( pInput != nullptr );
    REQUIRE( pOutput != nullptr );

    pInput->setText( QStringLiteral( "  document.status  " ) );
    REQUIRE( QMetaObject::invokeMethod(
        pInput,
        "returnPressed",
        Qt::DirectConnection ) );
    CHECK( cSubmissions == 1 );
    CHECK( submitted == QStringLiteral( "document.status" ) );
    CHECK( pOutput->toPlainText().contains(
        QStringLiteral( "> document.status" ) ) );
    CHECK( pInput->text().isEmpty() );

    QKeyEvent historyUp(
        QEvent::KeyPress,
        Qt::Key_Up,
        Qt::NoModifier );
    QApplication::sendEvent( pInput, &historyUp );
    CHECK( pInput->text() == QStringLiteral( "document.status" ) );

    QKeyEvent historyDown(
        QEvent::KeyPress,
        Qt::Key_Down,
        Qt::NoModifier );
    QApplication::sendEvent( pInput, &historyDown );
    CHECK( pInput->text().isEmpty() );
}

TEST_CASE( "Picasso console applies host completion and clears records",
           "[CypherTools][Picasso][Console]" )
{
    QApplication &application = EnsureApplication();
    (void)application;
    cypher::tools::picasso::PicassoConsole console;
    console.setCompleteCallback( []( const QString &partial ) {
        return partial == QStringLiteral( "tool.set i" )
            ? QStringList{ QStringLiteral( "tool.set inspect" ) }
            : QStringList{};
    } );

    auto *pInput = console.findChild<QLineEdit *>(
        QStringLiteral( "PicassoConsoleInput" ) );
    auto *pOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "PicassoConsoleOutput" ) );
    REQUIRE( pInput != nullptr );
    REQUIRE( pOutput != nullptr );

    pInput->setText( QStringLiteral( "tool.set i" ) );
    QKeyEvent complete(
        QEvent::KeyPress,
        Qt::Key_Tab,
        Qt::NoModifier );
    QApplication::sendEvent( pInput, &complete );
    CHECK( pInput->text() == QStringLiteral( "tool.set inspect" ) );

    console.appendRecord(
        cypher::tools::picasso::picasso_console_record_t::WARNING,
        QStringLiteral( "warning: test" ) );
    CHECK( pOutput->toPlainText().endsWith(
        QStringLiteral( "Picasso: warning: test" ) ) );
    console.clearRecords();
    CHECK( pOutput->toPlainText().isEmpty() );
}

TEST_CASE( "Picasso workspace exposes its complete authoring layout",
           "[CypherTools][Picasso][Workspace]" )
{
    QApplication &application = EnsureApplication();
    cypher::tools::picasso::PicassoTheme_Apply( application );
    cypher::tools::picasso::PicassoMainWindow window;
    window.resize( 1600, 1000 );
    window.show();
    application.processEvents();

    QDockWidget *pLayersDock = window.findChild<QDockWidget *>(
        QStringLiteral( "PicassoLayersDock" ) );
    QDockWidget *pAuthoringDock = window.findChild<QDockWidget *>(
        QStringLiteral( "PicassoAuthoringDock" ) );
    QDockWidget *pPreviewDock = window.findChild<QDockWidget *>(
        QStringLiteral( "PicassoPreviewDock" ) );
    QDockWidget *pHistoryDock = window.findChild<QDockWidget *>(
        QStringLiteral( "PicassoHistoryDock" ) );
    QWidget *pBrushContext = window.findChild<QWidget *>(
        QStringLiteral( "PicassoBrushContext" ) );

    CHECK_FALSE( window.menuBar()->isNativeMenuBar() );
    CHECK( window.findChild<QToolBar *>(
        QStringLiteral( "PicassoToolRail" ) ) != nullptr );
    CHECK( window.findChild<QToolBar *>(
        QStringLiteral( "PicassoOperationToolbar" ) ) != nullptr );
    CHECK( window.findChild<QWidget *>(
        QStringLiteral( "PicassoDocumentHost" ) ) != nullptr );
    REQUIRE( pLayersDock != nullptr );
    REQUIRE( pAuthoringDock != nullptr );
    REQUIRE( pPreviewDock != nullptr );
    REQUIRE( pHistoryDock != nullptr );
    REQUIRE( pBrushContext != nullptr );
    CHECK( window.dockWidgetArea( pAuthoringDock ) == Qt::LeftDockWidgetArea );
    CHECK( window.dockWidgetArea( pLayersDock ) == Qt::RightDockWidgetArea );
    CHECK( window.dockWidgetArea( pPreviewDock ) == Qt::RightDockWidgetArea );
    CHECK( window.tabifiedDockWidgets( pLayersDock ).contains( pHistoryDock ) );
    CHECK( pBrushContext->isHidden() );

    QAction *pBrushAction = nullptr;
    QAction *pSelectAction = nullptr;
    for ( QAction *pAction : window.findChildren<QAction *>() ) {
        const QByteArray toolName = pAction->property( "PicassoToolName" )
            .toByteArray();
        if ( toolName == QByteArrayLiteral( "brush" ) ) {
            pBrushAction = pAction;
        } else if ( toolName == QByteArrayLiteral( "select" ) ) {
            pSelectAction = pAction;
        }
    }
    REQUIRE( pBrushAction != nullptr );
    REQUIRE( pSelectAction != nullptr );
    pBrushAction->trigger();
    application.processEvents();
    CHECK_FALSE( pBrushContext->isHidden() );
    pSelectAction->trigger();
    application.processEvents();
    CHECK( pBrushContext->isHidden() );
    CHECK( window.findChild<QDockWidget *>(
        QStringLiteral( "PicassoConsoleDock" ) ) != nullptr );
    CHECK( window.findChild<QStatusBar *>(
        QStringLiteral( "PicassoStatusBar" ) ) != nullptr );
    CHECK_FALSE( cypher::tools::picasso::PicassoIcon_Create(
        u"brush",
        cypher::tools::picasso::picasso_icon_tone_t::TEAL ).isNull() );

    // Developers can request a deterministic offscreen workspace image when
    // reviewing QSS, dock geometry, or icon changes on a headless test host.
    const QByteArray capturePath = qgetenv( "PICASSO_CAPTURE_WORKSPACE" );
    if ( !capturePath.isEmpty() ) {
        REQUIRE( window.grab().save( QString::fromUtf8( capturePath ) ) );
    }
}

TEST_CASE( "Picasso canvas uses Space as a temporary pan modifier",
           "[CypherTools][Picasso][Canvas][Input]" )
{
    QApplication &application = EnsureApplication();
    (void)application;
    cypher::tools::picasso::PicassoCanvas canvas;
    canvas.setTool( cypher::tools::picasso::picasso_canvas_tool_t::BRUSH );
    REQUIRE( canvas.cursor().shape() == Qt::CrossCursor );

    QKeyEvent pressSpace(
        QEvent::KeyPress,
        Qt::Key_Space,
        Qt::NoModifier );
    QApplication::sendEvent( &canvas, &pressSpace );
    CHECK( pressSpace.isAccepted() );
    CHECK( canvas.cursor().shape() == Qt::OpenHandCursor );
    CHECK( canvas.tool() ==
        cypher::tools::picasso::picasso_canvas_tool_t::BRUSH );

    QKeyEvent releaseSpace(
        QEvent::KeyRelease,
        Qt::Key_Space,
        Qt::NoModifier );
    QApplication::sendEvent( &canvas, &releaseSpace );
    CHECK( releaseSpace.isAccepted() );
    CHECK( canvas.cursor().shape() == Qt::CrossCursor );
    CHECK( canvas.tool() ==
        cypher::tools::picasso::picasso_canvas_tool_t::BRUSH );
}

TEST_CASE( "Picasso workspace commits canvas strokes into undo history",
           "[CypherTools][Picasso][Workspace][Paint]" )
{
    QApplication &application = EnsureApplication();
    cypher::tools::picasso::PicassoTheme_Apply( application );
    cypher::tools::picasso::PicassoMainWindow window;
    window.resize( 1400, 900 );
    window.show();
    application.processEvents();

    auto *pCanvas = static_cast<cypher::tools::picasso::PicassoCanvas *>(
        window.findChild<QWidget *>( QStringLiteral( "PicassoCanvas" ) ) );
    auto *pHistory = window.findChild<QListWidget *>(
        QStringLiteral( "PicassoHistoryList" ) );
    QAction *pBrushAction = nullptr;
    for ( QAction *pAction : window.findChildren<QAction *>() ) {
        if ( pAction->property( "PicassoToolName" ).toByteArray() ==
             QByteArrayLiteral( "brush" ) ) {
            pBrushAction = pAction;
            break;
        }
    }
    REQUIRE( pCanvas != nullptr );
    REQUIRE( pHistory != nullptr );
    REQUIRE( pBrushAction != nullptr );
    REQUIRE( pHistory->count() == 1 );

    pBrushAction->trigger();
    const QPointF start = pCanvas->rect().center();
    const QPointF finish = start + QPointF( 48.0, 12.0 );
    const QPointF startGlobal = pCanvas->mapToGlobal(
        start.toPoint() );
    const QPointF finishGlobal = pCanvas->mapToGlobal(
        finish.toPoint() );
    QMouseEvent press(
        QEvent::MouseButtonPress,
        start,
        start,
        startGlobal,
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier );
    QApplication::sendEvent( pCanvas, &press );
    QMouseEvent move(
        QEvent::MouseMove,
        finish,
        finish,
        finishGlobal,
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier );
    QApplication::sendEvent( pCanvas, &move );
    QMouseEvent release(
        QEvent::MouseButtonRelease,
        finish,
        finish,
        finishGlobal,
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier );
    QApplication::sendEvent( pCanvas, &release );
    application.processEvents();

    REQUIRE( pHistory->count() == 2 );
    CHECK( pHistory->item( 1 )->text().contains(
        QStringLiteral( "Brush Stroke" ) ) );
    CHECK( window.windowTitle().contains( QLatin1Char( '*' ) ) );
}
