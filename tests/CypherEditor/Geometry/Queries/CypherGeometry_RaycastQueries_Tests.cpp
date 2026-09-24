//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_RaycastQueries_Tests.cpp
//  Purpose: Verifies brute-force brush and editable-mesh ray casting.
//  Details: Covers hit/no-hit semantics, normalized rays, culling, stable
//           provenance, non-quad polygons, explicit mesh scratch, deterministic
//           ties, malformed topology, and scratch rewind on every path.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_RaycastQueries.h"

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

using Catch::Approx;

struct RaycastFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    RaycastFixture()
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
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
                 geometry_status_t::OK );
    }

    ~RaycastFixture()
    {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
        BrushSolid_Shutdown( &brush );
    }
};

struct ConcavePrismFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    brush_boundary_t boundary{};
    editable_mesh_t mesh{};

    ConcavePrismFixture()
    {
        REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
                 geometry_status_t::OK );

        // Counter-clockwise L footprint. The reflex corner at (1, 1) makes
        // both cap faces genuinely concave rather than merely collinear.
        constexpr std::array<math::vec3d_t, 12u> kVertices{{
            { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 },
            { 2.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 },
            { 1.0, 2.0, 0.0 }, { 0.0, 2.0, 0.0 },
            { 0.0, 0.0, 1.0 }, { 2.0, 0.0, 1.0 },
            { 2.0, 1.0, 1.0 }, { 1.0, 1.0, 1.0 },
            { 1.0, 2.0, 1.0 }, { 0.0, 2.0, 1.0 }
        }};
        for ( const math::vec3d_t vertex : kVertices ) {
            REQUIRE( common::Vector_PushBack(
                &boundary.vertices, vertex ) );
        }

        const auto addUniqueEdge = [&]( common::u32 iA,
                                        common::u32 iB ) {
            const common::u32 iLow = iA < iB ? iA : iB;
            const common::u32 iHigh = iA < iB ? iB : iA;
            for ( common::usize i = 0u;
                  i < boundary.edges.nCount;
                  ++i ) {
                const brush_boundary_edge_t &edge =
                    boundary.edges.pData[i];
                if ( edge.iVertex0 == iLow &&
                     edge.iVertex1 == iHigh ) {
                    return;
                }
            }
            REQUIRE( common::Vector_PushBack(
                &boundary.edges,
                brush_boundary_edge_t{ iLow, iHigh } ) );
        };
        const auto addFace = [&]( common::u32 iSourceSide,
                                  const common::u32 *pIndices,
                                  common::usize cIndices ) {
            REQUIRE( cIndices >= 3u );
            REQUIRE( cIndices <= common::CY_U32_MAX );
            REQUIRE( boundary.faceVertexIndices.nCount <=
                     common::CY_U32_MAX );
            const brush_boundary_face_t face{
                iSourceSide,
                static_cast<common::u32>(
                    boundary.faceVertexIndices.nCount ),
                static_cast<common::u32>( cIndices )
            };
            REQUIRE( common::Vector_PushBack( &boundary.faces, face ) );
            for ( common::usize i = 0u; i < cIndices; ++i ) {
                REQUIRE( common::Vector_PushBack(
                    &boundary.faceVertexIndices, pIndices[i] ) );
                addUniqueEdge( pIndices[i],
                               pIndices[( i + 1u ) % cIndices] );
            }
        };

        constexpr std::array<common::u32, 6u> kBottom{
            5u, 4u, 3u, 2u, 1u, 0u };
        constexpr std::array<common::u32, 6u> kTop{
            6u, 7u, 8u, 9u, 10u, 11u };
        addFace( 0u, kBottom.data(), kBottom.size() );
        addFace( 1u, kTop.data(), kTop.size() );
        for ( common::u32 i = 0u; i < 6u; ++i ) {
            const common::u32 iNext = ( i + 1u ) % 6u;
            const std::array<common::u32, 4u> side{
                i, iNext, 6u + iNext, 6u + i };
            addFace( 2u + i, side.data(), side.size() );
        }

        REQUIRE( boundary.edges.nCount == 18u );
        REQUIRE( EditableMesh_Init( &mesh, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshBuilder_TryBuildFromBoundary( &mesh, &boundary ) ==
                 geometry_status_t::OK );
    }

    ~ConcavePrismFixture()
    {
        EditableMesh_Shutdown( &mesh );
        BrushBoundary_Shutdown( &boundary );
    }
};

