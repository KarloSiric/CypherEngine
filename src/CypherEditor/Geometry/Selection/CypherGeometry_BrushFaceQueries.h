//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushFaceQueries.h
//  Purpose: Declares face queries that span several brushes - the coplanar
//           flood fill that selects a whole surface made of many brushes'
//           faces (a floor built from several blocks).
//  Details: A face joins the surface when its side plane faces the same way
//           as the seed's, lies on the same plane (within the policy's
//           coplanar distance and angular tolerances), and its polygon
//           touches or overlaps a face already on the surface. Touching is
//           decided in the seed plane with a separating-axis test, so faces
//           that share only an edge or a corner are connected and faces
//           separated by a gap are not. Back-to-back faces (a floor and the
//           ceiling below it) face opposite ways and never join.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_FACE_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_FACE_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_Policy.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

struct brush_face_ref_t {
    common::u32 iBrush{ 0u }; // index into the brush span
    common::u32 iSide{ 0u };  // side index within that brush
};

// Replaces *pFacesOut (initialized) with the coplanar surface through `seed`,
// sorted by (brush, side), the seed included. INVALID_ARGUMENT for a seed
// that names no brush/side or a side that has no face.
CYPHER_NODISCARD geometry_status_t BrushFaces_TrySelectCoplanar(
    common::span_t<const brush_solid_t *const> brushes,
    brush_face_ref_t seed,
    const geometry_policy_t &policy,
    const common::allocator_t *pAllocator,
    common::vector_t<brush_face_ref_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_FACE_QUERIES_H
