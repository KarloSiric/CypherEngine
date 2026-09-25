//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookVisibility.h
//  Purpose: Declares the visibility cook input: the geometry a visibility
//           compiler (BSP + portals + PVS, or an occlusion baker) may treat
//           as sealing, separated from the geometry it must not.
//  Details: Three classes, each traced to its source:
//             hulls   every brush that reconstructs, as its outward plane
//                     set - what a BSP compiler splits space with (Quake's
//                     qbsp works on brush planes, not triangles);
//             shells  closed non-brush surfaces (closed meshes) as
//                     triangle ranges - solid occluders;
//             detail  open surfaces (patches, heightfields, open meshes):
//                     they bound nothing, so they can occlude but never
//                     seal a leak and never define a leaf.
//           Choosing leaves, portals, and PVS policy is the compiler's job;
//           this only publishes validated inputs (README): hull planes are
//           finite and unit, shells really are closed, and an object whose
//           surface could not be built appears in none of the lists.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_VISIBILITY_H
#define CYPHER_EDITOR_GEOMETRY_COOK_VISIBILITY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookSurfaces.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookVisibilityVersion = 1u;

struct cook_visibility_plane_t {
    math::planed_t plane{}; // outward
    geometry_source_id_t sideId{};
};

struct cook_visibility_hull_t {
    geometry_source_id_t brushId{};
    common::u32 iFirstPlane{ 0u }, cPlanes{ 0u };
    common::f64 lo[3]{}, hi[3]{};
};

struct cook_visibility_range_t {
    geometry_source_id_t objectId{};
    common::u32 iSoupObject{ 0u };
    common::u32 iFirstTriangle{ 0u }, cTriangles{ 0u }; // soup triangles
};

struct cook_visibility_t {
    common::vector_t<cook_visibility_hull_t> hulls{};
    common::vector_t<cook_visibility_plane_t> planes{};
    common::vector_t<cook_visibility_range_t> shells{};
    common::vector_t<cook_visibility_range_t> detail{};
    common::f64 worldLo[3]{}, worldHi[3]{};
    common::content_hash_t contentHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

CYPHER_NODISCARD geometry_status_t CookVisibility_Init( cook_visibility_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void CookVisibility_Shutdown( cook_visibility_t *pOut ) noexcept;

// The soup must have been built from this snapshot (same revision:
// INVALID_ARGUMENT otherwise).
CYPHER_NODISCARD geometry_status_t CookVisibility_TryBuild(
    const geometry_snapshot_t *pSnapshot,
    const cook_surface_soup_t *pSoup,
    cook_visibility_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_VISIBILITY_H