geometry_raycast_ray_t DownwardRay() noexcept
{
    return {
        math::Vec3d_Make( 0.0, 0.0, 5.0 ),
        math::Vec3d_Make( 0.0, 0.0, -1.0 )
    };
}

geometry_raycast_options_t DefaultOptions() noexcept
{
    geometry_raycast_options_t options{};
    options.fMaximumDistance = 100.0;
    return options;
}

geometry_status_t AcquireScratch(
    geometry_scratch_t *pScratch,
    std::vector<common::byte> *pStorage,
    common::usize cbCapacity,
    const common::allocator_t *pAllocator )
{
    pStorage->assign( cbCapacity + 64u, common::byte{} );
    return GeometryScratch_Acquire(
        pScratch,
        {
            { pStorage->data(), pStorage->size() },
            pAllocator,
            cbCapacity,
            cbCapacity,
            alignof( std::max_align_t )
        } );
}

geometry_mesh_edge_handle_t FirstEdge(
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

void RequireScratchEmpty( const geometry_scratch_t &scratch )
{
    geometry_scratch_stats_t stats{};
    REQUIRE( GeometryScratch_QueryStats( &scratch, &stats ) ==
             geometry_status_t::OK );
    REQUIRE( stats.cbUsed == 0u );
}

} // namespace

TEST_CASE( "RaycastQueries: brush reports nearest hit and stable provenance",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    brush_raycast_hit_t hit{};
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::OK );

    REQUIRE( hit.bHit );
    REQUIRE( hit.fDistance == Approx( 4.0 ) );
    REQUIRE( hit.position.x == Approx( 0.0 ) );
    REQUIRE( hit.position.y == Approx( 0.0 ) );
    REQUIRE( hit.position.z == Approx( 1.0 ) );
    REQUIRE( hit.normal.x == Approx( 0.0 ) );
    REQUIRE( hit.normal.y == Approx( 0.0 ) );
    REQUIRE( hit.normal.z == Approx( 1.0 ) );
    REQUIRE( hit.brushSourceId.value == f.brush.sourceId.value );
    REQUIRE( hit.iSide == 4u );
    REQUIRE( hit.sideSourceId.value == f.brush.sides.pData[4].sourceId.value );
    REQUIRE( hit.iTriangleInFace == 0u );
    REQUIRE( hit.iVertex0 < f.boundary.vertices.nCount );
    REQUIRE( hit.iVertex1 < f.boundary.vertices.nCount );
    REQUIRE( hit.iVertex2 < f.boundary.vertices.nCount );
    REQUIRE( hit.barycentric.x + hit.barycentric.y + hit.barycentric.z ==
             Approx( 1.0 ) );
}

TEST_CASE( "RaycastQueries: brush fan handles an octagonal cap",
           "[RaycastQueries][Queries]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAllocator{};
    brush_solid_t brush{};
    brush_boundary_t boundary{};

    REQUIRE( BrushGenerator_TryMakeCylinder(
                 &brush, &allocator, policy, &idAllocator,
                 math::CY_VEC3D_ZERO, 2.0, 1.0, 8u, 2u ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &boundary, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &boundary, &brush, policy ) == geometry_status_t::OK );

    brush_raycast_hit_t hit{};
    REQUIRE( BrushQueries_TryRaycast(
                 &brush, &boundary, DownwardRay(),
                 DefaultOptions(), policy, &hit ) ==
             geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.iSide == 8u );
    REQUIRE( boundary.faces.pData[hit.iFace].cVertices == 8u );
    REQUIRE( hit.fDistance == Approx( 4.0 ) );

    BrushBoundary_Shutdown( &boundary );
    BrushSolid_Shutdown( &brush );
}

TEST_CASE( "RaycastQueries: no hit is OK and leaves a neutral record",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    brush_raycast_hit_t hit{};
    hit.bHit = true;
    hit.fDistance = 123.0;

    const geometry_raycast_ray_t miss{
        math::Vec3d_Make( 5.0, 5.0, 5.0 ),
        math::Vec3d_Make( 0.0, 0.0, -1.0 )
    };
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, miss,
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE( hit.fDistance == 0.0 );
    REQUIRE( hit.iFace == CY_INVALID_INDEX );
    REQUIRE_FALSE( GeometrySourceId_IsValid( hit.brushSourceId ) );
}

