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
#include "CypherTileConsoleColors.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <QVariant>

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

class shell_palette_guard_t final
{
public:
    explicit shell_palette_guard_t( QApplication &application )
        : m_application( application ),
          m_palette( application.palette() ),
          m_accent( application.property( "TileEditorAccentColor" ) )
    {
    }

    ~shell_palette_guard_t()
    {
        m_application.setPalette( m_palette );
        m_application.setProperty( "TileEditorAccentColor", m_accent );
        m_application.processEvents();
    }

private:
    QApplication &m_application;
    QPalette m_palette;
    QVariant m_accent;
};

QColor ShellForegroundAt( QPlainTextEdit *pOutput, const QString &needle )
{
    const int offset = pOutput->toPlainText().indexOf( needle );
    if ( offset < 0 ) return {};
    QTextCursor cursor( pOutput->document() );
    cursor.setPosition( offset );
    cursor.setPosition( offset + needle.size(), QTextCursor::KeepAnchor );
    return cursor.charFormat().foreground().color();
}

} // namespace

TEST_CASE( "Standalone tile shell recolors retained output for the active palette",
           "[CypherTools][CypherTileEditor][Console][Shell][Theme]" )
{
    QApplication &application = EnsureShellApplication();
    shell_palette_guard_t restorePalette( application );

    QPalette dark = application.palette();
    dark.setColor( QPalette::Base, QColor( "#10151a" ) );
    dark.setColor( QPalette::Text, QColor( "#eef2f4" ) );
    dark.setColor( QPalette::Highlight, QColor( "#f19a28" ) );
    application.setProperty(
        "TileEditorAccentColor", QColor( "#f19a28" ) );
    application.setPalette( dark );

    cypher::tools::tile_editor::CypherTileShellRunner runner;
    REQUIRE_FALSE( runner.shellProgram().isEmpty() );
#ifdef Q_OS_WIN
    runner.executeCommand( QStringLiteral( "echo shell-theme-entry" ) );
#else
    runner.executeCommand( QStringLiteral(
        "printf 'shell-theme-entry\\n'" ) );
#endif
    REQUIRE( ProcessUntil( [&runner] { return !runner.isRunning(); }, 5000 ) );

    auto *pOutput = runner.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileShellOutput" ) );
    REQUIRE( pOutput != nullptr );
    const QString transcript = pOutput->toPlainText();
    const QColor darkCommand = ShellForegroundAt(
        pOutput, QStringLiteral( "shell-theme-entry" ) );
    REQUIRE( darkCommand.isValid() );

    QPalette light = dark;
    light.setColor( QPalette::Base, QColor( "#f5f6f7" ) );
    light.setColor( QPalette::Text, QColor( "#202428" ) );
    light.setColor( QPalette::Highlight, QColor( "#9a4d00" ) );
    application.setProperty(
        "TileEditorAccentColor", QColor( "#9a4d00" ) );
    application.setPalette( light );
    application.processEvents();

    const QColor lightCommand = ShellForegroundAt(
        pOutput, QStringLiteral( "shell-theme-entry" ) );
    REQUIRE( lightCommand.isValid() );
    CHECK( pOutput->toPlainText() == transcript );
    CHECK( lightCommand != darkCommand );
    CHECK( cypher::tools::tile_editor::detail::ConsoleContrastRatio(
        lightCommand, pOutput->palette().color( QPalette::Base ) ) >= 4.5 );
}

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
