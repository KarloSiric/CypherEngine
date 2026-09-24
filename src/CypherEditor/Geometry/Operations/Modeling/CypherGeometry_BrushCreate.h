//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCreate.h
//  Purpose: Declares creation of new brushes from generated pieces inside
//           a geometry transaction.
//  Details: The bridge between pure primitive generators and the document:
//           every piece becomes a new brush with fresh identities and the
//           given surfacing (a material plus the default projection,
//           re-based onto each face). Creating a whole piece list is one
//           atomic preview step, so an interactive draw-shape tool can
//           replace its preview on every parameter change and commit once.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CREATE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CREATE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushPrimitives.h"
#include "CypherGeometry_PieceMaterialize.h"

namespace cypher::editor::geometry
{

// Surfacing applied to every face of created brushes. The projection is
// re-based per face, so one world-aligned projection textures every side.
struct geometry_create_surfacing_t {
    geometry_brush_side_attributes_t attributes{ BrushSideAttributes_MakeDefault() };
};

// Previews one new brush per piece in the list, in list order. The IDs of
// the created brushes are written to pBrushIdsOut (optional; must hold at
// least PieceList_Count entries). Atomic: on failure no brush is previewed.
CYPHER_NODISCARD geometry_status_t BrushCreate_TryFromPieces(
    geometry_transaction_t *pTransaction,
    const geometry_piece_list_t *pPieces,
    const geometry_create_surfacing_t &surfacing,
    common::span_t<geometry_source_id_t> brushIdsOut ) noexcept;

// Single-piece convenience.
CYPHER_NODISCARD geometry_status_t BrushCreate_TryFromPiece(
    geometry_transaction_t *pTransaction,
    const geometry_brush_piece_t *pPiece,
    const geometry_create_surfacing_t &surfacing,
    geometry_source_id_t *pBrushIdOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CREATE_H
