//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Style_Tests.cpp
//  Purpose: Contract tests for the editor style: tokens, resolution, the
//           generated stylesheet, and icon tinting.
//  Details: The stylesheet template and the style code must agree on every
//           placeholder; a leftover placeholder would reach Qt's CSS parser
//           as garbage and silently unstyle part of the editor.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Application.h"
#include "CypherEditorGui_Style.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QDir>
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmapCache>
#include <QStyleOptionMenuItem>
#include <QStyleOptionToolButton>
#include <QStyleOptionTab>
#include <QSvgRenderer>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <set>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

QString Template()
{
    EditorGui_RegisterResources();
    QFile file( QString::fromLatin1( EDITOR_STYLE_TEMPLATE_RESOURCE ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() );
}

// The framework registry, for resolving.
struct registry_t {
    theme_registry_t registry{};
    registry_t()
    {
        REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
        REQUIRE( EditorStyle_RegisterTokens( &registry ) == theme_status_t::OK );
    }
    ~registry_t() { EditorThemeRegistry_Shutdown( &registry ); }
    editor_style_t Resolve( const key_value_t *const *ppChain = nullptr, usize nChain = 0u ) const
    {
        return EditorStyle_Resolve( &registry, ppChain, nChain );
    }
};

struct theme_t {
    settings_document_t store{};
    explicit theme_t( const char *pText )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( pText ) ).status == settings_document_status_t::OK );
    }
    ~theme_t() { SettingsDocument_Shutdown( &store ); }
    const key_value_t *Root() const { return SettingsDocument_Root( &store ); }
};

struct svg_font_warning_capture_t {
    QStringList messages;
    QtMessageHandler previous{};
    inline static svg_font_warning_capture_t *pCurrent{};
    svg_font_warning_capture_t()
    {
        pCurrent = this;
        previous = qInstallMessageHandler( []( QtMsgType type, const QMessageLogContext &context, const QString &message ) {
            if ( message.contains( QStringLiteral( "Helvetica, Arial" ), Qt::CaseInsensitive ) ) { pCurrent->messages.append( message ); }
            if ( pCurrent->previous != nullptr ) { pCurrent->previous( type, context, message ); }
        } );
    }
    ~svg_font_warning_capture_t() { qInstallMessageHandler( previous ); pCurrent = nullptr; }
};

} // namespace

TEST_CASE( "Framework tokens register and resolve to their defaults and formulas", "[editor][gui][style]" )
{
    registry_t r;
    CHECK( EditorStyle_RegisterTokens( &r.registry ) == theme_status_t::DUPLICATE_TOKEN );
    usize nTokens = 0u;
    ( void )EditorStyle_Tokens( &nTokens );
    CHECK( nTokens == 162u ); // 125 colours (11 for code), 7 fonts, 26 metrics, 4 choices.
    const editor_style_t style = r.Resolve();
    CHECK( style.colors[STYLE_COLOR_BACKGROUND] == 0x3C3C3CFFu );
    CHECK( style.colors[STYLE_COLOR_AXIS_X] == 0xDE524CFFu );
    CHECK( style.colors[STYLE_COLOR_WIRE] == 0xC8CCD2FFu );
    // Derived chrome matches what the stylesheet used to blend in code.
    CHECK( EditorStyle_TokenColor( style, "ui.border" ) == QColor( 0x5c, 0x5c, 0x5c ) );
    CHECK( EditorStyle_TokenColor( style, "ui.edge" ) == QColor( 0x22, 0x22, 0x22 ) );
    CHECK( EditorStyle_TokenColor( style, "viewport.selection.fill" ).alpha() == 0x40 );
    CHECK( EditorStyle_TokenColor( style, "viewport.grid.band" ) == EditorStyle_TokenColor( style, "viewport.grid.major" ) );
    CHECK( EditorStyle_TokenColor( style, "no.such.token" ) == QColor( 0xff, 0x00, 0xff ) );
    CHECK( style.iconSize == 28.0 );
    CHECK( style.density == 1.0 );
    CHECK( EditorStyle_Choice( style, "viewport.grid.style" ) == QStringLiteral( "lines" ) );
    CHECK( EditorStyle_Font( style, "ui.heading" ).weight() == QFont::DemiBold );
    CHECK( EditorStyle_Font( style, "viewport.dimensions" ).pointSizeF() == 12.0 );
    CHECK( EditorStyle_Font( style, "viewport.labels" ).pointSizeF() == 9.0 );
    CHECK( style.consoleFont.fixedPitch() );
    CHECK_FALSE( style.bLight );
    CHECK( style.nInvalidSkipped == 0u );
}

