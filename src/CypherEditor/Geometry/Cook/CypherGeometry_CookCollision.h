//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookCollision.h
//  Purpose: Declares the collision cook: immutable snapshot in, bounded
//           static collision surfaces out (binary32 positions, triangles,
//           per-triangle source element IDs), with diagnostics tied to
//           source elements and incremental reuse via cook keys.
//  Details: One surface per source object, in ascending source-ID order
//           (independent of document order). Brush surfaces come from the
//           reconstructed convex boundary and are flagged convex, so a
//           physics cooker can use them as hulls directly; mesh surfaces are
//           triangle meshes flagged closed when they have no boundary edges;
//           patch surfaces are their adaptive tessellation (open, each
//           triangle naming the patch); heightfield surfaces share one vertex
//           per field sample across tiles, skip holes, and name each
//           triangle's tile.
//
//           Float conversion is the one place authoring doubles become
//           runtime floats (ARCHITECTURE.md: "float conversion only in Cook,
//           and checked"). A coordinate outside binary32 range is a
//           FLOAT_RANGE diagnostic and the surface is skipped; a triangle
//           that becomes zero-area after rounding is dropped with a
//           DEGENERATE_AFTER_FLOAT diagnostic naming its source element.
//           Failures of one object never abort the cook of the others.
//
//           Incremental: given the previous output, any surface whose
//           product key (CookKeys.h) is unchanged is copied verbatim rather
//           than rebuilt; the counters report how many were reused. Reused
//           and rebuilt outputs are byte-identical, which the tests check.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_COLLISION_H
#define CYPHER_EDITOR_GEOMETRY_COOK_COLLISION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookKeys.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookCollisionVersion = 1u;

struct cook_float3_t {
    common::f32 x{ 0.0f };
    common::f32 y{ 0.0f };
    common::f32 z{ 0.0f };
};

enum class cook_diagnostic_kind_t : common::u8 {
    NONE = 0u,
    BOUNDARY_FAILED,        // brush planes do not form a valid closed boundary
    TESSELLATION_FAILED,    // mesh face could not be triangulated
    FLOAT_RANGE,            // coordinate not representable in binary32
    DEGENERATE_AFTER_FLOAT, // triangle collapsed by float rounding (dropped)
    SOURCE_MISSING          // key without a matching snapshot object
};

struct cook_diagnostic_t {
    cook_diagnostic_kind_t kind{ cook_diagnostic_kind_t::NONE };
    geometry_source_id_t objectId{};
    geometry_source_id_t elementId{}; // side / face, when known
};

struct cook_collision_surface_t {
    geometry_source_id_t objectId{};
    cook_source_kind_t kind{ cook_source_kind_t::INVALID };
    common::u32 iFirstVertex{ 0u };
    common::u32 cVertices{ 0u };
    common::u32 iFirstTriangle{ 0u };
    common::u32 cTriangles{ 0u };
    bool bConvex{ false };
    bool bClosed{ false };
    common::content_hash_t productKey{};
    common::content_hash_t dataHash{};
};

struct cook_collision_t {
    common::vector_t<cook_float3_t> positions{};
    common::vector_t<common::u32> indices{};                     // 3 per triangle, surface-local
    common::vector_t<geometry_source_id_t> triangleElement{};    // 1 per triangle
    common::vector_t<cook_collision_surface_t> surfaces{};
    common::vector_t<cook_diagnostic_t> diagnostics{};
    common::content_hash_t contentHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
    common::u32 cSurfacesReused{ 0u };
    common::u32 cSurfacesRebuilt{ 0u };
};

CYPHER_NODISCARD geometry_status_t CookCollision_Init(
    cook_collision_t *pOut,
    const common::allocator_t *pAllocator ) noexcept;

void CookCollision_Shutdown( cook_collision_t *pOut ) noexcept;

// Cooks every object in `keys` (which must have been built from this
// snapshot). pPrevious may be nullptr or an earlier output of this cook; it
// must not alias *pOut. On error (argument/allocation) *pOut is left empty.
CYPHER_NODISCARD geometry_status_t CookCollision_TryBuild(
    const geometry_snapshot_t *pSnapshot,
    const cook_key_set_t *pKeys,
    const cook_collision_t *pPrevious,
    cook_collision_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_COLLISION_H
