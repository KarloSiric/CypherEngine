//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileDocumentBridge.h
//  Purpose: Isolates the Qt shell from the tile-map Core API surface.
//  Details: File I/O and QString conversion live here. Canvas and window code
//           never mutate document fields or allocator-backed vectors directly.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_DOCUMENTBRIDGE_H
#define CYPHER_TOOLS_TILEEDITOR_DOCUMENTBRIDGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Core/CypherTileMapDocument.h"

#include <QString>

#include <memory>
#include <span>

namespace cypher::tools::tile_editor
{

class CypherTileDocumentBridge final
{
public:
    CypherTileDocumentBridge();
    ~CypherTileDocumentBridge();

    CypherTileDocumentBridge( const CypherTileDocumentBridge & ) = delete;
    CypherTileDocumentBridge &operator=( const CypherTileDocumentBridge & ) = delete;

    const tile_map_document_t *document() const;
    tile_map_document_t *document();
    bool isInitialized() const;
    bool isDirty() const;
    const QString &filePath() const;
    void markSaved();

    bool newDocument(
        const tile_map_document_desc_t &description,
        QString *pErrorOut = nullptr );
    // Updates the live document as one undoable action; never reloads its file.
    bool setDescription(
        const tile_map_document_desc_t &description,
        QString *pErrorOut = nullptr );
    bool loadFromFile( const QString &path, QString *pErrorOut = nullptr );
    bool saveToFile( const QString &path, QString *pErrorOut = nullptr );
    // Writes the current committed revision for renderer/tool consumption
    // without changing the authored file path or saved/dirty revision.
    bool writeSnapshotToFile(
        const QString &path,
        QString *pErrorOut = nullptr ) const;

    bool beginEdit( const QString &label, QString *pErrorOut = nullptr );
    bool commitEdit( QString *pErrorOut = nullptr );
    void cancelEdit();

    bool paintCell(
        tile_map_grid_coord_t coordinate,
        const tile_map_paint_t &paint,
        QString *pErrorOut = nullptr );
    bool eraseCell(
        tile_map_grid_coord_t coordinate,
        QString *pErrorOut = nullptr );
    bool paintRect(
        const tile_map_grid_rect_t &rectangle,
        const tile_map_paint_t &paint,
        QString *pErrorOut = nullptr );
    bool eraseRect(
        const tile_map_grid_rect_t &rectangle,
        QString *pErrorOut = nullptr );
    bool placePlayerSpawn(
        tile_map_grid_coord_t coordinate,
        float yawDegrees,
        QString *pErrorOut = nullptr );
    bool removePlayerSpawn( QString *pErrorOut = nullptr );
    bool placeDoor(
        tile_map_grid_coord_t coordinate,
        tile_map_marker_side_t side,
        unique_id_t *pDoorIdOut = nullptr,
        QString *pErrorOut = nullptr );
    bool removeDoorAt(
        tile_map_grid_coord_t coordinate,
        tile_map_marker_side_t side,
        QString *pErrorOut = nullptr );

    bool canUndo() const;
    bool moveRegion( tile_map_grid_rect_t rectangle, int dx, int dy, bool copy, QString *pErrorOut = nullptr );
    bool deleteRegion( tile_map_grid_rect_t rectangle, QString *pErrorOut = nullptr );
    bool raiseRegion( tile_map_grid_rect_t rectangle, int delta, QString *pErrorOut = nullptr );
    bool rotateRegion( tile_map_grid_rect_t rectangle, QString *pErrorOut = nullptr );
    bool moveSelection( std::span<const tile_map_grid_coord_t> selection,
        int dx, int dy, bool copy, QString *pErrorOut = nullptr );
    bool deleteSelection( std::span<const tile_map_grid_coord_t> selection,
        QString *pErrorOut = nullptr );
    bool raiseSelection( std::span<const tile_map_grid_coord_t> selection,
        int delta, QString *pErrorOut = nullptr );
    bool adjustSelectionWallHeight( std::span<const tile_map_grid_coord_t> selection,
        int delta, QString *pErrorOut = nullptr );
    bool rotateSelection( std::span<const tile_map_grid_coord_t> selection,
        QString *pErrorOut = nullptr );
    bool applySelectionProperties( std::span<const tile_map_grid_coord_t> selection,
        const tile_map_selection_patch_t &patch, QString *pErrorOut = nullptr );
    bool canRedo() const;
    bool undo( QString *pErrorOut = nullptr );
    bool redo( QString *pErrorOut = nullptr );

private:
    static QString documentError(
        const QString &operation,
        tile_map_document_status_t status );
    void replaceDocument(
        std::unique_ptr<tile_map_document_t> pReplacement,
        const QString &filePath );

    std::unique_ptr<tile_map_document_t> m_pDocument{};
    QString m_filePath{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_DOCUMENTBRIDGE_H
