//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCutting.h
//  Purpose: Declares clip and split operations for brushes inside a
//           geometry transaction.
//  Details: Mirrors the map-editor clip tool: a plane, usually built from
//           three points, cuts the brush; the host chooses to keep the
//           back half (inside n·p + d <= 0), the front half, or both. The
//           kept part that survives in place keeps the brush's identity and
//           the IDs of every side it still has; a second half from a split
//           is a new brush. The new cut face copies surfacing from the side
//           whose normal is closest to the cut normal.
//
//           All edits are previews in the caller's transaction, so repeated
//           clip updates while dragging clip points collapse into one
//           commit, and cancel restores everything.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CUTTING_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CUTTING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PieceMaterialize.h"

namespace cypher::editor::geometry
{

enum class geometry_clip_keep_t : common::u8 {
    BACK = 0u,   // keep the part inside the plane's half-space
    FRONT,       // keep the part outside
    BOTH,        // split into two brushes
    COUNT
};

struct geometry_clip_result_t {
    // Brush holding the back / front part after the operation; invalid when
    // that part was not kept or is empty.
    geometry_source_id_t backBrushId{};
    geometry_source_id_t frontBrushId{};
    // False when the plane does not pass through the brush: nothing changed.
    common::bool_t bCut{ false };
};

// Builds the clip plane through three points, oriented so that
// (b - a) x (c - a) is its normal. DEGENERATE for collinear or coincident
// points.
CYPHER_NODISCARD geometry_status_t BrushCutting_TryPlaneFromPoints(
    const geometry_numerical_policy_t &policy,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    math::planed_t *pPlaneOut ) noexcept;

// Clips one brush. When the plane misses the brush the preview is
// unchanged and bCut is false. Keeping only a half that is empty would
// delete the brush; that is refused with DEGENERATE (use a remove preview
// to delete). On failure the transaction's previews are unchanged.
CYPHER_NODISCARD geometry_status_t BrushCutting_TryClip(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::planed_t plane,
    geometry_clip_keep_t keep,
    geometry_clip_result_t *pResultOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CUTTING_H
