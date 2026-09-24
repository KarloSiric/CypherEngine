//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexEdit.h
//  Purpose: Declares vertex, edge, and face-component editing of convex
//           brushes (the map-editor vertex tool).
//  Details: Moving components moves the underlying boundary vertices; the
//           brush is then rebuilt as the convex hull of the moved point
//           set, exactly how Radiant-family editors implement vertex
//           editing on plane-defined brushes. The hull's faces are matched
//           back to the old sides by vertex correspondence, so a face that
//           was merely tilted keeps its side ID and surfacing; genuinely
//           new faces (a quad split into two triangles by a non-planar
//           move) get fresh IDs and copy surfacing from the closest side.
//
//           The edit is refused (DEGENERATE) when a moved vertex would
//           disappear into the hull, when the points collapse to a plane,
//           or when the result fails brush validation. The last valid
//           preview is kept.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_EDIT_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_EDIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PieceMaterialize.h"
#include "CypherGeometry_Selection.h"

namespace cypher::editor::geometry
{

struct geometry_vertex_edit_result_t {
    common::u32 cMovedVertices{ 0u };
    common::u32 cKeptSides{ 0u };   // sides that kept their identity
    common::u32 cNewSides{ 0u };    // faces created by the edit
    common::u32 cLostSides{ 0u };   // sides that no longer contribute a face
};

// Moves every boundary vertex referenced by the components (vertices, the
// two ends of edges, every vertex of sides; BRUSH references move all
// vertices, i.e. translate) by `delta`. All components must belong to
// brushId. INVALID_ARGUMENT for foreign or unresolvable components.
CYPHER_NODISCARD geometry_status_t BrushVertexEdit_TryMoveComponents(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    common::span_t<const geometry_component_ref_t> components,
    math::vec3d_t delta,
    geometry_vertex_edit_result_t *pResultOut ) noexcept;

// Moves explicit boundary vertex positions: vertex i of the brush's
// current preview boundary goes to positions[i] (same count as the
// boundary). Used by tools that edit several vertices independently.
CYPHER_NODISCARD geometry_status_t BrushVertexEdit_TrySetVertexPositions(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    common::span_t<const math::vec3d_t> positions,
    geometry_vertex_edit_result_t *pResultOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VERTEX_EDIT_H