TEST_CASE( "RaycastQueries: hits at the ray origin are excluded",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    const geometry_raycast_ray_t ray{
        math::Vec3d_Make( 0.0, 0.0, 1.0 ),
        math::Vec3d_Make( 0.0, 0.0, 1.0 )
    };
    brush_raycast_hit_t hit{};
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, ray,
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
}

TEST_CASE( "RaycastQueries: brush validates rays ranges and cull modes",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    brush_raycast_hit_t hit{};
    hit.bHit = true;

    geometry_raycast_ray_t ray = DownwardRay();
    ray.direction.z = -2.0;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, ray,
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( hit.bHit );

    geometry_raycast_options_t options = DefaultOptions();
    options.fMinimumDistance = 2.0;
    options.fMaximumDistance = 1.0;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 options, f.policy, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );

    options = DefaultOptions();
    options.cullMode = geometry_raycast_cull_mode_t::COUNT;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 options, f.policy, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );

    ray = DownwardRay();
    ray.origin.x = f.policy.numerical.fCoordinateMagnitudeLimit * 2.0;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, ray,
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );

    geometry_policy_t invalidPolicy = f.policy;
    invalidPolicy.numerical.fAbsoluteDistanceTolerance = -1.0;
    hit.bHit = true;
    hit.fDistance = 123.0;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 DefaultOptions(), invalidPolicy, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE( hit.fDistance == 0.0 );
    REQUIRE( hit.iFace == CY_INVALID_INDEX );
}

TEST_CASE( "RaycastQueries: face culling follows outward winding",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    geometry_raycast_options_t options = DefaultOptions();
    options.fMaximumDistance = 4.5;
    options.cullMode = geometry_raycast_cull_mode_t::BACK_FACE;

    brush_raycast_hit_t hit{};
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 options, f.policy, &hit ) ==
             geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.iSide == 4u );

    options.cullMode = geometry_raycast_cull_mode_t::FRONT_FACE;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 options, f.policy, &hit ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
}

TEST_CASE( "RaycastQueries: malformed brush boundary is rejected neutrally",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    f.boundary.faceVertexIndices.pData[0] = CY_U32_MAX;
    brush_raycast_hit_t hit{};
    hit.bHit = true;

    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 DefaultOptions(), f.policy, &hit ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE( hit.iFace == CY_INVALID_INDEX );
}

