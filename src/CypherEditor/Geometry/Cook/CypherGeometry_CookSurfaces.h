//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSurfaces.h
//  Purpose: Declares the shared cook input: every surface of a snapshot
//           (brushes, meshes, patches, heightfields) as one double-precision
//           triangle soup, each triangle carrying its source object,
//           authored element, material and outward normal.
//  Details: Navigation, batching, visibility, lighting and the compiler
//           interchange all start from "the triangles of the map with their
//           provenance". Producing that once keeps them agreeing on the
//           surfaces (same tessellation choices as the collision cook: brush
//           boundaries, corner-aware mesh ear clipping, adaptive patch
//           tessellation, full-resolution heightfields sharing one vertex
//           per sample) and keeps each product's file about its own job.
//
//           Objects are in ascending source-ID order, like every cook, so
//           the soup does not depend on document order. Positions stay in
//           doubles here; each product converts to floats where it
//           publishes (ARCHITECTURE.md: float conversion only in Cook).
//
//           An object whose surface cannot be produced (bad brush planes, an
//           untriangulable face) is skipped and reported, never aborting the
//           rest - the same policy as the collision cook.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_SURFACES_H
#define CYPHER_EDITOR_GEOMETRY_COOK_SURFACES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookKeys.h"
#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

struct cook_soup_triangle_t {
    common::u32 v[3]{ 0u, 0u, 0u };        // soup vertices, counter-clockwise from outside
    common::u32 iObject{ 0u };             // index into objects
    geometry_source_id_t elementId{};      // side / face / patch / tile
    geometry_material_ref_t material{};
    math::vec3d_t normal{};                // unit (zero for a degenerate triangle)
    common::f64 area{ 0.0 };
};

struct cook_soup_object_t {
    geometry_source_id_t objectId{};
    cook_source_kind_t kind{ cook_source_kind_t::INVALID };
    common::u32 iFirstVertex{ 0u }, cVertices{ 0u };
    common::u32 iFirstTriangle{ 0u }, cTriangles{ 0u };
    bool bClosed{ false };
    bool bConvex{ false };
};

struct cook_soup_problem_t {
    geometry_source_id_t objectId{};
    geometry_source_id_t elementId{};
    geometry_status_t status{ geometry_status_t::OK };
};

struct cook_surface_soup_t {
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<cook_soup_triangle_t> triangles{};
    common::vector_t<cook_soup_object_t> objects{};
    common::vector_t<cook_soup_problem_t> problems{}; // skipped objects
    math::vec3d_t lo{}, hi{};                          // bounds of all positions
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
};

CYPHER_NODISCARD geometry_status_t CookSurfaces_Init( cook_surface_soup_t *pSoup, const common::allocator_t *pAllocator ) noexcept;
void CookSurfaces_Shutdown( cook_surface_soup_t *pSoup ) noexcept;

// Builds the soup of every object in the snapshot. Only allocation failure
// (ALLOCATION_FAILED) or bad arguments fail the whole build; the soup is
// empty then.
CYPHER_NODISCARD geometry_status_t CookSurfaces_TryBuild(
    const geometry_snapshot_t *pSnapshot,
    cook_surface_soup_t *pSoup ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_SURFACES_H
