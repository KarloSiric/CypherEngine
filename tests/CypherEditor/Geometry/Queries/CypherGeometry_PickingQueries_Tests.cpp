//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PickingQueries_Tests.cpp
//  Purpose: Verifies component and planar-region picking contracts.
//  Details: Covers canonical unit rays, bounded world-distance results,
//           deterministic ties, policy and topology rejection, neutral error
//           outputs, canonical empty meshes, and planar filled-area behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PickingQueries.h"

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

namespace
{

using Catch::Approx;

geometry_raycast_ray_t Ray(
    common::f64 ox,
    common::f64 oy,
    common::f64 oz,
    common::f64 dx,
    common::f64 dy,
    common::f64 dz ) noexcept
{
    return {
        math::Vec3d_Make( ox, oy, oz ),
        math::Vec3d_Make( dx, dy, dz )
    };
}

geometry_proximity_pick_options_t ProximityOptions(
    common::f64 fMaximumProximity ) noexcept
{
    geometry_proximity_pick_options_t options{};
    options.rayRange.fMaximumDistance = 100.0;
    options.fMaximumProximity = fMaximumProximity;
    return options;
}

void RequireNeutral( const boundary_vertex_pick_t &pick )
{
    REQUIRE_FALSE( pick.bHit );
    REQUIRE( pick.fRayDistance == 0.0 );
    REQUIRE( pick.fProximity == 0.0 );
    REQUIRE( pick.iVertex == CY_INVALID_INDEX );
}

void RequireNeutral( const boundary_edge_pick_t &pick )
{
    REQUIRE_FALSE( pick.bHit );
    REQUIRE( pick.fRayDistance == 0.0 );
    REQUIRE( pick.fProximity == 0.0 );
    REQUIRE( pick.fEdgeParameter == 0.0 );
    REQUIRE( pick.iEdge == CY_INVALID_INDEX );
}

void RequireNeutral( const mesh_vertex_pick_t &pick )
{
    REQUIRE_FALSE( pick.bHit );
    REQUIRE( pick.fRayDistance == 0.0 );
    REQUIRE( pick.fProximity == 0.0 );
    REQUIRE_FALSE( GeometryHandle_IsValid( pick.hVertex ) );
}

void RequireNeutral( const mesh_edge_pick_t &pick )
{
    REQUIRE_FALSE( pick.bHit );
    REQUIRE( pick.fRayDistance == 0.0 );
    REQUIRE( pick.fProximity == 0.0 );
    REQUIRE( pick.fEdgeParameter == 0.0 );
    REQUIRE_FALSE( GeometryHandle_IsValid( pick.hEdge ) );
}

void RequireNeutral( const planar_region_pick_t &pick )
{
    REQUIRE_FALSE( pick.bHit );
    REQUIRE( pick.fDistance == 0.0 );
    REQUIRE( pick.position.x == 0.0 );
    REQUIRE( pick.position.y == 0.0 );
    REQUIRE( pick.position.z == 0.0 );
    REQUIRE( pick.positionInFrame.x == 0.0 );
    REQUIRE( pick.positionInFrame.y == 0.0 );
    REQUIRE_FALSE( GeometrySourceId_IsValid( pick.regionSourceId ) );
    REQUIRE_FALSE( GeometrySourceId_IsValid( pick.polygonSourceId ) );
    REQUIRE( pick.iPolygon == CY_INVALID_INDEX );
}

struct BoxFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    BoxFixture()
    {
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAllocator,
                     math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                     math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushBoundary_TryReconstruct(
                     &boundary, &brush, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary(
                     &mesh, &boundary ) == geometry_status_t::OK );
    }

