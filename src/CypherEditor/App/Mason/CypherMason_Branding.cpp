// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
#include "CypherMason_Branding.h"
#include "CypherEditorGui_Application.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QSplashScreen>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>

namespace cypher::mason
{
using namespace editor::gui;

const char *Mason_Version() noexcept { return CYPHER_MASON_VERSION; }

// All sizes and the native app icon use the supplied mason_source.png.
// A dedicated small size keeps title bars crisp, while the larger sizes
// cover welcome, About and high DPI displays without changing the artwork.
QIcon Mason_ApplicationIcon()
{
    EditorGui_RegisterResources();
    QIcon icon;
    icon.addFile( QStringLiteral( ":/cypher/editor/branding/mason_64.png" ), QSize( 64, 64 ) );
    icon.addFile( QStringLiteral( ":/cypher/editor/branding/mason.png" ), QSize( 512, 512 ) );
    icon.addFile( QStringLiteral( ":/cypher/editor/branding/mason_1024.png" ), QSize( 1024, 1024 ) );
    return icon;
}

QPixmap Mason_StartupImage( const editor_style_t &style, qreal devicePixelRatio )
{
    EditorGui_RegisterResources();
    const qreal ratio = std::isfinite( devicePixelRatio ) ? std::clamp( devicePixelRatio, 1.0, 4.0 ) : 1.0;
    QPixmap image( qRound( 840 * ratio ), qRound( 460 * ratio ) );
    image.setDevicePixelRatio( ratio );
    const QRect logicalRect( 0, 0, 840, 460 );
    const QColor background = EditorStyle_TokenColor( style, "viewport.background.3d" );
    const QColor accent = EditorStyle_TokenColor( style, "ui.accent" );
    const QColor text = EditorStyle_TokenColor( style, "ui.text" );
    image.fill( background );
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing );
    QLinearGradient light( 0, 0, 840, 460 );
    QColor glow = accent; glow.setAlpha( 24 );
    light.setColorAt( 0, glow ); light.setColorAt( 1, Qt::transparent );
    painter.fillRect( logicalRect, light );

    // An isometric construction plane with architectural wire outlines.
    const auto project = []( double x, double y, double z ) {
        return QPointF( 584.0 + ( x - y ) * 23.0, 288.0 + ( x + y ) * 11.0 - z * 23.0 );
    };
    QColor grid = EditorStyle_TokenColor( style, "viewport.grid.major" ); grid.setAlpha( 72 );
    painter.setPen( QPen( grid, 1 ) );
    for ( int i = -7; i <= 7; ++i ) {
        painter.drawLine( project( i, -7, 0 ), project( i, 7, 0 ) );
        painter.drawLine( project( -7, i, 0 ), project( 7, i, 0 ) );
    }
    const auto box = [&]( double x, double y, double width, double depth, double height, QColor color ) {
        QPointF points[8];
        for ( int i = 0; i < 8; ++i ) {
            points[i] = project( x + ( ( i & 1 ) ? width : 0.0 ), y + ( ( i & 2 ) ? depth : 0.0 ), ( i & 4 ) ? height : 0.0 );
        }
        painter.setPen( QPen( color, 1.3 ) );
        for ( int i = 0; i < 8; ++i ) {
            for ( int bit : { 1, 2, 4 } ) { if ( ( i & bit ) == 0 ) { painter.drawLine( points[i], points[i | bit] ); } }
        }
    };
    QColor wire = text; wire.setAlpha( 115 );
    box( -4, -3, 8, 6, 0.35, wire );
    box( -4, -3, 0.4, 6, 3.0, wire );
    box( -4, 2.6, 8, 0.4, 3.0, wire );
    QColor selected = accent; selected.setAlpha( 210 );
    box( -1.5, -1.0, 3, 2, 1.6, selected );

    painter.setRenderHint( QPainter::SmoothPixmapTransform );
    painter.drawPixmap( QRectF( 40, 66, 122, 122 ), QPixmap( QStringLiteral( ":/cypher/editor/branding/mason.png" ) ), QRectF( 0, 0, 512, 512 ) );
    QFont title = style.uiFont; title.setPixelSize( 68 ); title.setBold( true );
    title.setLetterSpacing( QFont::AbsoluteSpacing, 5 );
    painter.setFont( title ); painter.setPen( text );
    painter.drawText( QRectF( 178, 67, 600, 94 ), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral( "MASON" ) );
    QFont subtitle = style.uiFont; subtitle.setPixelSize( 16 ); subtitle.setLetterSpacing( QFont::AbsoluteSpacing, 3 );
    painter.setFont( subtitle ); painter.setPen( accent );
    painter.drawText( QRectF( 182, 165, 560, 30 ), Qt::AlignLeft, QStringLiteral( "CYPHER WORLD EDITOR" ) );
    QFont detail = style.uiFont; detail.setPixelSize( 13 );
    painter.setFont( detail ); painter.setPen( EditorStyle_TokenColor( style, "ui.text.muted" ) );
    painter.drawText( QRectF( 42, 232, 330, 50 ), Qt::AlignLeft | Qt::AlignTop,
                      QStringLiteral( "Build worlds. Shape geometry.\nCreate maps for CypherEngine." ) );
    painter.drawText( QRectF( 42, 367, 360, 24 ), Qt::AlignLeft, QStringLiteral( "Version %1" ).arg( QString::fromLatin1( Mason_Version() ) ) );
    painter.setPen( QPen( accent, 2 ) ); painter.drawLine( 42, 407, 798, 407 );
    painter.setPen( QPen( EditorStyle_TokenColor( style, "ui.border" ), 1 ) );
    painter.drawRect( logicalRect.adjusted( 0, 0, -1, -1 ) );
    return image;
}

QSplashScreen *Mason_StartupScreen( const editor_style_t &style )
{
    QScreen *display = QGuiApplication::primaryScreen();
    auto *pScreen = new QSplashScreen( Mason_StartupImage( style, display != nullptr ? display->devicePixelRatio() : 1.0 ) );
    pScreen->setObjectName( QStringLiteral( "MasonStartupScreen" ) );
    pScreen->setWindowIcon( Mason_ApplicationIcon() );
    pScreen->setFont( style.uiFont );
    return pScreen;
}
}
