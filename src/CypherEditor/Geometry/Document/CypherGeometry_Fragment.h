//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Fragment.h
//  Purpose: Declares geometry fragments: a detached set of authored objects
//           (brushes with their surface records, meshes, patches,
//           heightfields) for copy, paste, duplicate, and moving geometry
//           between documents.
//  Details: A fragment owns deep copies and is independent of any
//           document. Its objects keep the IDs they had where they came
//           from until they are remapped; inserting a fragment back into a
//           document that still holds those IDs is IDENTITY_CONFLICT, so a
//           paste or duplicate first remaps the fragment to fresh IDs of the
//           destination (GeometryFragment_TryRemapForDocument). The remap
//           table is returned as provenance: which new object came from
//           which old one, element by element.
//
//           Insertion is all-or-nothing: if any object is rejected, every
//           object already added is taken out again and the identity
//           registry is restored exactly (DocumentRollback.h).
//
//           Text form: a fragment saves as an ordinary geometry document
//           (schema cypher.geometry), which is what goes on the system
//           clipboard; loading reads one back.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_FRAGMENT_H
#define CYPHER_EDITOR_GEOMETRY_FRAGMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_Patch.h"

#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::editor::geometry
{

struct geometry_fragment_t {
    common::vector_t<brush_source_t *> brushes{};
    common::vector_t<mesh_source_t *> meshes{};
    common::vector_t<patch_surface_t *> patches{};
    common::vector_t<heightfield_t *> heightFields{};
    const common::allocator_t *pAllocator{ nullptr };
};

CYPHER_NODISCARD geometry_status_t GeometryFragment_Init( geometry_fragment_t *pFragment, const common::allocator_t *pAllocator ) noexcept;
void GeometryFragment_Shutdown( geometry_fragment_t *pFragment ) noexcept;
CYPHER_NODISCARD bool GeometryFragment_IsInitialized( const geometry_fragment_t *pFragment ) noexcept;
CYPHER_NODISCARD common::usize GeometryFragment_ObjectCount( const geometry_fragment_t *pFragment ) noexcept;
// Frees every object, keeping the fragment initialized.
void GeometryFragment_Clear( geometry_fragment_t *pFragment ) noexcept;

// Appends deep copies of the document objects named by their root IDs
// (brushes, meshes, patches, heightfields in any mix), keeping their IDs.
// Unknown ID -> INVALID_HANDLE; an ID listed twice -> INVALID_ARGUMENT.
// Failure leaves the fragment unchanged.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryExtract(
    const geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> rootIds,
    geometry_fragment_t *pFragment ) noexcept;

// Appends copies of every object in the document.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryExtractAll(
    const geometry_document_t *pDocument,
    geometry_fragment_t *pFragment ) noexcept;

// Appends deep copies of objects built outside any document (generators,
// importers), keeping their IDs; any span may be empty. Brushes are
// validated under `policy`. All or nothing: failure leaves the fragment
// unchanged.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryAppendCopies(
    geometry_fragment_t *pFragment,
    const geometry_policy_t &policy,
    common::span_t<const brush_source_t> brushes,
    common::span_t<const mesh_source_t> meshes,
    common::span_t<const patch_surface_t> patches,
    common::span_t<const heightfield_t> heightFields ) noexcept;

// Gives every ID in the fragment a fresh ID of the destination document
// (beyond its identity high-water mark, so never a live or retired one).
// The document itself is not changed - inserting registers the IDs.
// pRemapOut (optional, canonical empty) receives old -> new entries sorted
// by old ID. Failure leaves the fragment unchanged.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryRemapForDocument(
    geometry_fragment_t *pFragment,
    const geometry_document_t *pDestination,
    geometry_source_id_remap_t *pRemapOut ) noexcept;

// Moves everything by `offset` (brush and patch textures move with their
// geometry - texture lock - and mesh UVs stay on their corners). A mesh,
// patch, or heightfield coordinate leaving its representation's range, or
// a non-finite brush plane, is NUMERIC_FAILURE and changes nothing. Brush
// extents are policy-dependent (the fragment has no policy), so the
// destination checks them on insert.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryTranslate(
    geometry_fragment_t *pFragment,
    math::vec3d_t offset ) noexcept;

// Adds every object to the document, all or nothing (see the file
// comment). pRootIdsOut (optional, initialized) receives the inserted root
// IDs: brushes, then meshes, patches, heightfields. Does not advance the
// document revision.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryInsert(
    const geometry_fragment_t *pFragment,
    geometry_document_t *pDocument,
    common::vector_t<geometry_source_id_t> *pRootIdsOut ) noexcept;

// Duplicate within one document: extract, remap to fresh IDs, translate,
// insert - Hammer's copy-drag / Ctrl+D. pNewRootIdsOut and pRemapOut as
// above (optional).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryDuplicate(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> rootIds,
    math::vec3d_t offset,
    common::vector_t<geometry_source_id_t> *pNewRootIdsOut,
    geometry_source_id_remap_t *pRemapOut ) noexcept;

// Clipboard text: the fragment as a geometry document, and back.
CYPHER_NODISCARD geometry_status_t GeometryFragment_TrySaveToText(
    const geometry_fragment_t *pFragment,
    const geometry_policy_t &policy,
    common::text_buffer_t *pTextOut ) noexcept;
CYPHER_NODISCARD geometry_status_t GeometryFragment_TryLoadFromText(
    common::string_view_t text,
    const geometry_policy_t &policy,
    geometry_fragment_t *pFragment ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_FRAGMENT_H
