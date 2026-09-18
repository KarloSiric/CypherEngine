//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Applies readable editor chrome from the configured palette and scale.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorTheme.h"
#include "CypherTileEditorSettingsDialog.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QRegularExpression>
#include <QResource>
#include <QStyleFactory>
#include <QVariant>

#include <algorithm>

static void RegisterThemeResources()
{
    static const bool registered = [] {
        Q_INIT_RESOURCE( CypherTileEditorResources );
        return true;
    }();
    (void)registered;
}

namespace cypher::tools::tile_editor
{
namespace
{
QColor Blend( const QColor &a, const QColor &b, double t )
{
    return QColor::fromRgbF(
        a.redF() * ( 1.0 - t ) + b.redF() * t,
        a.greenF() * ( 1.0 - t ) + b.greenF() * t,
        a.blueF() * ( 1.0 - t ) + b.blueF() * t );
}

QString ScaleMetrics( const QString &style, double scale )
{
    // Scale physical widget dimensions with font size, retaining crisp 1px borders.
    static const QRegularExpression expression( QStringLiteral( R"((\d+)px\b)" ) );
    QString output;
    qsizetype previous = 0;
    auto matches = expression.globalMatch( style );
    while ( matches.hasNext() ) {
        const auto match = matches.next();
        const int value = match.captured( 1 ).toInt();
        output += style.mid( previous, match.capturedStart() - previous );
        output += QString::number( value <= 1 ? value : qRound( value * scale ) ) + QStringLiteral( "px" );
        previous = match.capturedEnd();
    }
    output += style.mid( previous );
    return output;
}
} // namespace

void CypherTileEditorTheme_Apply( QApplication &application )
{
    CypherTileEditorTheme_Apply( application, tile_editor_preferences_t{} );
}

void CypherTileEditorTheme_Apply(
    QApplication &application, const tile_editor_preferences_t &preferences )
{
    RegisterThemeResources();
    const auto prefs = TileEditorPreferences_Normalize( preferences );
    QString signature = QString::number( prefs.uiFontPointSize );
    signature += QLatin1Char( ':' ) + QString::number( prefs.showActiveViewBorder );
    signature += QLatin1Char( ':' ) + QString::number( prefs.highlightActiveView );
    for ( const QColor &value : { prefs.uiBackgroundColor, prefs.panelColor, prefs.textColor,
          prefs.accentColor, prefs.activeViewColor, prefs.canvasColor, prefs.perspectiveColor } )
        signature += QLatin1Char( ':' ) + value.name( QColor::HexArgb );
    if ( application.property( "TileEditorThemeSignature" ).toString() == signature &&
         !application.styleSheet().isEmpty() ) return;
    if ( application.property( "TileEditorFusionInstalled" ).toBool() == false ) {
        application.setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );
        application.setProperty( "TileEditorFusionInstalled", true );
    }
    QFont font = application.font();
    font.setPointSize( prefs.uiFontPointSize );
    application.setFont( font );

    const QColor border = Blend( prefs.uiBackgroundColor, prefs.textColor, 0.20 );
    const QColor button = Blend( prefs.uiBackgroundColor, prefs.textColor, 0.09 );
    const QColor muted = Blend( prefs.uiBackgroundColor, prefs.textColor, 0.68 );
    const QColor disabled = Blend( prefs.uiBackgroundColor, prefs.textColor, 0.42 );
    const QColor selectedText = Blend( prefs.textColor, prefs.accentColor, 0.34 );
    const QColor selectionBackground = Blend( prefs.panelColor, prefs.accentColor, 0.12 );
    const QColor inset = Blend( prefs.panelColor, Qt::black, 0.16 );
    const QColor edge = Blend( prefs.panelColor, Qt::black, 0.32 );
    const double statusSaturation = std::clamp(
        prefs.accentColor.hslSaturationF() < 0.0
            ? 0.70
            : prefs.accentColor.hslSaturationF(),
        0.58,
        0.90 );
    const bool darkChrome = prefs.uiBackgroundColor.lightnessF() < 0.5;
    const double statusLightness = darkChrome ? 0.68 : 0.36;
    const QColor errorBase = QColor::fromHslF(
        0.010, statusSaturation, statusLightness );
    const QColor successBase = QColor::fromHslF(
        0.365, statusSaturation, statusLightness );
    const QColor infoBase = QColor::fromHslF(
        0.565, statusSaturation, statusLightness );
    const QColor errorText = Blend( prefs.textColor, errorBase, 0.58 );
    const QColor errorBackground = Blend( prefs.panelColor, errorBase, 0.13 );
    const QColor errorBorder = Blend( border, errorBase, 0.52 );
    const QColor successText = Blend( prefs.textColor, successBase, 0.48 );
    const QColor successBorder = Blend( border, successBase, 0.48 );
    const QColor infoText = Blend( prefs.textColor, infoBase, 0.48 );
    const QColor infoBorder = Blend( border, infoBase, 0.48 );
    application.setProperty( "TileEditorTextColor", prefs.textColor );
    application.setProperty( "TileEditorMutedColor", muted );
    application.setProperty( "TileEditorConsoleBackgroundColor", inset );
    application.setProperty( "TileEditorAccentColor", prefs.accentColor );
    application.setProperty( "TileEditorWarningColor", prefs.accentColor );
    application.setProperty( "TileEditorErrorColor", errorText );
    application.setProperty( "TileEditorSuccessColor", successText );
    application.setProperty( "TileEditorInfoColor", infoText );
    QPalette palette;
    palette.setColor( QPalette::Window, prefs.uiBackgroundColor );
    palette.setColor( QPalette::WindowText, prefs.textColor );
    palette.setColor( QPalette::Base, prefs.panelColor );
    palette.setColor( QPalette::AlternateBase, Blend( prefs.panelColor, prefs.textColor, 0.035 ) );
    palette.setColor( QPalette::Text, prefs.textColor );
    palette.setColor( QPalette::Button, button );
    palette.setColor( QPalette::ButtonText, prefs.textColor );
    palette.setColor( QPalette::Light, border.lighter( 110 ) );
    palette.setColor( QPalette::Midlight, border );
    palette.setColor( QPalette::Mid, prefs.panelColor );
    palette.setColor( QPalette::Dark, edge );
    palette.setColor( QPalette::Shadow, edge.darker( 120 ) );
    palette.setColor( QPalette::Highlight, selectionBackground );
    palette.setColor( QPalette::HighlightedText, selectedText );
    palette.setColor( QPalette::ToolTipBase, inset );
    palette.setColor( QPalette::ToolTipText, prefs.textColor );
    palette.setColor( QPalette::PlaceholderText, muted );
    palette.setColor( QPalette::Disabled, QPalette::Text, disabled );
    palette.setColor( QPalette::Disabled, QPalette::ButtonText, disabled );
    application.setPalette( palette );

