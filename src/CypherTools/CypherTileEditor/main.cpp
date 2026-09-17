//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditor/main.cpp
//  Purpose: Starts the standalone Qt tile-map editor.
//
//////////////////////////////////////////////////////////////////////////

#include "Gui/CypherTileEditorMainWindow.h"
#include "Gui/CypherTileEditorTheme.h"
#include "Gui/CypherTileViewWorkspace.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>

namespace
{

QIcon CreateApplicationIcon()
{
    QPixmap pixmap( 64, 64 );
    pixmap.fill( QColor( 19, 25, 31 ) );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing, false );
    painter.setPen( QPen( QColor( 70, 91, 104 ), 2 ) );
    for ( int i = 8; i <= 56; i += 12 ) {
        painter.drawLine( i, 8, i, 56 );
        painter.drawLine( 8, i, 56, i );
    }
    painter.fillRect( QRect( 21, 21, 23, 23 ), QColor( 111, 139, 153 ) );
    painter.setPen( QPen( QColor( 79, 184, 229 ), 3 ) );
    painter.drawRect( QRect( 20, 20, 25, 25 ) );
    return QIcon( pixmap );
}

} // namespace

int main( int argc, char **argv )
{
    // QOpenGLWidget renders into a private framebuffer that Qt composites with
    // the rest of the window. On macOS the widget and compositor contexts can
    // only share resources when the application-wide format is selected before
    // QApplication creates either context.
    QSurfaceFormat renderFormat;
    renderFormat.setRenderableType( QSurfaceFormat::OpenGL );
    renderFormat.setVersion( 4, 1 );
    renderFormat.setProfile( QSurfaceFormat::CoreProfile );
    renderFormat.setDepthBufferSize( 24 );
    renderFormat.setStencilBufferSize( 8 );
    renderFormat.setSwapBehavior( QSurfaceFormat::DoubleBuffer );
    renderFormat.setSwapInterval( 1 );
    QSurfaceFormat::setDefaultFormat( renderFormat );

    QApplication application( argc, argv );
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherEngine" ) );
    QCoreApplication::setOrganizationDomain( QStringLiteral( "cypherengine.dev" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "CypherTileEditor" ) );
    QCoreApplication::setApplicationVersion( QStringLiteral( "0.1.0" ) );
    application.setWindowIcon( CreateApplicationIcon() );

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr( "CypherEngine grid blockout and tile-map editor." ) );
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTest(
        QStringLiteral( "smoke-test" ),
        QObject::tr( "Show the workspace briefly and exit successfully." ) );
    parser.addOption( smokeTest );
    parser.addPositionalArgument(
        QStringLiteral( "map" ),
        QObject::tr( "Optional .cymap document to open." ),
        QStringLiteral( "[map.cymap]" ) );
    parser.process( application );

    cypher::tools::tile_editor::CypherTileEditorTheme_Apply( application );
    cypher::tools::tile_editor::CypherTileEditorMainWindow window;
    const QStringList positional = parser.positionalArguments();
    // The constructor owns an untouched placeholder document. A positional
    // startup file replaces it directly; prompting to save that placeholder
    // would make command-line launches appear blocked behind a false warning.
    if ( !positional.isEmpty() ) {
        window.openFilePath( positional.front(), false );
    }
    if ( parser.isSet( smokeTest ) &&
         ( application.platformName() == QStringLiteral( "offscreen" ) ||
           application.platformName() == QStringLiteral( "minimal" ) ) ) {
        // These Qt plugins cannot create a QOpenGLWidget context. The headless
        // smoke checks the shell/top canvas; native verification covers 3D.
        auto *pWorkspace = static_cast<cypher::tools::tile_editor::CypherTileViewWorkspace *>(
            window.centralWidget() );
        pWorkspace->focusView( cypher::tools::tile_editor::tile_editor_view_t::TOP );
        pWorkspace->toggleMaximize();
    }
    window.show();

    if ( parser.isSet( smokeTest ) ) {
        // Running a local loop avoids QApplication's close-all-windows path,
        // which correctly prompts about the freshly created unsaved document.
        // Destroying the stack-owned window after this loop still exercises
        // construction, layout, painting, and orderly widget destruction.
        QEventLoop smokeLoop;
        QTimer::singleShot( 150, &smokeLoop, &QEventLoop::quit );
        return smokeLoop.exec();
    }
    return application.exec();
}