TEST_CASE( "The built-in theme lists every framework token and changes nothing", "[editor][gui][style]" )
{
    registry_t r;
    settings_document_t theme{};
    REQUIRE( SettingsDocument_Init( &theme, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
    REQUIRE( EditorGui_LoadResource( EDITOR_BUILTIN_THEME_RESOURCE, &theme ) == editor_gui_status_t::OK );
    const key_value_t *pRoot = SettingsDocument_Root( &theme );
    theme_problem_t problems[4]{};
    CHECK( EditorTheme_Audit( &r.registry, pRoot, problems, 4u ) == 0u );
    usize nTokens = 0u;
    const theme_token_t *pTokens = EditorStyle_Tokens( &nTokens );
    for ( usize i = 0u; i < nTokens; ++i ) {
        const char *pSection = pTokens[i].kind == theme_token_kind_t::COLOR    ? "colors"
                               : pTokens[i].kind == theme_token_kind_t::FONT   ? "fonts"
                               : pTokens[i].kind == theme_token_kind_t::METRIC ? "metrics"
                                                                               : "choices";
        INFO( pTokens[i].pId );
        CHECK( KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( pSection ) ), StringView_FromCString( pTokens[i].pId ) ) != nullptr );
    }
    // Resolving through it gives exactly the defaults.
    const key_value_t *chain[]{ pRoot };
    const editor_style_t themed = r.Resolve( chain, 1u );
    const editor_style_t plain = r.Resolve();
    CHECK( themed.tokenColors == plain.tokenColors );
    CHECK( themed.metrics == plain.metrics );
    CHECK( themed.choices == plain.choices );
    CHECK( themed.nInvalidSkipped == 0u );
}

TEST_CASE( "The stylesheet fills every placeholder", "[editor][gui][style]" )
{
    registry_t r;
    const editor_style_t style = r.Resolve();
    const QString sheet = EditorStyle_BuildStyleSheet( style, Template() );
    REQUIRE_FALSE( sheet.isEmpty() );
    CHECK_FALSE( sheet.contains( QLatin1Char( '@' ) ) );
    CHECK( sheet.contains( QStringLiteral( "#3c3c3c" ) ) );
    CHECK( sheet.contains( QStringLiteral( "#e59a2f" ) ) );
    CHECK( sheet.contains( QStringLiteral( "close-button_dark.svg" ) ) ); // Dark theme, light glyphs.
    // Unknown placeholders are refused rather than passed to Qt.
    CHECK( EditorStyle_BuildStyleSheet( style, QStringLiteral( "QWidget { color: @NOT_A_TOKEN@; }" ) ).isEmpty() );
    CHECK( EditorStyle_BuildStyleSheet( style, QStringLiteral( "QWidget { color: @no.such.token@; }" ) ).isEmpty() );
    // Translucent tokens become rgba().
    CHECK( EditorStyle_BuildStyleSheet( style, QStringLiteral( "a { color: @viewport.selection.fill@; }" ) )
               .contains( QStringLiteral( "rgba(208, 186, 98, 64)" ) ) );
}

TEST_CASE( "Default interaction colours stay restrained and authored theme colours remain authoritative", "[editor][gui][style][hover]" )
{
    registry_t r;
    const editor_style_t style = r.Resolve();
    CHECK( EditorStyle_TokenColor( style, "viewport.selection" ) == QColor( "#d0ba62" ) );
    CHECK( EditorStyle_TokenColor( style, "viewport.hover" ) == QColor( "#8cba87" ) );
    for ( const char *id : { "ui.hover", "ui.checked.hover" } ) {
        const QColor hover = EditorStyle_TokenColor( style, id );
        CHECK( hover.blue() > hover.red() );
        CHECK( hover.green() > hover.red() );
    }
    CHECK( EditorStyle_TokenColor( style, "ui.accent" ) == QColor( "#e59a2f" ) );
    theme_t theme( "@cykv 1\n@schema \"cypher.theme\" 2\n"
        "{ id = \"authored\" name = \"Authored\" colors = { \"ui.hover\" = \"#4a754a\" \"ui.checked.hover\" = \"#705055\" "
        "\"viewport.selection\" = \"#da7139\" \"viewport.selection.fill\" = \"#da713928\" \"viewport.hover\" = \"#785fbd\" } }" );
    const key_value_t *chain[]{ theme.Root() };
    const editor_style_t authored = r.Resolve( chain, 1u );
    CHECK( EditorStyle_TokenColor( authored, "ui.hover" ) == QColor( "#4a754a" ) );
    CHECK( EditorStyle_TokenColor( authored, "ui.checked.hover" ) == QColor( "#705055" ) );
    CHECK( EditorStyle_TokenColor( authored, "viewport.selection" ) == QColor( "#da7139" ) );
    CHECK( EditorStyle_TokenColor( authored, "viewport.selection.fill" ) == QColor( 0xda, 0x71, 0x39, 0x28 ) );
    CHECK( EditorStyle_TokenColor( authored, "viewport.hover" ) == QColor( "#785fbd" ) );
    CHECK( authored.nInvalidSkipped == 0u );
}

