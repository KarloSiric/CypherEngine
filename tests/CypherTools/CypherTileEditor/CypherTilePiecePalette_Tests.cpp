//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies real piece footprints, atomic stamping and palette activation.
//////////////////////////////////////////////////////////////////////////
#include "CypherTilePiecePalette.h"
#include "CypherTileCanvas.h"
#include "CypherTileEditorMainWindow.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <limits>

using namespace cypher::tools::tile_editor;

namespace
{
void EnsurePieceApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TilePiecePaletteTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

tile_piece_t Piece( const char *id )
{
    for ( const auto &piece : TileEditorPieces_Definitions() )
        if ( piece.id == QString::fromLatin1( id ) ) return piece;
    FAIL( "Missing requested piece preset" );
    return {};
}

QListWidgetItem *PaletteItem( QListWidget &list, const char *id )
{
    for ( int index = 0; index < list.count(); ++index )
        if ( list.item( index )->data( Qt::UserRole + 1 ).toString() == QString::fromLatin1( id ) )
            return list.item( index );
    FAIL( "Missing requested palette item" );
    return nullptr;
}

void Activate( QListWidget &list, QListWidgetItem *item, const char *signal )
{
    REQUIRE( QMetaObject::invokeMethod( &list, signal, Qt::DirectConnection,
                                        Q_ARG( QListWidgetItem *, item ) ) );
}

int FloorCount( const CypherTileDocumentBridge &bridge )
{
    int count = 0;
    for ( cypher::common::u32 y = 0; y < bridge.document()->nHeight; ++y )
        for ( cypher::common::u32 x = 0; x < bridge.document()->nWidth; ++x )
            if ( CypherTileMapDocument_CellHasFloor( bridge.document(),
                    { static_cast<cypher::common::i32>( x ), static_cast<cypher::common::i32>( y ) } ) ) ++count;
    return count;
}
}

TEST_CASE( "Piece presets contain distinct room shapes and four explicit stair and door directions",
           "[TileEditor][Pieces]" )
{
    QSet<QString> ids;
    int stairCount = 0;
    int doorCount = 0;
    for ( const auto &piece : TileEditorPieces_Definitions() ) {
        INFO( piece.id.toStdString() );
        CHECK_FALSE( piece.id.isEmpty() );
        CHECK_FALSE( ids.contains( piece.id ) );
        ids.insert( piece.id );
        if ( piece.kind == tile_piece_kind_t::STAIRS ) {
            CHECK( piece.orientation == stairCount );
            ++stairCount;
        }
        if ( piece.kind == tile_piece_kind_t::DOOR ) {
            CHECK( piece.orientation == doorCount );
            ++doorCount;
        }
        if ( piece.kind == tile_piece_kind_t::BOUNDARY ) CHECK( piece.wallLevels > 0 );
    }
    CHECK( ids.size() >= 40 );
    CHECK( stairCount == 4 );
    CHECK( doorCount == 4 );
    CHECK( TileEditorPiece_Cells( Piece( "room.small" ) ).size() == 36 );
    CHECK( TileEditorPiece_Cells( Piece( "room.medium" ) ).size() == 80 );
    CHECK( TileEditorPiece_Cells( Piece( "room.large" ) ).size() == 192 );
    CHECK( TileEditorPiece_Cells( Piece( "corridor.horizontal" ) ).size() == 36 );
    CHECK( TileEditorPiece_Cells( Piece( "corridor.vertical" ) ).size() == 36 );
    CHECK( TileEditorPiece_Cells( Piece( "junction.cross" ) ).size() == 20 );
    CHECK( TileEditorPiece_Cells( Piece( "junction.cross.wide" ) ).size() == 45 );
    CHECK( TileEditorPiece_Cells( Piece( "courtyard.small" ) ).size() == 48 );
    CHECK( TileEditorPiece_Cells( Piece( "courtyard.large" ) ).size() == 80 );
    CHECK( TileEditorPiece_Cells( Piece( "ushape.0" ) ).size() == 40 );
}

