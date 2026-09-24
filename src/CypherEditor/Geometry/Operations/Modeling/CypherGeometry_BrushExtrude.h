//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushExtrude.h
//  Purpose: Declares face extrusion of brushes, including TrenchBroom's
//           split-extrude modes.
//  Details: Three behaviors, all previews in the caller's transaction:
//
//           - GROW moves the side outward (or inward for negative
//             distances) in place; identical to a side drag.
//           - SPLIT_OUT (distance > 0) leaves the brush untouched and adds
//             a new slab brush swept from the face: the brush's other
//             sides, the side moved out by `distance`, and the original
//             face plane reversed. This is the "extrude to new brush"
//             gesture for building walls and ledges.
//           - SPLIT_IN (distance > 0) cuts the brush `distance` inside the
//             face, producing a new slab brush of that thickness next to
//             the face and shrinking the original.
//
//           New brushes copy surfacing from the source brush: the moved
//           face from the extruded side, the new back face from it too
//           (flipped), side walls from the matching walls.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_EXTRUDE_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_EXTRUDE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Propagation.h"
#include "CypherGeometry_PieceMaterialize.h"

namespace cypher::editor::geometry
{

enum class geometry_extrude_mode_t : common::u8 {
    GROW = 0u,
    SPLIT_OUT,
    SPLIT_IN,
    COUNT
};

// Extrudes one side. *pNewBrushIdOut (optional) receives the slab created
// by the split modes (invalid for GROW). INVALID_ARGUMENT when the side is
// not on the brush or the split distance is not positive; DEGENERATE when
// the result would have no volume or SPLIT_IN would consume the brush.
CYPHER_NODISCARD geometry_status_t BrushExtrude_TryExtrudeSide(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::f64 distance,
    geometry_extrude_mode_t mode,
    geometry_texture_lock_t lock,
    geometry_source_id_t *pNewBrushIdOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_EXTRUDE_H
