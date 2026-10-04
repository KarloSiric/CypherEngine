//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Wireframe.h
//  Purpose: Declares the wireframe of a map: every object reduced to line
//           segments and bounds, which the 2D views draw and pick against.
//  Details: Brushes are stored as planes, so their edges have to be
//           reconstructed; doing that once per document change instead of
//           once per paint keeps panning a large map smooth. The wireframe
//           is plain data with no Qt, so picking is tested headlessly and the
//           same data can feed the 3D view's line pass later.
//
//           Terrain is thinned to at most MAP_WIRE_TERRAIN_LINES grid lines
//           per axis: a 1025 x 1025 field drawn sample by sample would be two
//           million segments, which no 2D view needs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_WIREFRAME_H
#define CYPHER_EDITOR_MAP_WIREFRAME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

namespace cypher::editor::map
{

inline constexpr common::u32 MAP_WIRE_TERRAIN_LINES = 64u;
inline constexpr common::usize MAP_WIRE_TEXT_CAPACITY = 64u;  // Class and name, truncated for display.
inline constexpr common::f64 MAP_WIRE_POINT_ENTITY_HALF = 8.0; // Half size of a point entity's box.

enum class map_wire_kind_t : common::u8 {
    BRUSH = 0u,
    MESH,
    PATCH,
    TERRAIN,
    ENTITY
};

struct map_wire_line_t {
    common::u32 iA{ 0u }; // Indices into map_wireframe_t::points.
    common::u32 iB{ 0u };
};

// One pickable object. Entities appear here too, with their box as bounds
// and no lines; their details are in map_wireframe_t::entities.
struct map_wire_object_t {
    common::u64 id{ 0u };
    common::u64 owner{ 0u };  // Owning entity for tied geometry, else 0.
    common::u32 iLayer{ 0u }; // Index into map_document_t::layers.
    map_wire_kind_t kind{ map_wire_kind_t::BRUSH };
    common::u32 iFirstLine{ 0u };
    common::u32 nLines{ 0u };
    map_bounds_t bounds{};
    common::u32 iFirstPoint{ 0u }; // Contiguous presentation points owned by this object.
    common::u32 nPoints{ 0u };     // Cache ranges are not authored component identities.
};

// One filled polygon for the 3D view's shaded modes: brush sides, mesh
// faces, patch control quads, and thinned terrain cells. Vertices index
// map_wireframe_t::points through faceIndices, wound so that normal (unit,
// Newell's method; outward for brushes) faces the viewer from the front.
struct map_wire_face_t {
    common::u64 id{ 0u };          // The object the face belongs to.
    common::u64 material{ 0u };    // Map-local material reference; 0 when unassigned.
    common::u64 sideId{ 0u };      // Persistent brush side identity, 0 for other geometry.
    common::u64 faceId{ 0u };      // Persistent authored mesh face identity, 0 for other geometry.
    math::vec3d_t normal{};
    common::u32 iFirstIndex{ 0u }; // Into map_wireframe_t::faceIndices.
    common::u32 nIndices{ 0u };
    common::bool_t bTwoSided{ common::CY_FALSE }; // Patches and terrain: drawn from both sides.
};

struct map_wire_entity_t {
    common::u64 id{ 0u };
    common::u32 iLayer{ 0u };
    math::vec3d_t origin{};
    common::bool_t bHasOrigin{ common::CY_FALSE }; // Originless logic records have no viewport position.
    common::u32 nOwned{ 0u };               // Geometry objects tied to it.
    char className[MAP_WIRE_TEXT_CAPACITY]{};
    char name[MAP_WIRE_TEXT_CAPACITY]{};
};

enum class map_wire_connection_status_t : common::u8 {
    RESOLVED = 0u,
    MISSING_TARGET,
    RUNTIME_TARGET, // !activator, !caller, !player; unknown without running the game.
    INVALID_OUTPUT // Missing/invalid output, input, or target syntax.
};

// One authored entity output resolved to one recipient. CYMAP names are not
// unique: an exact name or trailing prefix wildcard fans out to every match.
// Unresolved outputs keep a diagnostic record with targetId == 0, never an
// invented endpoint. Draw a line only when RESOLVED and both origin flags
// are true. Full names are used for matching; these text arrays are display
// labels only. Ordering is source ID, authored output index, then target ID.
struct map_wire_connection_t {
    common::u64 sourceId{ 0u };
    common::u64 targetId{ 0u };
    math::vec3d_t sourceOrigin{};
    math::vec3d_t targetOrigin{};
    common::bool_t bHasSourceOrigin{ common::CY_FALSE };
    common::bool_t bHasTargetOrigin{ common::CY_FALSE };
    common::u32 iSourceLayer{ 0u };
    common::u32 iTargetLayer{ 0u };
    common::usize iOutput{ 0u };
    map_wire_connection_status_t status{ map_wire_connection_status_t::INVALID_OUTPUT };
    char output[MAP_WIRE_TEXT_CAPACITY]{};
    char input[MAP_WIRE_TEXT_CAPACITY]{};
    char target[MAP_WIRE_TEXT_CAPACITY]{};
};

struct map_wireframe_t {
    map_wireframe_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( map_wireframe_t );
    ~map_wireframe_t() noexcept;

    common::vector_t<math::vec3d_t> points{};
    // Exactly parallel to points after a successful build. Mesh points carry
    // their authored vertex source IDs; reconstructed/control points carry 0.
    // Selection stores these IDs with object.id, never a transient point index.
    common::vector_t<common::u64> pointSourceIds{};
    common::vector_t<map_wire_line_t> lines{};
    common::vector_t<map_wire_object_t> objects{};  // Sorted by ID.
    common::vector_t<map_wire_entity_t> entities{}; // Sorted by ID.
    common::vector_t<map_wire_connection_t> connections{};
    common::vector_t<map_wire_face_t> faces{};      // In object build order.
    common::vector_t<common::u32> faceIndices{};
    map_bounds_t bounds{};                          // Everything, for "zoom to map".
    common::usize nBrokenBrushes{ 0u };             // Brushes whose edges could not be rebuilt.
    const common::allocator_t *pAllocator{ nullptr };
};

CYPHER_NODISCARD common::bool_t MapWireframe_Init( map_wireframe_t *pWire, const common::allocator_t *pAllocator ) noexcept;
void MapWireframe_Shutdown( map_wireframe_t *pWire ) noexcept;

// Rebuilds the wireframe from the document. OUT_OF_MEMORY leaves it empty.
CYPHER_NODISCARD map_status_t MapWireframe_Build( map_wireframe_t *pWire, const map_document_t &map ) noexcept;

CYPHER_NODISCARD const map_wire_object_t *MapWireframe_FindObject( const map_wireframe_t &wire, common::u64 id ) noexcept;
CYPHER_NODISCARD const map_wire_entity_t *MapWireframe_FindEntity( const map_wireframe_t &wire, common::u64 id ) noexcept;

// Picks in a 2D view that shows world axes axisU and axisV (0 = x, 1 = y,
// 2 = z) at a view point in world units. An object is hit when one of its
// lines passes within tolerance, or - for entities - when the point is
// inside its box. Among several hits the one with the smallest projected
// area wins, so a small brush inside a big room is reachable. Returns 0
// when nothing is hit.
CYPHER_NODISCARD common::u64 MapWireframe_Pick2D(
    const map_wireframe_t &wire,
    common::u32 axisU,
    common::u32 axisV,
    common::f64 u,
    common::f64 v,
    common::f64 tolerance ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_WIREFRAME_H
