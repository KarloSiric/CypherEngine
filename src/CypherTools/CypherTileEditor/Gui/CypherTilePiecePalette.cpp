//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Parameterized floor footprints and explicit placement presets.
//////////////////////////////////////////////////////////////////////////
#include "CypherTilePiecePalette.h"
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <utility>
namespace cypher::tools::tile_editor
{
namespace
{
int NormalizedTurns( int orientation )
{
    return ( orientation % 4 + 4 ) % 4;
}

bool HasFloorFootprint( tile_piece_kind_t kind )
{
    return kind != tile_piece_kind_t::STAIRS && kind != tile_piece_kind_t::DOOR &&
           kind != tile_piece_kind_t::BOUNDARY;
}

bool UsesPassageWidth( tile_piece_kind_t kind )
{
    return kind == tile_piece_kind_t::CORNER || kind == tile_piece_kind_t::T_JUNCTION ||
           kind == tile_piece_kind_t::CROSS || kind == tile_piece_kind_t::COURTYARD ||
           kind == tile_piece_kind_t::U_SHAPE;
}

void AppendOriented( QList<tile_piece_t> &pieces, const QString &idPrefix,
                     const QString &labelPrefix, tile_piece_kind_t kind, QSize size,
                     int passageWidth )
{
    for ( int orientation = 0; orientation < 4; ++orientation )
        pieces.append( { QStringLiteral( "%1.%2" ).arg( idPrefix ).arg( orientation ),
                         QObject::tr( "%1 %2°" ).arg( labelPrefix ).arg( orientation * 90 ),
                         kind,
                         size,
                         orientation,
                         1,
                         passageWidth } );
}
}

QList<tile_piece_t> TileEditorPieces_Definitions()
{
    QList<tile_piece_t> pieces{
        { "room.tiny", QObject::tr( "Tiny room" ), tile_piece_kind_t::ROOM, { 4, 4 } },
        { "room.small", QObject::tr( "Small room" ), tile_piece_kind_t::ROOM, { 6, 6 } },
        { "room.medium", QObject::tr( "Medium room" ), tile_piece_kind_t::ROOM, { 10, 8 } },
        { "room.large", QObject::tr( "Large room" ), tile_piece_kind_t::ROOM, { 16, 12 } },
        { "room.hall", QObject::tr( "Great hall" ), tile_piece_kind_t::ROOM, { 20, 16 } },
        { "corridor.horizontal", QObject::tr( "Corridor E–W" ), tile_piece_kind_t::CORRIDOR, { 12, 3 } },
        { "corridor.vertical", QObject::tr( "Corridor N–S" ), tile_piece_kind_t::CORRIDOR, { 3, 12 } },
        { "corridor.long.horizontal", QObject::tr( "Long corridor E–W" ),
          tile_piece_kind_t::CORRIDOR, { 20, 3 } },
        { "corridor.long.vertical", QObject::tr( "Long corridor N–S" ),
          tile_piece_kind_t::CORRIDOR, { 3, 20 } },
        { "corridor.wide.horizontal", QObject::tr( "Wide corridor E–W" ),
          tile_piece_kind_t::CORRIDOR, { 16, 5 } },
        { "corridor.wide.vertical", QObject::tr( "Wide corridor N–S" ),
          tile_piece_kind_t::CORRIDOR, { 5, 16 } } };

    AppendOriented( pieces, QStringLiteral( "corner" ), QObject::tr( "Corner" ),
                    tile_piece_kind_t::CORNER, { 6, 6 }, 2 );
    AppendOriented( pieces, QStringLiteral( "corner.wide" ), QObject::tr( "Wide corner" ),
                    tile_piece_kind_t::CORNER, { 9, 9 }, 3 );
    AppendOriented( pieces, QStringLiteral( "junction" ), QObject::tr( "Junction" ),
                    tile_piece_kind_t::T_JUNCTION, { 6, 6 }, 2 );
    AppendOriented( pieces, QStringLiteral( "junction.wide" ), QObject::tr( "Wide junction" ),
                    tile_piece_kind_t::T_JUNCTION, { 9, 9 }, 3 );

    pieces.append(
        { "junction.cross", QObject::tr( "Cross junction" ), tile_piece_kind_t::CROSS, { 6, 6 } } );
    pieces.append( { "junction.cross.wide", QObject::tr( "Wide cross junction" ),
                     tile_piece_kind_t::CROSS, { 9, 9 }, 0, 1, 3 } );
    pieces.append( { "courtyard.small", QObject::tr( "Small courtyard ring" ),
                     tile_piece_kind_t::COURTYARD, { 8, 8 }, 0, 1, 2 } );
    pieces.append( { "courtyard.large", QObject::tr( "Large courtyard ring" ),
                     tile_piece_kind_t::COURTYARD, { 14, 10 }, 0, 1, 2 } );
    AppendOriented( pieces, QStringLiteral( "ushape" ), QObject::tr( "U-shaped room" ),
                    tile_piece_kind_t::U_SHAPE, { 8, 8 }, 2 );

    const QStringList directions{ QObject::tr( "North" ), QObject::tr( "East" ), QObject::tr( "South" ),
                                  QObject::tr( "West" ) };
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

QSize TileEditorPiece_FootprintSize( const tile_piece_t &piece )
{
    if ( piece.size.width() <= 0 || piece.size.height() <= 0 || piece.size.width() > 64 ||
         piece.size.height() > 64 )
        return {};
    if ( NormalizedTurns( piece.orientation ) % 2 != 0 )
        return { piece.size.height(), piece.size.width() };
    return piece.size;
}

QList<QPoint> TileEditorPiece_Cells( const tile_piece_t &piece )
{
    QList<QPoint> cells;
    if ( TileEditorPiece_FootprintSize( piece ).isEmpty() || !HasFloorFootprint( piece.kind ) )
        return cells;
    const int width = piece.size.width();
    const int height = piece.size.height();
    const int passage = piece.passageWidth;
    if ( UsesPassageWidth( piece.kind ) &&
         ( passage <= 0 || passage > std::min( width, height ) ) )
        return cells;
    if ( piece.kind == tile_piece_kind_t::COURTYARD &&
         ( passage * 2 >= width || passage * 2 >= height ) )
        return cells;

    const int centeredX = ( width - passage ) / 2;
    const int centeredY = ( height - passage ) / 2;
    const int turns = NormalizedTurns( piece.orientation );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            bool include = true;
            if ( piece.kind == tile_piece_kind_t::CORNER )
                include = x < passage || y >= height - passage;
            if ( piece.kind == tile_piece_kind_t::T_JUNCTION )
                include = y < passage || ( x >= centeredX && x < centeredX + passage );
            if ( piece.kind == tile_piece_kind_t::CROSS )
                include = ( x >= centeredX && x < centeredX + passage ) ||
                          ( y >= centeredY && y < centeredY + passage );
            if ( piece.kind == tile_piece_kind_t::COURTYARD )
                include = x < passage || x >= width - passage || y < passage ||
                          y >= height - passage;
            if ( piece.kind == tile_piece_kind_t::U_SHAPE )
                include = x < passage || x >= width - passage || y >= height - passage;
            if ( !include )
                continue;
            QPoint p( x, y );
            int rotatedWidth = width;
            int rotatedHeight = height;
            for ( int i = 0; i < turns; ++i )
            {
                p = { rotatedHeight - 1 - p.y(), p.x() };
                std::swap( rotatedWidth, rotatedHeight );
            }
            cells.append( p );
        }
    return cells;
}

QList<tile_map_grid_coord_t> TileEditorPiece_PlacedCells( const tile_piece_t &piece,
                                                          tile_map_grid_coord_t center )
{
    QList<tile_map_grid_coord_t> placed;
    const auto footprint = TileEditorPiece_Cells( piece );
    const QSize footprintSize = TileEditorPiece_FootprintSize( piece );
    if ( footprint.isEmpty() || footprintSize.isEmpty() ) return placed;

    const qint64 left = static_cast<qint64>( center.x ) - footprintSize.width() / 2;
    const qint64 top = static_cast<qint64>( center.y ) - footprintSize.height() / 2;
    placed.reserve( footprint.size() );
    for ( const QPoint &cell : footprint )
    {
        const qint64 x = left + cell.x();
        const qint64 y = top + cell.y();
        if ( x < std::numeric_limits<i32>::min() || x > std::numeric_limits<i32>::max() ||
             y < std::numeric_limits<i32>::min() || y > std::numeric_limits<i32>::max() )
            return {};
        placed.append( { static_cast<i32>( x ), static_cast<i32>( y ) } );
    }
    return placed;
}

bool TileEditorPiece_Stamp( CypherTileDocumentBridge &bridge, const tile_piece_t &piece,
                            tile_map_grid_coord_t center, tile_map_paint_t paint, QString &error )
{
    const auto cells = TileEditorPiece_Cells( piece );
    const QSize footprintSize = TileEditorPiece_FootprintSize( piece );
    if ( !bridge.isInitialized() || cells.isEmpty() )
    {
        error = QObject::tr( "Choose a room or corridor footprint." );
        return false;
    }
    const auto *doc = bridge.document();
    const qint64 left = static_cast<qint64>( center.x ) - footprintSize.width() / 2;
    const qint64 top = static_cast<qint64>( center.y ) - footprintSize.height() / 2;
    if ( left < 0 || top < 0 || left + footprintSize.width() > doc->nWidth ||
         top + footprintSize.height() > doc->nHeight )
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
        const QSize footprintSize = TileEditorPiece_FootprintSize( piece );
        const qreal scale = std::min( 72.0 / footprintSize.width(), 44.0 / footprintSize.height() );
        const QPointF start( ( 88 - footprintSize.width() * scale ) / 2,
                             ( 56 - footprintSize.height() * scale ) / 2 );
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

QString PieceKindToken( tile_piece_kind_t kind )
{
    switch ( kind )
    {
        case tile_piece_kind_t::ROOM: return QStringLiteral( "room" );
        case tile_piece_kind_t::CORRIDOR: return QStringLiteral( "corridor" );
        case tile_piece_kind_t::CORNER: return QStringLiteral( "corner" );
        case tile_piece_kind_t::T_JUNCTION: return QStringLiteral( "t-junction" );
        case tile_piece_kind_t::CROSS: return QStringLiteral( "cross" );
        case tile_piece_kind_t::COURTYARD: return QStringLiteral( "courtyard" );
        case tile_piece_kind_t::U_SHAPE: return QStringLiteral( "u-shape" );
        case tile_piece_kind_t::STAIRS: return QStringLiteral( "stairs" );
        case tile_piece_kind_t::DOOR: return QStringLiteral( "door" );
        case tile_piece_kind_t::BOUNDARY: return QStringLiteral( "boundary" );
    }
    return QStringLiteral( "piece" );
}

QString PieceKindLabel( tile_piece_kind_t kind )
{
    switch ( kind )
    {
        case tile_piece_kind_t::ROOM: return QObject::tr( "Room" );
        case tile_piece_kind_t::CORRIDOR: return QObject::tr( "Corridor" );
        case tile_piece_kind_t::CORNER: return QObject::tr( "L corner" );
        case tile_piece_kind_t::T_JUNCTION: return QObject::tr( "T junction" );
        case tile_piece_kind_t::CROSS: return QObject::tr( "Cross junction" );
        case tile_piece_kind_t::COURTYARD: return QObject::tr( "Courtyard ring" );
        case tile_piece_kind_t::U_SHAPE: return QObject::tr( "U-shaped room" );
        case tile_piece_kind_t::STAIRS: return QObject::tr( "Stairs" );
        case tile_piece_kind_t::DOOR: return QObject::tr( "Door" );
        case tile_piece_kind_t::BOUNDARY: return QObject::tr( "Boundary" );
    }
    return QObject::tr( "Piece" );
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

    auto *custom = new QGroupBox( tr( "Custom footprint" ), this );
    custom->setObjectName( "TilePieceCustomGroup" );
    auto *customLayout = new QGridLayout( custom );
    customLayout->setContentsMargins( 6, 6, 6, 6 );
    customLayout->setHorizontalSpacing( 5 );
    customLayout->setVerticalSpacing( 4 );
    auto *kind = new QComboBox( custom );
    kind->setObjectName( "TilePieceCustomType" );
    for ( const tile_piece_kind_t value : { tile_piece_kind_t::ROOM, tile_piece_kind_t::CORRIDOR,
                                            tile_piece_kind_t::CORNER,
                                            tile_piece_kind_t::T_JUNCTION,
                                            tile_piece_kind_t::CROSS,
                                            tile_piece_kind_t::COURTYARD,
                                            tile_piece_kind_t::U_SHAPE } )
        kind->addItem( PieceKindLabel( value ), static_cast<int>( value ) );
    auto makeDimension = [custom]( const char *name, int value ) {
        auto *field = new QSpinBox( custom );
        field->setObjectName( QString::fromLatin1( name ) );
        field->setRange( 1, 64 );
        field->setValue( value );
        return field;
    };
    auto *width = makeDimension( "TilePieceCustomWidth", 6 );
    auto *depth = makeDimension( "TilePieceCustomDepth", 6 );
    auto *passage = makeDimension( "TilePieceCustomPassage", 2 );
    auto *orientation = new QComboBox( custom );
    orientation->setObjectName( "TilePieceCustomOrientation" );
    for ( int turns = 0; turns < 4; ++turns )
        orientation->addItem( tr( "%1°" ).arg( turns * 90 ), turns );
    auto *activateCustom = new QPushButton( tr( "Use footprint" ), custom );
    activateCustom->setObjectName( "TilePieceCustomActivate" );
    activateCustom->setToolTip( tr( "Activate this footprint for repeated placement in the map views." ) );
    customLayout->addWidget( new QLabel( tr( "Type" ), custom ), 0, 0 );
    customLayout->addWidget( kind, 0, 1, 1, 3 );
    customLayout->addWidget( new QLabel( tr( "W" ), custom ), 1, 0 );
    customLayout->addWidget( width, 1, 1 );
    customLayout->addWidget( new QLabel( tr( "D" ), custom ), 1, 2 );
    customLayout->addWidget( depth, 1, 3 );
    customLayout->addWidget( new QLabel( tr( "Path" ), custom ), 2, 0 );
    customLayout->addWidget( passage, 2, 1 );
    customLayout->addWidget( new QLabel( tr( "Turn" ), custom ), 2, 2 );
    customLayout->addWidget( orientation, 2, 3 );
    customLayout->addWidget( activateCustom, 3, 0, 1, 4 );
    layout->addWidget( custom );

    auto updateCustomLimits = [kind, width, depth, passage, orientation, activateCustom]() {
        const auto selectedKind = static_cast<tile_piece_kind_t>( kind->currentData().toInt() );
        const bool usesPassage = UsesPassageWidth( selectedKind );
        passage->setEnabled( usesPassage );
        if ( usesPassage )
        {
            int maximum = std::min( width->value(), depth->value() );
            if ( selectedKind == tile_piece_kind_t::COURTYARD ) maximum = ( maximum - 1 ) / 2;
            passage->setMaximum( std::max( 1, maximum ) );
        }
        tile_piece_t candidate;
        candidate.kind = selectedKind;
        candidate.size = { width->value(), depth->value() };
        candidate.orientation = orientation->currentData().toInt();
        candidate.passageWidth = passage->value();
        activateCustom->setEnabled( !TileEditorPiece_Cells( candidate ).isEmpty() );
    };
    connect( kind, &QComboBox::currentIndexChanged, this,
             [updateCustomLimits]( int ) { updateCustomLimits(); } );
    connect( width, &QSpinBox::valueChanged, this,
             [updateCustomLimits]( int ) { updateCustomLimits(); } );
    connect( depth, &QSpinBox::valueChanged, this,
             [updateCustomLimits]( int ) { updateCustomLimits(); } );
    connect( passage, &QSpinBox::valueChanged, this,
             [updateCustomLimits]( int ) { updateCustomLimits(); } );
    connect( orientation, &QComboBox::currentIndexChanged, this,
             [updateCustomLimits]( int ) { updateCustomLimits(); } );
    updateCustomLimits();
    connect( activateCustom, &QPushButton::clicked, this,
             [this, kind, width, depth, passage, orientation]() {
        const auto selectedKind = static_cast<tile_piece_kind_t>( kind->currentData().toInt() );
        const int turns = orientation->currentData().toInt();
        tile_piece_t piece;
        piece.kind = selectedKind;
        piece.size = { width->value(), depth->value() };
        piece.orientation = turns;
        piece.passageWidth = passage->value();
        piece.id = QStringLiteral( "custom.%1.%2x%3.p%4.r%5" )
                       .arg( PieceKindToken( selectedKind ) )
                       .arg( piece.size.width() )
                       .arg( piece.size.height() )
                       .arg( piece.passageWidth )
                       .arg( turns );
        piece.label = tr( "Custom %1 · %2 × %3" )
                          .arg( PieceKindLabel( selectedKind ) )
                          .arg( piece.size.width() )
                          .arg( piece.size.height() );
        if ( m_activate ) m_activate( piece );
    } );

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
                                : tr( "%1 × %2 cells. Click to activate this footprint for repeated placement." )
                                      .arg( TileEditorPiece_FootprintSize( piece ).width() )
                                      .arg( TileEditorPiece_FootprintSize( piece ).height() ) );
    }
    connect( list, &QListWidget::itemClicked, this, [this, pieces]( QListWidgetItem *item ) {
        const auto &piece = pieces[item->data( Qt::UserRole ).toInt()];
        if ( m_activate ) m_activate( piece );
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
    auto *hint = new QLabel( tr( "Choose a preset or build a custom footprint, then click in a map view to "
                                 "place it. The active footprint remains ready for repeated use." ),
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
