//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentSurfaces.h
//  Purpose: Declares the geometry document's patch and heightfield stores,
//           so curved patches and terrain live, save, snapshot, and cook
//           alongside brushes and meshes.
//  Details: Same ownership model as meshes: the document owns individually
//           allocated deep copies, keyed by root source ID, and every ID an
//           object carries (patch root + every control; heightfield root +
//           every tile) is part of the document's single identity domain.
//           Add, remove, and replace are failure-atomic, including the
//           identity registry (see DocumentIdentity.h); replace keeps the
//           root ID and may change the rest (a patch that gained a column
//           gains control IDs).
//
//           Undo at this level is whole-object: a command keeps the object
//           as it was and replaces it back.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_SURFACES_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_SURFACES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_Patch.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kGeometryDocumentPatchesMax = 65536u;
inline constexpr common::usize kGeometryDocumentHeightFieldsMax = 4096u;

// ---------------------------------------------------------------------------
// Patches
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize GeometryDocument_PatchCount( const geometry_document_t *pDocument ) noexcept;
// Borrowed until the next document mutation; nullptr when absent.
CYPHER_NODISCARD const patch_surface_t *GeometryDocument_PatchAt( const geometry_document_t *pDocument, common::usize iPatch ) noexcept;
CYPHER_NODISCARD const patch_surface_t *GeometryDocument_FindPatch( const geometry_document_t *pDocument,
                                                                    geometry_source_id_t patchId ) noexcept;

// Adds a deep copy. The patch must pass Patch_Validate and none of its IDs
// may be live in the document (IDENTITY_CONFLICT). Invalid patch -> the
// status matching its fault; over kGeometryDocumentPatchesMax or the
// document's identity capacity -> LIMIT_EXCEEDED.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddPatch( geometry_document_t *pDocument,
                                                                 const patch_surface_t *pPatch ) noexcept;
// Removes a patch; its IDs retire. Unknown ID -> INVALID_ARGUMENT.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryRemovePatch( geometry_document_t *pDocument,
                                                                    geometry_source_id_t patchId ) noexcept;
// Replaces the patch with the same root ID by a deep copy of pPatch.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryReplacePatch( geometry_document_t *pDocument,
                                                                     const patch_surface_t *pPatch ) noexcept;

// ---------------------------------------------------------------------------
// Heightfields
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize GeometryDocument_HeightFieldCount( const geometry_document_t *pDocument ) noexcept;
CYPHER_NODISCARD const heightfield_t *GeometryDocument_HeightFieldAt( const geometry_document_t *pDocument,
                                                                      common::usize iField ) noexcept;
CYPHER_NODISCARD const heightfield_t *GeometryDocument_FindHeightField( const geometry_document_t *pDocument,
                                                                        geometry_source_id_t fieldId ) noexcept;

// As the patch functions, with HeightField_Validate.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddHeightField( geometry_document_t *pDocument,
                                                                       const heightfield_t *pField ) noexcept;
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryRemoveHeightField( geometry_document_t *pDocument,
                                                                          geometry_source_id_t fieldId ) noexcept;
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryReplaceHeightField( geometry_document_t *pDocument,
                                                                           const heightfield_t *pField ) noexcept;

// Appends the IDs of every stored patch (root + controls) and heightfield
// (root + tiles), unsorted. The document-wide identity audits (snapshot,
// save, mesh-set publication) use it to account for every live ID.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCollectSurfaceIds(
    const geometry_document_t *pDocument,
    common::vector_t<geometry_source_id_t> *pIdsOut ) noexcept;

// Frees every stored patch and heightfield (document shutdown).
void GeometryDocument_FreeAllSurfaces( geometry_document_t *pDocument ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_SURFACES_H