TEST_CASE( "Bundled Hammer Charcoal uses the shared blue hover and muted viewport feedback", "[editor][gui][style][hover][presets]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    QFile file( QStringLiteral( ":/cypher/editor/themes/presets/hammer_charcoal.cytheme" ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QByteArray bytes = file.readAll();
    theme_t preset( bytes.constData() );
    settings_document_t builtin{};
    REQUIRE( SettingsDocument_Init( &builtin, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
    REQUIRE( EditorGui_LoadResource( EDITOR_BUILTIN_THEME_RESOURCE, &builtin ) == editor_gui_status_t::OK );
    const key_value_t *chain[]{ preset.Root(), SettingsDocument_Root( &builtin ) };
    const editor_style_t style = r.Resolve( chain, 2u );
    const QColor hover = EditorStyle_TokenColor( style, "ui.hover" );
    CHECK( hover.blue() > hover.red() );
    CHECK( hover.green() > hover.red() );
    CHECK( hover != QColor( "#484c50" ) );
    CHECK( EditorStyle_TokenColor( style, "ui.accent" ) == QColor( "#e68a24" ) );
    CHECK( EditorStyle_TokenColor( style, "viewport.selection" ) == QColor( "#d0ba62" ) );
    CHECK( EditorStyle_TokenColor( style, "viewport.selection.fill" ) == QColor( 0xd0, 0xba, 0x62, 0x40 ) );
    CHECK( EditorStyle_TokenColor( style, "viewport.hover" ) == QColor( "#8cba87" ) );
    CHECK( style.nInvalidSkipped == 0u );
}

TEST_CASE( "Toolbar and panel icon buttons share blue hover surfaces and retain checked orange rims", "[editor][gui][style][hover]" )
{
    registry_t r;
    editor_style_t style = r.Resolve();
    style.uiFont.setPointSizeF( 9.0 );
    const QString sheet = EditorStyle_BuildStyleSheet( style, Template() );
    for ( const char *name : { "masonMainToolBar", "masonSelectModes", "EditorToolStrip", "AssetBrowserToolbar",
                              "EditorConsoleToolbar", "MapOutlinerFilters", "EditorViewHeader" } ) {
        INFO( name );
        QToolBar toolbar;
        toolbar.setObjectName( QString::fromLatin1( name ) );
        toolbar.setStyleSheet( sheet );
        auto *pButton = new QToolButton( &toolbar );
        pButton->setProperty( "authoringTool", true );
        pButton->setCheckable( true );
        pButton->setFixedSize( 36, 36 );
        toolbar.addWidget( pButton );
        pButton->ensurePolished();
        const auto raster = [&]( bool checked, bool enabled ) {
            pButton->setChecked( checked );
            pButton->setEnabled( enabled );
            QImage image( pButton->size(), QImage::Format_ARGB32_Premultiplied );
            image.fill( Qt::transparent );
            QPainter painter( &image );
            QStyleOptionToolButton option;
            option.initFrom( pButton );
            option.state = QStyle::State_Raised | QStyle::State_MouseOver | ( checked ? QStyle::State_On : QStyle::State_Off );
            if ( enabled ) { option.state |= QStyle::State_Enabled; }
            pButton->style()->drawComplexControl( QStyle::CC_ToolButton, &option, &painter, pButton );
            return image;
        };
        const QImage off = raster( false, true );
        CHECK( off.pixelColor( 18, 18 ) == EditorStyle_TokenColor( style, "ui.hover" ) );
        const QImage on = raster( true, true );
        CHECK( on.pixelColor( 18, 18 ) == EditorStyle_TokenColor( style, "ui.checked.hover" ) );
        CHECK( on.pixelColor( 0, 18 ) == EditorStyle_TokenColor( style, "ui.accent.light" ) );
        const QImage disabled = raster( true, false );
        CHECK( disabled.pixelColor( 18, 18 ) != EditorStyle_TokenColor( style, "ui.hover" ) );
        CHECK( disabled.pixelColor( 18, 18 ) != EditorStyle_TokenColor( style, "ui.checked.hover" ) );
    }
}

TEST_CASE( "Selected tabs and asset tabs retain the interaction hover surface", "[editor][gui][style][hover]" )
{
    registry_t r;
    editor_style_t style = r.Resolve();
    style.uiFont.setPointSizeF( 9.0 );
    const QString sheet = EditorStyle_BuildStyleSheet( style, Template() );
    for ( const char *name : { "", "AssetBrowserTabs" } ) {
        INFO( name );
        QTabBar tabs;
        tabs.setObjectName( QString::fromLatin1( name ) );
        tabs.setStyleSheet( sheet );
        tabs.addTab( QStringLiteral( "Materials" ) );
        tabs.ensurePolished();
        for ( const bool selected : { false, true } ) {
            QImage image( 90, 28, QImage::Format_ARGB32_Premultiplied );
            image.fill( Qt::transparent );
            QPainter painter( &image );
            QStyleOptionTab option;
            option.initFrom( &tabs );
            option.rect = image.rect();
            option.shape = QTabBar::RoundedNorth;
            option.state = QStyle::State_Enabled | QStyle::State_MouseOver;
            if ( selected ) { option.state |= QStyle::State_Selected; }
            tabs.style()->drawControl( QStyle::CE_TabBarTabShape, &option, &painter, &tabs );
            CHECK( image.pixelColor( 45, 14 ) == EditorStyle_TokenColor( style, "ui.hover" ) );
        }
    }
}

TEST_CASE( "A theme's base colours flow into every derived colour", "[editor][gui][style]" )
{
    registry_t r;
    theme_t theme( "@cykv 1\n@schema \"cypher.theme\" 2\n"
                   "{ id = \"t\" name = \"T\" colors = { \"ui.accent\" = \"#12ab34\" \"ui.text\" = \"bad\" \"ui.background\" = \"#f0f0f0\" }\n"
                   "  choices = { \"ui.density\" = \"compact\" } }" );
    const key_value_t *chain[]{ theme.Root() };
    const editor_style_t style = r.Resolve( chain, 1u );
    CHECK( style.colors[STYLE_COLOR_ACCENT] == 0x12AB34FFu );
    CHECK( style.colors[STYLE_COLOR_TEXT] == 0xDCDCDCFFu ); // Invalid value: default.
    CHECK( style.nInvalidSkipped == 1u );
    CHECK( EditorStyle_TokenColor( style, "ui.status.warning.border" ) == QColor( 0x12, 0xab, 0x34 ) ); // Follows the accent.
    CHECK( style.bLight );
    CHECK( style.density == 0.8 );
    const QString sheet = EditorStyle_BuildStyleSheet( style, Template() );
    CHECK( sheet.contains( QStringLiteral( "#12ab34" ) ) );
    CHECK( sheet.contains( QStringLiteral( "close-button.svg" ) ) ); // Light theme, dark glyphs.
}

TEST_CASE( "Icons tint from the theme and missing icons are null", "[editor][gui][style]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    const editor_style_t style = r.Resolve();
    const QIcon icon = EditorStyle_Icon( style, "pointer", true );
    REQUIRE_FALSE( icon.isNull() );
    const QImage normal = icon.pixmap( 24, QIcon::Normal, QIcon::Off ).toImage();
    const QImage checked = icon.pixmap( 24, QIcon::Normal, QIcon::On ).toImage();
    CHECK_FALSE( normal.isNull() );
    CHECK( normal == checked ); // Outline-only artwork stays neutral; the button provides the selection cue.
    CHECK( EditorStyle_Icon( style, "no-such-icon" ).isNull() );
}

namespace
{

int ColoredPixelCount( const QImage &image )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            const QColor color = image.pixelColor( x, y );
            const int range = std::max( { color.red(), color.green(), color.blue() } )
                - std::min( { color.red(), color.green(), color.blue() } );
            if ( color.alpha() > 128 && range > 35 ) { ++count; }
        }
    }
    return count;
}