    ~BoxFixture()
    {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

struct PlanarFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    planar_region_t region{};

    PlanarFixture()
    {
        planar_frame_t frame{};
        REQUIRE( PlanarFrame_TryFromPlane(
                     math::Planed_Make(
                         math::Vec3d_Make( 0.0, 0.0, 1.0 ), -2.0 ),
                     &frame ) == geometry_status_t::OK );
        REQUIRE( PlanarRegion_Init(
                     &region, &allocator, frame, { 101u } ) ==
                 geometry_status_t::OK );

        const auto project = [&]( common::f64 x, common::f64 y ) {
            return PlanarFrame_Project(
                frame, math::Vec3d_Make( x, y, 2.0 ) );
        };
        const math::vec2d_t outer[] = {
            project( -4.0, -4.0 ), project( 4.0, -4.0 ),
            project( 4.0, 4.0 ), project( -4.0, 4.0 )
        };
        const math::vec2d_t hole[] = {
            project( -1.0, -1.0 ), project( -1.0, 1.0 ),
            project( 1.0, 1.0 ), project( 1.0, -1.0 )
        };
        const math::vec2d_t touching[] = {
            project( 4.0, 4.0 ), project( 6.0, 4.0 ),
            project( 6.0, 6.0 ), project( 4.0, 6.0 )
        };
        REQUIRE( PlanarRegion_TryAddPolygon(
                     &region, { 201u }, { 301u },
                     { outer, 4u }, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( PlanarRegion_TryAddHole(
                     &region, { 302u }, { hole, 4u } ) ==
                 geometry_status_t::OK );
        REQUIRE( PlanarRegion_TryAddPolygon(
                     &region, { 202u }, { 303u },
                     { touching, 4u }, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( PlanarRegion_Validate(
                     &region, policy ).status == geometry_status_t::OK );
    }

    ~PlanarFixture()
    {
        PlanarRegion_Shutdown( &region );
    }
};

geometry_mesh_edge_handle_t FirstMeshEdge(
    const editable_mesh_t *pMesh ) noexcept
{
    geometry_mesh_edge_handle_t result{};
    (void)common::GenerationPool_ForEach(
        &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t & ) noexcept -> common::bool_t {
            result = hEdge;
            return false;
        } );
    return result;
}

geometry_mesh_vertex_handle_t FirstMeshVertex(
    const editable_mesh_t *pMesh ) noexcept
{
    geometry_mesh_vertex_handle_t result{};
    (void)common::GenerationPool_ForEach(
        &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hVertex,
             const mesh_vertex_record_t & ) noexcept -> common::bool_t {
            result = hVertex;
            return false;
        } );
    return result;
}

} // namespace

TEST_CASE( "PickingQueries: boundary picks use bounded world distances",
           "[PickingQueries][Boundary]" )
{
    BoxFixture f;
    geometry_proximity_pick_options_t options =
        ProximityOptions( 0.1 );
    const geometry_raycast_ray_t ray =
        Ray( 1.05, 1.0, -5.0, 0.0, 0.0, 1.0 );

    boundary_vertex_pick_t vertex{};
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.bHit );
    REQUIRE( vertex.iVertex < f.boundary.vertices.nCount );
    const math::vec3d_t position =
        f.boundary.vertices.pData[vertex.iVertex];
    REQUIRE( position.x == Approx( 1.0 ) );
    REQUIRE( position.y == Approx( 1.0 ) );
    REQUIRE( position.z == Approx( -1.0 ) );
    REQUIRE( vertex.fRayDistance == Approx( 4.0 ) );
    REQUIRE( vertex.fProximity == Approx( 0.05 ) );

    boundary_edge_pick_t edge{};
    REQUIRE( BoundaryQueries_TryPickNearestEdge(
                 &f.boundary, ray, options, f.policy, &edge ) ==
             geometry_status_t::OK );
    REQUIRE( edge.bHit );
    REQUIRE( edge.fRayDistance == Approx( 4.0 ) );
    REQUIRE( edge.fProximity == Approx( 0.05 ) );
    REQUIRE( edge.fEdgeParameter >= 0.0 );
    REQUIRE( edge.fEdgeParameter <= 1.0 );

    // Closest approach is computed against the bounded ray segment. A vertex
    // beyond fMaximumDistance therefore clamps to the range endpoint.
    options = ProximityOptions( 3.0 );
    options.rayRange.fMaximumDistance = 2.0;
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary,
                 Ray( 0.0, 0.0, -5.0, 0.0, 0.0, 1.0 ),
                 options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.bHit );
    REQUIRE( vertex.fRayDistance == Approx( 2.0 ) );
    REQUIRE( vertex.fProximity == Approx( std::sqrt( 6.0 ) ) );
}

