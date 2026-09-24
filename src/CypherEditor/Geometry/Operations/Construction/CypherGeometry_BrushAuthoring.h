//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushAuthoring.h
//  Purpose: Declares authored-brush edits that create or reshape brushes
//           while carrying their surfaces (material + UV projection) along:
//           the extrude tool's split mode and mirroring.
//  Details: These act on brush_source_t (planes plus their surface records),
//           because a brush that gains or loses sides must keep every
//           remaining side bound to the right record - an index into a
//           store that is not moved with it would dangle. New brushes get a
//           compact store holding exactly the records their sides use
//           (records shared by several sides stay shared).
//
//           All functions are failure-atomic: sources, outputs, and the ID
//           allocator are unchanged unless the call returns OK.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_AUTHORING_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_AUTHORING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// The extrude tool's split mode on side iSide, `distance` along its normal:
//   - distance > 0: the brush is unchanged and *pNewOut is the brush the
//     face would sweep out - together the two have exactly the shape an
//     ordinary extrude would have produced;
//   - distance < 0: the brush is cut |distance| inside the face; the brush
//     keeps the inner part (the face, with its identity and surface, moves
//     in) and *pNewOut is the slab next to the face.
// Every side of the new brush copies the surface of the side it came from
// (so textures continue across the seam); the internal face against the
// original brush gets the extruded face's surface. The new brush and its
// sides get fresh IDs. *pNewOut must be canonical empty. A distance that
// leaves either part without volume is DEGENERATE.
CYPHER_NODISCARD geometry_status_t BrushAuthoring_TryExtrudeSplit(
    brush_source_t *pSource,
    common::usize iSide,
    common::f64 distance,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    brush_source_t *pNewOut ) noexcept;

// Reflects the brush across `mirror` (unit normal). Reflection maps each
// side's half-space onto a half-space, so the brush keeps the same sides,
// identities, and bindings. With bTextureLock every surface projection is
// reflected with it, so each mirrored point keeps exactly the UV it had
// (the texture is mirrored along with the geometry); without it, each
// reflected side gets a world-aligned projection for its new orientation
// (keeping its material and texture scale). A shared record is reflected
// once.
CYPHER_NODISCARD geometry_status_t BrushAuthoring_TryMirror(
    brush_source_t *pSource,
    math::planed_t mirror,
    bool bTextureLock,
    const geometry_policy_t &policy ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_AUTHORING_H