TEST_CASE( "Corner and T-junction footprints rotate clockwise without filling their cutouts",
           "[TileEditor][Pieces]" )
{
    for ( int orientation = 0; orientation < 4; ++orientation ) {
        const QByteArray cornerId = QStringLiteral( "corner.%1" ).arg( orientation ).toLatin1();
        const QByteArray junctionId = QStringLiteral( "junction.%1" ).arg( orientation ).toLatin1();
        const auto corners = TileEditorPiece_Cells( Piece( cornerId.constData() ) );
        const auto junctions = TileEditorPiece_Cells( Piece( junctionId.constData() ) );
        REQUIRE( corners.size() == 20 );
        REQUIRE( junctions.size() == 20 );
        for ( int y = 0; y < 6; ++y ) for ( int x = 0; x < 6; ++x ) {
            INFO( "Orientation " << orientation << ", cell " << x << ", " << y );
            const bool cornerExpected[4]{ x < 2 || y >= 4, x < 2 || y < 2,
                                          x >= 4 || y < 2, x >= 4 || y >= 4 };
            const bool junctionExpected[4]{ y < 2 || ( x >= 2 && x < 4 ),
                                            x >= 4 || ( y >= 2 && y < 4 ),
                                            y >= 4 || ( x >= 2 && x < 4 ),
                                            x < 2 || ( y >= 2 && y < 4 ) };
            CHECK( corners.contains( QPoint( x, y ) ) == cornerExpected[orientation] );
            CHECK( junctions.contains( QPoint( x, y ) ) == junctionExpected[orientation] );
        }
    }
    auto wrapped = Piece( "corner.0" );
    wrapped.orientation = -1;
    CHECK( TileEditorPiece_Cells( wrapped ) == TileEditorPiece_Cells( Piece( "corner.3" ) ) );
    wrapped.orientation = 5;
    CHECK( TileEditorPiece_Cells( wrapped ) == TileEditorPiece_Cells( Piece( "corner.1" ) ) );
}

TEST_CASE( "Parameterized footprints honor passage width and rotate rectangular bounds",
           "[TileEditor][Pieces]" )
{
    const struct expectation_t
    {
        tile_piece_kind_t kind;
        int cellCount;
    } expectations[]{
        { tile_piece_kind_t::CORNER, 36 },
        { tile_piece_kind_t::T_JUNCTION, 36 },
        { tile_piece_kind_t::CROSS, 36 },
        { tile_piece_kind_t::COURTYARD, 54 },
        { tile_piece_kind_t::U_SHAPE, 48 },
    };
    for ( const auto &expected : expectations )
    {
        tile_piece_t piece{ "test", "Test", expected.kind, { 8, 7 }, 0, 1, 3 };
        INFO( static_cast<int>( expected.kind ) );
        CHECK( TileEditorPiece_Cells( piece ).size() == expected.cellCount );
        CHECK( TileEditorPiece_FootprintSize( piece ) == QSize( 8, 7 ) );
        piece.orientation = 1;
        CHECK( TileEditorPiece_Cells( piece ).size() == expected.cellCount );
        CHECK( TileEditorPiece_FootprintSize( piece ) == QSize( 7, 8 ) );
        for ( const QPoint &cell : TileEditorPiece_Cells( piece ) )
        {
            CHECK( cell.x() >= 0 );
            CHECK( cell.x() < 7 );
            CHECK( cell.y() >= 0 );
            CHECK( cell.y() < 8 );
        }
    }

    tile_piece_t invalid{ "invalid", "Invalid", tile_piece_kind_t::CORNER, { 8, 7 }, 0, 1, 0 };
    CHECK( TileEditorPiece_Cells( invalid ).isEmpty() );
    invalid = { "invalid", "Invalid", tile_piece_kind_t::COURTYARD, { 6, 6 }, 0, 1, 3 };
    CHECK( TileEditorPiece_Cells( invalid ).isEmpty() );
}

