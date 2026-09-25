//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushAttributes.h
//  Purpose: Declares the brush surface records the geometry document keeps
//           next to its brushes (material + UV projection per side), and
//           publishing whole authored brushes (brush_source_t) into it.
//  Details: A brush side stores only an index into a per-brush record
//           table, so a document that kept the solid alone would lose every
//           material and UV decision on save, undo, or snapshot. The
//           document therefore owns one record table per brush
//           (geometry_document_t::brushAttributes, parallel to brushes) and
//           maintains one invariant: every side's iAttributeIndex resolves
//           in its brush's table.
//
//           Brushes that arrive without records (the legacy
//           GeometryDocument_TryAddBrush, older files, older deltas) get
//           default records - no material, an axis-aligned unit projection -
//           appended until every index resolves. The solid itself is never
//           rewritten, so identities and bindings survive exactly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_ATTRIBUTES_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_ATTRIBUTES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_Document.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Record tables
// ---------------------------------------------------------------------------

// Document-owned tables are individually allocated (like brushes) because
// the table type is non-movable. Free is safe on nullptr.
CYPHER_NODISCARD geometry_brush_side_attribute_store_t *BrushAttributes_Allocate(
    const common::allocator_t *pAllocator ) noexcept;
void BrushAttributes_Free(
    const common::allocator_t *pAllocator,
    geometry_brush_side_attribute_store_t *pStore ) noexcept;

// True when every side of pSolid addresses a record of pStore.
CYPHER_NODISCARD bool BrushAttributes_Covers(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pStore ) noexcept;

// Builds *pOut (canonical empty) as a copy of pBase - or an empty table when
// pBase is null - extended with default records until every side of pSolid
// resolves. Invalid base records -> their validation status; more records
// than cBrushSidesPerBrushMax -> LIMIT_EXCEEDED. Failure leaves *pOut empty.
CYPHER_NODISCARD geometry_status_t BrushAttributes_TryBuildCovering(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pBase,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_brush_side_attribute_store_t *pOut ) noexcept;

// ---------------------------------------------------------------------------
// Document lookup
// ---------------------------------------------------------------------------

// The record table of a brush, or nullptr for an unknown brush. Borrowed
// until the next document mutation, like GeometryDocument_FindBrush.
CYPHER_NODISCARD const geometry_brush_side_attribute_store_t *GeometryDocument_FindBrushAttributes(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// Mutable access for transaction and delta code, which keep the covering
// invariant themselves. Not part of the public editing API.
CYPHER_NODISCARD geometry_brush_side_attribute_store_t *GeometryDocument_FindBrushAttributesMutable(
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept;

// The table of the brush at document position i (GeometryDocument order),
// or nullptr when out of range.
CYPHER_NODISCARD const geometry_brush_side_attribute_store_t *GeometryDocument_BrushAttributesAt(
    const geometry_document_t *pDocument,
    common::usize iBrush ) noexcept;

// ---------------------------------------------------------------------------
// Publication
// ---------------------------------------------------------------------------

// GeometryDocument_TryAddBrush with the brush's own records: pAttributes
// (may be null for defaults) is copied and extended to cover the brush.
// Rejections as TryAddBrush, plus invalid records.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddBrushWithAttributes(
    geometry_document_t *pDocument,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes ) noexcept;

// Adds a complete authored brush (solid + records).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryAddBrushSource(
    geometry_document_t *pDocument,
    const brush_source_t *pSource ) noexcept;

// Copies a document brush and its records out as one authored brush
// (canonical-empty *pOut). INVALID_ARGUMENT for an unknown brush.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCopyBrushSource(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    const common::allocator_t *pAllocator,
    brush_source_t *pOut ) noexcept;

// Checks the pairing invariant: one table per brush, every table valid, and
// every side resolving. CORRUPT_STATE names a broken pairing.
CYPHER_NODISCARD geometry_status_t GeometryDocument_ValidateBrushAttributes(
    const geometry_document_t *pDocument ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_BRUSH_ATTRIBUTES_H
