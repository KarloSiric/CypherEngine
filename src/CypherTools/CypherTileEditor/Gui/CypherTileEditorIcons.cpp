//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Offline, high-DPI Tabler SVG icons with consistent visual states.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorIcons.h"
#include <QColor>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QWidget>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QResource>
#include <QSvgRenderer>

static void EnsureTileEditorResourcesRegistered()
{
    static const bool registered = [] { Q_INIT_RESOURCE( CypherTileEditorResources ); return true; }();
    (void)registered;
}
namespace cypher::tools::tile_editor
{
namespace {
struct icon_source_t { QString name; tile_editor_icon_tone_t tone; };
QHash<qint64, icon_source_t> iconSources;
QStringView IconResourceName( tile_editor_icon_t icon )
{
    switch ( icon ) {
        case tile_editor_icon_t::NEW_MAP:   return u"file-plus";
        case tile_editor_icon_t::OPEN:      return u"folder-open";
        case tile_editor_icon_t::SAVE:      return u"device-floppy";
        case tile_editor_icon_t::UNDO:      return u"arrow-back-up";
        case tile_editor_icon_t::REDO:      return u"arrow-forward-up";
        case tile_editor_icon_t::SELECT:    return u"editor-select";
        case tile_editor_icon_t::PAINT:     return u"brush";
        case tile_editor_icon_t::ERASE:     return u"eraser";
        case tile_editor_icon_t::RECTANGLE: return u"rectangle";
        case tile_editor_icon_t::STAMP:     return u"rubber-stamp";
        case tile_editor_icon_t::SPAWN:     return u"map-pin-plus";
        case tile_editor_icon_t::DOOR:      return u"door";
        case tile_editor_icon_t::MATERIAL:  return u"texture";
        case tile_editor_icon_t::GRID:      return u"grid-3x3";
        case tile_editor_icon_t::VALIDATE:  return u"validate-map";
        case tile_editor_icon_t::BUILD:     return u"build-map";
        case tile_editor_icon_t::PREVIEW:   return u"perspective";
        case tile_editor_icon_t::FIT_VIEW:  return u"focus-centered";
        case tile_editor_icon_t::SETTINGS:  return u"adjustments-horizontal";
        case tile_editor_icon_t::SEARCH:    return u"search";
        case tile_editor_icon_t::CONSOLE:   return u"terminal-2";
        case tile_editor_icon_t::PAN: return u"hand-move";
        case tile_editor_icon_t::EYEDROPPER: return u"color-picker";
        case tile_editor_icon_t::LINE: return u"line";
        case tile_editor_icon_t::FILL: return u"bucket";
        case tile_editor_icon_t::FOUR_VIEWS: return u"layout-grid";
        case tile_editor_icon_t::MATERIAL_LIBRARY: return u"material-library";
        case tile_editor_icon_t::STOP: return u"stop-map";
        case tile_editor_icon_t::PLAY: return u"run-map";
    }
    return {};
}

QColor ToneColor( tile_editor_icon_tone_t tone )
{
    const bool light = qApp && qApp->palette().color( QPalette::Button ).lightness() > 150;
    switch ( tone ) {
        case tile_editor_icon_tone_t::BLUE: return QColor( light ? "#236d9f" : "#74b9ec" );
        case tile_editor_icon_tone_t::TEAL: return QColor( light ? "#366f69" : "#a9c3be" );
        case tile_editor_icon_tone_t::GOLD: return QColor( light ? "#a06a18" : "#efb65c" );
        case tile_editor_icon_tone_t::GREEN: return QColor( light ? "#207344" : "#68cd82" );
        case tile_editor_icon_tone_t::ORANGE: return QColor( light ? "#8c5b36" : "#c8b3a3" );
        case tile_editor_icon_tone_t::RED: return QColor( light ? "#a23d36" : "#ef756d" );
        default: return qApp ? qApp->palette().color( QPalette::ButtonText ) : QColor( "#b8b8b8" );
    }
}
}
QIcon CypherTileEditorIcon_Create( QStringView name, tile_editor_icon_tone_t tone )
{
    EnsureTileEditorResourcesRegistered();
    const QString key = name.toString() + QString::number( static_cast<int>( tone ) );
    static QHash<QString, QIcon> cache;
    static QColor previousAccent;
    static QPalette previousPalette;
    QColor accent = qApp != nullptr ? qApp->property( "TileEditorAccentColor" ).value<QColor>() : QColor();
    if ( !accent.isValid() ) accent = QColor( "#e3a63d" );
    const auto palette = qApp ? qApp->palette() : QPalette();
    if ( accent != previousAccent || palette != previousPalette ) {
        cache.clear(); previousAccent = accent; previousPalette = palette;
    }
    if ( cache.contains( key ) ) return cache.value( key );
    QFile file( QStringLiteral( ":/cypher/tile-editor/icons/%1.svg" ).arg( name ) );
    if ( !file.open( QIODevice::ReadOnly ) ) return {};
    const QByteArray source = file.readAll();
    QIcon icon;
    for ( QIcon::Mode mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled } ) {
        for ( QIcon::State state : { QIcon::Off, QIcon::On } ) {
            QColor color = ToneColor( tone );
            if ( mode == QIcon::Disabled ) color = palette.color( QPalette::Disabled, QPalette::ButtonText );
            else if ( state == QIcon::On || mode == QIcon::Selected ) color = accent;
            else if ( mode == QIcon::Active ) color = palette.color( QPalette::Button ).lightness() > 150
                ? color.darker( 120 ) : color.lighter( 120 );
            QByteArray svg = source;
            svg.replace( "currentColor", color.name().toUtf8() );
            QSvgRenderer renderer( svg );
            if ( !renderer.isValid() ) return {};
            for ( int size : { 16, 18, 20, 22, 24, 32 } ) {
                for ( int ratio : { 1, 2 } ) {
                    QPixmap pixmap( size * ratio, size * ratio );
                    pixmap.setDevicePixelRatio( ratio );
                    pixmap.fill( Qt::transparent );
                    QPainter painter( &pixmap );
                    renderer.render( &painter, QRectF( 0, 0, size, size ) );
                    painter.end();
                    icon.addPixmap( pixmap, mode, state );
                }
            }
        }
    }
    cache.insert( key, icon );
    iconSources.insert( icon.cacheKey(), { name.toString(), tone } );
    return icon;
}
void CypherTileEditorIcons_Refresh( QWidget *root )
{
    if ( root == nullptr ) return;
    const auto refreshed = []( const QIcon &icon ) {
        const auto found = iconSources.constFind( icon.cacheKey() );
        if ( found == iconSources.cend() ) return icon;
        const auto source = *found;
        return CypherTileEditorIcon_Create( source.name, source.tone );
    };
    for ( auto *action : root->findChildren<QAction *>() ) action->setIcon( refreshed( action->icon() ) );
    for ( auto *button : root->findChildren<QAbstractButton *>() ) button->setIcon( refreshed( button->icon() ) );
}
QIcon CypherTileEditorIcon_Create( tile_editor_icon_t icon )
{
    const auto tone = icon == tile_editor_icon_t::VALIDATE ? tile_editor_icon_tone_t::BLUE
        : icon == tile_editor_icon_t::BUILD ? tile_editor_icon_tone_t::GOLD
        : icon == tile_editor_icon_t::PLAY ? tile_editor_icon_tone_t::GREEN
        : icon == tile_editor_icon_t::STOP ? tile_editor_icon_tone_t::RED
        : tile_editor_icon_tone_t::STEEL;
    return CypherTileEditorIcon_Create( IconResourceName( icon ), tone );
}
}
