//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Small tile footprints and oriented placement presets.
//////////////////////////////////////////////////////////////////////////
#pragma once
#include "CypherTileDocumentBridge.h"
#include <QList>
#include <QPoint>
#include <QSize>
#include <QWidget>
#include <functional>
namespace cypher::tools::tile_editor
{
enum class tile_piece_kind_t
{
    ROOM,
    CORNER,
    T_JUNCTION,
    CROSS,
    STAIRS,
    DOOR,
    BOUNDARY
};
struct tile_piece_t
{
    QString id, label;
    tile_piece_kind_t kind{ tile_piece_kind_t::ROOM };
    QSize size{ 1, 1 };
    int orientation{ 0 }; // Clockwise quarter turns, north first.
    int wallLevels{ 1 };
};
QList<tile_piece_t> TileEditorPieces_Definitions();
QList<QPoint> TileEditorPiece_Cells( const tile_piece_t &piece );
bool TileEditorPiece_Stamp( CypherTileDocumentBridge &bridge, const tile_piece_t &piece,
                            tile_map_grid_coord_t center, tile_map_paint_t paint, QString &error );
class CypherTilePiecePalette final : public QWidget
{
  public:
    explicit CypherTilePiecePalette( QWidget *parent = nullptr );
    void setActivateCallback( std::function<void( const tile_piece_t & )> callback );

  private:
    std::function<void( const tile_piece_t & )> m_activate;
};
} // namespace cypher::tools::tile_editor
