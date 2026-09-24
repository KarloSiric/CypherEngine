//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PieceMaterialize.h
//  Purpose: Declares conversion of identity-free convex pieces into
//           published brush values inside a transaction.
//  Details: This is the one place where CSG, clipping, primitive, and hull
//           results acquire persistent identity and surfacing. Identity
//           rules:
//
//           - The brush keeps `brushId` when valid, otherwise it gets a
//             fresh ID from the transaction.
//           - With bReuseSideIds, a plane that is an unmodified SOURCE_SIDE
//             of that same brush keeps its side ID, so the surviving part
//             of an edited brush keeps selection- and undo-stable sides.
//             Every other plane gets a fresh ID.
//
//           Surfacing comes from the donors via
//           AttributePropagation_TrySelectForFace using the plane's
//           provenance. Fresh IDs are drawn through the transaction, so a
//           cancelled edit retires them.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PIECE_MATERIALIZE_H
#define CYPHER_EDITOR_GEOMETRY_PIECE_MATERIALIZE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Propagation.h"
#include "CypherGeometry_BrushPiece.h"
#include "CypherGeometry_Transaction.h"

namespace cypher::editor::geometry
{

struct geometry_materialize_desc_t {
    const geometry_brush_piece_t *pPiece{ nullptr };
    geometry_source_id_t brushId{};
    common::bool_t bReuseSideIds{ false };
    // Values whose sides donate material and UV projection.
    common::span_t<const geometry_brush_value_t *const> donors{};
};

// Creates (but does not preview) a brush value for a SOLID piece. The
// caller owns the returned reference. Statuses: NOT_INITIALIZED for an
// inactive transaction, INVALID_ARGUMENT for a null or empty piece, plus
// any identity-allocation, attribute, or value-validation failure. On
// failure fresh IDs already drawn stay pending in the transaction and are
// retired when it ends.
CYPHER_NODISCARD geometry_status_t GeometryMaterialize_TryPiece(
    geometry_transaction_t *pTransaction,
    const geometry_materialize_desc_t &desc,
    const geometry_brush_value_t **ppValueOut ) noexcept;

// Convenience: extracts the brush of a committed or previewed value as a
// SOURCE_SIDE piece. pPiece must be initialized.
CYPHER_NODISCARD geometry_status_t GeometryMaterialize_TryPieceFromValue(
    const geometry_brush_value_t *pValue,
    geometry_brush_piece_t *pPiece ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PIECE_MATERIALIZE_H
