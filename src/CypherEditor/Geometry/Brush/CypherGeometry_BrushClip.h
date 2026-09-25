//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushClip.h
//  Purpose: Declares failure-atomic plane clipping and slicing for brushes.
//  Details: A clip keeps the nonpositive half-space of a normalized plane.
//           Productive clips are canonicalized so every retained side owns a
//           real boundary face. A slice emits two independently publishable
//           brushes with document-unique source identities.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_CLIP_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_CLIP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// Relationship between a cutting plane and the derived volume. FRONT and
// BACK refer to the sign of Planed_SignedDistance; clipping retains BACK.
enum class brush_clip_classification_t : common::u8 {
    INTERSECTS = 0u,
    ALL_FRONT,
    ALL_BACK,
    ON_PLANE,
    INVALID
};

// Classifies a complete initialized boundary against a normalized plane.
// Coplanarity uses numericalPolicy.fCoplanarDistanceTolerance. INVALID is
// returned for malformed boundary state, invalid policy, non-finite or
// non-normalized planes, out-of-policy coordinates, and non-finite vertices.
CYPHER_NODISCARD brush_clip_classification_t BrushClip_Classify(
    const brush_boundary_t *pBoundary,
    math::planed_t clipPlane,
    const geometry_numerical_policy_t &numericalPolicy ) noexcept;

// Replaces pBrush with its intersection with clipPlane's nonpositive
// half-space. The input brush must be initialized, strictly canonical, and
// carry unique valid brush/side source IDs.
//
// INTERSECTS appends a fresh clip-side identity, removes sides made redundant
// by the cut, validates the final strict boundary, then publishes. ALL_BACK is
// a successful exact no-op. ALL_FRONT and ON_PLANE return DEGENERATE because
// brush_solid_t cannot represent an empty or zero-volume result.
//
// Failure leaves pBrush and *pIdAllocator unchanged. A successful productive
// clip preserves the brush ID and all retained side IDs/attribute bindings.
CYPHER_NODISCARD geometry_status_t BrushClip_TryClip(
    brush_solid_t *pBrush,
    math::planed_t clipPlane,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy ) noexcept;

// Splits pSrc into nonempty back and front halves. The source is unchanged.
// Both destinations must be distinct, canonical zero-initialized brushes and
// may not alias pSrc. Every published brush and side receives a fresh identity
// so the source and both halves can coexist in one document.
//
// Only INTERSECTS has two volumetric products. ALL_FRONT, ALL_BACK, and
// ON_PLANE return DEGENERATE. On every failure both destinations remain
// canonical empty and *pIdAllocator is unchanged.
CYPHER_NODISCARD geometry_status_t BrushClip_TrySlice(
    const brush_solid_t *pSrc,
    const common::allocator_t *pAllocator,
    math::planed_t clipPlane,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    brush_solid_t *pBackOut,
    brush_solid_t *pFrontOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_CLIP_H
