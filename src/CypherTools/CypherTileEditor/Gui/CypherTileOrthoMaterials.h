//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Bounded CPU material display shared by the orthographic panes.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_TOOLS_TILEEDITOR_ORTHOMATERIALS_H
#define CYPHER_TOOLS_TILEEDITOR_ORTHOMATERIALS_H
#pragma once

#include "Core/CypherTileMapDocument.h"

#include <QByteArray>
#include <QBrush>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;

namespace cypher::tools::tile_editor
{

inline constexpr int TILE_ORTHO_MATERIAL_MAX_IMAGE_SIZE = 256;

struct tile_ortho_material_t {
    // Owned, tinted sRGB thumbnail. Duplicate paths share Qt's immutable image
    // storage. Empty on decode failure; error explains the diagnostic checker.
    QImage image{};
    QBrush textureBrush{};
    QColor color{};
    QString label{};
    QString path{};
    QString error{};
    u32 textureWidth{ 0u };
    u32 textureHeight{ 0u };
    float tint[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    float uvScale[2]{ 1.0f, 1.0f };
    bool sRGB{ true };
    bool generateMips{ false };
    bool bound{ false };
};

struct tile_ortho_material_cache_t {
    // Only authored bindings are stored (at most TILE_MAP_MAX_MATERIAL_BINDINGS).
    // Unbound slots resolve through the shared built-in blockout palette.
    QHash<u16, tile_ortho_material_t> entries{};
    QString cookedRoot{};
    QByteArray bindingSignature{};
    bool initialized{ false };
};

// Call on document/binding/root changes, outside paintEvent. Cell/selection edits
// with unchanged bindings perform no file I/O. A root change or force refresh
// decodes again; otherwise unchanged paths reuse their existing cached image.
// Returns true if a new cache state was published. Failed changed bindings replace
// the old image with a diagnostic record instead of displaying stale materials.
bool TileOrthoMaterials_Refresh( tile_ortho_material_cache_t &cache,
    const tile_map_document_t &document, const QString &cookedRoot,
    bool force = false );

// Borrowed until the next refresh. nullptr means an unbound palette slot.
const tile_ortho_material_t *TileOrthoMaterials_Find(
    const tile_ortho_material_cache_t &cache, u16 slot );

// Human-readable slot/name/path, plus a missing-resource diagnostic when needed.
QString TileOrthoMaterials_Describe( const tile_ortho_material_cache_t *cache,
    u16 slot );

// Draws a cached material in rect, with one base UV interval across each axis.
// uv_scale repeats that interval; a negative scale mirrors it. This flat material
// view deliberately omits lighting. The caller's painter state is preserved,
// opacity multiplies its current opacity, and no file I/O/decoding occurs here.
// Missing bound assets use a magenta checker; unbound slots use palette colors.
void TileOrthoMaterials_Paint( QPainter &painter, const QRectF &rect,
    const tile_ortho_material_cache_t *cache, u16 slot, qreal opacity );

} // namespace cypher::tools::tile_editor
#endif
