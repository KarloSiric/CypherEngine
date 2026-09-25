//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_HeightFieldTessellation.h
//  Purpose: Declares per-tile HeightField tessellation with level of
//           detail, crack-free stitching to coarser neighbours, and source
//           mapping from every vertex and triangle back to authored data.
//  Details: A tile at LOD l uses every 2^l-th sample. Where a neighbour is
//           coarser, the vertices on the shared edge that the neighbour
//           does not have are snapped onto the neighbour's edge segment
//           (height linearly interpolated between the neighbour's
//           vertices). Both tiles then describe the same edge polyline, so
//           there is no gap regardless of rendering precision on the
//           shared vertices, which are bit-identical samples.
//
//           Only the finer tile adapts, so building a tile needs its own
//           LOD and its neighbours' LODs, nothing else. When a tile changes
//           LOD, its finer neighbours must be rebuilt; choosing LODs and
//           scheduling rebuilds is Cook's job, not this module's.
//
//           Coarse quads that cover any hole cell are omitted, so holes
//           grow to the quad size at coarse LODs. LOD 0 reproduces holes
//           exactly and matches HeightField_TryHeightAt/TryRaycast.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_TESSELLATION_H
#define CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_TESSELLATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_HeightField.h"

namespace cypher::editor::geometry
{

enum heightfield_edge_t : common::u32 {
    HEIGHTFIELD_EDGE_WEST  = 0u, // -X
    HEIGHTFIELD_EDGE_EAST  = 1u, // +X
    HEIGHTFIELD_EDGE_SOUTH = 2u, // -Y
    HEIGHTFIELD_EDGE_NORTH = 3u, // +Y
    HEIGHTFIELD_EDGE_COUNT = 4u
};

struct heightfield_tile_lods_t {
    common::u32 lod{ 0u };
    // LOD of the neighbour across each edge. Ignored on the field border.
    common::u32 neighbourLod[HEIGHTFIELD_EDGE_COUNT]{ 0u, 0u, 0u, 0u };
};

struct heightfield_tile_mesh_t {
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<math::vec3d_t> normals{};      // full-resolution sample normals
    common::vector_t<common::u32> vertexSample{};   // field sample index under the vertex
    common::vector_t<common::u8> vertexSnapped{};   // 1 = height snapped onto a coarser edge
    common::vector_t<common::u32> indices{};        // CCW seen from +Z
    common::vector_t<common::u32> triangleCell{};   // field cell index of the quad's min corner
    common::u32 iTile{ CY_INVALID_INDEX };
    geometry_source_id_t tileId{};
    common::u64 revision{ 0u };   // tile revision the mesh was built from
    common::u32 lod{ 0u };
    common::u32 cHoleQuadsSkipped{ 0u };
};

CYPHER_NODISCARD geometry_status_t HeightFieldTileMesh_Init(
    heightfield_tile_mesh_t *pMesh,
    const common::allocator_t *pAllocator ) noexcept;

void HeightFieldTileMesh_Shutdown( heightfield_tile_mesh_t *pMesh ) noexcept;

// Highest LOD for the field's tile size: log2(tileCells).
CYPHER_NODISCARD common::u32 HeightFieldTessellation_MaxLod( const heightfield_t *pField ) noexcept;

// Builds one tile. Every LOD (own and neighbours') must be <= MaxLod. On
// failure the mesh is left empty. The recorded revision is the tile's at
// build time; pass it to HeightField_ClearDirty after publishing the mesh.
CYPHER_NODISCARD geometry_status_t HeightFieldTessellation_TryBuildTile(
    const heightfield_t *pField,
    common::u32 iTile,
    const heightfield_tile_lods_t &lods,
    heightfield_tile_mesh_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_TESSELLATION_H
