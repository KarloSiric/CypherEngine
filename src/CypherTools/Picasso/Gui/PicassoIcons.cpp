//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoIcons.cpp
//  Purpose: Implements Picasso's platform-independent icon factory.
//  Details: SVG source remains resolution-independent in the executable while
//           pre-rendered QIcon states avoid relying on host theme colors.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoIcons.h"

#include <QColor>
#include <QFile>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace cypher::tools::picasso
{

namespace
{

QByteArray RecolorSvg(
    const QByteArray &source,
    const QColor &color )
{
    QByteArray colored = source;
    const QByteArray encodedColor = color.name( QColor::HexRgb ).toUtf8();
    if ( colored.contains( "currentColor" ) ) {
        colored.replace( "currentColor", encodedColor );
    } else {
        colored.replace(
            "<svg ",
            QByteArray( "<svg fill=\"" ) + encodedColor + "\" " );
    }
    colored.replace( "stroke-width=\"2\"", "stroke-width=\"2.55\"" );
    return colored;
}

QPixmap RenderMask(
    const QByteArray &source,
    int extent,
    qreal devicePixelRatio )
{
    const QByteArray maskSource = RecolorSvg( source, Qt::white );
    QSvgRenderer renderer( maskSource );
    const int physicalExtent = qRound( extent * devicePixelRatio );
    QPixmap pixmap( physicalExtent, physicalExtent );
    pixmap.setDevicePixelRatio( devicePixelRatio );
    pixmap.fill( Qt::transparent );
    if ( !renderer.isValid() ) {
        return pixmap;
    }

    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    const qreal inset = extent >= 26 ? 2.5 : 2.0;
    const QRectF glyphRect(
        inset,
        inset - 0.5,
        extent - inset * 2.0,
        extent - inset * 2.0 );
    renderer.render( &painter, glyphRect );
    return pixmap;
}

QPixmap TintMask(
    const QPixmap &mask,
    const QColor &topColor,
    const QColor &bottomColor )
{
    QPixmap tinted( mask.size() );
    tinted.setDevicePixelRatio( mask.devicePixelRatio() );
    tinted.fill( Qt::transparent );

    QPainter painter( &tinted );
    painter.drawPixmap( QPointF( 0.0, 0.0 ), mask );
    painter.setCompositionMode( QPainter::CompositionMode_SourceIn );

    QLinearGradient gradient(
        0.0,
        1.0,
        0.0,
        mask.deviceIndependentSize().height() );
    gradient.setColorAt( 0.0, topColor );
    gradient.setColorAt( 0.48, topColor.darker( 108 ) );
    gradient.setColorAt( 1.0, bottomColor );
    painter.fillRect(
        QRectF( QPointF( 0.0, 0.0 ), mask.deviceIndependentSize() ),
        gradient );
    return tinted;
}

QPixmap RenderIcon(
    const QByteArray &source,
    const QColor &color,
    int extent,
    qreal devicePixelRatio )
{
    const QPixmap mask = RenderMask( source, extent, devicePixelRatio );
    QPixmap pixmap( mask.size() );
    pixmap.setDevicePixelRatio( devicePixelRatio );
    pixmap.fill( Qt::transparent );
    if ( mask.isNull() ) {
        return pixmap;
    }

    const QPixmap shadow = TintMask(
        mask,
        QColor( 12, 13, 13, 230 ),
        QColor( 3, 4, 4, 245 ) );
    const QPixmap glyph = TintMask(
        mask,
        color.lighter( 138 ),
        color.darker( 128 ) );

    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    // Dense authoring icons use a dark lower edge and a brighter upper face.
    // The translated mask supplies that depth without losing SVG sharpness.
    painter.drawPixmap( QPointF( 1.0, 1.25 ), shadow );
    painter.drawPixmap( QPointF( 0.0, 0.0 ), glyph );
    return pixmap;
}

void AddIconState(
    QIcon *pIcon,
    const QByteArray &source,
    const QColor &color,
    QIcon::Mode mode,
    QIcon::State state )
{
    constexpr int extents[]{ 18, 22, 24, 26, 30 };
    for ( int extent : extents ) {
        for ( qreal devicePixelRatio : { 1.0, 2.0 } ) {
            pIcon->addPixmap(
                RenderIcon( source, color, extent, devicePixelRatio ),
                mode,
                state );
        }
    }
}

} // namespace

QIcon PicassoIcon_Create( QStringView name, picasso_icon_tone_t tone )
{
    const QString path = QStringLiteral( ":/picasso/icons/%1.svg" )
        .arg( name );
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }

    QColor normalColor;
    QColor activeColor;
    switch ( tone ) {
        case picasso_icon_tone_t::BLUE:
            normalColor = QColor( 68, 100, 116 );
            activeColor = QColor( 102, 151, 173 );
            break;
        case picasso_icon_tone_t::TEAL:
            normalColor = QColor( 64, 105, 101 );
            activeColor = QColor( 91, 151, 143 );
            break;
        case picasso_icon_tone_t::GOLD:
            normalColor = QColor( 117, 96, 54 );
            activeColor = QColor( 166, 132, 70 );
            break;
        case picasso_icon_tone_t::GREEN:
            normalColor = QColor( 72, 105, 67 );
            activeColor = QColor( 105, 151, 96 );
            break;
        case picasso_icon_tone_t::ORANGE:
            normalColor = QColor( 119, 82, 48 );
            activeColor = QColor( 174, 115, 59 );
            break;
        case picasso_icon_tone_t::RED:
            normalColor = QColor( 111, 67, 64 );
            activeColor = QColor( 160, 92, 85 );
            break;
        case picasso_icon_tone_t::STEEL:
        default:
            normalColor = QColor( 103, 113, 117 );
            activeColor = QColor( 163, 174, 178 );
            break;
    }

    const QByteArray source = file.readAll();
    QIcon icon;
    AddIconState( &icon, source, normalColor, QIcon::Normal, QIcon::Off );
    AddIconState( &icon, source, activeColor, QIcon::Active, QIcon::Off );
    AddIconState(
        &icon, source, normalColor.lighter( 116 ), QIcon::Normal, QIcon::On );
    AddIconState(
        &icon, source, activeColor, QIcon::Active, QIcon::On );
    AddIconState(
        &icon, source, QColor( 88, 91, 92 ), QIcon::Disabled, QIcon::Off );
    AddIconState(
        &icon, source, QColor( 111, 84, 55 ), QIcon::Disabled, QIcon::On );
    return icon;
}

} // namespace cypher::tools::picasso
