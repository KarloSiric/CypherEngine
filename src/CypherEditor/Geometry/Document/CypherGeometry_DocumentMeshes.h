//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshes.h
//  Purpose: Declares document ownership of editable mesh sources: lookup,
//           add, remove, and whole-mesh replacement with exact source-ID
//           registry bookkeeping.
//  Details: Every mutation here is one atomic "mesh X goes from old to new"
//           step (add = none -> new, remove = old -> none, replace = old ->
//           new with the same root ID). The step:
//             1. validates the incoming mesh completely;
//             2. computes the identity difference between old and new;
//             3. rejects IDs that are live anywhere else in the document
//                (brushes, other meshes) - cross-kind collisions included;
//             4. checks document-wide limits;
//             5. deep-copies the incoming mesh and reserves registry and
//                container capacity before touching shared state;
//             6. retires removed IDs and activates added ones (retired IDs
//                are restored, never reissued) with full rollback;
//             7. publishes the pointer swap.
//           A failure at any step leaves the document exactly as it was.
//
//           Like brush add/remove, these do not advance the document
//           revision: publication is the job of transactions and delta
//           application (see MeshTransaction / MeshDelta), which call these.
//
//           Document-wide mesh limits reuse the existing policy and count
//           every live mesh element in its matching pool: vertices,
//           half-edges, edges, loops, faces, and shells.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESHES_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESHES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

CYPHER_NODISCARD common::usize GeometryDocument_MeshCount( const geometry_document_t *pDocument ) noexcept;

// Mesh at position iMesh in document order (insertion order, with removal
// swap-erasing like brushes). nullptr when out of range.
CYPHER_NODISCARD const mesh_source_t *GeometryDocument_MeshAt(
    const geometry_document_t *pDocument, common::usize iMesh ) noexcept;

CYPHER_NODISCARD const mesh_source_t *GeometryDocument_FindMesh(
    const geometry_document_t *pDocument, geometry_source_id_t meshId ) noexcept;

// Adds a deep copy of *pMesh. The mesh must pass MeshSource_Validate and
// none of its IDs may be live in the document.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddMesh(
    geometry_document_t *pDocument,
    const mesh_source_t *pMesh ) noexcept;

// Removes the mesh and retires all of its IDs.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryRemoveMesh(
    geometry_document_t *pDocument,
    geometry_source_id_t meshId ) noexcept;

// Replaces the mesh with the same root ID by a deep copy of *pMesh. IDs
// present only in the old mesh are retired; IDs present only in the new one
// are activated (a previously retired ID is restored, which is how undo
// brings deleted elements back under their original identity).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryReplaceMesh(
    geometry_document_t *pDocument,
    const mesh_source_t *pMesh ) noexcept;

// Frees every mesh without touching the registry. Only for document
// shutdown, where the registry is discarded as a whole.
void GeometryDocument_FreeAllMeshes( geometry_document_t *pDocument ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_MESHES_H