TEST_CASE( "RaycastQueries: boundary must correspond to the supplied brush",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    brush_solid_t otherBrush{};
    brush_boundary_t otherBoundary{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &otherBrush, &f.allocator, f.policy, &f.idAllocator,
                 math::Vec3d_Make( 0.0, 0.0, 2.0 ),
                 math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_Init( &otherBoundary, &f.allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct(
                 &otherBoundary, &otherBrush, f.policy ) ==
             geometry_status_t::OK );
    REQUIRE( otherBrush.sides.nCount == f.brush.sides.nCount );
    REQUIRE( otherBoundary.faces.nCount == f.boundary.faces.nCount );

    brush_raycast_hit_t otherHit{};
    REQUIRE( BrushQueries_TryRaycast(
                 &otherBrush, &otherBoundary, DownwardRay(),
                 DefaultOptions(), f.policy, &otherHit ) ==
             geometry_status_t::OK );
    REQUIRE( otherHit.bHit );
    REQUIRE( otherHit.fDistance == Approx( 2.0 ) );

    brush_raycast_hit_t mismatchedHit{};
    mismatchedHit.bHit = true;
    mismatchedHit.fDistance = 23.0;
    mismatchedHit.brushSourceId = f.brush.sourceId;
    mismatchedHit.iFace = 0u;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &otherBoundary, DownwardRay(),
                 DefaultOptions(), f.policy, &mismatchedHit ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( mismatchedHit.bHit );
    REQUIRE( mismatchedHit.fDistance == 0.0 );
    REQUIRE_FALSE( GeometrySourceId_IsValid(
        mismatchedHit.brushSourceId ) );
    REQUIRE( mismatchedHit.iFace == CY_INVALID_INDEX );

    BrushBoundary_Shutdown( &otherBoundary );
    BrushSolid_Shutdown( &otherBrush );
}

TEST_CASE( "RaycastQueries: brush boundary allocator ownership is validated",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    common::allocator_t alternateAllocator{
        *common::Allocator_GetSystem() };
    const common::allocator_t *pOriginal =
        f.boundary.edges.pAllocator;
    f.boundary.edges.pAllocator = &alternateAllocator;

    brush_raycast_hit_t hit{};
    hit.bHit = true;
    const geometry_status_t status = BrushQueries_TryRaycast(
        &f.brush, &f.boundary, DownwardRay(),
        DefaultOptions(), f.policy, &hit );
    f.boundary.edges.pAllocator = pOriginal;

    REQUIRE( status == geometry_status_t::CORRUPT_STATE );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE( hit.iFace == CY_INVALID_INDEX );
}

TEST_CASE( "RaycastQueries: mesh hit retains source face and corner handles",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    REQUIRE( cbScratch > 0u );

    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    const geometry_source_id_t meshId{ 9001u };
    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, meshId, DownwardRay(), DefaultOptions(),
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.fDistance == Approx( 4.0 ) );
    REQUIRE( hit.meshSourceId.value == meshId.value );
    REQUIRE( GeometryHandle_IsValid( hit.hShell ) );
    REQUIRE( GeometryHandle_IsValid( hit.hFace ) );
    REQUIRE( GeometryHandle_IsValid( hit.hLoop ) );
    REQUIRE( GeometryHandle_IsValid( hit.hVertex0 ) );
    REQUIRE( GeometryHandle_IsValid( hit.hVertex1 ) );
    REQUIRE( GeometryHandle_IsValid( hit.hVertex2 ) );
    REQUIRE( hit.iSourceSide == 4u );
    REQUIRE( hit.normal.z == Approx( 1.0 ) );
    RequireScratchEmpty( scratch );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: mesh scratch failure is explicit and rewound",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    common::usize cbRequired = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbRequired ) ==
             geometry_status_t::OK );
    REQUIRE( cbRequired > 1u );

    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbRequired - 1u, &f.allocator ) ==
             geometry_status_t::OK );

    mesh_raycast_hit_t hit{};
    hit.bHit = true;
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 17u }, DownwardRay(), DefaultOptions(),
                 f.policy, &scratch, &hit ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE_FALSE( GeometryHandle_IsValid( hit.hFace ) );
    RequireScratchEmpty( scratch );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: mesh requires initialized scratch and source identity",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    mesh_raycast_hit_t hit{};
    hit.bHit = true;

    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 8u }, DownwardRay(), DefaultOptions(),
                 f.policy, nullptr, &hit ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE_FALSE( hit.bHit );

    geometry_scratch_t releasedScratch{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 8u }, DownwardRay(), DefaultOptions(),
                 f.policy, &releasedScratch, &hit ) ==
             geometry_status_t::NOT_INITIALIZED );

    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, {}, DownwardRay(), DefaultOptions(),
                 f.policy, &releasedScratch, &hit ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "RaycastQueries: policy bounds raycast work before scratch",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;

    geometry_policy_t edgePolicy = f.policy;
    edgePolicy.limits.cEdgesMax = 1u;
    REQUIRE( GeometryPolicy_IsValid( edgePolicy ) );
    brush_raycast_hit_t edgeLimitedHit{};
    edgeLimitedHit.bHit = true;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 DefaultOptions(), edgePolicy, &edgeLimitedHit ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE_FALSE( edgeLimitedHit.bHit );

    geometry_policy_t brushPolicy = f.policy;
    brushPolicy.limits.cIntersectionEventsMax = 1u;
    REQUIRE( GeometryPolicy_IsValid( brushPolicy ) );
    brush_raycast_hit_t brushHit{};
    brushHit.bHit = true;
    REQUIRE( BrushQueries_TryRaycast(
                 &f.brush, &f.boundary, DownwardRay(),
                 DefaultOptions(), brushPolicy, &brushHit ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE_FALSE( brushHit.bHit );

    f.policy.limits.cTraversalDepthMax = 3u;
    REQUIRE( GeometryPolicy_IsValid( f.policy ) );

    common::usize cbScratch = 19u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( cbScratch == 0u );

    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 77u }, DownwardRay(), DefaultOptions(),
                 f.policy, nullptr, &hit ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE_FALSE( hit.bHit );
}