TEST_CASE( "PickingQueries: boundary ties use stable component indices",
           "[PickingQueries][Boundary]" )
{
    BoxFixture f;
    const geometry_raycast_ray_t ray =
        Ray( 0.0, 0.0, -5.0, 0.0, 0.0, 1.0 );
    geometry_proximity_pick_options_t options =
        ProximityOptions( 2.0 );

    common::u32 iExpectedVertex = CY_INVALID_INDEX;
    for ( common::usize i = 0u;
          i < f.boundary.vertices.nCount;
          ++i ) {
        if ( f.boundary.vertices.pData[i].z == -1.0 ) {
            iExpectedVertex = static_cast<common::u32>( i );
            break;
        }
    }
    REQUIRE( iExpectedVertex != CY_INVALID_INDEX );

    boundary_vertex_pick_t vertex{};
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.bHit );
    REQUIRE( vertex.iVertex == iExpectedVertex );
    REQUIRE( vertex.fRayDistance == Approx( 4.0 ) );

    common::u32 iExpectedEdge = CY_INVALID_INDEX;
    for ( common::usize i = 0u; i < f.boundary.edges.nCount; ++i ) {
        const brush_boundary_edge_t &candidate =
            f.boundary.edges.pData[i];
        const math::vec3d_t a =
            f.boundary.vertices.pData[candidate.iVertex0];
        const math::vec3d_t b =
            f.boundary.vertices.pData[candidate.iVertex1];
        if ( a.z == -1.0 && b.z == -1.0 ) {
            iExpectedEdge = static_cast<common::u32>( i );
            break;
        }
    }
    REQUIRE( iExpectedEdge != CY_INVALID_INDEX );

    boundary_edge_pick_t edge{};
    REQUIRE( BoundaryQueries_TryPickNearestEdge(
                 &f.boundary, ray, options, f.policy, &edge ) ==
             geometry_status_t::OK );
    REQUIRE( edge.bHit );
    REQUIRE( edge.iEdge == iExpectedEdge );
    REQUIRE( edge.fRayDistance == Approx( 4.0 ) );
    REQUIRE( edge.fProximity == Approx( 1.0 ) );
}

