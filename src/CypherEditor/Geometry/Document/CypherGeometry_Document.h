//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document.h
//  Purpose: Declares the geometry-local aggregate authoring store.
//  Details: A geometry document owns canonical editable representations,
//           persistent source identity, policy, and committed revision state.
//           It is not a Mason scene document and owns no host objects,
//           entities, materials, selection, renderer state, or UI.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Policy.h"
#include "CypherGeometry_SourceIdRegistry.h"
#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_MeshSource.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Monotonic committed-state revision.
//
// Revision zero represents an initialized document with no committed mutation
// yet. Preview mutations never advance this value. Exactly one successful
// non-no-op transaction commit advances it once.
using geometry_revision_t = common::u64;

inline constexpr geometry_revision_t GEOMETRY_REVISION_INITIAL = 0u;

// Stored representations declared elsewhere (DocumentSurfaces.h includes
// their headers).
struct patch_surface_t;
struct heightfield_t;

// Geometry-local authoritative authoring store.
//
// Gate 3 begins with BrushSolid storage only. Additional canonical
// representations are added to this aggregate when their implementation gates
// become real; the document contract does not predict placeholder pools.
//
// Threading:
//   - exactly one mutable owner/writer;
//   - mutable Document functions are not thread-safe;
//   - background systems consume immutable snapshots.
//
// Identity:
//   - sourceIds owns the document's persistent identity domain;
//   - brushes stored here must use IDs owned by sourceIds;
//   - side IDs are also members of the same document identity domain.
//
// Revision:
//   - revision describes committed authored state;
//   - direct internal preview replacement does not publish a revision;
//   - public transaction commit is responsible for publication.
//
// Storage:
//   - brush_solid_t contains a vector_t (non-copyable/non-movable), so the
//     document stores individually allocated brush pointers instead of inline
//     brush values. Each brush is allocated through the document's allocator
//     and freed on removal or shutdown.
struct geometry_document_t {
    common::vector_t<brush_solid_t *> brushes{};
    // Surface records (material + UV projection) of each brush, parallel to
    // `brushes`: brushAttributes.pData[i] belongs to brushes.pData[i], and
    // every side's iAttributeIndex of that brush resolves in it. A brush
    // added without records gets default ones covering its indices, so the
    // pairing always holds. Kept parallel (rather than storing
    // brush_source_t) so the many readers of `brushes` are unaffected; every
    // mutation of `brushes` updates this vector in the same step. See
    // DocumentBrushAttributes.h.
    common::vector_t<geometry_brush_side_attribute_store_t *> brushAttributes{};
    // Bezier patches and heightfields (terrain), individually allocated and
    // managed through DocumentSurfaces.h. Declared as opaque pointers here so
    // this header does not pull the representations into every user.
    common::vector_t<patch_surface_t *> patches{};
    common::vector_t<heightfield_t *> heightFields{};
    // Editable mesh sources, individually allocated for the same reason as
    // brushes (non-movable members). Managed through DocumentMeshes.h.
    common::vector_t<mesh_source_t *> meshes{};

    geometry_source_id_registry_t sourceIds{};
    geometry_policy_t policy{};

    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };

    const common::allocator_t *pAllocator{ nullptr };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Registry capacity (maximum claimed source IDs) implied by a policy: every
// brush and side, plus every mesh root, vertex, and face. Mesh roots are
// bounded by cShellsMax because each mesh source owns at least one shell.
// Init, snapshot validation, and serialization all use this one function so
// the budget cannot drift between them. Returns 0 if the sum overflows.
CYPHER_NODISCARD common::u64 GeometryDocument_SourceIdCapacity(
    const geometry_policy_t &policy ) noexcept;

CYPHER_NODISCARD geometry_status_t GeometryDocument_Init(
    geometry_document_t *pDocument,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy ) noexcept;

void GeometryDocument_Shutdown(
    geometry_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool GeometryDocument_IsInitialized(
    const geometry_document_t *pDocument ) noexcept;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_revision_t GeometryDocument_GetRevision(
    const geometry_document_t *pDocument ) noexcept;

CYPHER_NODISCARD common::usize GeometryDocument_BrushCount(
    const geometry_document_t *pDocument ) noexcept;

// ---------------------------------------------------------------------------
// Brush lookup
// ---------------------------------------------------------------------------

// Returns a borrowed immutable pointer to canonical document storage.
// The pointer remains valid only until a document mutation that can relocate
// brush storage. Long-lived consumers must use source IDs or snapshots.
CYPHER_NODISCARD const brush_solid_t *GeometryDocument_FindBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// Returns a borrowed mutable pointer. Used internally by transactions —
// not part of the public editing API.
CYPHER_NODISCARD brush_solid_t *GeometryDocument_FindBrushMutable(
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// ---------------------------------------------------------------------------
// Brush publication
// ---------------------------------------------------------------------------

// Deep-copies pBrush into document-owned storage and registers all its
// source IDs (brush + sides) in the document's identity domain.
//
// Gate 3 uses this operation for controlled document construction.
// Interactive edits go through Transactions.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddBrush(
    geometry_document_t *pDocument,
    const brush_solid_t *pBrush ) noexcept;

// Removes one brush from live document storage.
//
// Removal retires the brush and side IDs from live membership but never
// makes those identities reusable. Transaction undo may explicitly restore
// retired identities through the SourceIdRegistry contract.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryRemoveBrush(
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// ---------------------------------------------------------------------------
// Internal helpers for Transactions
// ---------------------------------------------------------------------------

// Deep-copies a brush_solid_t: initializes pDst with pAllocator, then copies
// all sides from pSrc. On failure pDst is left in a clean shutdown state.
CYPHER_NODISCARD geometry_status_t BrushSolid_DeepCopy(
    brush_solid_t *pDst,
    const brush_solid_t *pSrc,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_H
