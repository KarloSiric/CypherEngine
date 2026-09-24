//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTransaction.h
//  Purpose: Declares mesh edit transactions (begin / edit working copy /
//           commit or cancel) and the one-shot publish commands for adding
//           and removing whole meshes, all producing undoable mesh deltas.
//  Details: Unlike the brush transaction, which previews in place and
//           restores on cancel, a mesh transaction edits a private working
//           copy. Mesh edits are multi-step topology surgery; keeping them
//           off the document means a half-finished or failed edit can never
//           be observed by snapshots, and cancel is just discarding the
//           copy. Hosts render the working copy for preview.
//
//           Commit:
//             1. refuses if the document revision moved since Begin
//                (STALE_REVISION) - the edit was made against old state;
//             2. gives every new vertex/face a fresh ID from the document
//                registry's allocator (the registry then registers them);
//             3. describes the working copy canonically; if it equals the
//                baseline nothing is published (no revision, no delta);
//             4. replaces the document mesh atomically, fills a REPLACED
//                delta with the baseline and new descriptions, and advances
//                the revision once.
//           Any failure leaves the document untouched and the transaction
//           active, so the caller can fix the edit or cancel.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_TRANSACTION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_TRANSACTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshDelta.h"

namespace cypher::editor::geometry
{

struct geometry_mesh_transaction_t {
    geometry_document_t *pDocument{ nullptr };
    geometry_source_id_t meshId{};
    geometry_revision_t baselineRevision{ GEOMETRY_REVISION_INITIAL };
    // Private allocation cursor for identities tentatively assigned to the
    // working copy. It survives failed commit attempts so a later edit cannot
    // receive an ID already present in that same working copy.
    geometry_source_id_allocator_t tentativeIds{};
    mesh_source_description_t baseline{};
    mesh_source_t working{};
    bool bActive{ false };
};

CYPHER_NODISCARD geometry_status_t GeometryMeshTransaction_Begin(
    geometry_mesh_transaction_t *pTransaction,
    geometry_document_t *pDocument,
    geometry_source_id_t meshId ) noexcept;

// The editable working copy (nullptr when inactive). Topology operations,
// attribute edits, and identity writes all target this.
CYPHER_NODISCARD mesh_source_t *GeometryMeshTransaction_Working(
    geometry_mesh_transaction_t *pTransaction ) noexcept;

// Commits as described above. *pDeltaOut must be initialized; it is
// overwritten on success. *pbChangedOut reports whether anything was
// published (false for an edit that ended where it started).
CYPHER_NODISCARD geometry_status_t GeometryMeshTransaction_Commit(
    geometry_mesh_transaction_t *pTransaction,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut,
    bool *pbChangedOut ) noexcept;

// Gives every unidentified vertex and face of the working copy its final ID
// now, from the transaction's private cursor (the same IDs commit would
// assign). Lets a tool address elements it just created - e.g. the outer
// edge of an edge extrusion for the next pull, or a selection remap - before
// committing. *pAssignedOut (optional) counts the IDs handed out.
CYPHER_NODISCARD geometry_status_t GeometryMeshTransaction_TryAssignIds(
    geometry_mesh_transaction_t *pTransaction,
    common::u32 *pAssignedOut ) noexcept;

void GeometryMeshTransaction_Cancel( geometry_mesh_transaction_t *pTransaction ) noexcept;

CYPHER_NODISCARD bool GeometryMeshTransaction_IsActive(
    const geometry_mesh_transaction_t *pTransaction ) noexcept;

// Publishes a new mesh (built from *pDesc, which must carry IDs not live in
// the document) and returns its ADDED delta. Advances the revision.
CYPHER_NODISCARD geometry_status_t GeometryMeshCommand_TryAdd(
    geometry_document_t *pDocument,
    const mesh_source_description_t *pDesc,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut ) noexcept;

// Removes a mesh and returns its REMOVED delta. Advances the revision.
CYPHER_NODISCARD geometry_status_t GeometryMeshCommand_TryRemove(
    geometry_document_t *pDocument,
    geometry_source_id_t meshId,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_TRANSACTION_H
