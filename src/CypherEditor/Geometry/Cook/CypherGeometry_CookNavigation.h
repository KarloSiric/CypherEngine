//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookNavigation.h
//  Purpose: Declares the navigation cook input: the surface triangles a
//           navmesh generator should consider walkable, by slope alone,
//           each traced to its source object and element.
//  Details: Geometry only decides "faces up within the slope limit"; it
//           does not know agents, step heights, clearance, or whether a
//           surface is inside a wall. Those belong to the navigation cooker
//           that consumes this (README: no gameplay walkability claims), so
//           nothing here is filtered by them. Triangles too small to matter
//           (below minArea) are left out so slivers do not seed regions.
//
//           Positions are converted to binary32 here; a triangle whose
//           corners do not fit is skipped and counted. Vertices are shared
//           among candidates of one object, so regions stay connected.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_NAVIGATION_H
#define CYPHER_EDITOR_GEOMETRY_COOK_NAVIGATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookCollision.h"
#include "CypherGeometry_CookSurfaces.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookNavigationVersion = 1u;

struct cook_navigation_options_t {
    math::vec3d_t up{ 0.0, 0.0, 1.0 }; // unit "up"
    common::f64 maxSlopeRadians{ 0.7853981633974483 }; // 45 degrees
    common::f64 minArea{ 1e-6 };
};

struct cook_navigation_t {
    common::vector_t<cook_float3_t> positions{};
    common::vector_t<common::u32> indices{};                   // 3 per candidate
    common::vector_t<geometry_source_id_t> triangleObject{};   // 1 per candidate
    common::vector_t<geometry_source_id_t> triangleElement{};  // 1 per candidate
    common::vector_t<common::f32> triangleSlope{};             // radians from up
    common::u32 cSkippedFloatRange{ 0u };
    common::content_hash_t contentHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

CYPHER_NODISCARD geometry_status_t CookNavigation_Init( cook_navigation_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void CookNavigation_Shutdown( cook_navigation_t *pOut ) noexcept;

// INVALID_ARGUMENT for a non-unit up or a slope outside [0, pi/2].
CYPHER_NODISCARD geometry_status_t CookNavigation_TryBuild(
    const cook_surface_soup_t *pSoup,
    const cook_navigation_options_t &options,
    cook_navigation_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_NAVIGATION_H
