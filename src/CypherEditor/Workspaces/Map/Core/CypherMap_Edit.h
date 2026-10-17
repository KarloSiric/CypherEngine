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
// ARCH..TORUS are Hammer 5's other Block tool shapes; they are made as one
// mesh each (MapCreate_BlockShape), the others as brushes.
enum class map_primitive_kind_t : common::u8 { BOX, WEDGE, CYLINDER, CONE, SPHERE, QUAD, ARCH, STAIRS, SPIRAL_STAIRS, PIPE, TORUS, COUNT };
CYPHER_NODISCARD constexpr bool MapPrimitive_IsMesh( map_primitive_kind_t kind ) noexcept { return kind >= map_primitive_kind_t::ARCH && kind < map_primitive_kind_t::COUNT; }
struct map_primitive_desc_t
{
    map_primitive_kind_t kind = map_primitive_kind_t::BOX;
    // The construction frame. Round shapes fit within this envelope; their
    // tessellation need not touch every face of the frame (for example an odd
    // sided cylinder). A UV sphere touches all six faces of the frame.
    math::aabbd_t bounds{};
    common::u32 axis = 2; // Cylinder/cone length or Quad normal: X=0, Y=1, Z=2.
    common::u32 nSides = 16; // Cylinder/cone: 3..128, also bounded by policy.
    common::u32 wedgeCutAxis = 0;
    common::u32 wedgeSlopeAxis = 2; // Distinct from cut axis; remaining axis is extruded.
    // Retained field name for construction settings compatibility. These are
    // UV-sphere detail presets: 0=16 sides x 8 bands, 1=24 sides x 10 bands.
    common::u32 sphereSubdivisions = 1; // 128/240 faces; map limit is 256.
    common::f64 coneTopRadiusRatio = 0.0; // 0=apex, (0,1)=frustum, 1=cylinder.
    common::f64 worldUnitsPerUv = 128.0; // World distance per one texture repeat.
    // Mesh shapes (round ones use nSides around, axis as their up):
    common::f64 wallThickness = 16.0;  // Arch and pipe wall, in world units.
    common::f64 arcDegrees = 180.0;    // Arch: how far round it goes.
    common::f64 startDegrees = 0.0;    // Arch: where it starts, from the first axis across `axis`.
    common::u32 nSteps = 8;            // Stairs and spiral stairs.
    common::i32 stairsDirection = 1;   // Stairs rise toward this axis: +-(1 + axis index); along `axis` means the first axis across it.
    common::f64 turnDegrees = 360.0;   // Spiral stairs: total turn (negative turns the other way).
    common::f64 innerRadius = 16.0;    // Spiral stairs: the open middle, in world units.
    common::f64 tubeRatio = 0.25;      // Torus: tube radius over the half footprint.
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
// Builds the same UV sphere directly as a mesh, without expensive convex
// brush reconstruction/conversion. Requires desc.kind == SPHERE; shares the
// bounds, detail, material, layer, ID and private-copy failure contracts above.
CYPHER_NODISCARD map_status_t MapEdit_CreateSphereMesh( map_document_t *pMap, const map_primitive_desc_t &desc,
    common::string_view_t material, common::string_view_t layer, common::u64 *pIdOut ) noexcept;
// Compatibility wrapper using the default primitive UV density.
CYPHER_NODISCARD map_status_t MapEdit_CreateBox( map_document_t *pMap, math::aabbd_t bounds,
    common::string_view_t material, common::string_view_t layer, common::u64 *pIdOut ) noexcept;
// Read-only target closure used by Translate/Rotate/Scale/Shear/Delete/Duplicate:
// roots, owned geometry, attached descendants and their owned geometry, once
// each. This does not expand groups or inspect editor lock/visibility state.
// pOut must be initialized. Prepare with its allocator and replace it only on
// success; failure preserves its storage and contents. ids may borrow pOut.
// Empty input publishes an empty result. Read-only maps can be queried.
CYPHER_NODISCARD map_status_t MapEdit_CollectAffectedObjects( const map_document_t *pMap,
    common::span_t<const common::u64> ids, common::vector_t<common::u64> *pOut ) noexcept;
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
// Shear (TrenchBroom's Shear tool): p' = p + slide * (p[axis] - pivot[axis]),
// so the plane through `pivot` across `axis` stays put and each parallel
// plane slides by `slide` per unit of distance from it (slide[axis] must be
// 0). Brushes keep their textures exactly with textureLock; meshes and
// patches carry their UVs; point entities and instances move without
// turning. Terrain refuses (INVALID_ARGUMENT): its cells stay square.
CYPHER_NODISCARD map_status_t MapEdit_Shear( map_document_t *pMap, common::span_t<const common::u64> ids, common::u32 axis,
    math::vec3d_t slide, math::vec3d_t pivot, bool textureLock = true ) noexcept;
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