TEST_CASE( "Placed footprint cells use rotated bounds and remain centered on the requested cell",
           "[TileEditor][Pieces]" )
{
    tile_piece_t piece{ "custom", "Custom", tile_piece_kind_t::CORRIDOR, { 6, 2 }, 1 };
    const auto placed = TileEditorPiece_PlacedCells( piece, { 10, 20 } );
    REQUIRE( placed.size() == 12 );
    CHECK( TileEditorPiece_FootprintSize( piece ) == QSize( 2, 6 ) );
    int minimumX = std::numeric_limits<int>::max();
    int maximumX = std::numeric_limits<int>::min();
    int minimumY = std::numeric_limits<int>::max();
    int maximumY = std::numeric_limits<int>::min();
    for ( const auto &cell : placed )
    {
        minimumX = std::min( minimumX, static_cast<int>( cell.x ) );
        maximumX = std::max( maximumX, static_cast<int>( cell.x ) );
        minimumY = std::min( minimumY, static_cast<int>( cell.y ) );
        maximumY = std::max( maximumY, static_cast<int>( cell.y ) );
    }
    CHECK( minimumX == 9 );
    CHECK( maximumX == 10 );
    CHECK( minimumY == 17 );
    CHECK( maximumY == 22 );
}

TEST_CASE( "Stamping a rotated footprint authors only its cells as one undoable flat-floor edit",
           "[TileEditor][Pieces]" )
{
    CypherTileDocumentBridge bridge;
    QString error;
    REQUIRE( bridge.newDocument( { 32u, 32u, 2.0f, 3.0f }, &error ) );
    REQUIRE( bridge.beginEdit( QStringLiteral( "Existing floor" ), &error ) );
    REQUIRE( bridge.paintCell( { 2, 2 }, { 4, 3u, 2u }, &error ) );
    REQUIRE( bridge.paintCell( { 14, 14 }, { 4, 3u, 2u, tile_map_cell_shape_t::STAIRS_WEST, 8u }, &error ) );
    REQUIRE( bridge.commitEdit( &error ) );
    bridge.markSaved();

    const auto piece = Piece( "corner.1" );
    REQUIRE( TileEditorPiece_Stamp( bridge, piece, { 16, 16 },
             { -2, 2u, 7u, tile_map_cell_shape_t::STAIRS_EAST, 6u }, error ) );
    REQUIRE( FloorCount( bridge ) == 21 );
    for ( int y = 0; y < 6; ++y ) for ( int x = 0; x < 6; ++x ) {
        const auto *cell = CypherTileMapDocument_CellAt( bridge.document(), { 13 + x, 13 + y } );
        REQUIRE( cell );
        const bool occupied = x < 2 || y < 2;
        CHECK( CypherTileMapDocument_CellHasFloor( bridge.document(), { 13 + x, 13 + y } ) == occupied );
        if ( occupied ) {
            CHECK( cell->shape == tile_map_cell_shape_t::FLAT );
            CHECK( cell->nFloorLevel == -2 );
            CHECK( cell->nWallHeightLevels == 2u );
            CHECK( cell->nMaterialSlot == 7u );
        }
    }
    const auto *untouched = CypherTileMapDocument_CellAt( bridge.document(), { 2, 2 } );
    REQUIRE( untouched );
    CHECK( untouched->nMaterialSlot == 2u );
    REQUIRE( bridge.undo( &error ) );
    CHECK_FALSE( bridge.isDirty() );
    CHECK( FloorCount( bridge ) == 2 );
    const auto *restored = CypherTileMapDocument_CellAt( bridge.document(), { 14, 14 } );
    REQUIRE( restored );
    CHECK( restored->shape == tile_map_cell_shape_t::STAIRS_WEST );
    CHECK( restored->nMaterialSlot == 2u );
    REQUIRE( bridge.redo( &error ) );
    CHECK( FloorCount( bridge ) == 21 );
    CHECK( CypherTileMapDocument_CellAt( bridge.document(), { 14, 14 } )->shape == tile_map_cell_shape_t::FLAT );
}