int OpaqueShadeCount( const QImage &image )
{
    std::set<int> shades;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            const QColor color = image.pixelColor( x, y );
            if ( color.alpha() == 255 ) { shades.insert( qGray( color.rgb() ) ); }
        }
    }
    return static_cast<int>( shades.size() );
}

} // namespace

TEST_CASE( "Tool icons show their full colours at rest by default, and grey out when disabled", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    const editor_style_t style = r.Resolve();
    for ( const char *pName : { "select-faces", "layout-four", "asset-material" } ) { // tool-block is steel grey by design.
        INFO( pName );
        const QIcon icon = EditorStyle_Icon( style, pName, true );
        REQUIRE_FALSE( icon.isNull() );
        CHECK( ColoredPixelCount( icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::Off ).toImage() ) > 8 );
        CHECK( ColoredPixelCount( icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Disabled, QIcon::Off ).toImage() ) == 0 );
    }
}

TEST_CASE( "Neutral geometric illustrations keep their original steel colours when checked", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    editor_style_t neutral = r.Resolve();
    neutral.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 ); // The neutral icon look.
    const QIcon icon = EditorStyle_Icon( neutral, "tool-block", true );
    REQUIRE_FALSE( icon.isNull() );
    const QImage normal = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::Off ).toImage();
    const QImage hover = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Active, QIcon::Off ).toImage();
    const QImage checked = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage();
    const QImage disabled = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Disabled, QIcon::On ).toImage();
    CHECK( ColoredPixelCount( normal ) == 0 );
    CHECK( hover != normal ); // Hover restores some original face detail.
    CHECK( ColoredPixelCount( checked ) == 0 ); // The steel cube must not turn orange.
    CHECK( checked != normal );
    CHECK( ColoredPixelCount( disabled ) == 0 );
    CHECK( disabled != normal );
    CHECK( OpaqueShadeCount( normal ) > 8 ); // Light/dark cube faces remain legible.
    CHECK( OpaqueShadeCount( checked ) > 8 );
    CHECK( normal.pixelColor( 0, 31 ).alpha() == 0 ); // No opaque icon background.
}

