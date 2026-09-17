//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileShellRunner_Tests.cpp
//  Purpose: Verifies asynchronous local command execution in the editor shell.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileShellRunner.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

namespace
{

QApplication &EnsureShellApplication()
{
    if ( QApplication::instance() != nullptr )
        return *static_cast<QApplication *>( QApplication::instance() );

    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char applicationName[] = "CypherTileShellRunnerTests";
    static char *argv[]{ applicationName, nullptr };
    static QApplication application( argc, argv );
    return application;
}

bool ProcessUntil( const std::function<bool()> &condition, int timeoutMilliseconds )
{
    QElapsedTimer elapsed;
    elapsed.start();
    while ( !condition() && elapsed.elapsed() < timeoutMilliseconds ) {
        QApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    QApplication::processEvents( QEventLoop::AllEvents, 10 );
    return condition();
}

} // namespace

TEST_CASE( "Tile shell streams stdout and stderr from its selected directory",
           "[CypherTools][CypherTileEditor][Console][Shell]" )
{
    QApplication &application = EnsureShellApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );

    cypher::tools::tile_editor::CypherTileShellRunner runner;
    REQUIRE_FALSE( runner.shellProgram().isEmpty() );
    runner.setWorkingDirectory( directory.path() );

#ifdef Q_OS_WIN
    runner.executeCommand( QStringLiteral(
        "echo cypher-shell-out & echo cypher-shell-error 1>&2 & cd" ) );
#else
    runner.executeCommand( QStringLiteral(
        "printf 'cypher-shell-out\\n'; printf 'cypher-shell-error\\n' >&2; pwd" ) );
#endif

    REQUIRE( ProcessUntil( [&runner] { return !runner.isRunning(); }, 5000 ) );
    auto *output = runner.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileShellOutput" ) );
    REQUIRE( output != nullptr );
    const QString text = output->toPlainText();
    CHECK( text.contains( QStringLiteral( "cypher-shell-out" ) ) );
    CHECK( text.contains( QStringLiteral( "cypher-shell-error" ) ) );
    CHECK( QDir::fromNativeSeparators( text ).contains(
        QDir::fromNativeSeparators( directory.path() ) ) );
    CHECK( runner.property( "state" ).toString() == QStringLiteral( "success" ) );
    CHECK_FALSE( runner.property( "busy" ).toBool() );
    application.processEvents();
}

TEST_CASE( "Tile shell starts long commands without blocking the editor thread",
           "[CypherTools][CypherTileEditor][Console][Shell]" )
{
    EnsureShellApplication();
    cypher::tools::tile_editor::CypherTileShellRunner runner;
    REQUIRE_FALSE( runner.shellProgram().isEmpty() );

    QElapsedTimer callDuration;
    callDuration.start();
#ifdef Q_OS_WIN
    runner.executeCommand( QStringLiteral(
        "ping -n 2 127.0.0.1 >NUL & echo asynchronous-shell-finished" ) );
#else
    runner.executeCommand( QStringLiteral(
        "sleep 0.4; printf 'asynchronous-shell-finished\\n'" ) );
#endif
    CHECK( callDuration.elapsed() < 200 );
    CHECK( runner.isRunning() );
    REQUIRE( ProcessUntil( [&runner] { return !runner.isRunning(); }, 5000 ) );

    auto *output = runner.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileShellOutput" ) );
    REQUIRE( output != nullptr );
    CHECK( output->toPlainText().contains(
        QStringLiteral( "asynchronous-shell-finished" ) ) );
}