TEST_CASE( "Invalid or out-of-bounds footprints leave the document and undo history intact",
           "[TileEditor][Pieces]" )
{
    CypherTileDocumentBridge bridge;
    QString error;
    REQUIRE( bridge.newDocument( { 12u, 12u, 2.0f, 3.0f }, &error ) );
    bridge.markSaved();
    const auto piece = Piece( "room.small" );
    for ( const tile_map_grid_coord_t center : {
            tile_map_grid_coord_t{ 0, 0 }, { 2, 6 }, { 10, 6 },
            { std::numeric_limits<cypher::common::i32>::min(), 6 },
            { std::numeric_limits<cypher::common::i32>::max(), 6 } } ) {
        INFO( "Center " << center.x << ", " << center.y );
        error.clear();
        CHECK_FALSE( TileEditorPiece_Stamp( bridge, piece, center, {}, error ) );
        CHECK_FALSE( error.isEmpty() );
        CHECK( FloorCount( bridge ) == 0 );
        CHECK_FALSE( bridge.canUndo() );
    }
    auto invalid = piece;
    invalid.size = { 65, 6 };
    CHECK_FALSE( TileEditorPiece_Stamp( bridge, invalid, { 6, 6 }, {}, error ) );
    invalid.size = { 0, 6 };
    CHECK_FALSE( TileEditorPiece_Stamp( bridge, invalid, { 6, 6 }, {}, error ) );
    CHECK_FALSE( TileEditorPiece_Stamp( bridge, Piece( "stairs.0" ), { 6, 6 }, {}, error ) );
    CHECK_FALSE( TileEditorPiece_Stamp( bridge, piece, { 6, 6 }, { 0, 0u, 0u }, error ) );
    CHECK( FloorCount( bridge ) == 0 );
    CHECK_FALSE( bridge.canUndo() );
    CHECK_FALSE( bridge.isDirty() );
    // Exact map-edge alignment is valid, and a failed paint must not leave an edit group open.
    REQUIRE( TileEditorPiece_Stamp( bridge, piece, { 3, 3 }, {}, error ) );
    CHECK( FloorCount( bridge ) == 36 );
    CHECK_FALSE( TileEditorPiece_Stamp( bridge, piece, { 0, 0 }, {}, error ) );
    CHECK( FloorCount( bridge ) == 36 );
    REQUIRE( bridge.undo( &error ) );
    CHECK( FloorCount( bridge ) == 0 );
    CHECK_FALSE( bridge.canUndo() );
}

