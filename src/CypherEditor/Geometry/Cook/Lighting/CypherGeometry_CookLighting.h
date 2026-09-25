//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookLighting.h
//  Purpose: Declares lightmap charts: the map's surfaces cut into charts
//           that each flatten without overlap, laid out in atlas pages, and
//           the resulting lightmap UV for every triangle corner - what a
//           lighting baker needs from geometry.
//  Details: Charts grow over each object's triangles from a seed, taking a
//           neighbour while its normal stays within chartAngle of the seed's.
//           Every triangle of a chart then faces the seed direction by more
//           than 90 - chartAngle degrees, so projecting the chart onto the
//           seed's plane cannot fold it (a flat brush side is always one
//           chart; a sphere becomes a few caps). Planar projection keeps
//           texel density uniform (texelsPerUnit) and is exact for the
//           planar surfaces a level is mostly made of; an LSCM unwrap can
//           replace it per chart later without changing this interface.
//
//           Layout: charts sorted by height, width and chart order, then
//           placed on shelves in pages of pageSize texels with padding
//           around each chart so bilinear filtering never bleeds between
//           neighbours. A chart larger than a page is scaled down to fit and
//           its scale recorded (the baker can report the lost density).
//           Everything is deterministic: same soup and options, same atlas.
//
//           Lighting equations, baking, probes and GPU resources are not
//           here (README).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_LIGHTING_H
#define CYPHER_EDITOR_GEOMETRY_COOK_LIGHTING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookSurfaces.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCookLightingVersion = 1u;
inline constexpr common::u32 kCookLightingPageMax = 8192u;

struct cook_lighting_options_t {
    common::f64 texelsPerUnit{ 1.0 / 16.0 };
    common::u32 paddingTexels{ 2u };
    common::u32 pageSize{ 1024u };
    common::f64 chartAngleRadians{ 0.7853981633974483 }; // 45 degrees, < 90
};

struct cook_light_chart_t {
    geometry_source_id_t objectId{};
    common::u32 page{ 0u };
    common::u32 x{ 0u }, y{ 0u }, w{ 0u }, h{ 0u }; // texels, padding included
    math::vec3d_t normal{}, uAxis{}, vAxis{};       // projection frame
    common::f64 scale{ 1.0 };                        // < 1 when shrunk to fit a page
    common::u32 iFirstTriangle{ 0u }, cTriangles{ 0u }; // into chartTriangles
};

struct cook_lighting_t {
    common::vector_t<cook_light_chart_t> charts{};
    common::vector_t<common::u32> chartTriangles{}; // soup triangles grouped by chart
    common::vector_t<common::f32> cornerUvs{};      // 6 per chart triangle, page-normalised [0, 1]
    common::u32 cPages{ 0u };
    common::content_hash_t contentHash{};
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

CYPHER_NODISCARD geometry_status_t CookLighting_Init( cook_lighting_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void CookLighting_Shutdown( cook_lighting_t *pOut ) noexcept;

// INVALID_ARGUMENT for non-positive density, a page size outside
// [16, kCookLightingPageMax], padding that leaves no room, or an angle
// outside (0, pi/2).
CYPHER_NODISCARD geometry_status_t CookLighting_TryBuild(
    const cook_surface_soup_t *pSoup,
    const cook_lighting_options_t &options,
    cook_lighting_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_LIGHTING_H
