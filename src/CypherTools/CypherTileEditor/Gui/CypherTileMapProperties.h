//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Stages live map dimensions and metrics as one document operation.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include "Core/CypherTileMapDocument.h"

#include <QWidget>
#include <functional>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace cypher::tools::tile_editor
{

class CypherTileMapProperties final : public QWidget
{
  public:
    explicit CypherTileMapProperties( QWidget *pParent = nullptr );

    // Borrowed until the caller replaces or clears it. Synchronization never
    // invokes the apply callback. Unchanged notifications preserve staged fields;
    // switching documents or externally changing these properties resynchronizes.
    void setDocument( const tile_map_document_t *pDocument );
    void setApplyCallback( std::function<bool( const tile_map_document_desc_t &, QString & )> callback );
    bool applyChanges();
    void resetChanges();

  private:
    tile_map_document_desc_t description() const;
    bool hasChanges() const;
    void fieldsChanged();
    void refreshSummary();
    void refreshButtons();

    const tile_map_document_t *m_pDocument{ nullptr };
    unique_id_t m_loadedMapId{};
    tile_map_document_desc_t m_loadedDescription{};
    double m_loadedCellSizeValue{ 0.0 };
    double m_loadedLevelHeightValue{ 0.0 };
    bool m_syncing{ false };
    bool m_applying{ false };
    std::function<bool( const tile_map_document_desc_t &, QString & )> m_apply;
    QSpinBox *m_pWidth{ nullptr };
    QSpinBox *m_pHeight{ nullptr };
    QDoubleSpinBox *m_pCellSize{ nullptr };
    QDoubleSpinBox *m_pLevelHeight{ nullptr };
    QLineEdit *m_pMapId{ nullptr };
    QLabel *m_pExtents{ nullptr };
    QLabel *m_pMessage{ nullptr };
    QPushButton *m_pApply{ nullptr };
    QPushButton *m_pRevert{ nullptr };
};

} // namespace cypher::tools::tile_editor
