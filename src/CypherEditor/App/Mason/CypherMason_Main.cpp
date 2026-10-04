//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_Main.cpp
//  Purpose: Entry point of the Mason editor.
//  Details: `Mason [map.cymap]` opens the map given on the command line.
//           Everything else lives in the main window library so tests can
//           build the same window headlessly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMason_MainWindow.h"
#include "CypherMason_Branding.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <QApplication>
#include <QMainWindow>
#include <QScreen>
#include <QStringList>
#include <QSplashScreen>
#include <QEventLoop>
#include <memory>

namespace
{
void StartupProgress( void *pContext, const cypher::editor::gui::editor_gui_t &gui, const char *pStage )
{
    if ( !cypher::editor::EditorSettings_Bool( &gui.settings, "editor.ui.show_splash", cypher::common::CY_TRUE ) ) { return; }
    auto &screen = *static_cast<std::unique_ptr<QSplashScreen> *>( pContext );
    if ( !screen ) { screen.reset( cypher::mason::Mason_StartupScreen( gui.style ) ); screen->show(); }
    screen->showMessage( QString::fromUtf8( pStage ), Qt::AlignBottom | Qt::AlignLeft,
                         cypher::editor::gui::EditorStyle_TokenColor( gui.style, "ui.text" ) );
    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
}
}

int main( int argc, char **argv )
{
    QApplication application( argc, argv );
    QApplication::setApplicationName( QStringLiteral( "Mason" ) );
    QApplication::setOrganizationName( QStringLiteral( "Cypher" ) );
    QApplication::setApplicationVersion( QString::fromLatin1( cypher::mason::Mason_Version() ) );
    QApplication::setWindowIcon( cypher::mason::Mason_ApplicationIcon() );

    std::unique_ptr<QSplashScreen> startup;
    cypher::mason::mason_t *pMason =
        cypher::mason::Mason_Create( &application, cypher::common::Allocator_GetSystem(), cypher::mason::MASON_FLAG_NONE,
                                    &StartupProgress, &startup );
    if ( pMason == nullptr ) { return 1; }
    QMainWindow *pWindow = cypher::mason::Mason_Window( pMason );
    // Start with usable editing space while remaining inside the current
    // screen's work area, including laptop displays and scaled desktops.
    const QRect available = pWindow->screen()->availableGeometry();
    const QSize initial( qMin( 1800, available.width() * 94 / 100 ),
                         qMin( 1120, available.height() * 94 / 100 ) );
    pWindow->resize( initial );
    pWindow->move( available.center() - QPoint( initial.width() / 2, initial.height() / 2 ) );
    if ( cypher::editor::EditorSettings_Bool( &cypher::mason::Mason_Gui( pMason )->settings, "editor.ui.start_maximized", cypher::common::CY_TRUE ) ) {
        pWindow->showMaximized();
    } else { pWindow->show(); }

    const QStringList arguments = QApplication::arguments();
    bool bOpenedMap = false;
    for ( qsizetype i = 1; i < arguments.size(); ++i ) {
        if ( arguments[i].endsWith( QStringLiteral( ".cymap" ) ) ) {
            StartupProgress( &startup, *cypher::mason::Mason_Gui( pMason ), "Opening map..." );
            bOpenedMap = cypher::mason::Mason_OpenMap( pMason, arguments[i] );
            break;
        }
    }
    if ( startup ) { startup->finish( pWindow ); startup.reset(); }
    // TrenchBroom's welcome window when no map was named: new, open, recent.
    if ( !bOpenedMap && cypher::editor::EditorSettings_Bool( &cypher::mason::Mason_Gui( pMason )->settings, "editor.ui.show_welcome", cypher::common::CY_TRUE ) ) {
        ( void )cypher::mason::Mason_ShowWelcome( pMason );
    }

    const int exitCode = QApplication::exec();
    cypher::mason::Mason_Destroy( pMason );
    return exitCode;
}
