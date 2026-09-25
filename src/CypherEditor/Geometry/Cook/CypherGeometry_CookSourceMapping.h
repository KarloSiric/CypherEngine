//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSourceMapping.h
//  Purpose: Declares cooked geometry products with source provenance.
//  Details: The neutral Cook takes a validated geometry document, performs
//           boundary reconstruction and tessellation for every brush, and
//           emits a flat vertex/triangle buffer with per-triangle source
//           mapping that traces each triangle back to its originating brush
//           and side. The output is deterministic: identical input produces
//           identical vertices, triangles, and content hash.
//
//           This is the "neutral Cook" — no material batching, visibility,
//           navigation, or lighting. Those are layered on top in later
//           gate deliverables.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAPPING_H
#define CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAPPING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushTessellation.h"
#include "CypherGeometry_Attributes_BrushSideStore.h"

#include "CypherCommon/Tier1/CypherCommon_ContentHash.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

// Per-triangle provenance record. Every cooked triangle can be traced
// back to the brush and side that generated it.
struct cook_source_record_t {
    // Persistent identity of the brush that owns the side.
    geometry_source_id_t brushSourceId{};

    // Persistent identity of the specific side whose tessellation
    // produced this triangle.
    geometry_source_id_t sideSourceId{};

    // Zero-based side index within the brush, for fast attribute lookup.
    common::u32 iSideIndex{ 0u };
};

// One triangle in the cooked mesh. Vertex indices reference the cook
// result's vertex array. Winding is CCW viewed from the outward normal.
struct cook_triangle_t {
    common::u32 iVertex0;
    common::u32 iVertex1;
    common::u32 iVertex2;
    cook_source_record_t source;
};

// Status codes for the Cook pipeline.
enum class geometry_cook_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    OUT_OF_MEMORY,
    LIMIT_EXCEEDED,
    BOUNDARY_FAILED,
    TESSELLATION_FAILED,
    ATTRIBUTE_FAILED,
    HASH_FAILED
};

// Complete cooked output from a geometry document. Owns all storage.
// The content hash covers the vertex and triangle data in their emitted
// order, so identical input always yields an identical hash.
struct geometry_cook_result_t {
    common::vector_t<math::vec3d_t> vertices{};
    common::vector_t<cook_triangle_t> triangles{};
    common::content_hash_t contentHash{};
    geometry_cook_status_t status{ geometry_cook_status_t::OK };
};

// Per-vertex data for the full cook output. Position, normal, and UV are
// stored together so the output maps directly to a GPU vertex buffer.
struct cook_vertex_t {
    math::vec3d_t position;
    math::vec3d_t normal;
    math::vec2d_t uv;
};

// Per-brush attribute source for the full cook. Maps brush index in
// document order to the attribute store that holds UV projections for
// its sides. A null pointer means "use default axis-aligned projection".
struct cook_brush_attributes_t {
    const geometry_brush_side_attribute_store_t *pStore;
};



// Extended cooked output with per-vertex normals and UV coordinates.
// Vertices are split per face (not shared) so each face-vertex carries
// its own normal and UV. A box produces 24 vertices (6 faces × 4),
// not 8 shared ones.
struct geometry_cook_full_result_t {
    common::vector_t<cook_vertex_t> vertices{};
    common::vector_t<cook_triangle_t> triangles{};
    common::content_hash_t contentHash{};
    geometry_cook_status_t status{ geometry_cook_status_t::OK };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Binds an allocator to the result's internal storage. Must be called
// before TryCook.
CYPHER_NODISCARD geometry_cook_status_t GeometryCook_Init(
    geometry_cook_result_t *pResult,
    const common::allocator_t *pAllocator ) noexcept;

// Releases vertex and triangle storage. Safe on a zero-initialized result.
void GeometryCook_Shutdown(
    geometry_cook_result_t *pResult ) noexcept;

// ---------------------------------------------------------------------------
// Cook
// ---------------------------------------------------------------------------

// Produces a deterministic cooked mesh from the document's brush source
// data. For each brush: reconstructs the boundary, tessellates faces,
// and emits vertices and triangles with source provenance records.
// Computes a content hash over the complete output.
//
// The result is cleared before cooking, so this is safe to call
// repeatedly on the same result struct.
//
// On failure the result is left empty with the status set.
CYPHER_NODISCARD geometry_cook_status_t GeometryCook_TryCook(
    geometry_cook_result_t *pResult,
    const geometry_document_t *pDocument,
    const geometry_policy_t &policy ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize GeometryCook_VertexCount(
    const geometry_cook_result_t *pResult ) noexcept;

CYPHER_NODISCARD common::usize GeometryCook_TriangleCount(
    const geometry_cook_result_t *pResult ) noexcept;

CYPHER_NODISCARD common::content_hash_t GeometryCook_ContentHash(
    const geometry_cook_result_t *pResult ) noexcept;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

CYPHER_NODISCARD const char *GeometryCook_StatusName(
    geometry_cook_status_t status ) noexcept;

// ---------------------------------------------------------------------------
// Full Cook (with normals + UVs)
// ---------------------------------------------------------------------------

// Binds an allocator to the full result. Must be called before TryCookFull.
CYPHER_NODISCARD geometry_cook_status_t GeometryCookFull_Init(
    geometry_cook_full_result_t *pResult,
    const common::allocator_t *pAllocator ) noexcept;

void GeometryCookFull_Shutdown(
    geometry_cook_full_result_t *pResult ) noexcept;

// Produces a deterministic cooked mesh with per-vertex normals and UVs.
// Vertices are emitted per face (split at face boundaries) so each
// face-vertex carries the face normal and its own UV coordinates.
//
// pBrushAttributes is an optional array of attribute sources, one per brush
// in document order (same indexing as document.brushes). Pass nullptr for
// a fully default cook (axis-aligned UV projection derived from face normal).
// Individual entries may also have pStore == nullptr for default projection
// on that specific brush.
//
// UV projection uses each brush side's canonical iAttributeIndex when an
// attribute store is provided. A missing, out-of-range, or invalid referenced
// record fails the cook instead of silently substituting another mapping.
// A null store requests axis-aligned planar projection based on the face's
// dominant axis.
//
// On every failure after output validation, vertices and triangles are empty,
// contentHash is invalid, and status names the failure.
CYPHER_NODISCARD geometry_cook_status_t GeometryCook_TryCookFull(
    geometry_cook_full_result_t *pResult,
    const geometry_document_t *pDocument,
    const geometry_policy_t &policy,
    const cook_brush_attributes_t *pBrushAttributes,
    common::usize cBrushAttributes ) noexcept;

CYPHER_NODISCARD common::usize GeometryCookFull_VertexCount(
    const geometry_cook_full_result_t *pResult ) noexcept;

CYPHER_NODISCARD common::usize GeometryCookFull_TriangleCount(
    const geometry_cook_full_result_t *pResult ) noexcept;

CYPHER_NODISCARD common::content_hash_t GeometryCookFull_ContentHash(
    const geometry_cook_full_result_t *pResult ) noexcept;

static_assert( std::is_trivially_copyable_v<cook_source_record_t> );
static_assert( std::is_trivially_copyable_v<cook_triangle_t> );
static_assert( std::is_trivially_copyable_v<cook_vertex_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAPPING_H
