//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Map-owned mesh authoring adapters over persistent geometry source IDs.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_MESH_EDIT_H
#define CYPHER_EDITOR_MAP_MESH_EDIT_H
#pragma once

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

// PRIVATE working-copy operations: clone with MapEdit_Clone and discard that
// copy after any failure. A batch can have changed an earlier object before
// failing. These functions perform no I/O and create no editor history.
//
// Converts the actual reconstructed convex boundaries to authored meshes.
// Roots and brush side -> mesh face IDs survive; only vertices receive fresh
// monotonic IDs. Materials, evaluated corner UVs, smoothing, layer, ownership,
// and retained root/face data survive. Unknown fields whose names conflict
// with the destination mesh schema are rejected instead of silently lost.
// pRootsOut must be initialized and remains unchanged on any failure.
CYPHER_NODISCARD map_status_t MapMeshEdit_ConvertBrushes(
    map_document_t *pMap,
    common::span_t<const common::u64> ids,
    common::vector_t<common::u64> *pRootsOut ) noexcept;

// Reverses every face of every selected authored mesh. Positions, persistent
// root/vertex/face identities, materials and each (face, vertex) corner's UVs
// and colors stay unchanged. Whole shells remain intact. Mixed-kind, empty,
// duplicate-ID and read-only selections are rejected.
CYPHER_NODISCARD map_status_t MapMeshEdit_FlipNormals(
    map_document_t *pMap,
    common::span_t<const common::u64> ids ) noexcept;

// Triangulates every non-triangle face of authored mesh selections through
// the source modeling operation. Existing vertex IDs and one face ID per
// original polygon survive; new triangles receive fresh IDs. Materials,
// corner attributes and unknown face records follow the backend's exact
// parent lineage. Fresh IDs start above both map and geometry high-water
// marks; exhaustion of either namespace is rejected. Already triangulated
// selections are a successful no-op and preserve both allocation cursors.
CYPHER_NODISCARD map_status_t MapMeshEdit_Triangulate(
    map_document_t *pMap,
    common::span_t<const common::u64> ids ) noexcept;

// Extrudes one authored mesh face outward along its normal. Distance must
// be finite and strictly positive; zero/negative distances are rejected
// before either allocation cursor changes. The cap retains the original
// face ID. New side faces inherit materials, corner attributes and unknown
// face records through the modeling backend's explicit parent lineage.
// Fresh vertex/face IDs begin above both map and geometry high-water marks.
// Root metadata, layer and ownership survive. PRIVATE working-copy contract
// above applies: discard the copy after any failure, including residual-data
// publication failure after the geometry transaction commits.
CYPHER_NODISCARD map_status_t MapMeshEdit_ExtrudeFace(
    map_document_t *pMap,
    common::u64 meshId,
    common::u64 faceId,
    common::f64 distance ) noexcept;

// Insets one authored mesh face by a finite, strictly positive corner distance
// toward its centroid (radial inset). This is not a uniform perpendicular
// border width; unequal corner radii can produce different ring widths.
// The inner face retains the original face ID; new ring faces inherit the
// original face's attributes and residual records via backend lineage.
// Invalid/collapsing margins fail instead of publishing degenerate geometry.
// Identity allocation and PRIVATE working-copy rules match ExtrudeFace.
CYPHER_NODISCARD map_status_t MapMeshEdit_InsetFace(
    map_document_t *pMap,
    common::u64 meshId,
    common::u64 faceId,
    common::f64 margin ) noexcept;

// Slices one authored quad into cU x cV cells. Counts are 1..64; 1 x 1
// is an unchanged success even when identity/revision cursors are exhausted.
// U follows corner 0 -> 1 and V corner 1 -> 2 in the source face's winding.
// The corner-0 cell keeps the face ID; adjacent faces receive shared edge
// vertices and retain their shape/IDs, so the result has no T-junctions.
// Materials, interpolated corner UVs/colors and retained face records follow
// explicit source lineage. This is topology slicing, not smooth subdivision.
// PRIVATE working-copy rules and identity allocation match ExtrudeFace.
CYPHER_NODISCARD map_status_t MapMeshEdit_QuadSliceFace(
    map_document_t *pMap,
    common::u64 meshId,
    common::u64 faceId,
    common::u32 cU,
    common::u32 cV ) noexcept;

} // namespace cypher::editor::map
#endif