TEST_CASE( "RaycastQueries: mesh query triangulates faces after edge split",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    const geometry_mesh_edge_handle_t hEdge = FirstEdge( &f.mesh );
    REQUIRE( GeometryHandle_IsValid( hEdge ) );
    REQUIRE( MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 ).status ==
             geometry_status_t::OK );

    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 44u }, DownwardRay(), DefaultOptions(),
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.fDistance == Approx( 4.0 ) );
    RequireScratchEmpty( scratch );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: mesh ear clipping respects a concave face",
           "[RaycastQueries][Queries]" )
{
    ConcavePrismFixture f;
    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    REQUIRE( cbScratch > 0u );

    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    const geometry_raycast_ray_t insideRay{
        math::Vec3d_Make( 0.5, 1.5, 5.0 ),
        math::Vec3d_Make( 0.0, 0.0, -1.0 )
    };
    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 81u }, insideRay, DefaultOptions(),
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.fDistance == Approx( 4.0 ) );
    REQUIRE( hit.position.x == Approx( 0.5 ) );
    REQUIRE( hit.position.y == Approx( 1.5 ) );
    REQUIRE( hit.position.z == Approx( 1.0 ) );
    REQUIRE( hit.iSourceSide == 1u );
    RequireScratchEmpty( scratch );

    // This ray crosses the missing square in the L footprint. A triangulator
    // that fills the polygon's concavity would report a false cap hit here.
    const geometry_raycast_ray_t notchRay{
        math::Vec3d_Make( 1.5, 1.5, 5.0 ),
        math::Vec3d_Make( 0.0, 0.0, -1.0 )
    };
    hit.bHit = true;
    hit.fDistance = 71.0;
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 81u }, notchRay, DefaultOptions(),
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
    REQUIRE( hit.fDistance == 0.0 );
    REQUIRE_FALSE( GeometryHandle_IsValid( hit.hFace ) );
    RequireScratchEmpty( scratch );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: mesh culling follows polygon winding",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    geometry_raycast_options_t options = DefaultOptions();
    options.fMaximumDistance = 4.5;
    options.cullMode = geometry_raycast_cull_mode_t::BACK_FACE;
    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 92u }, DownwardRay(), options,
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.iSourceSide == 4u );
    RequireScratchEmpty( scratch );

    options.cullMode = geometry_raycast_cull_mode_t::FRONT_FACE;
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 92u }, DownwardRay(), options,
                 f.policy, &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
    RequireScratchEmpty( scratch );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: mesh tie result is deterministic",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    mesh_raycast_hit_t a{};
    mesh_raycast_hit_t b{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 55u }, DownwardRay(), DefaultOptions(),
                 f.policy, &scratch, &a ) == geometry_status_t::OK );
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 55u }, DownwardRay(), DefaultOptions(),
                 f.policy, &scratch, &b ) == geometry_status_t::OK );
    REQUIRE( a.bHit );
    REQUIRE( b.bHit );
    REQUIRE( a.hFace.nSlot == b.hFace.nSlot );
    REQUIRE( a.hFace.nGeneration == b.hFace.nGeneration );
    REQUIRE( a.iTriangleInFace == b.iTriangleInFace );
    REQUIRE( a.hVertex0.nSlot == b.hVertex0.nSlot );
    REQUIRE( a.hVertex1.nSlot == b.hVertex1.nSlot );
    REQUIRE( a.hVertex2.nSlot == b.hVertex2.nSlot );
    RequireScratchEmpty( scratch );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "RaycastQueries: non-finite mesh geometry fails before scratch use",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;
    bool changed = false;
    (void)common::GenerationPool_ForEach(
        &f.mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             mesh_vertex_record_t &vertex ) noexcept -> common::bool_t {
            vertex.position.x = std::numeric_limits<common::f64>::quiet_NaN();
            changed = true;
            return false;
        } );
    REQUIRE( changed );

    mesh_raycast_hit_t hit{};
    hit.bHit = true;
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 91u }, DownwardRay(), DefaultOptions(),
                 f.policy, nullptr, &hit ) ==
             geometry_status_t::NUMERIC_FAILURE );
    REQUIRE_FALSE( hit.bHit );
}

TEST_CASE( "RaycastQueries: canonical empty mesh is an allocation-free miss",
           "[RaycastQueries][Queries]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    editable_mesh_t mesh{};
    REQUIRE( EditableMesh_Init( &mesh, &allocator ) == geometry_status_t::OK );

    common::usize cbScratch = 99u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &mesh, policy, &cbScratch ) == geometry_status_t::OK );
    REQUIRE( cbScratch == 0u );

    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &mesh, { 1u }, DownwardRay(), DefaultOptions(),
                 policy, nullptr, &hit ) == geometry_status_t::OK );
    REQUIRE_FALSE( hit.bHit );
    EditableMesh_Shutdown( &mesh );
}