TEST_CASE( "Preset clicks and the custom builder emit complete placement pieces",
           "[TileEditor][Pieces][Workspace]" )
{
    EnsurePieceApplication();
    CypherTilePiecePalette palette;
    auto *list = palette.findChild<QListWidget *>( QStringLiteral( "TileStampPalette" ) );
    auto *filter = palette.findChild<QLineEdit *>( QStringLiteral( "TilePieceFilter" ) );
    auto *kind = palette.findChild<QComboBox *>( QStringLiteral( "TilePieceCustomType" ) );
    auto *width = palette.findChild<QSpinBox *>( QStringLiteral( "TilePieceCustomWidth" ) );
    auto *depth = palette.findChild<QSpinBox *>( QStringLiteral( "TilePieceCustomDepth" ) );
    auto *passage = palette.findChild<QSpinBox *>( QStringLiteral( "TilePieceCustomPassage" ) );
    auto *orientation = palette.findChild<QComboBox *>( QStringLiteral( "TilePieceCustomOrientation" ) );
    auto *activateCustom = palette.findChild<QPushButton *>( QStringLiteral( "TilePieceCustomActivate" ) );
    REQUIRE( list );
    REQUIRE( filter );
    REQUIRE( kind );
    REQUIRE( width );
    REQUIRE( depth );
    REQUIRE( passage );
    REQUIRE( orientation );
    REQUIRE( activateCustom );
    QList<tile_piece_t> selected;
    palette.setActivateCallback( [&]( const tile_piece_t &piece ) { selected.append( piece ); } );
    auto *room = PaletteItem( *list, "corner.2" );
    REQUIRE_FALSE( room->icon().isNull() );
    Activate( *list, room, "itemClicked" );
    REQUIRE( selected.size() == 1 );
    CHECK( selected.back().id == QStringLiteral( "corner.2" ) );
    CHECK( selected.back().orientation == 2 );

    auto *stairs = PaletteItem( *list, "stairs.3" );
    Activate( *list, stairs, "itemClicked" );
    REQUIRE( selected.size() == 2 );
    CHECK( selected.back().kind == tile_piece_kind_t::STAIRS );
    CHECK( selected.back().orientation == 3 );
    Activate( *list, PaletteItem( *list, "door.1" ), "itemClicked" );
    REQUIRE( selected.size() == 3 );
    CHECK( selected.back().kind == tile_piece_kind_t::DOOR );
    CHECK( selected.back().orientation == 1 );
    Activate( *list, PaletteItem( *list, "boundary.high" ), "itemClicked" );
    REQUIRE( selected.size() == 4 );
    CHECK( selected.back().wallLevels == 2 );

    kind->setCurrentIndex( kind->findData( static_cast<int>( tile_piece_kind_t::COURTYARD ) ) );
    width->setValue( 10 );
    depth->setValue( 8 );
    passage->setValue( 2 );
    orientation->setCurrentIndex( orientation->findData( 1 ) );
    activateCustom->click();
    REQUIRE( selected.size() == 5 );
    CHECK( selected.back().kind == tile_piece_kind_t::COURTYARD );
    CHECK( selected.back().size == QSize( 10, 8 ) );
    CHECK( selected.back().passageWidth == 2 );
    CHECK( selected.back().orientation == 1 );
    CHECK( selected.back().id == QStringLiteral( "custom.courtyard.10x8.p2.r1" ) );
    CHECK( TileEditorPiece_FootprintSize( selected.back() ) == QSize( 8, 10 ) );
    CHECK( TileEditorPiece_Cells( selected.back() ).size() == 56 );

    list->setCurrentItem( room );
    filter->setText( QStringLiteral( "sTaIrS" ) );
    CHECK( room->isHidden() );
    CHECK( list->currentItem() == nullptr );
    int visible = 0;
    for ( int index = 0; index < list->count(); ++index )
        if ( !list->item( index )->isHidden() ) ++visible;
    CHECK( visible == 4 );
    Activate( *list, room, "itemActivated" );
    CHECK( selected.size() == 5 );
    filter->clear();
    CHECK_FALSE( room->isHidden() );
}

TEST_CASE( "The editor starts selection-first and places a chosen footprint with one map click",
           "[TileEditor][Pieces][Workspace][DirectPlacement]" )
{
    EnsurePieceApplication();
    QTemporaryDir settingsDirectory;
    REQUIRE( settingsDirectory.isValid() );
    QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "DirectPiecePlacement" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope,
                        settingsDirectory.path() );

    CypherTileEditorMainWindow window;
    auto *canvasWidget = window.findChild<QWidget *>( QStringLiteral( "CypherTileCanvas" ) );
    auto *canvas = static_cast<CypherTileCanvas *>( canvasWidget );
    auto *palette = window.findChild<QListWidget *>( QStringLiteral( "TileStampPalette" ) );
    auto *select = window.findChild<QAction *>( QStringLiteral( "tool.select" ) );
    auto *stamp = window.findChild<QAction *>( QStringLiteral( "tool.stamp" ) );
    REQUIRE( canvas );
    REQUIRE( palette );
    REQUIRE( select );
    REQUIRE( stamp );
    CHECK( canvas->tool() == tile_canvas_tool_t::SELECT );
    CHECK( select->isChecked() );

    canvas->resize( 640, 480 );
    canvas->selectCell( { 20, 20 }, true );
    REQUIRE( canvas->selectedCells().size() == 1 );
    Activate( *palette, PaletteItem( *palette, "room.small" ), "itemClicked" );
    CHECK( canvas->tool() == tile_canvas_tool_t::STAMP );
    CHECK( stamp->isChecked() );
    CHECK( canvas->selectedCells().size() == 1 );

    const QPointF center( canvas->width() * 0.5, canvas->height() * 0.5 );
    QMouseEvent press( QEvent::MouseButtonPress, center, center,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( canvas, &press );
    REQUIRE( canvas->selectedCells().size() == 36 );
    CHECK( canvas->tool() == tile_canvas_tool_t::STAMP );
    CHECK( canvas->selectedCell().x == 17 );
    CHECK( canvas->selectedCell().y == 17 );
}