TEST_CASE( "PickingQueries: boundary errors are deterministic and neutral",
           "[PickingQueries][Boundary]" )
{
    BoxFixture f;
    geometry_proximity_pick_options_t options =
        ProximityOptions( 1.0 );
    geometry_raycast_ray_t ray =
        Ray( 0.0, 0.0, -5.0, 0.0, 0.0, 1.0 );

    boundary_vertex_pick_t vertex{};
    vertex.bHit = true;
    ray.direction.z = 2.0;
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, f.policy, &vertex ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireNeutral( vertex );

    ray.direction.z = 1.0;
    options.rayRange.fMinimumDistance = 3.0;
    options.rayRange.fMaximumDistance = 2.0;
    vertex.bHit = true;
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, f.policy, &vertex ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireNeutral( vertex );

    options = ProximityOptions( 1.0 );
    geometry_policy_t invalidPolicy = f.policy;
    invalidPolicy.numerical.fUnitNormalTolerance = -1.0;
    vertex.bHit = true;
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, invalidPolicy, &vertex ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireNeutral( vertex );

    boundary_edge_pick_t edge{};
    edge.bHit = true;
    const brush_boundary_edge_t savedEdge = f.boundary.edges.pData[0];
    f.boundary.edges.pData[0].iVertex1 =
        f.boundary.edges.pData[0].iVertex0;
    REQUIRE( BoundaryQueries_TryPickNearestEdge(
                 &f.boundary, ray, options, f.policy, &edge ) ==
             geometry_status_t::INVALID_TOPOLOGY );
    RequireNeutral( edge );
    f.boundary.edges.pData[0] = savedEdge;

    const math::vec3d_t savedVertex = f.boundary.vertices.pData[0];
    f.boundary.vertices.pData[0].x =
        std::numeric_limits<common::f64>::quiet_NaN();
    vertex.bHit = true;
    REQUIRE( BoundaryQueries_TryPickNearestVertex(
                 &f.boundary, ray, options, f.policy, &vertex ) ==
             geometry_status_t::NUMERIC_FAILURE );
    RequireNeutral( vertex );
    f.boundary.vertices.pData[0] = savedVertex;

    geometry_policy_t limited = f.policy;
    limited.limits.cEdgesMax = f.boundary.edges.nCount - 1u;
    edge.bHit = true;
    REQUIRE( BoundaryQueries_TryPickNearestEdge(
                 &f.boundary, ray, options, limited, &edge ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    RequireNeutral( edge );
}

TEST_CASE( "PickingQueries: mesh components preserve live handle identity",
           "[PickingQueries][Mesh]" )
{
    BoxFixture f;
    const geometry_raycast_ray_t nearCorner =
        Ray( -1.02, -1.0, 9.0, 0.0, 0.0, -1.0 );
    const geometry_proximity_pick_options_t options =
        ProximityOptions( 0.05 );

    mesh_vertex_pick_t vertex{};
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &f.mesh, nearCorner, options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.bHit );
    const mesh_vertex_record_t *pVertex =
        EditableMesh_GetVertex( &f.mesh, vertex.hVertex );
    REQUIRE( pVertex != nullptr );
    REQUIRE( pVertex->position.z == Approx( 1.0 ) );
    REQUIRE( vertex.fRayDistance == Approx( 8.0 ) );
    REQUIRE( vertex.fProximity == Approx( 0.02 ) );

    mesh_edge_pick_t edge{};
    REQUIRE( MeshQueries_TryPickNearestEdge(
                 &f.mesh, nearCorner, options, f.policy, &edge ) ==
             geometry_status_t::OK );
    REQUIRE( edge.bHit );
    REQUIRE( EditableMesh_GetEdge( &f.mesh, edge.hEdge ) != nullptr );
    REQUIRE( edge.fProximity == Approx( 0.02 ) );
}

TEST_CASE( "PickingQueries: mesh ties use handle order and empty mesh misses",
           "[PickingQueries][Mesh]" )
{
    BoxFixture f;
    const geometry_raycast_ray_t ray =
        Ray( 0.0, 0.0, 5.0, 0.0, 0.0, -1.0 );
    const geometry_proximity_pick_options_t options =
        ProximityOptions( 2.0 );

    geometry_mesh_vertex_handle_t hExpectedVertex{};
    (void)common::GenerationPool_ForEach(
        &f.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t hVertex,
             const mesh_vertex_record_t &vertex ) noexcept -> common::bool_t {
            if ( vertex.position.z == 1.0 ) {
                hExpectedVertex = hVertex;
                return false;
            }
            return true;
        } );
    REQUIRE( GeometryHandle_IsValid( hExpectedVertex ) );

    mesh_vertex_pick_t vertex{};
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &f.mesh, ray, options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    REQUIRE( vertex.bHit );
    REQUIRE( vertex.hVertex.nSlot == hExpectedVertex.nSlot );
    REQUIRE( vertex.hVertex.nGeneration ==
             hExpectedVertex.nGeneration );

    geometry_mesh_edge_handle_t hExpectedEdge{};
    (void)common::GenerationPool_ForEach(
        &f.mesh.edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edgeRecord ) noexcept -> common::bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                EditableMesh_GetHalfEdge(
                    &f.mesh, edgeRecord.hHalfEdge );
            REQUIRE( pHalfEdge != nullptr );
            const mesh_half_edge_record_t *pTwin =
                EditableMesh_GetHalfEdge( &f.mesh, pHalfEdge->hTwin );
            REQUIRE( pTwin != nullptr );
            const mesh_vertex_record_t *pA =
                EditableMesh_GetVertex( &f.mesh, pHalfEdge->hOrigin );
            const mesh_vertex_record_t *pB =
                EditableMesh_GetVertex( &f.mesh, pTwin->hOrigin );
            REQUIRE( pA != nullptr );
            REQUIRE( pB != nullptr );
            if ( pA->position.z == 1.0 && pB->position.z == 1.0 ) {
                hExpectedEdge = hEdge;
                return false;
            }
            return true;
        } );
    REQUIRE( GeometryHandle_IsValid( hExpectedEdge ) );

    mesh_edge_pick_t edge{};
    REQUIRE( MeshQueries_TryPickNearestEdge(
                 &f.mesh, ray, options, f.policy, &edge ) ==
             geometry_status_t::OK );
    REQUIRE( edge.bHit );
    REQUIRE( edge.hEdge.nSlot == hExpectedEdge.nSlot );
    REQUIRE( edge.hEdge.nGeneration == hExpectedEdge.nGeneration );

    editable_mesh_t empty{};
    REQUIRE( EditableMesh_Init( &empty, &f.allocator ) ==
             geometry_status_t::OK );
    vertex.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &empty, ray, options, f.policy, &vertex ) ==
             geometry_status_t::OK );
    RequireNeutral( vertex );
    edge.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestEdge(
                 &empty, ray, options, f.policy, &edge ) ==
             geometry_status_t::OK );
    RequireNeutral( edge );
    EditableMesh_Shutdown( &empty );
}

