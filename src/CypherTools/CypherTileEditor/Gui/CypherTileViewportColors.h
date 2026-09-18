//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileViewportColors.h
//  Purpose: Derives readable viewport feedback colors from an editor theme.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_VIEWPORTCOLORS_H
#define CYPHER_TOOLS_TILEEDITOR_VIEWPORTCOLORS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QColor>

#include <algorithm>
#include <cmath>

namespace cypher::tools::tile_editor
{

struct tile_editor_viewport_colors_t {
    QColor underlay{};
    QColor mutedText{};
    QColor warning{};
    QColor error{};
    QColor success{};
    QColor info{};
};

inline double TileEditorViewportColor_LinearChannel( int channel )
{
    const double value = static_cast<double>( channel ) / 255.0;
    return value <= 0.04045
        ? value / 12.92
        : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
}

inline double TileEditorViewportColor_Luminance( const QColor &color )
{
    return 0.2126 * TileEditorViewportColor_LinearChannel( color.red() ) +
        0.7152 * TileEditorViewportColor_LinearChannel( color.green() ) +
        0.0722 * TileEditorViewportColor_LinearChannel( color.blue() );
}

inline double TileEditorViewportColor_Contrast(
    const QColor &left,
    const QColor &right )
{
    const double leftLuminance = TileEditorViewportColor_Luminance( left );
    const double rightLuminance = TileEditorViewportColor_Luminance( right );
    return ( std::max( leftLuminance, rightLuminance ) + 0.05 ) /
        ( std::min( leftLuminance, rightLuminance ) + 0.05 );
}

inline QColor TileEditorViewportColor_Blend(
    const QColor &left,
    const QColor &right,
    double amount )
{
    amount = std::clamp( amount, 0.0, 1.0 );
    return QColor::fromRgbF(
        left.redF() * ( 1.0 - amount ) + right.redF() * amount,
        left.greenF() * ( 1.0 - amount ) + right.greenF() * amount,
        left.blueF() * ( 1.0 - amount ) + right.blueF() * amount );
}

inline QColor TileEditorViewportColor_BestContrast( const QColor &background )
{
    const QColor black( Qt::black );
    const QColor white( Qt::white );
    return TileEditorViewportColor_Contrast( black, background ) >=
            TileEditorViewportColor_Contrast( white, background )
        ? black
        : white;
}

inline QColor TileEditorViewportColor_Readable(
    QColor candidate,
    const QColor &background,
    double minimumContrast = 3.0 )
{
    candidate.setAlpha( 255 );
    if ( TileEditorViewportColor_Contrast( candidate, background ) >= minimumContrast )
        return candidate;

    const QColor contrast = TileEditorViewportColor_BestContrast( background );
    for ( int step = 1; step <= 10; ++step ) {
        const QColor adjusted = TileEditorViewportColor_Blend(
            candidate, contrast, static_cast<double>( step ) / 10.0 );
        if ( TileEditorViewportColor_Contrast( adjusted, background ) >= minimumContrast )
            return adjusted;
    }
    return contrast;
}

inline tile_editor_viewport_colors_t TileEditorViewportColors_Derive(
    QColor background,
    QColor text,
    QColor accent )
{
    background.setAlpha( 255 );
    text = TileEditorViewportColor_Readable( text, background, 4.5 );
    const double saturation = std::clamp(
        accent.hslSaturationF() < 0.0 ? 0.70 : accent.hslSaturationF(),
        0.58,
        0.90 );
    const bool darkBackground = TileEditorViewportColor_Luminance( background ) < 0.35;
    const double statusLightness = darkBackground ? 0.68 : 0.36;
    const QColor error = QColor::fromHslF( 0.010, saturation, statusLightness );
    const QColor success = QColor::fromHslF( 0.365, saturation, statusLightness );
    const QColor info = QColor::fromHslF( 0.565, saturation, statusLightness );

    tile_editor_viewport_colors_t colors;
    colors.underlay = TileEditorViewportColor_BestContrast( background );
    colors.underlay.setAlpha( 185 );
    colors.mutedText = TileEditorViewportColor_Readable(
        TileEditorViewportColor_Blend( background, text, 0.72 ),
        background,
        3.0 );
    colors.warning = TileEditorViewportColor_Readable( accent, background );
    colors.error = TileEditorViewportColor_Readable( error, background );
    colors.success = TileEditorViewportColor_Readable( success, background );
    colors.info = TileEditorViewportColor_Readable( info, background );
    return colors;
}

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_VIEWPORTCOLORS_H