TEST_CASE( "Icon hover color is restrained configurable and isolated from checked or disabled states", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    editor_style_t plain = r.Resolve();
    plain.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 ); // The neutral icon look.
    plain.metrics.insert( QStringLiteral( "ui.icon.hover_color_strength" ), 0.0 );
    editor_style_t subtle = plain;
    subtle.metrics.insert( QStringLiteral( "ui.icon.hover_color_strength" ), 0.35 );
    editor_style_t original = plain;
    original.metrics.insert( QStringLiteral( "ui.icon.hover_color_strength" ), 1.0 );
    const QIcon neutralIcon = EditorStyle_Icon( plain, "select-faces", true );
    const QIcon hoverIcon = EditorStyle_Icon( subtle, "select-faces", true );
    const QIcon originalIcon = EditorStyle_Icon( original, "select-faces", true );
    const auto raster = []( const QIcon &icon, QIcon::Mode mode, QIcon::State state = QIcon::Off ) {
        return icon.pixmap( QSize( 32, 32 ), 1.0, mode, state ).toImage();
    };
    const QImage idle = raster( neutralIcon, QIcon::Normal );
    const QImage hover = raster( hoverIcon, QIcon::Active );
    const QImage source = raster( originalIcon, QIcon::Active );
    CHECK( raster( neutralIcon, QIcon::Active ) == idle );
    CHECK( raster( hoverIcon, QIcon::Normal ) == idle );
    CHECK( hover != idle );
    CHECK( hover != source );
    CHECK( ColoredPixelCount( hover ) > 8 );
    for ( int y = 0; y < hover.height(); ++y ) {
        for ( int x = 0; x < hover.width(); ++x ) {
            const QColor from = idle.pixelColor( x, y );
            const QColor to = source.pixelColor( x, y );
            const QColor mixed = hover.pixelColor( x, y );
            CHECK( mixed.alpha() == from.alpha() );
            if ( from.alpha() != 255 ) { continue; } // Alpha rounding has its own coverage contract.
            CHECK( mixed.red() == qRound( from.red() + ( to.red() - from.red() ) * 0.35 ) );
            CHECK( mixed.green() == qRound( from.green() + ( to.green() - from.green() ) * 0.35 ) );
            CHECK( mixed.blue() == qRound( from.blue() + ( to.blue() - from.blue() ) * 0.35 ) );
        }
    }
    CHECK( raster( hoverIcon, QIcon::Active, QIcon::On ) == raster( hoverIcon, QIcon::Normal, QIcon::On ) );
    CHECK( raster( neutralIcon, QIcon::Disabled ) == raster( hoverIcon, QIcon::Disabled ) );
    CHECK( raster( hoverIcon, QIcon::Disabled, QIcon::On ) == raster( hoverIcon, QIcon::Disabled ) );
    // Asking for the earlier theme again must not return the later cached hover raster.
    CHECK( raster( neutralIcon, QIcon::Active ) == idle );
}

TEST_CASE( "Run build and diagnostic icons retain semantic colours only while enabled", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    const editor_style_t style = r.Resolve();
    for ( const char *pName : { "map-run", "map-compile", "map-check", "log-error", "log-warning" } ) {
        INFO( pName );
        const QIcon icon = EditorStyle_Icon( style, pName );
        REQUIRE_FALSE( icon.isNull() );
        CHECK( ColoredPixelCount( icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::Off ).toImage() ) > 8 );
        CHECK( ColoredPixelCount( icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Disabled, QIcon::Off ).toImage() ) == 0 );
    }
}

TEST_CASE( "Icon rasters follow theme changes and arbitrary display scaling", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    theme_t theme( "@cykv 1\n@schema \"cypher.theme\" 2\n"
                   "{ id = \"icon-test\" name = \"Icon test\" colors = { \"ui.text\" = \"#202020\" \"ui.panel\" = \"#eeeeee\" \"ui.accent\" = \"#227744\" } }" );
    const key_value_t *chain[]{ theme.Root() };
    // Neutral icons are drawn in the theme's colours; full-colour art is not.
    editor_style_t darkStyle = r.Resolve();
    editor_style_t lightStyle = r.Resolve( chain, 1u );
    darkStyle.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 );
    lightStyle.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 );
    const QIcon darkIcon = EditorStyle_Icon( darkStyle, "select-faces" );
    const QIcon lightIcon = EditorStyle_Icon( lightStyle, "select-faces" );
    const QSize logicalSize( 29, 29 );
    const QPixmap dark = darkIcon.pixmap( logicalSize, 2.0, QIcon::Normal, QIcon::Off );
    const QPixmap light = lightIcon.pixmap( logicalSize, 2.0, QIcon::Normal, QIcon::Off );
    CHECK( dark.size() == QSize( 58, 58 ) );
    CHECK( dark.devicePixelRatio() == 2.0 );
    CHECK( light.size() == dark.size() );
    CHECK( light.toImage() != dark.toImage() );
    CHECK( OpaqueShadeCount( light.toImage() ) > 8 );
    const QPixmap fractional = darkIcon.pixmap( QSize( 24, 24 ), 1.5, QIcon::Normal, QIcon::Off );
    CHECK( fractional.size() == QSize( 36, 36 ) );
    CHECK( fractional.devicePixelRatio() == 1.5 );
    // A subsequent request for the old palette must still return that palette.
    CHECK( darkIcon.pixmap( logicalSize, 2.0, QIcon::Normal, QIcon::Off ).toImage() == dark.toImage() );
}