TEST_CASE( "PickingQueries: malformed meshes fail instead of skipping",
           "[PickingQueries][Mesh]" )
{
    BoxFixture f;
    const geometry_raycast_ray_t ray =
        Ray( 0.0, 0.0, 5.0, 0.0, 0.0, -1.0 );
    const geometry_proximity_pick_options_t options =
        ProximityOptions( 2.0 );

    editable_mesh_t uninitialized{};
    mesh_vertex_pick_t vertex{};
    vertex.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &uninitialized, ray, options, f.policy, &vertex ) ==
             geometry_status_t::NOT_INITIALIZED );
    RequireNeutral( vertex );

    const geometry_mesh_edge_handle_t hEdge = FirstMeshEdge( &f.mesh );
    mesh_edge_record_t *pEdge = common::GenerationPool_Get(
        &f.mesh.edges, hEdge );
    REQUIRE( pEdge != nullptr );
    const geometry_mesh_half_edge_handle_t savedHalfEdge =
        pEdge->hHalfEdge;
    pEdge->hHalfEdge = {};
    mesh_edge_pick_t edge{};
    edge.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestEdge(
                 &f.mesh, ray, options, f.policy, &edge ) ==
             geometry_status_t::INVALID_TOPOLOGY );
    RequireNeutral( edge );
    pEdge->hHalfEdge = savedHalfEdge;

    const geometry_mesh_vertex_handle_t hVertex =
        FirstMeshVertex( &f.mesh );
    mesh_vertex_record_t *pVertex = common::GenerationPool_Get(
        &f.mesh.vertices, hVertex );
    REQUIRE( pVertex != nullptr );
    const math::vec3d_t savedPosition = pVertex->position;
    pVertex->position.x =
        std::numeric_limits<common::f64>::infinity();
    vertex.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &f.mesh, ray, options, f.policy, &vertex ) ==
             geometry_status_t::NUMERIC_FAILURE );
    RequireNeutral( vertex );
    pVertex->position = savedPosition;

    geometry_policy_t limited = f.policy;
    limited.limits.cVerticesMax =
        EditableMesh_VertexCount( &f.mesh ) - 1u;
    vertex.bHit = true;
    REQUIRE( MeshQueries_TryPickNearestVertex(
                 &f.mesh, ray, options, limited, &vertex ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    RequireNeutral( vertex );
}

TEST_CASE( "PickingQueries: planar picking respects holes and provenance",
           "[PickingQueries][Planar]" )
{
    PlanarFixture f;
    geometry_ray_distance_range_t range{};
    range.fMaximumDistance = 100.0;

    planar_region_pick_t pick{};
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 3.0, 3.0, 10.0, 0.0, 0.0, -1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    REQUIRE( pick.bHit );
    REQUIRE( pick.fDistance == Approx( 8.0 ) );
    REQUIRE( pick.position.z == Approx( 2.0 ) );
    REQUIRE( pick.iPolygon == 0u );
    REQUIRE( pick.regionSourceId.value == 101u );
    REQUIRE( pick.polygonSourceId.value == 201u );

    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 0.0, 0.0, 10.0, 0.0, 0.0, -1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    RequireNeutral( pick );
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 9.0, 9.0, 10.0, 0.0, 0.0, -1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    RequireNeutral( pick );
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 0.0, 0.0, 10.0, 1.0, 0.0, 0.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    RequireNeutral( pick );

    range.fMaximumDistance = 7.0;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 3.0, 3.0, 10.0, 0.0, 0.0, -1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    RequireNeutral( pick );

    // Two valid polygons touch at this boundary point. Ascending polygon
    // order is the documented stable tie break.
    range.fMaximumDistance = 100.0;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 4.0, 4.0, 10.0, 0.0, 0.0, -1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    REQUIRE( pick.bHit );
    REQUIRE( pick.iPolygon == 0u );
    REQUIRE( pick.polygonSourceId.value == 201u );

    // Hits at the ray origin are excluded consistently with surface raycasts.
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region,
                 Ray( 3.0, 3.0, 2.0, 0.0, 0.0, 1.0 ),
                 range, f.policy, &pick ) == geometry_status_t::OK );
    RequireNeutral( pick );
}

