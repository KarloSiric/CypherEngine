//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Selection-driven material information for the tile editor.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_TOOLS_TILEEDITOR_SELECTIONMATERIALINSPECTOR_H
#define CYPHER_TOOLS_TILEEDITOR_SELECTIONMATERIALINSPECTOR_H
#pragma once

#include "CypherTileOrthoMaterials.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>
#include <span>
#include <vector>

class QLabel;
class QPushButton;

namespace cypher::tools::tile_editor
{

enum class tile_selection_material_state_t : u8 {
    NO_SELECTION = 0u,
    NO_SURFACES,
    MIXED,
    BUILTIN,
    PROJECT_READY,
    PROJECT_UNAVAILABLE
};

// A presentation snapshot. It borrows nothing from the document or cache, so
// the widget remains safe across document rebuilds and material-cache refreshes.
struct tile_selection_material_inspection_t {
    tile_selection_material_state_t state{
        tile_selection_material_state_t::NO_SELECTION };
    usize nSelectedCells{ 0u };
    usize nFloorCells{ 0u };
    usize nDistinctSlots{ 0u };
    u16 nSlot{ 0u };
    QString name{};
    QString builtInName{};
    QString stableId{};
    QString path{};
    QString status{};
    QString diagnostic{};
    QString slotsText{};
    QImage preview{};
    QColor color{};
    std::vector<QColor> swatches{};
    u32 textureWidth{ 0u };
    u32 textureHeight{ 0u };
    f32 tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    f32 uvScale[2]{ 1.0f, 1.0f };
    bool sRGB{ true };
    bool generateMips{ false };
};

tile_selection_material_inspection_t TileSelectionMaterial_Inspect(
    const tile_map_document_t *pDocument,
    std::span<const tile_map_grid_coord_t> selection,
    const tile_ortho_material_cache_t *pCache );

class CypherTileSelectionMaterialInspector final : public QWidget
{
public:
    explicit CypherTileSelectionMaterialInspector( QWidget *pParent = nullptr );

    void setSelection( const tile_map_document_t *pDocument,
        std::span<const tile_map_grid_coord_t> selection,
        const tile_ortho_material_cache_t *pCache );
    void setUseForPaintCallback( std::function<void( u16 )> callback );
    void setBrowseCallback( std::function<void( u16, const QString & )> callback );

    const tile_selection_material_inspection_t &inspection() const noexcept;

private:
    void publish( tile_selection_material_inspection_t inspection );

    QLabel *m_pPreview{ nullptr };
    QLabel *m_pName{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QLabel *m_pSlot{ nullptr };
    QLabel *m_pBinding{ nullptr };
    QLabel *m_pFallback{ nullptr };
    QLabel *m_pTexture{ nullptr };
    QLabel *m_pTint{ nullptr };
    QLabel *m_pUvScale{ nullptr };
    QLabel *m_pDiagnostic{ nullptr };
    QPushButton *m_pUseForPaint{ nullptr };
    QPushButton *m_pBrowse{ nullptr };
    tile_selection_material_inspection_t m_inspection{};
    std::function<void( u16 )> m_useForPaintCallback{};
    std::function<void( u16, const QString & )> m_browseCallback{};
};

} // namespace cypher::tools::tile_editor

#endif