TEST_CASE( "Decorative icons can opt out of checked illustration colours independently of command actions", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    editor_style_t style = r.Resolve();
    style.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 ); // The neutral icon look.
    for ( const char *pName : { "view-console", "view-assets", "texture-lock", "snap-grid", "grid-show" } ) {
        INFO( pName );
        const QIcon quiet = EditorStyle_Icon( style, pName );
        const QIcon accented = EditorStyle_Icon( style, pName, true );
        REQUIRE_FALSE( quiet.isNull() );
        const QImage normal = quiet.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::Off ).toImage();
        const QImage checked = quiet.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage();
        CHECK( normal == checked );
        CHECK( ColoredPixelCount( checked ) == 0 );
        CHECK( checked != accented.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage() );
    }
}

TEST_CASE( "Checked illustrations retain their source hues independently of the button accent", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    editor_style_t style = r.Resolve();
    style.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 ); // Neutral at rest; colour returns when checked.
    for ( const char *name : { "select-meshes", "select-faces", "tool-block" } ) {
        INFO( name );
        QSvgRenderer original( QStringLiteral( ":/cypher/editor/icons/color/%1.svg" ).arg( QString::fromLatin1( name ) ) );
        REQUIRE( original.isValid() );
        QImage source( 32, 32, QImage::Format_ARGB32_Premultiplied );
        source.fill( Qt::transparent );
        { QPainter painter( &source ); original.render( &painter ); }
        const QIcon icon = EditorStyle_Icon( style, name, true );
        const QImage idle = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::Off ).toImage();
        const QImage checked = icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage();
        REQUIRE( idle != checked );
        int opaqueSamples = 0;
        for ( int y = 0; y < checked.height(); ++y ) {
            for ( int x = 0; x < checked.width(); ++x ) {
                const QColor from = idle.pixelColor( x, y );
                const QColor to = source.pixelColor( x, y );
                const QColor actual = checked.pixelColor( x, y );
                CHECK( actual.alpha() == to.alpha() ); // Preserve the original silhouette.
                if ( to.alpha() != 255 ) { continue; }
                ++opaqueSamples;
                CHECK( actual.red() == qRound( from.red() + ( to.red() - from.red() ) * 0.65 ) );
                CHECK( actual.green() == qRound( from.green() + ( to.green() - from.green() ) * 0.65 ) );
                CHECK( actual.blue() == qRound( from.blue() + ( to.blue() - from.blue() ) * 0.65 ) );
            }
        }
        CHECK( opaqueSamples > 80 );
        editor_style_t changedAccent = style;
        changedAccent.tokenColors.insert( QStringLiteral( "ui.accent" ), QColor( "#ff00aa" ) );
        const QImage recoloredBorder = EditorStyle_Icon( changedAccent, name, true )
            .pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage();
        CHECK( recoloredBorder == checked ); // Border colour never recolours the artwork.
        editor_style_t neutralChecked = style;
        neutralChecked.metrics.insert( QStringLiteral( "ui.icon.checked_color_strength" ), 0.0 );
        CHECK( EditorStyle_Icon( neutralChecked, name, true ).pixmap( QSize( 32, 32 ), 1.0,
               QIcon::Normal, QIcon::On ).toImage() == idle );
        CHECK( icon.pixmap( QSize( 32, 32 ), 1.0, QIcon::Normal, QIcon::On ).toImage() == checked );
    }
}

