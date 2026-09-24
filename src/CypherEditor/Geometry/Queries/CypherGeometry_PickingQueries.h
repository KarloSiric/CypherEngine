//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PickingQueries.h
//  Purpose: Declares allocation-free component and planar-region picking.
//  Details: Surface intersection belongs to RaycastQueries. This module adds
//           the complementary editor operations that select the nearest
//           boundary or mesh component and the filled part of a planar region.
//           Every operation uses the canonical binary64, unit-direction ray;
//           ray parameters and result distances are therefore world distances.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PICKING_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_PICKING_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_RaycastQueries.h"

namespace cypher::editor::geometry
{

// Inclusive world-distance interval along a canonical ray. As with surface
// raycasts, a query raises the accepted minimum to the policy's absolute
// distance tolerance so a component or plane at the origin is not selected.
struct geometry_ray_distance_range_t {
    common::f64 fMinimumDistance{ 0.0 };
    common::f64 fMaximumDistance{ 1.0e6 };
};

// A component is eligible only when its closest point to the bounded ray is
// no farther than fMaximumProximity in world units.
struct geometry_proximity_pick_options_t {
    geometry_ray_distance_range_t rayRange{};
    common::f64 fMaximumProximity{ 0.1 };
};

// OK plus bHit=false is the ordinary no-component result. Every error leaves
// the complete output neutral, including the invalid component identity.
struct boundary_vertex_pick_t {
    common::bool_t bHit{ false };
    common::f64 fRayDistance{ 0.0 };
    common::f64 fProximity{ 0.0 };
    common::u32 iVertex{ CY_INVALID_INDEX };
};

struct boundary_edge_pick_t {
    common::bool_t bHit{ false };
    common::f64 fRayDistance{ 0.0 };
    common::f64 fProximity{ 0.0 };
    common::f64 fEdgeParameter{ 0.0 };
    common::u32 iEdge{ CY_INVALID_INDEX };
};

struct mesh_vertex_pick_t {
    common::bool_t bHit{ false };
    common::f64 fRayDistance{ 0.0 };
    common::f64 fProximity{ 0.0 };
    geometry_mesh_vertex_handle_t hVertex{};
};

struct mesh_edge_pick_t {
    common::bool_t bHit{ false };
    common::f64 fRayDistance{ 0.0 };
    common::f64 fProximity{ 0.0 };
    common::f64 fEdgeParameter{ 0.0 };
    geometry_mesh_edge_handle_t hEdge{};
};

struct planar_region_pick_t {
    common::bool_t bHit{ false };
    common::f64 fDistance{ 0.0 };
    math::vec3d_t position{};
    math::vec2d_t positionInFrame{};
    geometry_source_id_t regionSourceId{};
    geometry_source_id_t polygonSourceId{};
    common::u32 iPolygon{ CY_INVALID_INDEX };
};

// These queries validate the complete canonical brush boundary before reading
// any candidate. Near-equal candidates use ray distance and then the lower
// boundary index as deterministic tie breakers. They allocate no memory.
CYPHER_NODISCARD geometry_status_t
BoundaryQueries_TryPickNearestVertex(
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    boundary_vertex_pick_t *pPickOut ) noexcept;

CYPHER_NODISCARD geometry_status_t
BoundaryQueries_TryPickNearestEdge(
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    boundary_edge_pick_t *pPickOut ) noexcept;

// Mesh inputs must be canonical closed editable meshes. A canonical empty mesh
// is an allocation-free miss. Near-equal candidates use ray distance followed
// by handle slot/generation, so pool iteration order cannot change the result.
CYPHER_NODISCARD geometry_status_t
MeshQueries_TryPickNearestVertex(
    const editable_mesh_t *pMesh,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    mesh_vertex_pick_t *pPickOut ) noexcept;

CYPHER_NODISCARD geometry_status_t
MeshQueries_TryPickNearestEdge(
    const editable_mesh_t *pMesh,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    mesh_edge_pick_t *pPickOut ) noexcept;

// Intersects the validated region plane and accepts the filled part of one
// polygon, including its boundary and excluding hole interiors. If weakly
// simple polygons meet at the hit point, the lowest polygon index wins.
CYPHER_NODISCARD geometry_status_t PlanarRegionQueries_TryPick(
    const planar_region_t *pRegion,
    geometry_raycast_ray_t ray,
    const geometry_ray_distance_range_t &range,
    const geometry_policy_t &policy,
    planar_region_pick_t *pPickOut ) noexcept;

static_assert( std::is_trivially_copyable_v<geometry_ray_distance_range_t> );
static_assert( std::is_trivially_copyable_v<geometry_proximity_pick_options_t> );
static_assert( std::is_trivially_copyable_v<boundary_vertex_pick_t> );
static_assert( std::is_trivially_copyable_v<boundary_edge_pick_t> );
static_assert( std::is_trivially_copyable_v<mesh_vertex_pick_t> );
static_assert( std::is_trivially_copyable_v<mesh_edge_pick_t> );
static_assert( std::is_trivially_copyable_v<planar_region_pick_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PICKING_QUERIES_H