TEST_CASE( "PickingQueries: planar errors are neutral and empty is valid",
           "[PickingQueries][Planar]" )
{
    PlanarFixture f;
    geometry_ray_distance_range_t range{};
    range.fMaximumDistance = 100.0;
    geometry_raycast_ray_t ray =
        Ray( 3.0, 3.0, 10.0, 0.0, 0.0, -1.0 );

    planar_region_pick_t pick{};
    pick.bHit = true;
    ray.direction.z = -2.0;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region, ray, range, f.policy, &pick ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireNeutral( pick );

    ray.direction.z = -1.0;
    range.fMinimumDistance = 2.0;
    range.fMaximumDistance = 1.0;
    pick.bHit = true;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region, ray, range, f.policy, &pick ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireNeutral( pick );

    range = {};
    range.fMaximumDistance = 100.0;
    const common::u32 savedFirstContour =
        f.region.polygons.pData[0].iFirstContour;
    f.region.polygons.pData[0].iFirstContour = 1u;
    pick.bHit = true;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region, ray, range, f.policy, &pick ) ==
             geometry_status_t::INVALID_TOPOLOGY );
    RequireNeutral( pick );
    f.region.polygons.pData[0].iFirstContour = savedFirstContour;

    const math::vec2d_t savedPoint = f.region.points.pData[0];
    f.region.points.pData[0].x =
        std::numeric_limits<common::f64>::quiet_NaN();
    pick.bHit = true;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &f.region, ray, range, f.policy, &pick ) ==
             geometry_status_t::NUMERIC_FAILURE );
    RequireNeutral( pick );
    f.region.points.pData[0] = savedPoint;

    planar_frame_t frame = f.region.frame;
    planar_region_t empty{};
    REQUIRE( PlanarRegion_Init(
                 &empty, &f.allocator, frame, { 999u } ) ==
             geometry_status_t::OK );
    pick.bHit = true;
    REQUIRE( PlanarRegionQueries_TryPick(
                 &empty, ray, range, f.policy, &pick ) ==
             geometry_status_t::OK );
    RequireNeutral( pick );
    PlanarRegion_Shutdown( &empty );
}

} // namespace cypher::editor::geometry
