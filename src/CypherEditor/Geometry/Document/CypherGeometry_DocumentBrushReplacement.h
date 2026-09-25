//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushReplacement.h
//  Purpose: Declares atomic exact-identity replacement of document brushes.
//  Details: This is the narrow document publication primitive used by
//           operations, such as CSG, that already produced complete brush
//           solids and assigned every root and side source ID.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_REPLACEMENT_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_REPLACEMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_Document.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Atomically replaces N existing brushes with deep copies of M complete,
// already-ID-assigned brushes.
//
// Identity:
//   - removeBrushIds contains brush root IDs, not side IDs;
//   - every removal root must exist and may appear only once;
//   - output root and side IDs are preserved exactly;
//   - one ID may identify only one output element, across every output root
//     and side;
//   - an output ID may reuse an ID owned by a removed brush or reactivate a
//     retired ID, but may not collide with any retained brush, mesh, root, or
//     component ID in the document;
//   - a fresh output ID must be at or beyond the document identity high-water
//     mark. Arbitrary old IDs must enter through an explicit import remap.
//
// Publication:
//   - all validation, output copies, vector construction, and registry changes
//     happen in private storage;
//   - success publishes the complete brush vector and registry with
//     allocation-free ownership swaps;
//   - any failure leaves document brushes, registry state, and revision
//     unchanged;
//   - success deliberately does not advance revision. The transaction or
//     command that owns this primitive publishes one committed revision.
//
// Empty-range semantics:
//   - N == 0, M > 0 appends exact-ID brushes;
//   - N > 0, M == 0 removes and retires the selected brushes;
//   - N == 0, M == 0 is a successful allocation-free no-op.
//
// Surviving brushes keep document order and storage addresses. Output brushes
// are appended in the order supplied by replacementBrushes.
//
// Surface records: every output gets a record table covering its sides
// (default records, since a bare solid carries none); removed brushes'
// tables are freed with them. See DocumentBrushAttributes.h.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryReplaceBrushesExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    common::span_t<const brush_solid_t> replacementBrushes ) noexcept;

// The same with complete authored brushes: each output keeps its own record
// table (extended with defaults if it does not cover every side), so CSG and
// other rebuilds publish materials and UVs, not just planes.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryReplaceBrushSourcesExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeBrushIds,
    common::span_t<const brush_source_t> replacementBrushes ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_REPLACEMENT_H