TEST_CASE( "RaycastQueries: mesh faces above the legacy corner cap are tested",
           "[RaycastQueries][Queries]" )
{
    RaycastFixture f;

    geometry_mesh_loop_handle_t hTopLoop{};
    (void)common::GenerationPool_ForEach(
        &f.mesh.faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> common::bool_t {
            if ( face.normal.z > 0.9 ) {
                hTopLoop = face.hOuterLoop;
                return false;
            }
            return true;
        } );
    REQUIRE( GeometryHandle_IsValid( hTopLoop ) );

    std::array<geometry_mesh_edge_handle_t, 4u> initialEdges{};
    common::usize cInitialEdges = 0u;
    bool bEdgeRecordsValid = true;
    (void)common::GenerationPool_ForEach(
        &f.mesh.edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> common::bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                EditableMesh_GetHalfEdge( &f.mesh, edge.hHalfEdge );
            const mesh_half_edge_record_t *pTwin =
                pHalfEdge != nullptr
                    ? EditableMesh_GetHalfEdge(
                          &f.mesh, pHalfEdge->hTwin )
                    : nullptr;
            const mesh_vertex_record_t *pA =
                pHalfEdge != nullptr
                    ? EditableMesh_GetVertex(
                          &f.mesh, pHalfEdge->hOrigin )
                    : nullptr;
            const mesh_vertex_record_t *pB =
                pTwin != nullptr
                    ? EditableMesh_GetVertex(
                          &f.mesh, pTwin->hOrigin )
                    : nullptr;
            if ( pHalfEdge == nullptr || pTwin == nullptr ||
                 pA == nullptr || pB == nullptr ) {
                bEdgeRecordsValid = false;
                return false;
            }
            if ( pA->position.z == 1.0 && pB->position.z == 1.0 ) {
                if ( cInitialEdges >= initialEdges.size() ) {
                    bEdgeRecordsValid = false;
                    return false;
                }
                initialEdges[cInitialEdges] = hEdge;
                ++cInitialEdges;
            }
            return true;
        } );
    REQUIRE( bEdgeRecordsValid );
    REQUIRE( cInitialEdges == initialEdges.size() );
    std::vector<geometry_mesh_edge_handle_t> splitQueue(
        initialEdges.begin(), initialEdges.end() );

    constexpr common::u32 cTargetCorners = 300u;
    common::usize iSplit = 0u;
    for ( ;; ) {
        const mesh_loop_record_t *pTopLoop =
            EditableMesh_GetLoop( &f.mesh, hTopLoop );
        REQUIRE( pTopLoop != nullptr );
        if ( pTopLoop->cHalfEdges >= cTargetCorners ) {
            REQUIRE( pTopLoop->cHalfEdges == cTargetCorners );
            break;
        }
        REQUIRE( iSplit < splitQueue.size() );
        const geometry_mesh_edge_handle_t hEdge = splitQueue[iSplit];
        ++iSplit;
        const mesh_split_edge_result_t split =
            MeshOps_SplitEdge( &f.mesh, hEdge, 0.5 );
        REQUIRE( split.status == geometry_status_t::OK );
        splitQueue.push_back( hEdge );
        splitQueue.push_back( split.hNewEdge );
    }

    common::usize cbScratch = 0u;
    REQUIRE( MeshQueries_TryGetRaycastScratchSize(
                 &f.mesh, f.policy, &cbScratch ) ==
             geometry_status_t::OK );
    REQUIRE( cbScratch > 0u );
    geometry_scratch_t scratch{};
    std::vector<common::byte> storage;
    REQUIRE( AcquireScratch(
                 &scratch, &storage, cbScratch, &f.allocator ) ==
             geometry_status_t::OK );

    const geometry_raycast_ray_t ray{
        math::Vec3d_Make( 0.0, 0.0, 3.0 ),
        math::Vec3d_Make( 0.0, 0.0, -1.0 )
    };
    mesh_raycast_hit_t hit{};
    REQUIRE( MeshQueries_TryRaycast(
                 &f.mesh, { 77u }, ray, DefaultOptions(), f.policy,
                 &scratch, &hit ) == geometry_status_t::OK );
    REQUIRE( hit.bHit );
    REQUIRE( hit.fDistance == Approx( 2.0 ) );
    REQUIRE( hit.position.z == Approx( 1.0 ) );
    RequireScratchEmpty( scratch );

    REQUIRE( GeometryScratch_Release( &scratch ) ==
             geometry_status_t::OK );
}

} // namespace cypher::editor::geometry
