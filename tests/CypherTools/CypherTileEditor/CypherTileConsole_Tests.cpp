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
#include "CypherTileConsoleColors.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <QVariant>

#include <functional>

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

bool ProcessConsoleUntil(
    const std::function<bool()> &condition,
    int timeoutMilliseconds )
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

class application_palette_guard_t final
{
public:
    explicit application_palette_guard_t( QApplication &application )
        : m_application( application ),
          m_palette( application.palette() ),
          m_accent( application.property( "TileEditorAccentColor" ) )
    {
    }

    ~application_palette_guard_t()
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

QColor ForegroundAt( QPlainTextEdit *pOutput, const QString &needle )
{
    const int offset = pOutput->toPlainText().indexOf( needle );
    if ( offset < 0 ) return {};
    QTextCursor cursor( pOutput->document() );
    cursor.setPosition( offset );
    cursor.setPosition( offset + needle.size(), QTextCursor::KeepAnchor );
    return cursor.charFormat().foreground().color();
}

} // namespace

TEST_CASE( "Tile console recolors its retained transcript for the active palette",
           "[CypherTools][CypherTileEditor][Console][Theme]" )
{
    QApplication &application = EnsureApplication();
    application_palette_guard_t restorePalette( application );

    QPalette dark = application.palette();
    dark.setColor( QPalette::Base, QColor( "#10151a" ) );
    dark.setColor( QPalette::Text, QColor( "#eef2f4" ) );
    dark.setColor( QPalette::Highlight, QColor( "#f19a28" ) );
    application.setProperty(
        "TileEditorAccentColor", QColor( "#f19a28" ) );
    application.setPalette( dark );

    cypher::tools::tile_editor::CypherTileConsole console;
    console.show();
    console.clear();
    console.appendError( QStringLiteral( "theme-retained-entry" ) );
    application.processEvents();

    auto *pOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    REQUIRE( pOutput != nullptr );
    const QString transcript = pOutput->toPlainText();
    const QColor darkError = ForegroundAt(
        pOutput, QStringLiteral( "error" ) );
    REQUIRE( darkError.isValid() );
    CHECK( cypher::tools::tile_editor::detail::ConsoleContrastRatio(
        darkError, pOutput->palette().color( QPalette::Base ) ) >= 4.5 );

    QPalette light = dark;
    light.setColor( QPalette::Base, QColor( "#f5f6f7" ) );
    light.setColor( QPalette::Text, QColor( "#202428" ) );
    light.setColor( QPalette::Highlight, QColor( "#9a4d00" ) );
    application.setProperty(
        "TileEditorAccentColor", QColor( "#9a4d00" ) );
    application.setPalette( light );
    application.processEvents();

    const QColor lightError = ForegroundAt(
        pOutput, QStringLiteral( "error" ) );
    REQUIRE( lightError.isValid() );
    CHECK( pOutput->toPlainText() == transcript );
    CHECK( lightError != darkError );
    CHECK( cypher::tools::tile_editor::detail::ConsoleContrastRatio(
        lightError, pOutput->palette().color( QPalette::Base ) ) >= 4.5 );
}

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

TEST_CASE( "Tile console initializes one transcript for editor and shell output",
           "[CypherTools][CypherTileEditor][Console][Shell][Unified]" )
{
    EnsureApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );

    cypher::tools::tile_editor::CypherTileConsole console;
    REQUIRE_FALSE( console.shellProgram().isEmpty() );
    console.setWorkingDirectory( directory.path() );

    auto *pOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    auto *pLegacyShellOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileShellOutput" ) );
    REQUIRE( pOutput != nullptr );
    REQUIRE( pLegacyShellOutput != nullptr );
    CHECK( pLegacyShellOutput->isHidden() );
    CHECK( pLegacyShellOutput->toPlainText().isEmpty() );
    CHECK( pOutput->toPlainText().contains(
        QStringLiteral( "Cypher Tile Editor console initialized" ) ) );
    CHECK( pOutput->toPlainText().contains(
        QDir::toNativeSeparators( directory.path() ) ) );

#ifdef Q_OS_WIN
    console.executeShellCommand( QStringLiteral(
        "echo unified-console-out & echo unified-console-error 1>&2" ) );
#else
    console.executeShellCommand( QStringLiteral(
        "printf 'unified-console-out\\n'; "
        "printf 'unified-console-error\\n' >&2" ) );
#endif
    REQUIRE( ProcessConsoleUntil(
        [&console] { return !console.isShellRunning(); }, 5000 ) );

    const QString transcript = pOutput->toPlainText();
    CHECK( transcript.contains( QStringLiteral( "unified-console-out" ) ) );
    CHECK( transcript.contains( QStringLiteral( "unified-console-error" ) ) );
    CHECK( transcript.contains( QStringLiteral( "stdout:" ) ) );
    CHECK( transcript.contains( QStringLiteral( "stderr:" ) ) );
    CHECK( transcript.contains( QStringLiteral( "exited with code 0" ) ) );
}

TEST_CASE( "Tile console routes bang-prefixed input to its local shell",
           "[CypherTools][CypherTileEditor][Console][Shell][Unified]" )
{
    EnsureApplication();
    cypher::tools::tile_editor::CypherTileConsole console;
    REQUIRE_FALSE( console.shellProgram().isEmpty() );

    int cEditorSubmissions = 0;
    console.setExecuteCallback(
        [&]( const QString & ) { ++cEditorSubmissions; } );
    auto *pInput = console.findChild<QLineEdit *>(
        QStringLiteral( "TileConsoleInput" ) );
    auto *pOutput = console.findChild<QPlainTextEdit *>(
        QStringLiteral( "TileConsoleOutput" ) );
    REQUIRE( pInput != nullptr );
    REQUIRE( pOutput != nullptr );

#ifdef Q_OS_WIN
    pInput->setText( QStringLiteral( "! echo bang-shell-route" ) );
#else
    pInput->setText( QStringLiteral(
        "! printf 'bang-shell-route\\n'" ) );
#endif
    SendKey( pInput, Qt::Key_Return );
    REQUIRE( ProcessConsoleUntil(
        [&console] { return !console.isShellRunning(); }, 5000 ) );

    CHECK( cEditorSubmissions == 0 );
    CHECK( pInput->text().isEmpty() );
    CHECK( pOutput->toPlainText().contains(
        QStringLiteral( "bang-shell-route" ) ) );
}
