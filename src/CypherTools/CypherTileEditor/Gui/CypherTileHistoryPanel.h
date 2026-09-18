//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileHistoryPanel.h
//  Purpose: Declares the editor's read-only undo/redo history inspector.
//  Details: The panel visualizes the core document stack and delegates the two
//           mutations to the owning window so every normal refresh path runs.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_HISTORYPANEL_H
#define CYPHER_TOOLS_TILEEDITOR_HISTORYPANEL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Core/CypherTileMapDocument.h"

#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace cypher::tools::tile_editor
{

class CypherTileHistoryPanel final : public QWidget
{
public:
    explicit CypherTileHistoryPanel( QWidget *pParent = nullptr );

    // Borrowed until the owner replaces or clears it. The panel never mutates
    // the document directly, and refresh() performs no allocation in Core.
    void setDocument( const tile_map_document_t *pDocument );
    void setUndoCallback( std::function<void()> callback );
    void setRedoCallback( std::function<void()> callback );
    void refresh();

private:
    [[nodiscard]] u64 stateRevision(
        const tile_map_history_info_t &history,
        usize iState ) const;
    [[nodiscard]] bool savedRevisionIsVisible(
        const tile_map_history_info_t &history ) const;
    QTreeWidgetItem *historyRow( usize iState );
    void hideHistoryRows();
    void refreshStateDetails( usize iState );

    const tile_map_document_t *m_pDocument{ nullptr };
    tile_map_history_info_t m_history{};
    std::function<void()> m_undo{};
    std::function<void()> m_redo{};
    QLabel *m_pStateBadge{ nullptr };
    QLabel *m_pSummary{ nullptr };
    QToolButton *m_pUndo{ nullptr };
    QToolButton *m_pRedo{ nullptr };
    QTreeWidget *m_pEntries{ nullptr };
    QLabel *m_pDetails{ nullptr };
    // QTreeWidget accessibility interfaces refer to item identities. Retain
    // every allocated row until the panel dies; shrinking or replacing a map
    // hides surplus rows instead of invalidating native accessibility wrappers.
    std::vector<QTreeWidgetItem *> m_historyRows{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_HISTORYPANEL_H
