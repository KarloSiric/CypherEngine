//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentRollback.h
//  Purpose: Internal helpers that undo a batch of appended document objects
//           without allocating, for multi-object operations (fragment
//           insertion) that must be all-or-nothing.
//  Details: Every document store appends new objects at the end, so a batch
//           is undone by recording the store sizes before it, freeing what
//           lies beyond them, and swapping back a registry clone taken
//           before the batch. None of this can fail, which is the point:
//           a rollback that could run out of memory would leave a half
//           inserted batch behind.
//
//           Not part of the public geometry API.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_ROLLBACK_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_ROLLBACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"

namespace cypher::editor::geometry
{

struct geometry_document_counts_t {
    common::usize cBrushes{ 0u };
    common::usize cMeshes{ 0u };
    common::usize cPatches{ 0u };
    common::usize cHeightFields{ 0u };
};

CYPHER_NODISCARD geometry_document_counts_t GeometryDocument_InternalCounts( const geometry_document_t *pDocument ) noexcept;

// Frees every object stored beyond `counts` (brushes with their record
// tables, meshes, patches, heightfields). The registry is not touched.
void GeometryDocument_InternalTruncate( geometry_document_t *pDocument, const geometry_document_counts_t &counts ) noexcept;

// Exchanges the document's identity registry with *pRegistry.
void GeometryDocument_InternalSwapRegistry( geometry_document_t *pDocument, geometry_source_id_registry_t *pRegistry ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_ROLLBACK_H
