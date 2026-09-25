//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_TriangleSoup.h
//  Purpose: Declares TriangleSoup: bounded independent triangles as they
//           arrive from importers (STL-style: three positions per triangle,
//           no shared vertices), and the explicit weld into PolygonSoup.
//  Details: Independent triangles are the least structured useful input.
//           Nothing about them is trusted: coincident corners are not
//           "the same vertex" until TriangleSoup_TryWeldToPolygonSoup is
//           asked to make them so, at a caller-chosen distance.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TRIANGLE_SOUP_H
#define CYPHER_EDITOR_GEOMETRY_TRIANGLE_SOUP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PolygonSoup.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kTriangleSoupTrianglesMax = 65536u;

struct triangle_soup_triangle_t {
    math::vec3d_t p[3]{};
    geometry_source_id_t sourceId{};
    common::u32 iGroup{ 0u };
};

struct triangle_soup_t {
    common::vector_t<triangle_soup_triangle_t> triangles{};
};

CYPHER_NODISCARD geometry_status_t TriangleSoup_Init(
    triangle_soup_t *pSoup,
    const common::allocator_t *pAllocator ) noexcept;

void TriangleSoup_Shutdown( triangle_soup_t *pSoup ) noexcept;

CYPHER_NODISCARD bool TriangleSoup_IsInitialized( const triangle_soup_t *pSoup ) noexcept;

CYPHER_NODISCARD geometry_status_t TriangleSoup_TryAdd(
    triangle_soup_t *pSoup,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    geometry_source_id_t sourceId,
    common::u32 iGroup ) noexcept;

CYPHER_NODISCARD common::usize TriangleSoup_Count( const triangle_soup_t *pSoup ) noexcept;

// Result of welding.
struct triangle_weld_result_t {
    common::u32 cVertices{ 0u };          // unique vertices in the output
    common::u32 cTrianglesKept{ 0u };
    common::u32 cTrianglesCollapsed{ 0u }; // two or more corners welded together
    common::f64 fMaxDisplacement{ 0.0 };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Welds corners within fWeldDistance (see PointWeld) and writes one
// 3-corner face per surviving triangle into pOut, which must be
// initialized and empty. Triangles whose corners collapse (two corners in
// one cluster) are dropped and counted — they have no area to keep.
// Source IDs and groups are carried per face. Failure-atomic: pOut stays
// empty on failure.
//
// Rejects non-finite corners with NUMERIC_FAILURE (a weld cannot place
// them) and fWeldDistance < 0 with INVALID_ARGUMENT.
CYPHER_NODISCARD triangle_weld_result_t TriangleSoup_TryWeldToPolygonSoup(
    const triangle_soup_t *pSoup,
    common::f64 fWeldDistance,
    polygon_soup_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TRIANGLE_SOUP_H
