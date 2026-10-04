//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Map-owned editing operations over the canonical geometry backend.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_EDIT_H
#define CYPHER_EDITOR_MAP_EDIT_H
#pragma once

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

// Creates an exact owned working copy without saving or modifying the source.
// The caller deletes the result. *ppOut must be null and remains null on failure.
// Interactive commands edit this private copy and publish it only on success;
// the old document is the undo state. This makes a mixed-object edit atomic.
CYPHER_NODISCARD map_status_t MapEdit_Clone( const map_document_t *pSource, map_document_t **ppOut ) noexcept;

// Retained allocation capacities plus document structures, for bounded undo
// history. Includes unused CYKV arenas; allocator implementation overhead
// (for example malloc bookkeeping) is outside this byte count.
CYPHER_NODISCARD common::usize MapEdit_EstimateBytes( const map_document_t *pMap ) noexcept;

// Operations below mutate a PRIVATE working copy. On failure the caller must
// discard that copy: a batch can have completed earlier objects before failing.
// No operation performs file I/O. IDs are never recycled. Origin-bearing map
// records support translation; their class-specific rotation/scale semantics
// are rejected. Heightfields support translation and equal XY scaling only.
enum class map_primitive_kind_t : common::u8 { BOX, WEDGE, CYLINDER, CONE, SPHERE, QUAD, COUNT };
struct map_primitive_desc_t
{
    map_primitive_kind_t kind = map_primitive_kind_t::BOX;
    // The construction frame. Round shapes fit within this envelope; their
    // tessellation need not touch every face of the frame (for example an odd
    // sided cylinder or the base icosphere).
    math::aabbd_t bounds{};
    common::u32 axis = 2; // Cylinder/cone length or Quad normal: X=0, Y=1, Z=2.
    common::u32 nSides = 16; // Cylinder/cone: 3..128, also bounded by policy.
    common::u32 wedgeCutAxis = 0;
    common::u32 wedgeSlopeAxis = 2; // Distinct from cut axis; remaining axis is extruded.
    common::u32 sphereSubdivisions = 1; // 0=20 faces, 1=80 faces; map limit is 256.
    common::f64 coneTopRadiusRatio = 0.0; // 0=apex, (0,1)=frustum, 1=cylinder.
    common::f64 worldUnitsPerUv = 128.0; // World distance per one texture repeat.
};
// Uses backend primitive generators and validates geometry before adoption.
// QUAD creates one open rectangular mesh face at bounds.minimum on `axis`,
// facing +axis; the other two extents must be positive. A thick envelope is
// allowed but does not give the Quad thickness. Other kinds create convex
// solids. Creates world geometry on the supplied layer (empty selects the first
// layer), with fresh root/part IDs and usable surface UVs.
// *pIdOut remains unchanged on failure; discard the private copy as above.
CYPHER_NODISCARD map_status_t MapEdit_CreatePrimitive( map_document_t *pMap, const map_primitive_desc_t &desc,
    common::string_view_t material, common::string_view_t layer, common::u64 *pIdOut ) noexcept;
// Compatibility wrapper using the default primitive UV density.
CYPHER_NODISCARD map_status_t MapEdit_CreateBox( map_document_t *pMap, math::aabbd_t bounds,
    common::string_view_t material, common::string_view_t layer, common::u64 *pIdOut ) noexcept;
CYPHER_NODISCARD map_status_t MapEdit_Translate( map_document_t *pMap, common::span_t<const common::u64> ids,
    math::vec3d_t offset, bool textureLock = true ) noexcept;
CYPHER_NODISCARD map_status_t MapEdit_Scale( map_document_t *pMap, common::span_t<const common::u64> ids,
    math::vec3d_t factors, math::vec3d_t pivot, bool textureLock = true ) noexcept;
// Extends each selected geometry root by the same signed world displacement.
// sides selects minimum (-1), maximum (+1), or unchanged (0) on each axis;
// delta must be zero on unchanged axes. Each object's opposite side remains
// fixed, or its own center remains fixed when bFromCenter is true. Snapping is
// a gesture policy supplied by the caller, not a change to authored units.
struct map_bounds_resize_t {
    math::vec3d_t sides{}, delta{};
    common::bool_t bFromCenter{ common::CY_FALSE };
};
// The same allocation-free calculation drives previews and publication.
// Active source/result extents must be positive; crossing an anchor is rejected
// rather than silently giving differently sized objects different travel.
// Both output values remain unchanged on failure and must be distinct.
CYPHER_NODISCARD map_status_t MapEdit_ResizeTransform( math::aabbd_t bounds, const map_bounds_resize_t &resize,
    math::vec3d_t *pFactorsOut, math::vec3d_t *pPivotOut ) noexcept;
// Direct brush, mesh, and patch roots only; entity helpers and terrain are not
// resized. Duplicate IDs are applied once. All original bounds are preflighted
// before mutation. Ownership, layer, identities, and retained keys stay intact.
// As with other edits, discard the private working copy on any failure.
CYPHER_NODISCARD map_status_t MapEdit_Resize( map_document_t *pMap, common::span_t<const common::u64> ids,
    const map_bounds_resize_t &resize, bool textureLock = true ) noexcept;
// Euler XYZ in degrees: X first, then Y, then Z, about the supplied world pivot.
CYPHER_NODISCARD map_status_t MapEdit_Rotate( map_document_t *pMap, common::span_t<const common::u64> ids,
    math::vec3d_t degrees, math::vec3d_t pivot, bool textureLock = true ) noexcept;
CYPHER_NODISCARD map_status_t MapEdit_Delete( map_document_t *pMap, common::span_t<const common::u64> ids ) noexcept;
// Preserves authored materials, UVs, layers, entity ownership, and retained
// metadata. Every copied root and part gets a fresh ID. idsOut must be initialized.
CYPHER_NODISCARD map_status_t MapEdit_Duplicate( map_document_t *pMap, common::span_t<const common::u64> ids,
    math::vec3d_t offset, common::vector_t<common::u64> *pIdsOut ) noexcept;

} // namespace cypher::editor::map
#endif
