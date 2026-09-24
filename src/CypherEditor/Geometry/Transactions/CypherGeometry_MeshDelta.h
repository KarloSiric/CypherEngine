//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshDelta.h
//  Purpose: Declares undo/redo records for mesh sources: added, removed,
//           or replaced, each carrying canonical descriptions of the mesh
//           before and/or after the change.
//  Details: Why descriptions and not mesh copies: a canonical description
//           is compact (flat arrays, no pool slack or handles), exactly
//           comparable, and independent of pool history, so applying a
//           delta can first prove the document is in the state the delta
//           expects. An undo applied out of order (the mesh was changed by
//           something else since) is refused with STALE_REVISION instead of
//           silently overwriting newer work.
//
//           Applying a delta is a publication: it advances the document
//           revision exactly once, like a transaction commit, so snapshot
//           and cook consumers keyed on revision see every undo and redo.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_DELTA_H
#define CYPHER_EDITOR_GEOMETRY_MESH_DELTA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_DocumentMeshes.h"

namespace cypher::editor::geometry
{

enum class geometry_mesh_delta_kind_t : common::u8 {
    INVALID       = 0u,
    MESH_ADDED    = 1u, // after valid
    MESH_REMOVED  = 2u, // before valid
    MESH_REPLACED = 3u  // before and after valid, same root ID
};

struct geometry_mesh_delta_t {
    geometry_mesh_delta_kind_t kind{ geometry_mesh_delta_kind_t::INVALID };
    geometry_source_id_t meshId{};
    mesh_source_description_t before{};
    mesh_source_description_t after{};
};

// Initializes an empty delta (kind INVALID) whose descriptions use pAllocator.
CYPHER_NODISCARD geometry_status_t GeometryMeshDelta_Init(
    geometry_mesh_delta_t *pDelta,
    const common::allocator_t *pAllocator ) noexcept;

void GeometryMeshDelta_Shutdown( geometry_mesh_delta_t *pDelta ) noexcept;

CYPHER_NODISCARD bool GeometryMeshDelta_IsInitialized( const geometry_mesh_delta_t *pDelta ) noexcept;

// Turns the delta into its own inverse in place (ADDED <-> REMOVED,
// REPLACED swaps before/after). Never allocates, so undo stacks can flip a
// record without a failure path.
void GeometryMeshDelta_Invert( geometry_mesh_delta_t *pDelta ) noexcept;

// Applies the delta to the document after checking its precondition:
//   ADDED    - no mesh with meshId exists;
//   REMOVED  - the mesh exists and describes exactly as `before`;
//   REPLACED - the mesh exists and describes exactly as `before`.
// A failed precondition returns STALE_REVISION and changes nothing. On
// success the document revision advances by one (*pNewRevisionOut).
CYPHER_NODISCARD geometry_status_t GeometryMeshDelta_TryApply(
    const geometry_mesh_delta_t *pDelta,
    geometry_document_t *pDocument,
    geometry_revision_t *pNewRevisionOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_DELTA_H