    QFile theme( QStringLiteral( ":/cypher/tile-editor/theme/editor.qss" ) );
    if ( !theme.open( QIODevice::ReadOnly ) ) {
        qWarning( "Could not load the bundled tile-editor QSS theme" );
        return;
    }
    QString style = QString::fromUtf8( theme.readAll() );
    auto color = [&]( const char *token, const QColor &value ) {
        style.replace( QString::fromLatin1( token ), value.name( QColor::HexRgb ) );
    };
    color( "@BACKGROUND@", prefs.uiBackgroundColor );
    color( "@CHROME@", Blend( prefs.uiBackgroundColor, prefs.textColor, 0.012 ) );
    color( "@PANEL@", prefs.panelColor );
    color( "@ALTERNATE@", palette.color( QPalette::AlternateBase ) );
    color( "@INPUT@", inset );
    color( "@INSET@", inset );
    color( "@EDGE@", edge );
    color( "@DEEPEST@", edge.darker( 115 ) );
    color( "@BUTTON@", button );
    color( "@HEADER@", Blend( prefs.uiBackgroundColor, prefs.textColor, 0.075 ) );
    color( "@HIGHLIGHT_EDGE@", border.lighter( 110 ) );
    color( "@BORDER@", border );
    color( "@HOVER@", Blend( button, prefs.textColor, 0.055 ) );
    color( "@VIEW_HEADER@", Blend( prefs.panelColor, prefs.canvasColor, 0.24 ) );
    color( "@VIEW_HEADER_ACTIVE@", prefs.highlightActiveView
        ? Blend( button, prefs.canvasColor, 0.15 )
        : Blend( prefs.panelColor, prefs.canvasColor, 0.24 ) );
    const QColor viewGutter = Blend( prefs.uiBackgroundColor, Qt::black, 0.48 );
    color( "@VIEW_GUTTER@", viewGutter );
    color( "@VIEW_GUTTER_LIGHT@", Blend( viewGutter, prefs.textColor, 0.13 ) );
    color( "@VIEW_GUTTER_SHADOW@", Blend( viewGutter, Qt::black, 0.55 ) );
    color( "@VIEW_GUTTER_HOVER@", Blend( viewGutter, prefs.textColor, 0.08 ) );
    color( "@ACCENT@", prefs.accentColor );
    color( "@ACCENT_LIGHT@", prefs.accentColor.lighter( 115 ) );
    color( "@CHECKED_HOVER@", Blend( inset, prefs.accentColor, 0.075 ) );
    color( "@SELECTION_BG@", selectionBackground );
    style.replace( QStringLiteral( "@ACTIVE_VIEW_BORDER@" ),
        prefs.showActiveViewBorder
            ? QStringLiteral( "border: 1px solid %1;" ).arg(
                  prefs.activeViewColor.name( QColor::HexRgb ) )
            : QStringLiteral( "border: 0;" ) );
    color( "@SELECTED_TEXT@", selectedText );
    color( "@TEXT@", prefs.textColor );
    color( "@TEXT_MUTED@", muted );
    color( "@DISABLED@", disabled );
    color( "@PERSPECTIVE@", prefs.perspectiveColor );
    color( "@OVERLAY_BG@", Blend( prefs.perspectiveColor, prefs.textColor, 0.055 ) );
    color( "@ERROR_TEXT@", errorText );
    color( "@ERROR_BG@", errorBackground );
    color( "@ERROR_BORDER@", errorBorder );
    color( "@SUCCESS_TEXT@", successText );
    color( "@SUCCESS_BORDER@", successBorder );
    color( "@INFO_TEXT@", infoText );
    color( "@INFO_BORDER@", infoBorder );
    style.replace( QStringLiteral( "@HEADER_FONT@" ), QString::number( std::max( 9, prefs.uiFontPointSize - 1 ) ) );
    style.replace( QStringLiteral( "@BODY_FONT@" ), QString::number( prefs.uiFontPointSize ) );
    application.setStyleSheet( ScaleMetrics( style, static_cast<double>( prefs.uiFontPointSize ) / 9.0 ) );
    application.setProperty( "TileEditorThemeSignature", signature );
}
} // namespace cypher::tools::tile_editor
