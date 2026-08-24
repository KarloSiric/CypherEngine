//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/main.cpp
//  Purpose: Provides the standalone Picasso application entry point.
//  Details: Process setup remains intentionally small; authoring behavior belongs
//           to PicassoCore and workspace composition belongs to PicassoMainWindow.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "Gui/PicassoMainWindow.h"
#include "Gui/PicassoTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>

using namespace cypher::tools::picasso;

int main( int argc, char **argv )
{
    Q_INIT_RESOURCE( PicassoResources );
    QApplication application( argc, argv );
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherEngine" ) );
    QCoreApplication::setOrganizationDomain( QStringLiteral( "cypherengine.local" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "Picasso" ) );
    QCoreApplication::setApplicationVersion( QStringLiteral( "1.0.0" ) );
    PicassoTheme_Apply( application );

    // A generated bootstrap icon keeps the first executable self-contained.
    // The final branded bitmap/icon bundle will replace it without affecting UI.
    QPixmap icon( 64, 64 );
    icon.fill( QColor( 24, 29, 34 ) );
    QPainter painter( &icon );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( Qt::NoPen );
    painter.setBrush( QColor( 224, 139, 48 ) );
    painter.drawRoundedRect( icon.rect().adjusted( 4, 4, -4, -4 ), 6, 6 );
    QFont font = painter.font();
    font.setBold( true );
    font.setPixelSize( 38 );
    painter.setFont( font );
    painter.setPen( QColor( 29, 24, 19 ) );
    painter.drawText( icon.rect(), Qt::AlignCenter, QStringLiteral( "P" ) );
    painter.end();
    application.setWindowIcon( QIcon( icon ) );

    PicassoMainWindow window;
    window.show();
    return application.exec();
}
