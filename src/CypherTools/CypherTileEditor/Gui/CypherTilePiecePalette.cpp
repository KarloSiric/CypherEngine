//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Real floor footprints and explicit stair/door/boundary presets.
//////////////////////////////////////////////////////////////////////////
#include "CypherTilePiecePalette.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
namespace cypher::tools::tile_editor
{
QList<tile_piece_t> TileEditorPieces_Definitions()
{
    QList<tile_piece_t> pieces{
        { "room.small", QObject::tr( "Small room" ), tile_piece_kind_t::ROOM, { 6, 6 } },
        { "room.medium", QObject::tr( "Medium room" ), tile_piece_kind_t::ROOM, { 10, 8 } },
        { "room.large", QObject::tr( "Large room" ), tile_piece_kind_t::ROOM, { 16, 12 } },
        { "corridor.horizontal", QObject::tr( "Corridor E–W" ), tile_piece_kind_t::ROOM, { 12, 3 } },
        { "corridor.vertical", QObject::tr( "Corridor N–S" ), tile_piece_kind_t::ROOM, { 3, 12 } } };
    const QStringList directions{ QObject::tr( "North" ), QObject::tr( "East" ), QObject::tr( "South" ),
                                  QObject::tr( "West" ) };
    for ( int i = 0; i < 4; ++i )
        pieces.append( { QString( "corner.%1" ).arg( i ),
                         QObject::tr( "Corner %1°" ).arg( i * 90 ),
                         tile_piece_kind_t::CORNER,
                         { 6, 6 },
                         i } );
    for ( int i = 0; i < 4; ++i )
        pieces.append( { QString( "junction.%1" ).arg( i ),
                         QObject::tr( "Junction %1°" ).arg( i * 90 ),
                         tile_piece_kind_t::T_JUNCTION,
                         { 6, 6 },
                         i } );
    pieces.append(
        { "junction.cross", QObject::tr( "Cross junction" ), tile_piece_kind_t::CROSS, { 6, 6 } } );
    for ( int i = 0; i < 4; ++i )
        pieces.append( { QString( "stairs.%1" ).arg( i ),
                         QObject::tr( "Stairs %1" ).arg( directions[i] ),
                         tile_piece_kind_t::STAIRS,
                         { 1, 1 },
                         i } );
    for ( int i = 0; i < 4; ++i )
        pieces.append( { QString( "door.%1" ).arg( i ),
                         QObject::tr( "Door %1" ).arg( directions[i] ),
                         tile_piece_kind_t::DOOR,
                         { 1, 1 },
                         i } );
    pieces.append( { "boundary.low",
                     QObject::tr( "Boundary · 1 level" ),
                     tile_piece_kind_t::BOUNDARY,
                     { 1, 1 },
                     0,
                     1 } );
    pieces.append( { "boundary.high",
                     QObject::tr( "Boundary · 2 levels" ),
                     tile_piece_kind_t::BOUNDARY,
                     { 1, 1 },
                     0,
                     2 } );
    return pieces;
}
QList<QPoint> TileEditorPiece_Cells( const tile_piece_t &piece )
{
    QList<QPoint> cells;
    if ( piece.size.width() <= 0 || piece.size.height() <= 0 || piece.size.width() > 64 ||
         piece.size.height() > 64 )
        return cells;
    if ( piece.kind == tile_piece_kind_t::STAIRS || piece.kind == tile_piece_kind_t::DOOR ||
         piece.kind == tile_piece_kind_t::BOUNDARY )
        return cells;
    for ( int y = 0; y < piece.size.height(); ++y )
        for ( int x = 0; x < piece.size.width(); ++x )
        {
            bool include = true;
            if ( piece.kind == tile_piece_kind_t::CORNER )
                include = x < 2 || y >= piece.size.height() - 2;
            if ( piece.kind == tile_piece_kind_t::T_JUNCTION )
                include = y < 2 || ( x >= 2 && x < 4 );
            if ( piece.kind == tile_piece_kind_t::CROSS )
                include = ( x >= 2 && x < 4 ) || ( y >= 2 && y < 4 );
            if ( !include )
                continue;
            QPoint p( x, y );
            if ( piece.size.width() == piece.size.height() )
                for ( int i = 0; i < ( piece.orientation % 4 + 4 ) % 4; ++i )
                    p = { piece.size.width() - 1 - p.y(), p.x() };
            cells.append( p );
        }
    return cells;
}
bool TileEditorPiece_Stamp( CypherTileDocumentBridge &bridge, const tile_piece_t &piece,
                            tile_map_grid_coord_t center, tile_map_paint_t paint, QString &error )
{
    const auto cells = TileEditorPiece_Cells( piece );
    if ( !bridge.isInitialized() || cells.isEmpty() )
    {
        error = QObject::tr( "Choose a room or corridor footprint." );
        return false;
    }
    const auto *doc = bridge.document();
    const qint64 left = static_cast<qint64>( center.x ) - piece.size.width() / 2;
    const qint64 top = static_cast<qint64>( center.y ) - piece.size.height() / 2;
    if ( left < 0 || top < 0 || left + piece.size.width() > doc->nWidth ||
         top + piece.size.height() > doc->nHeight )
    {
        error = QObject::tr( "This footprint extends outside the map. Select a cell farther from its edge." );
        return false;
    }
    // A room footprint always authors flat floors; it must not inherit a stair brush.
    paint.shape = tile_map_cell_shape_t::FLAT;
    if ( !bridge.beginEdit( piece.label, &error ) )
        return false;
    for ( const auto &cell : cells )
    {
        if ( !bridge.paintCell( { static_cast<i32>( left + cell.x() ), static_cast<i32>( top + cell.y() ) },
                                paint, &error ) )
        {
            bridge.cancelEdit();
            return false;
        }
    }
    if ( !bridge.commitEdit( &error ) )
    {
        bridge.cancelEdit();
        return false;
    }
    return true;
}
namespace
{
QIcon PieceIcon( const tile_piece_t &piece )
{
    QPixmap image( 176, 112 );
    image.setDevicePixelRatio( 2 );
    image.fill( QColor( "#171e24" ) );
    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing );
    const auto cells = TileEditorPiece_Cells( piece );
    if ( !cells.isEmpty() )
    {
        const qreal scale = std::min( 72.0 / piece.size.width(), 44.0 / piece.size.height() );
        const QPointF start( ( 88 - piece.size.width() * scale ) / 2,
                             ( 56 - piece.size.height() * scale ) / 2 );
        painter.setPen( QPen( QColor( "#476675" ), 0.65 ) );
        painter.setBrush( QColor( "#7195a3" ) );
        for ( const auto &p : cells )
            painter.drawRect(
                QRectF( start + QPointF( p.x() * scale, p.y() * scale ), QSizeF( scale, scale ) ) );
        painter.setPen( QPen( QColor( "#bedee4" ), 1.4 ) );
        for ( const auto &p : cells )
        {
            const QRectF r( start + QPointF( p.x() * scale, p.y() * scale ), QSizeF( scale, scale ) );
            if ( !cells.contains( p + QPoint( -1, 0 ) ) )
                painter.drawLine( r.topLeft(), r.bottomLeft() );
            if ( !cells.contains( p + QPoint( 1, 0 ) ) )
                painter.drawLine( r.topRight(), r.bottomRight() );
            if ( !cells.contains( p + QPoint( 0, -1 ) ) )
                painter.drawLine( r.topLeft(), r.topRight() );
            if ( !cells.contains( p + QPoint( 0, 1 ) ) )
                painter.drawLine( r.bottomLeft(), r.bottomRight() );
        }
    }
    else
    {
        painter.translate( 44, 28 );
        painter.rotate( piece.orientation * 90 );
        painter.setPen( QPen( QColor( "#a6bac7" ), 1.5 ) );
        painter.setBrush( QColor( "#31434e" ) );
        painter.drawRect( QRectF( -19, -19, 38, 38 ) );
        if ( piece.kind == tile_piece_kind_t::STAIRS )
        {
            painter.setPen( QPen( QColor( "#b9cdd8" ), 1 ) );
            for ( int y = -14; y <= 14; y += 6 )
                painter.drawLine( QPointF( -17, y ), QPointF( 17, y ) );
            painter.setPen( QPen( QColor( "#e3a63d" ), 2 ) );
            painter.drawLine( QPointF( 0, 14 ), QPointF( 0, -13 ) );
            painter.drawLine( QPointF( 0, -13 ), QPointF( -5, -7 ) );
            painter.drawLine( QPointF( 0, -13 ), QPointF( 5, -7 ) );
        }
        else if ( piece.kind == tile_piece_kind_t::DOOR )
        {
            painter.setPen( QPen( QColor( "#67c6e2" ), 4 ) );
            painter.drawLine( QPointF( -10, -19 ), QPointF( 10, -19 ) );
            painter.setPen( QPen( QColor( "#67c6e2" ), 1.3, Qt::DashLine ) );
            painter.drawArc( QRectF( -10, -39, 40, 40 ), 270 * 16, -90 * 16 );
        }
        else if ( piece.wallLevels > 0 )
        {
            painter.setPen( QPen( QColor( "#bedee4" ), piece.wallLevels == 2 ? 5 : 3 ) );
            painter.drawLine( QPointF( -19, -19 ), QPointF( 19, -19 ) );
            painter.drawLine( QPointF( 19, -19 ), QPointF( 19, 19 ) );
        }
    }
    return QIcon( image );
}
} // namespace
CypherTilePiecePalette::CypherTilePiecePalette( QWidget *parent ) : QWidget( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 4 );
    auto *filter = new QLineEdit( this );
    filter->setObjectName( "TilePieceFilter" );
    filter->setPlaceholderText( tr( "Filter rooms, corridors, stairs, doors…" ) );
    filter->setClearButtonEnabled( true );
    layout->addWidget( filter );
    auto *list = new QListWidget( this );
    list->setObjectName( "TileStampPalette" );
    list->setViewMode( QListView::IconMode );
    list->setResizeMode( QListView::Adjust );
    list->setMovement( QListView::Static );
    list->setIconSize( { 88, 56 } );
    list->setGridSize( { 132, 94 } );
    list->setMinimumHeight( 100 );
    list->setWordWrap( true );
    layout->addWidget( list, 1 );
    const auto pieces = TileEditorPieces_Definitions();
    for ( int i = 0; i < pieces.size(); ++i )
    {
        const auto &piece = pieces[i];
        auto *item = new QListWidgetItem( PieceIcon( piece ), piece.label, list );
        item->setData( Qt::UserRole, i );
        item->setData( Qt::UserRole + 1, piece.id );
        const bool brush = TileEditorPiece_Cells( piece ).isEmpty();
        item->setToolTip( brush ? tr( "Click to select this oriented placement tool, then place in Top." )
                                : tr( "%1 × %2 cells. Double-click to stamp around the selected cell as one "
                                      "undoable edit. Boundary walls follow the footprint." )
                                      .arg( piece.size.width() )
                                      .arg( piece.size.height() ) );
    }
    connect( list, &QListWidget::itemClicked, this, [this, pieces]( QListWidgetItem *item ) {
        const auto &piece = pieces[item->data( Qt::UserRole ).toInt()];
        if ( TileEditorPiece_Cells( piece ).isEmpty() && m_activate )
            m_activate( piece );
    } );
    connect( list, &QListWidget::itemActivated, this, [this, pieces]( QListWidgetItem *item ) {
        if ( !item->isHidden() && m_activate )
            m_activate( pieces[item->data( Qt::UserRole ).toInt()] );
    } );
    connect( filter, &QLineEdit::textChanged, this, [list]( const QString &text ) {
        for ( int i = 0; i < list->count(); ++i )
            list->item( i )->setHidden( !list->item( i )->text().contains( text, Qt::CaseInsensitive ) );
        if ( list->currentItem() && list->currentItem()->isHidden() )
        {
            list->clearSelection();
            list->setCurrentRow( -1 );
        }
    } );
    auto *hint = new QLabel( tr( "Double-click a footprint at the selected cell. Click stairs, doors, or "
                                 "boundary presets to choose a placement tool." ),
                             this );
    hint->setWordWrap( true );
    hint->setProperty( "muted", true );
    layout->addWidget( hint );
}
void CypherTilePiecePalette::setActivateCallback( std::function<void( const tile_piece_t & )> callback )
{
    m_activate = std::move( callback );
}
} // namespace cypher::tools::tile_editor
