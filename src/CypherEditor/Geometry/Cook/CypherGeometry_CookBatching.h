//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookBatching.h
//  Purpose: Declares static batch proposals: the map's surface triangles
//           grouped by material and by spatial chunk, for a renderer or
//           world builder to turn into draw batches and streaming cells.
//  Details: A batch is one material in one chunk (a cube of chunkSize on a
//           grid anchored at the world origin, chosen by triangle centroid).
//           Materials never mix in a batch - that is what makes it one draw.
//           Provenance is never lost: inside a batch the triangles stay
//           grouped by source object, each group a range that maps back to
//           the object and, per triangle, to the authored element, so
//           picking, per-object visibility toggles and incremental rebuilds
//           still work on batched geometry.
//
//           Order is fully deterministic: batches by (material, chunk z, y,
//           x), ranges by object ID, triangles in soup order. Streaming
//           policy and lifetime are the world system's (README).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_BATCHING_H
#define CYPHER_EDITOR_GEOMETRY_COOK_BATCHING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookSurfaces.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookBatchingVersion = 1u;

struct cook_batching_options_t {
    common::f64 chunkSize{ 1024.0 }; // world units, > 0
};

struct cook_batch_range_t {
    geometry_source_id_t objectId{};
    common::u32 iFirstTriangle{ 0u }; // into cook_batching_t::triangles
    common::u32 cTriangles{ 0u };
};

struct cook_batch_t {
    geometry_material_ref_t material{};
    common::i32 chunk[3]{ 0, 0, 0 };
    common::u32 iFirstRange{ 0u }, cRanges{ 0u };
    common::u32 iFirstTriangle{ 0u }, cTriangles{ 0u };
    common::f64 lo[3]{}, hi[3]{}; // bounds of its triangles
};

struct cook_batching_t {
    common::vector_t<cook_batch_t> batches{};
    common::vector_t<cook_batch_range_t> ranges{};
    common::vector_t<common::u32> triangles{}; // soup triangle indices, batch-ordered
    common::content_hash_t contentHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

CYPHER_NODISCARD geometry_status_t CookBatching_Init( cook_batching_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void CookBatching_Shutdown( cook_batching_t *pOut ) noexcept;

// INVALID_ARGUMENT for a non-positive chunk size; LIMIT_EXCEEDED when a
// chunk coordinate does not fit an i32.
CYPHER_NODISCARD geometry_status_t CookBatching_TryBuild(
    const cook_surface_soup_t *pSoup,
    const cook_batching_options_t &options,
    cook_batching_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_BATCHING_H
