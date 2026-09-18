//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileConsoleColors.h
//  Purpose: Resolves readable semantic console colors from the active theme.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_CONSOLECOLORS_H
#define CYPHER_TOOLS_TILEEDITOR_CONSOLECOLORS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace cypher::tools::tile_editor::detail
{

enum class console_color_role_t {
    TEXT,
    MUTED,
    ACCENT,
    INFO,
    WARNING,
    ERROR
};

struct console_colors_t {
    QColor text{};
    QColor muted{};
    QColor accent{};
    QColor info{};
    QColor warning{};
    QColor error{};
};

inline double ConsoleLinearChannel( int channel )
{
    const double value = static_cast<double>( channel ) / 255.0;
    return value <= 0.04045 ? value / 12.92
                            : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
}

inline double ConsoleRelativeLuminance( const QColor &color )
{
    return 0.2126 * ConsoleLinearChannel( color.red() ) +
        0.7152 * ConsoleLinearChannel( color.green() ) +
        0.0722 * ConsoleLinearChannel( color.blue() );
}

inline double ConsoleContrastRatio( const QColor &a, const QColor &b )
{
    const double aLuminance = ConsoleRelativeLuminance( a );
    const double bLuminance = ConsoleRelativeLuminance( b );
    return ( std::max( aLuminance, bLuminance ) + 0.05 ) /
        ( std::min( aLuminance, bLuminance ) + 0.05 );
}

inline QColor ConsoleBlend( const QColor &a, const QColor &b, double amount )
{
    amount = std::clamp( amount, 0.0, 1.0 );
    return QColor::fromRgbF(
        a.redF() * ( 1.0 - amount ) + b.redF() * amount,
        a.greenF() * ( 1.0 - amount ) + b.greenF() * amount,
        a.blueF() * ( 1.0 - amount ) + b.blueF() * amount );
}

inline QColor ConsoleReadableColor(
    QColor candidate,
    const QColor &background,
    QColor themeText,
    double minimumContrast = 7.0 )
{
    candidate.setAlpha( 255 );
    themeText.setAlpha( 255 );
    if ( ConsoleContrastRatio( candidate, background ) >= minimumContrast )
        return candidate;

    if ( ConsoleContrastRatio( themeText, background ) < minimumContrast ) {
        const QColor black( Qt::black );
        const QColor white( Qt::white );
        themeText = ConsoleContrastRatio( black, background ) >=
                ConsoleContrastRatio( white, background )
            ? black
            : white;
    }

    for ( int iStep = 1; iStep <= 10; ++iStep ) {
        const QColor adjusted = ConsoleBlend(
            candidate, themeText, static_cast<double>( iStep ) / 10.0 );
        if ( ConsoleContrastRatio( adjusted, background ) >= minimumContrast )
            return adjusted;
    }
    return themeText;
}

inline QColor ConsoleApplicationColor( const char *propertyName )
{
    if ( qApp == nullptr ) return {};
    const QVariant value = qApp->property( propertyName );
    if ( !value.canConvert<QColor>() ) return {};
    const QColor color = value.value<QColor>();
    return color.isValid() ? color : QColor{};
}

inline console_colors_t ConsoleThemeColors()
{
    // The editor publishes every custom theme through QApplication. Using
    // that palette also avoids reading a child's previous palette while Qt is
    // still propagating ApplicationPaletteChange through the widget tree.
    const QPalette palette = QApplication::palette();
    QColor background = ConsoleApplicationColor(
        "TileEditorConsoleBackgroundColor" );
    if ( !background.isValid() ) background = palette.color( QPalette::Base );
    QColor themeText = ConsoleApplicationColor( "TileEditorTextColor" );
    if ( !themeText.isValid() ) themeText = palette.color( QPalette::Text );
    themeText = ConsoleReadableColor( themeText, background, themeText );

    QColor accent = ConsoleApplicationColor( "TileEditorAccentColor" );
    if ( !accent.isValid() ) accent = palette.color( QPalette::Highlight );
    accent = ConsoleReadableColor( accent, background, themeText );

    QColor muted = ConsoleApplicationColor( "TileEditorMutedColor" );
    if ( !muted.isValid() )
        muted = ConsoleBlend( background, themeText, 0.70 );
    muted = ConsoleReadableColor( muted, background, themeText );

    QColor info = ConsoleApplicationColor( "TileEditorInfoColor" );
    if ( !info.isValid() ) info = accent;
    info = ConsoleReadableColor( info, background, themeText );

    const double saturation = std::clamp(
        accent.hslSaturationF() < 0.0 ? 0.70 : accent.hslSaturationF(),
        0.58,
        0.90 );
    const bool bDarkBackground = ConsoleRelativeLuminance( background ) < 0.35;

    QColor warning = ConsoleApplicationColor( "TileEditorWarningColor" );
    if ( !warning.isValid() ) {
        warning = QColor::fromHslF(
            0.105, saturation, bDarkBackground ? 0.68 : 0.34 );
    }
    warning = ConsoleReadableColor( warning, background, themeText );

    QColor error = ConsoleApplicationColor( "TileEditorErrorColor" );
    if ( !error.isValid() ) {
        error = QColor::fromHslF(
            0.010, saturation, bDarkBackground ? 0.68 : 0.38 );
    }
    error = ConsoleReadableColor( error, background, themeText );

    return { themeText, muted, accent, info, warning, error };
}

inline QColor ConsoleRoleColor(
    const console_colors_t &colors,
    console_color_role_t role )
{
    switch ( role ) {
        case console_color_role_t::TEXT: return colors.text;
        case console_color_role_t::MUTED: return colors.muted;
        case console_color_role_t::ACCENT: return colors.accent;
        case console_color_role_t::INFO: return colors.info;
        case console_color_role_t::WARNING: return colors.warning;
        case console_color_role_t::ERROR: return colors.error;
    }
    return colors.text;
}

inline QColor ConsoleBodyColor(
    const console_colors_t &colors,
    console_color_role_t role )
{
    if ( role == console_color_role_t::TEXT ||
         role == console_color_role_t::MUTED ) {
        return ConsoleRoleColor( colors, role );
    }
    QColor background = ConsoleApplicationColor(
        "TileEditorConsoleBackgroundColor" );
    if ( !background.isValid() )
        background = QApplication::palette().color( QPalette::Base );
    return ConsoleReadableColor(
        ConsoleBlend( ConsoleRoleColor( colors, role ), colors.text, 0.32 ),
        background,
        colors.text );
}

} // namespace cypher::tools::tile_editor::detail

#endif // CYPHER_TOOLS_TILEEDITOR_CONSOLECOLORS_H
