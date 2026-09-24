//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsgCommands.h
//  Purpose: Declares document-level brush CSG commands: subtract,
//           intersect, hollow, and convex merge.
//  Details: Each command reads the brushes as the transaction currently
//           previews them, runs the pure piece algorithm, materializes the
//           results, and previews the replacement. Commands are atomic at
//           the preview level: either every preview change lands or the
//           transaction is left exactly as it was. They compose freely in
//           one transaction and commit as one undo step.
//
//           Identity policy: the first result of an operation on a brush
//           keeps that brush's ID (and the IDs of its untouched sides);
//           additional fragments are new brushes. Consumed operands are
//           removed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_COMMANDS_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_COMMANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushCsg.h"
#include "CypherGeometry_PieceMaterialize.h"

namespace cypher::editor::geometry
{

struct geometry_csg_command_result_t {
    // Brushes modified in place, created, and removed by the command.
    common::u32 cModified{ 0u };
    common::u32 cCreated{ 0u };
    common::u32 cRemoved{ 0u };
    // For single-result commands (intersect, merge): the result brush.
    geometry_source_id_t resultBrushId{};
};

// Default bound on fragments produced for one target brush.
inline constexpr common::usize CSG_FRAGMENTS_PER_TARGET_MAX = 1024u;

// Carves every cutter out of every target. Targets that no cutter touches
// are left alone; a target fully inside the cutters is removed. When
// bRemoveCutters is true the cutters are removed afterwards (the usual
// "carve" behavior); otherwise they stay. A brush listed as both cutter and
// target is treated as a cutter only.
CYPHER_NODISCARD geometry_status_t CsgCommand_TrySubtract(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> cutterIds,
    common::span_t<const geometry_source_id_t> targetIds,
    common::bool_t bRemoveCutters,
    geometry_csg_command_result_t *pResultOut ) noexcept;

// Replaces the brushes with their common intersection, kept under the first
// brush's ID; the others are removed. Needs at least two brushes. An empty
// intersection is refused with DEGENERATE rather than deleting the inputs.
CYPHER_NODISCARD geometry_status_t CsgCommand_TryIntersect(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    geometry_csg_command_result_t *pResultOut ) noexcept;

// Replaces a brush with walls of the given thickness around its former
// interior. DEGENERATE when the brush is too thin.
CYPHER_NODISCARD geometry_status_t CsgCommand_TryHollow(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::f64 thickness,
    geometry_csg_command_result_t *pResultOut ) noexcept;

// Replaces the brushes with their convex hull, kept under the first
// brush's ID. Needs at least two brushes.
CYPHER_NODISCARD geometry_status_t CsgCommand_TryMerge(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    geometry_csg_command_result_t *pResultOut ) noexcept;

// Offsets every side of a brush outward by `distance` (negative shrinks).
// Surfacing is unchanged. The brush and all side IDs are kept.
CYPHER_NODISCARD geometry_status_t CsgCommand_TryExpand(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::f64 distance ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CSG_COMMANDS_H
