//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComposition.h
//  Purpose: Declares exact composition operations for authored mesh sources.
//  Details: Composition changes which document mesh root owns topology. It is
//           deliberately separate from welding and Boolean union: joining
//           sources preserves disconnected shells, component identities, and
//           every authored attribute exactly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPOSITION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPOSITION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

struct mesh_source_join_report_t {
    common::u32 cInputSources{ 0u };
    common::u32 cVertices{ 0u };
    common::u32 cFaces{ 0u };
    common::u32 cCorners{ 0u };
    common::u32 cAttributedEdges{ 0u };
    common::u32 cShells{ 0u };
};

// Aggregates two or more complete mesh sources into one multi-shell source.
//
// This is an exact ownership composition operation:
//   - retainedRootId must be the root ID of exactly one input;
//   - the retained root ID survives and the other input root IDs disappear;
//   - every vertex ID, face ID, corner attribute, face attribute, and
//     non-default edge attribute survives exactly;
//   - coincident vertices are not welded;
//   - disconnected shells are not connected;
//   - inputs are never modified.
//
// All input-owned root, vertex, and face IDs must be globally unique. This is
// the same invariant a geometry document enforces and lets the pure operation
// reject accidental cross-document/corrupt inputs before donor roots vanish.
//
// pOutput must be canonical-empty and must not alias an input. Failure leaves
// it canonical-empty. The operation is allocation-failure atomic.
CYPHER_NODISCARD geometry_status_t MeshSource_TryJoinExact(
    common::span_t<const mesh_source_t *const> inputs,
    geometry_source_id_t retainedRootId,
    const common::allocator_t *pAllocator,
    mesh_source_t *pOutput,
    mesh_source_join_report_t *pReportOut = nullptr ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_COMPOSITION_H
