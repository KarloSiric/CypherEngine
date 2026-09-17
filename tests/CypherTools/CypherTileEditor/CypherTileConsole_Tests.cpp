//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileConsole_Tests.cpp
//  Purpose: Tests tile-editor console completion interaction.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileConsole.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCompleter>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>

namespace
{

QApplication &EnsureApplication()
{
    if ( QApplication::instance() != nullptr ) {
        return *static_cast<QApplication *>( QApplication::instance() );
    }

    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char applicationName[] = "CypherTileConsoleTests";
    static char *argv[]{ applicationName, nullptr };
    static QApplication application( argc, argv );
    return application;
}

void SendKey(
    QLineEdit *pInput,
    Qt::Key key,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent event( QEvent::KeyPress, key, modifiers );
    QApplication::sendEvent( pInput, &event );
}

void EmitTextEdited( QLineEdit *pInput )
{
    REQUIRE( QMetaObject::invokeMethod(
        pInput,
        "textEdited",
        Qt::DirectConnection,
        Q_ARG( QString, pInput->text() ) ) );
}

} // namespace

TEST_CASE( "Tile console cycles full-line completions in both directions",
           "[CypherTools][CypherTileEditor][Console]" )
{
    QApplication &application = EnsureApplication();
    cypher::tools::tile_editor::CypherTileConsole console;
    console.setCompletions( {
        QStringLiteral( "door" ),
        QStringLiteral( "door east" ),
        QStringLiteral( "door north" ),
        QStringLiteral( "material 0" )
    } );

    auto *pInput = console.findChild<QLineEdit *>(
        QStringLiteral( "TileConsoleInput" ) );
    REQUIRE( pInput != nullptr );

    pInput->setText( QStringLiteral( "door" ) );
    EmitTextEdited( pInput );
    SendKey( pInput, Qt::Key_Tab );
    CHECK( pInput->text() == QStringLiteral( "door east" ) );
    SendKey( pInput, Qt::Key_Tab );
    CHECK( pInput->text() == QStringLiteral( "door north" ) );
    SendKey( pInput, Qt::Key_Backtab, Qt::ShiftModifier );
    CHECK( pInput->text() == QStringLiteral( "door east" ) );

    // A real edit starts a new completion session instead of continuing the
    // old `door` candidate index.
    pInput->setText( QStringLiteral( "material" ) );
    EmitTextEdited( pInput );
    SendKey( pInput, Qt::Key_Tab );
    CHECK( pInput->text() == QStringLiteral( "material 0" ) );
    application.processEvents();
}

TEST_CASE( "Tile console exposes arguments and Enter accepts the popup row",
           "[CypherTools][CypherTileEditor][Console]" )
{
    QApplication &application = EnsureApplication();
    cypher::tools::tile_editor::CypherTileConsole console;
    int cSubmissions = 0;
    console.setExecuteCallback( [&]( const QString & ) { ++cSubmissions; } );
    console.setCompletions( {
        QStringLiteral( "door" ),
        QStringLiteral( "door east" ),
        QStringLiteral( "door north" )
    } );
    console.show();

    auto *pInput = console.findChild<QLineEdit *>(
        QStringLiteral( "TileConsoleInput" ) );
    auto *pHint = console.findChild<QLabel *>(
        QStringLiteral( "TileConsoleCompletionHint" ) );
    auto *pCompleter = console.findChild<QCompleter *>();
    REQUIRE( pInput != nullptr );
    REQUIRE( pHint != nullptr );
    REQUIRE( pCompleter != nullptr );

    pInput->setFocus();
    pInput->setText( QStringLiteral( "door" ) );
    EmitTextEdited( pInput );
    application.processEvents();

    CHECK( pCompleter->completionCount() == 3 );
    CHECK_FALSE( pHint->isHidden() );
    CHECK( pHint->text().contains( QStringLiteral( "door east" ) ) );

    SendKey( pInput, Qt::Key_Return );
    CHECK( pInput->text() == QStringLiteral( "door east" ) );
    CHECK( cSubmissions == 0 );
}