TEST_CASE( "Bundled themes retain readable configurable toolbar and rail icon sizes", "[editor][gui][style]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    QDir presets( QStringLiteral( ":/cypher/editor/themes/presets" ) );
    const QStringList files = presets.entryList( { QStringLiteral( "*.cytheme" ) }, QDir::Files );
    REQUIRE_FALSE( files.isEmpty() );
    for ( const QString &name : files ) {
        INFO( name.toStdString() );
        QFile file( presets.filePath( name ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        const QByteArray bytes = file.readAll();
        theme_t theme( bytes.constData() );
        const key_value_t *chain[]{ theme.Root() };
        const editor_style_t style = r.Resolve( chain, 1u );
        CHECK( EditorStyle_Metric( style, "ui.icon_size", -1.0 ) == 28.0 );
        CHECK( EditorStyle_Metric( style, "ui.tool_strip.icon_size", -1.0 ) == 32.0 );
    }
}

TEST_CASE( "Checked toolbar buttons are a lighter grey with an accent rim, as Hammer's are", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    QFile file( QStringLiteral( ":/cypher/editor/themes/presets/graphite.cytheme" ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QByteArray bytes = file.readAll();
    theme_t graphite( bytes.constData() );
    const key_value_t *chain[]{ graphite.Root() };
    const editor_style_t style = r.Resolve( chain, 1u );
    QToolBar toolbar;
    toolbar.setStyleSheet( EditorStyle_BuildStyleSheet( style, Template() ) );
    auto *pButton = new QToolButton( &toolbar );
    pButton->setCheckable( true );
    pButton->setFixedSize( 36, 36 );
    toolbar.addWidget( pButton );
    toolbar.show();
    QApplication::processEvents();
    const auto centre = [pButton]() {
        QImage image( pButton->size(), QImage::Format_ARGB32_Premultiplied );
        image.fill( Qt::transparent );
        QPainter painter( &image );
        QStyleOptionToolButton option;
        option.initFrom( pButton );
        option.state = QStyle::State_Raised | ( pButton->isChecked() ? QStyle::State_On : QStyle::State_Off );
        if ( pButton->isEnabled() ) { option.state |= QStyle::State_Enabled; }
        pButton->style()->drawComplexControl( QStyle::CC_ToolButton, &option, &painter, pButton );
        return image.pixelColor( image.width() / 2, image.height() / 2 );
    };
    const QColor off = centre();
    pButton->setChecked( true );
    const QColor on = centre();
    CHECK( on != off ); // This tests the interior surface, not a border or icon.
    CHECK( qGray( on.rgb() ) > qGray( off.rgb() ) );            // Switched on is a lighter face...
    CHECK( std::abs( on.red() - on.blue() ) < 12 );             // ...still grey: the rim carries the accent.
    CHECK( std::abs( off.red() - off.blue() ) < 12 );           // Off stays neutral grey.
    pButton->setEnabled( false );
    const QColor disabled = centre();
    CHECK( disabled != on );
    CHECK( std::abs( disabled.red() - disabled.blue() ) < 20 );
}

TEST_CASE( "Checked toolbar borders remain distinct from neutral hover borders", "[editor][gui][style][icons]" )
{
    registry_t r;
    editor_style_t style = r.Resolve();
    style.uiFont.setPointSizeF( 9.0 );
    QToolBar toolbar;
    toolbar.setStyleSheet( EditorStyle_BuildStyleSheet( style, Template() ) );
    auto *pButton = new QToolButton( &toolbar );
    pButton->setCheckable( true );
    pButton->setFixedSize( 40, 40 );
    toolbar.addWidget( pButton );
    pButton->ensurePolished();
    const auto border = [pButton]( bool checked, bool hover ) {
        QImage image( pButton->size(), QImage::Format_ARGB32_Premultiplied );
        image.fill( Qt::transparent );
        QPainter painter( &image );
        QStyleOptionToolButton option;
        option.initFrom( pButton );
        option.state = QStyle::State_Enabled | QStyle::State_Raised;
        option.state |= checked ? QStyle::State_On : QStyle::State_Off;
        if ( hover ) { option.state |= QStyle::State_MouseOver; }
        pButton->style()->drawComplexControl( QStyle::CC_ToolButton, &option, &painter, pButton );
        return image.pixelColor( 0, 20 );
    };
    CHECK( border( true, false ) == EditorStyle_TokenColor( style, "ui.accent" ) ); // Hammer's accent rim.
    CHECK( border( false, false ) == EditorStyle_TokenColor( style, "ui.border" ) );
    CHECK( border( true, true ) == EditorStyle_TokenColor( style, "ui.accent.light" ) );
    CHECK( border( false, true ) == EditorStyle_TokenColor( style, "ui.border.highlight" ) );
}

TEST_CASE( "Toolbar breathing room follows theme padding without enlarging the font", "[editor][gui][style]" )
{
    registry_t r;
    editor_style_t style = r.Resolve();
    style.uiFont.setPointSizeF( 9.0 );
    style.density = 1.0;
    style.metrics.insert( QStringLiteral( "ui.padding" ), 8.0 );
    const QString sheet = EditorStyle_BuildStyleSheet( style,
        QStringLiteral( "QToolBar QToolButton { padding: @TOOLBAR_PADDING@px; } QWidget#EditorToolPalette QToolButton { padding: @TOOL_PALETTE_PADDING@px; }" ) );
    CHECK( sheet.contains( QStringLiteral( "padding: 6px" ) ) );
    CHECK( sheet.contains( QStringLiteral( "padding: 4px" ) ) );
    CHECK( style.uiFont.pointSizeF() == 9.0 );
}

TEST_CASE( "Split menu buttons paint their arrow half with the themed surface", "[editor][gui][style][menus]" )
{
    registry_t r;
    editor_style_t style = r.Resolve();
    style.uiFont.setPointSizeF( 9.0 );
    QToolBar toolbar;
    toolbar.setStyleSheet( EditorStyle_BuildStyleSheet( style, Template() ) );
    auto *pButton = new QToolButton( &toolbar );
    pButton->setCheckable( true );
    pButton->setMenu( new QMenu( pButton ) );
    pButton->setPopupMode( QToolButton::MenuButtonPopup );
    pButton->setFixedSize( 48, 36 );
    toolbar.addWidget( pButton );
    pButton->ensurePolished();
    for ( const bool checked : { false, true } ) {
        pButton->setChecked( checked );
        QImage image( pButton->size(), QImage::Format_ARGB32_Premultiplied );
        image.fill( Qt::transparent );
        QPainter painter( &image );
        QStyleOptionToolButton option;
        option.initFrom( pButton );
        option.state = QStyle::State_Enabled | QStyle::State_Raised |
                       ( pButton->isChecked() ? QStyle::State_On : QStyle::State_Off );
        option.features = QStyleOptionToolButton::MenuButtonPopup | QStyleOptionToolButton::HasMenu;
        const QRect menuHalf = pButton->style()->subControlRect( QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, pButton );
        REQUIRE( menuHalf.width() >= 10 );
        pButton->style()->drawComplexControl( QStyle::CC_ToolButton, &option, &painter, pButton );
        const QColor surface = image.pixelColor( menuHalf.center().x(), menuHalf.top() + 3 );
        const QColor border = image.pixelColor( 0, image.height() / 2 );
        INFO( "checked=" << pButton->isChecked() << " menu surface=" << surface.name( QColor::HexArgb ).toStdString()
              << " outer border=" << border.name( QColor::HexArgb ).toStdString() );
        // Qt excludes State_On from inactive menu subcontrols: the options half
        // stays neutral, and the outer rim carries the state (Hammer).
        CHECK( surface == EditorStyle_TokenColor( style, "ui.chrome" ) );
        CHECK( border == EditorStyle_TokenColor( style, pButton->isChecked() ? "ui.accent" : "ui.border" ) );
    }
}

TEST_CASE( "Disabled toolbar controls never regain checked or hover styling", "[editor][gui][style][icons]" )
{
    registry_t r;
    const QString sheet = EditorStyle_BuildStyleSheet( r.Resolve(), Template() );
    for ( const QString &name : { QStringLiteral( "masonMainToolBar" ), QStringLiteral( "masonSelectModes" ),
                                  QStringLiteral( "EditorToolStrip" ) } ) {
        INFO( name.toStdString() );
        QToolBar toolbar;
        toolbar.setObjectName( name );
        toolbar.setStyleSheet( sheet );
        auto *pButton = new QToolButton( &toolbar );
        pButton->setProperty( "authoringTool", true );
        pButton->setCheckable( true );
        pButton->setChecked( true );
        pButton->setEnabled( false );
        pButton->setFixedSize( 36, 36 );
        toolbar.addWidget( pButton );
        toolbar.show();
        QApplication::processEvents();
        const auto surface = [pButton]( bool hover ) {
            QImage image( pButton->size(), QImage::Format_ARGB32_Premultiplied );
            image.fill( Qt::transparent );
            QPainter painter( &image );
            QStyleOptionToolButton option;
            option.initFrom( pButton );
            option.state = QStyle::State_Raised | ( pButton->isChecked() ? QStyle::State_On : QStyle::State_Off );
            if ( hover ) { option.state |= QStyle::State_MouseOver; }
            // Explicit hover makes this independent of the OS cursor location
            // and exercises the exact QSS state used during button painting.
            pButton->style()->drawComplexControl( QStyle::CC_ToolButton, &option, &painter, pButton );
            return image;
        };
        for ( const bool checked : { false, true } ) {
            pButton->setChecked( checked );
            INFO( "checked=" << checked );
            const QImage disabled = surface( false );
            REQUIRE( disabled.pixelColor( 18, 18 ).alpha() == 255 );
            CHECK( surface( true ) == disabled ); // Check the fill and border together.
        }
    }
}

TEST_CASE( "Text icons resolve SVG fallback fonts before parsing and painting", "[editor][gui][style][icons]" )
{
    EditorGui_RegisterResources();
    registry_t r;
    const editor_style_t style = r.Resolve();
    QPixmapCache::clear();
    svg_font_warning_capture_t warnings;
    for ( const char *name : { "view-top", "view-front", "view-side", "view-3d", "asset-font", "asset-shader", "help" } ) {
        INFO( name );
        const QIcon icon = EditorStyle_Icon( style, name );
        REQUIRE_FALSE( icon.isNull() );
        CHECK_FALSE( icon.pixmap( 32, QIcon::Normal, QIcon::Off ).isNull() );
    }
    CHECK( warnings.messages.isEmpty() );
}

TEST_CASE( "Compact popup menus retain check marks and submenu arrows", "[editor][gui][style][menus]" )
{
    registry_t r;
    QMenu menu;
    menu.setStyleSheet( EditorStyle_BuildStyleSheet( r.Resolve(), Template() ) );
    menu.ensurePolished();
    const auto raster = [&menu]( bool checked, bool submenu, bool enabled = true, bool selected = false ) {
        QImage image( 180, 26, QImage::Format_ARGB32_Premultiplied );
        image.fill( Qt::transparent );
        QPainter painter( &image );
        QStyleOptionMenuItem option;
        option.initFrom( &menu );
        option.rect = image.rect();
        option.menuRect = image.rect();
        option.state = enabled ? QStyle::State_Enabled : QStyle::State_None;
        if ( selected ) { option.state |= QStyle::State_Selected; }
        option.menuItemType = submenu ? QStyleOptionMenuItem::SubMenu : QStyleOptionMenuItem::Normal;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.menuHasCheckableItems = true;
        option.checked = checked;
        option.text = QStringLiteral( "Grid" );
        menu.style()->drawControl( QStyle::CE_MenuItem, &option, &painter, &menu );
        return image;
    };
    const QImage normal = raster( false, false );
    const QImage checked = raster( true, false );
    const QImage submenu = raster( false, true );
    CHECK( normal.copy( 0, 0, 26, 26 ) != checked.copy( 0, 0, 26, 26 ) );
    CHECK( normal.copy( 154, 0, 26, 26 ) != submenu.copy( 154, 0, 26, 26 ) );
    CHECK( raster( true, false, false, false ) == raster( true, false, false, true ) );
}
