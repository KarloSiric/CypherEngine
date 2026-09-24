//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvIslands_Tests.cpp
//  Purpose: Verifies atomic seam editing and deterministic UV-island
//           discovery.
//  Details: Covers source-ID addressing, seam/no-op/toggle semantics,
//           orientation-aware corner matching, independent UV channels,
//           disconnected shells, canonical ordering, bad inputs, and every
//           observed allocator-failure point.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshUvIslands.h"

#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

namespace
{

geometry_source_id_t Id( common::u64 value ) noexcept
{
    return geometry_source_id_t{ value };
}

struct failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_USIZE_MAX };
    common::usize cRejectedAllocations{ 0u };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
};

void *FailureAllocate( void *pUserData, common::usize cbSize,
                       common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cAllocationCalls++;
    if ( iCall == pState->iFailOnCall ) {
        ++pState->cRejectedAllocations;
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) { ++pState->cSuccessfulAllocations; }
    return pMemory;
}

void FailureFree( void *pUserData, void *pMemory, common::usize cbSize,
                  common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) { ++pState->cFrees; }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailureAllocator(
    failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        &FailureAllocate, nullptr, &FailureFree, pState };
}

struct source_fixture_t {
    common::allocator_t allocator{};
    mesh_source_description_t description{};
    mesh_source_t source{};

    explicit source_fixture_t(
        const common::allocator_t &allocatorValue =
            *common::Allocator_GetSystem() )
        : allocator( allocatorValue )
    {
        REQUIRE( MeshSourceDescription_Init(
                     &description, &allocator, Id( 1u ) ) ==
                 geometry_status_t::OK );
    }

    ~source_fixture_t()
    {
        MeshSource_Shutdown( &source );
        MeshSourceDescription_Shutdown( &description );
    }

    void AddCube( common::u64 vertexBase,
                  const std::array<common::u64, 6u> &faceIds,
                  math::vec3d_t offset = {} )
    {
        const common::u32 firstVertex =
            static_cast<common::u32>( description.vertices.nCount );
        for ( common::u32 i = 0u; i < 8u; ++i ) {
            const math::vec3d_t position{
                offset.x + ( ( i & 1u ) != 0u ? 1.0 : 0.0 ),
                offset.y + ( ( i & 2u ) != 0u ? 1.0 : 0.0 ),
                offset.z + ( ( i & 4u ) != 0u ? 1.0 : 0.0 ) };
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &description, position, Id( vertexBase + i ),
                         nullptr ) == geometry_status_t::OK );
        }
        constexpr common::u32 localFaces[6u][4u] = {
            { 0u, 2u, 3u, 1u }, { 4u, 5u, 7u, 6u },
            { 0u, 1u, 5u, 4u }, { 2u, 6u, 7u, 3u },
            { 0u, 4u, 6u, 2u }, { 1u, 3u, 7u, 5u }
        };
        for ( common::u32 iFace = 0u; iFace < 6u; ++iFace ) {
            common::u32 indices[4u]{};
            for ( common::u32 i = 0u; i < 4u; ++i ) {
                indices[i] = firstVertex + localFaces[iFace][i];
            }
            REQUIRE( MeshSourceDescription_TryAddFace(
                         &description,
                         common::span_t<const common::u32>{ indices, 4u },
                         Id( faceIds[iFace] ), mesh_face_attributes_t{},
                         nullptr ) == geometry_status_t::OK );
        }
    }

    void AddDefaultCube( common::u64 vertexBase = 10u,
                         common::u64 faceBase = 100u )
    {
        AddCube( vertexBase,
                 { faceBase, faceBase + 1u, faceBase + 2u,
                   faceBase + 3u, faceBase + 4u,
                   faceBase + 5u } );
    }

    void AddOpenStrip()
    {
        constexpr math::vec3d_t positions[6u] = {
            { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 },
            { 2.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 },
            { 1.0, 1.0, 0.0 }, { 2.0, 1.0, 0.0 }
        };
        for ( common::u32 i = 0u; i < 6u; ++i ) {
            REQUIRE( MeshSourceDescription_TryAddVertex(
                         &description, positions[i], Id( 10u + i ),
                         nullptr ) == geometry_status_t::OK );
        }
        constexpr common::u32 left[4u] = { 0u, 1u, 4u, 3u };
        constexpr common::u32 right[4u] = { 1u, 2u, 5u, 4u };
        REQUIRE( MeshSourceDescription_TryAddFace(
                     &description, { left, 4u }, Id( 100u ),
                     mesh_face_attributes_t{}, nullptr ) ==
                 geometry_status_t::OK );
        REQUIRE( MeshSourceDescription_TryAddFace(
                     &description, { right, 4u }, Id( 101u ),
                     mesh_face_attributes_t{}, nullptr ) ==
                 geometry_status_t::OK );
    }

    void Build()
    {
        REQUIRE( MeshSource_TryBuild(
                     &description, &allocator, &source ) ==
                 geometry_status_t::OK );
    }
};

struct scoped_islands_t {
    mesh_uv_island_result_t value{};

    explicit scoped_islands_t( const common::allocator_t *pAllocator )
    {
        REQUIRE( MeshUvIslandResult_Init( &value, pAllocator ) ==
                 geometry_status_t::OK );
    }

    ~scoped_islands_t()
    {
        MeshUvIslandResult_Shutdown( &value );
    }
};

mesh_source_edge_key_t Edge( common::u64 a, common::u64 b ) noexcept
{
    return mesh_source_edge_key_t{ Id( a ), Id( b ) };
}

std::array<mesh_source_edge_key_t, 4u> TopEdges(
    common::u64 vertexBase = 10u ) noexcept
{
    return { Edge( vertexBase + 4u, vertexBase + 5u ),
             Edge( vertexBase + 5u, vertexBase + 7u ),
             Edge( vertexBase + 7u, vertexBase + 6u ),
             Edge( vertexBase + 6u, vertexBase + 4u ) };
}

void CheckIsland( const mesh_uv_island_result_t &result,
                  common::u32 iIsland,
                  std::initializer_list<common::u64> expected )
{
    REQUIRE( iIsland < result.islands.nCount );
    const mesh_uv_island_range_t &range = result.islands.pData[iIsland];
    REQUIRE( range.cFaces == expected.size() );
    common::u32 i = 0u;
    for ( const common::u64 id : expected ) {
        CHECK( result.faceIds.pData[range.iFirstFace + i].value == id );
        ++i;
    }
}

void MakeTopUv0Discontinuous( source_fixture_t *pFixture )
{
    constexpr common::u64 topVertices[4u] = { 14u, 15u, 17u, 16u };
    for ( common::u32 i = 0u; i < 4u; ++i ) {
        mesh_corner_attributes_t corner{};
        corner.uv0 = math::vec2d_t{
            10.0 + static_cast<common::f64>( i ),
            20.0 + static_cast<common::f64>( i ) };
        REQUIRE( MeshSourceEdit_TrySetCornerAttributes(
                     &pFixture->source, Id( 101u ), Id( topVertices[i] ),
                     corner ) == geometry_status_t::OK );
    }
}

void SetCubeUv0ByVertex( source_fixture_t *pFixture )
{
    constexpr common::u64 faceVertices[6u][4u] = {
        { 10u, 12u, 13u, 11u }, { 14u, 15u, 17u, 16u },
        { 10u, 11u, 15u, 14u }, { 12u, 16u, 17u, 13u },
        { 10u, 14u, 16u, 12u }, { 11u, 13u, 17u, 15u }
    };
    for ( common::u32 iFace = 0u; iFace < 6u; ++iFace ) {
        for ( common::u32 iCorner = 0u; iCorner < 4u; ++iCorner ) {
            const common::u64 vertexId =
                faceVertices[iFace][iCorner];
            mesh_corner_attributes_t corner{};
            corner.uv0 = math::vec2d_t{
                static_cast<common::f64>( vertexId ),
                0.5 * static_cast<common::f64>( vertexId ) };
            REQUIRE( MeshSourceEdit_TrySetCornerAttributes(
                         &pFixture->source, Id( 100u + iFace ),
                         Id( vertexId ), corner ) ==
                     geometry_status_t::OK );
        }
    }
}

mesh_corner_attributes_t *FindCornerAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    geometry_source_id_t vertexId ) noexcept
{
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_vertex_handle_t hVertex{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &hFace ) ||
         !MeshSource_TryFindVertex( pSource, vertexId, &hVertex ) ) {
        return nullptr;
    }
    const mesh_face_record_t *pFace =
        common::GenerationPool_Get( &pSource->mesh.faces, hFace );
    const mesh_loop_record_t *pLoop = pFace
        ? common::GenerationPool_Get(
              &pSource->mesh.loops, pFace->hOuterLoop )
        : nullptr;
    if ( pLoop == nullptr ) { return nullptr; }

    geometry_mesh_half_edge_handle_t hCorner = pLoop->hFirstHalfEdge;
    for ( common::u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pHalf =
            common::GenerationPool_Get(
                &pSource->mesh.halfEdges, hCorner );
        if ( pHalf == nullptr ) { return nullptr; }
        if ( pHalf->hOrigin.nSlot == hVertex.nSlot &&
             pHalf->hOrigin.nGeneration == hVertex.nGeneration ) {
            if ( hCorner.nSlot >= pSource->attributes.corners.nCount ) {
                return nullptr;
            }
            auto &slot = pSource->attributes.corners.pData[hCorner.nSlot];
            return slot.nGeneration == hCorner.nGeneration
                ? &slot.value
                : nullptr;
        }
        hCorner = pHalf->hNext;
    }
    return nullptr;
}

} // namespace

TEST_CASE( "Mesh UV seams edit the complete source-ID set atomically",
           "[Geometry][Mesh][UV][Seams]" )
{
    source_fixture_t fixture{};
    fixture.AddDefaultCube();
    fixture.Build();
    const auto topEdges = TopEdges();

    mesh_edge_attributes_t firstAttributes{};
    firstAttributes.flags = MESH_EDGE_FLAG_HARD;
    REQUIRE( MeshSourceEdit_TrySetEdgeAttributes(
                 &fixture.source, topEdges[0].vertexA,
                 topEdges[0].vertexB, firstAttributes, 0.75 ) ==
             geometry_status_t::OK );

    common::u32 cChanged = 99u;
    CHECK( MeshUv_TryEditSeams(
               &fixture.source,
               common::span_t<const mesh_source_edge_key_t>{
                   topEdges.data(), topEdges.size() },
               mesh_uv_seam_edit_t::CLEAR, &cChanged ) ==
           geometry_status_t::OK );
    CHECK( cChanged == 0u );

    REQUIRE( MeshUv_TryEditSeams(
                 &fixture.source,
                 common::span_t<const mesh_source_edge_key_t>{
                     topEdges.data(), topEdges.size() },
                 mesh_uv_seam_edit_t::SET, &cChanged ) ==
             geometry_status_t::OK );
    CHECK( cChanged == 4u );
    CHECK( MeshUv_TryEditSeams(
               &fixture.source,
               common::span_t<const mesh_source_edge_key_t>{
                   topEdges.data(), topEdges.size() },
               mesh_uv_seam_edit_t::SET, &cChanged ) ==
           geometry_status_t::OK );
    CHECK( cChanged == 0u );

    geometry_mesh_edge_handle_t hFirst{};
    REQUIRE( MeshSourceEdit_TryFindEdge(
        &fixture.source, topEdges[0].vertexA, topEdges[0].vertexB,
        &hFirst ) );
    const mesh_edge_attributes_t afterSet =
        MeshAttributeStore_GetEdge( &fixture.source.attributes, hFirst );
    CHECK( ( afterSet.flags & MESH_EDGE_FLAG_HARD ) != 0u );
    CHECK( ( afterSet.flags & MESH_EDGE_FLAG_SEAM ) != 0u );
    REQUIRE( common::GenerationPool_Get(
                 &fixture.source.mesh.edges, hFirst ) != nullptr );
    CHECK( common::GenerationPool_Get(
               &fixture.source.mesh.edges, hFirst )->creaseWeight == 0.75 );

    REQUIRE( MeshUv_TryEditSeams(
                 &fixture.source,
                 common::span_t<const mesh_source_edge_key_t>{
                     topEdges.data(), topEdges.size() },
                 mesh_uv_seam_edit_t::TOGGLE, &cChanged ) ==
             geometry_status_t::OK );
    CHECK( cChanged == 4u );
    CHECK( ( MeshAttributeStore_GetEdge(
                 &fixture.source.attributes, hFirst ).flags &
             MESH_EDGE_FLAG_SEAM ) == 0u );

    const mesh_source_edge_key_t invalidSet[2u] = {
        topEdges[0], Edge( 14u, 999999u )
    };
    CHECK( MeshUv_TryEditSeams(
               &fixture.source,
               common::span_t<const mesh_source_edge_key_t>{
                   invalidSet, 2u },
               mesh_uv_seam_edit_t::SET, &cChanged ) ==
           geometry_status_t::INVALID_HANDLE );
    CHECK( cChanged == 0u );
    CHECK( ( MeshAttributeStore_GetEdge(
                 &fixture.source.attributes, hFirst ).flags &
             MESH_EDGE_FLAG_SEAM ) == 0u );

    const mesh_source_edge_key_t duplicates[2u] = {
        topEdges[0],
        mesh_source_edge_key_t{
            topEdges[0].vertexB, topEdges[0].vertexA }
    };
    CHECK( MeshUv_TryEditSeams(
               &fixture.source,
               common::span_t<const mesh_source_edge_key_t>{
                   duplicates, 2u },
               mesh_uv_seam_edit_t::TOGGLE, &cChanged ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( cChanged == 0u );
}

TEST_CASE( "Mesh UV islands split a cube at authored seam edges",
           "[Geometry][Mesh][UV][Islands]" )
{
    source_fixture_t fixture{};
    fixture.AddDefaultCube();
    fixture.Build();
    const auto topEdges = TopEdges();
    REQUIRE( MeshUv_TryEditSeams(
                 &fixture.source,
                 common::span_t<const mesh_source_edge_key_t>{
                     topEdges.data(), topEdges.size() },
                 mesh_uv_seam_edit_t::SET ) == geometry_status_t::OK );

    scoped_islands_t result{ common::Allocator_GetSystem() };
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 2u );
    REQUIRE( result.value.faceIds.nCount == 6u );
    CheckIsland( result.value, 0u, { 100u, 102u, 103u, 104u, 105u } );
    CheckIsland( result.value, 1u, { 101u } );
}

TEST_CASE( "Mesh UV island continuity uses oriented endpoints and the selected channel",
           "[Geometry][Mesh][UV][Islands]" )
{
    source_fixture_t fixture{};
    fixture.AddDefaultCube();
    fixture.Build();
    SetCubeUv0ByVertex( &fixture );

    scoped_islands_t result{ common::Allocator_GetSystem() };
    // The two half-edges have opposite directions. Matching UVs by source
    // vertex (h with twin.next, h.next with twin) keeps the cube connected;
    // comparing half-edge origins directly would incorrectly split it.
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 1u );
    CheckIsland( result.value, 0u,
                 { 100u, 101u, 102u, 103u, 104u, 105u } );

    MakeTopUv0Discontinuous( &fixture );

    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 2u );
    CheckIsland( result.value, 0u, { 100u, 102u, 103u, 104u, 105u } );
    CheckIsland( result.value, 1u, { 101u } );

    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::LIGHTMAP, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 1u );
    CheckIsland( result.value, 0u,
                 { 100u, 101u, 102u, 103u, 104u, 105u } );

    CHECK( MeshUv_TryDiscoverIslands(
               &fixture.source, mesh_uv_set_t::MATERIAL, -1.0,
               &result.value ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshUv_TryDiscoverIslands(
               &fixture.source, mesh_uv_set_t::MATERIAL,
               std::numeric_limits<common::f64>::infinity(),
               &result.value ) == geometry_status_t::INVALID_ARGUMENT );
    // The failed calls leave the previous lightmap result intact.
    REQUIRE( result.value.islands.nCount == 1u );
    CheckIsland( result.value, 0u,
                 { 100u, 101u, 102u, 103u, 104u, 105u } );
}

TEST_CASE( "Mesh UV islands order disconnected shells by face source identity",
           "[Geometry][Mesh][UV][Islands][Determinism]" )
{
    source_fixture_t fixture{};
    fixture.AddCube( 1000u, { 90u, 50u, 70u, 110u, 130u, 150u } );
    fixture.AddCube( 2000u, { 20u, 40u, 60u, 80u, 100u, 120u },
                     math::vec3d_t{ 4.0, 0.0, 0.0 } );
    fixture.Build();

    scoped_islands_t result{ common::Allocator_GetSystem() };
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 2u );
    CheckIsland( result.value, 0u,
                 { 20u, 40u, 60u, 80u, 100u, 120u } );
    CheckIsland( result.value, 1u,
                 { 50u, 70u, 90u, 110u, 130u, 150u } );
}

TEST_CASE( "Mesh UV islands handle open boundaries without phantom adjacency",
           "[Geometry][Mesh][UV][Islands][Boundary]" )
{
    source_fixture_t fixture{};
    fixture.AddOpenStrip();
    fixture.Build();

    scoped_islands_t result{ common::Allocator_GetSystem() };
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 1u );
    CheckIsland( result.value, 0u, { 100u, 101u } );

    const mesh_source_edge_key_t shared = Edge( 11u, 14u );
    REQUIRE( MeshUv_TryEditSeams(
                 &fixture.source, { &shared, 1u },
                 mesh_uv_seam_edit_t::SET ) == geometry_status_t::OK );
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 2u );
    CheckIsland( result.value, 0u, { 100u } );
    CheckIsland( result.value, 1u, { 101u } );
}

TEST_CASE( "Mesh UV island tolerance is inclusive at the exact boundary",
           "[Geometry][Mesh][UV][Islands][Tolerance]" )
{
    source_fixture_t fixture{};
    fixture.AddOpenStrip();
    fixture.Build();

    constexpr common::f64 tolerance = 0.25;
    mesh_corner_attributes_t corner{};
    corner.uv0.x = tolerance;
    REQUIRE( MeshSourceEdit_TrySetCornerAttributes(
                 &fixture.source, Id( 101u ), Id( 11u ), corner ) ==
             geometry_status_t::OK );

    scoped_islands_t result{ common::Allocator_GetSystem() };
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, tolerance,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 1u );
    CheckIsland( result.value, 0u, { 100u, 101u } );

    corner.uv0.x = std::nextafter(
        tolerance, std::numeric_limits<common::f64>::infinity() );
    REQUIRE( MeshSourceEdit_TrySetCornerAttributes(
                 &fixture.source, Id( 101u ), Id( 11u ), corner ) ==
             geometry_status_t::OK );
    REQUIRE( MeshUv_TryDiscoverIslands(
                 &fixture.source, mesh_uv_set_t::MATERIAL, tolerance,
                 &result.value ) == geometry_status_t::OK );
    REQUIRE( result.value.islands.nCount == 2u );
    CheckIsland( result.value, 0u, { 100u } );
    CheckIsland( result.value, 1u, { 101u } );
}

TEST_CASE( "Mesh UV island numeric failure preserves the previous output",
           "[Geometry][Mesh][UV][Islands][NumericFailure]" )
{
    source_fixture_t fixture{};
    fixture.AddOpenStrip();
    fixture.Build();
    mesh_corner_attributes_t *pCorrupt = FindCornerAttributes(
        &fixture.source, Id( 100u ), Id( 11u ) );
    REQUIRE( pCorrupt != nullptr );
    pCorrupt->uv0.x =
        std::numeric_limits<common::f64>::quiet_NaN();

    scoped_islands_t result{ common::Allocator_GetSystem() };
    REQUIRE( common::Vector_PushBack(
        &result.value.faceIds, Id( 999999u ) ) );
    REQUIRE( common::Vector_PushBack(
        &result.value.islands,
        mesh_uv_island_range_t{ 0u, 1u } ) );
    geometry_source_id_t *pFaceStorage = result.value.faceIds.pData;
    mesh_uv_island_range_t *pIslandStorage =
        result.value.islands.pData;
    const common::usize cFaceCapacity =
        result.value.faceIds.nCapacity;
    const common::usize cIslandCapacity =
        result.value.islands.nCapacity;

    CHECK( MeshUv_TryDiscoverIslands(
               &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
               &result.value ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( result.value.faceIds.pData == pFaceStorage );
    CHECK( result.value.faceIds.nCount == 1u );
    CHECK( result.value.faceIds.nCapacity == cFaceCapacity );
    CHECK( result.value.faceIds.pData[0u].value == 999999u );
    CHECK( result.value.islands.pData == pIslandStorage );
    CHECK( result.value.islands.nCount == 1u );
    CHECK( result.value.islands.nCapacity == cIslandCapacity );
    CHECK( result.value.islands.pData[0u].iFirstFace == 0u );
    CHECK( result.value.islands.pData[0u].cFaces == 1u );
}

TEST_CASE( "Mesh UV island discovery is output-atomic at every allocation failure",
           "[Geometry][Mesh][UV][Islands][AllocationFailure]" )
{
    source_fixture_t fixture{};
    fixture.AddDefaultCube();
    fixture.Build();

    common::usize cDiscoveryAllocations = 0u;
    {
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        scoped_islands_t result{ &allocator };
        REQUIRE( MeshUv_TryDiscoverIslands(
                     &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                     &result.value ) == geometry_status_t::OK );
        cDiscoveryAllocations = state.cAllocationCalls;
        REQUIRE( cDiscoveryAllocations > 0u );
    }

    for ( common::usize iFail = 0u;
          iFail < cDiscoveryAllocations; ++iFail ) {
        CAPTURE( iFail, cDiscoveryAllocations );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        {
            scoped_islands_t result{ &allocator };
            REQUIRE( common::Vector_PushBack(
                &result.value.faceIds, Id( 999999u ) ) );
            REQUIRE( common::Vector_PushBack(
                &result.value.islands,
                mesh_uv_island_range_t{ 0u, 1u } ) );

            state.iFailOnCall = state.cAllocationCalls + iFail;
            CHECK( MeshUv_TryDiscoverIslands(
                       &fixture.source, mesh_uv_set_t::MATERIAL, 0.0,
                       &result.value ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            REQUIRE( result.value.faceIds.nCount == 1u );
            CHECK( result.value.faceIds.pData[0].value == 999999u );
            REQUIRE( result.value.islands.nCount == 1u );
            CHECK( result.value.islands.pData[0].iFirstFace == 0u );
            CHECK( result.value.islands.pData[0].cFaces == 1u );
            CHECK( state.cRejectedAllocations == 1u );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

TEST_CASE( "Mesh UV seam editing is source-atomic on allocation failure",
           "[Geometry][Mesh][UV][Seams][AllocationFailure]" )
{
    common::usize cEditAllocations = 0u;
    {
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        source_fixture_t fixture{ allocator };
        fixture.AddDefaultCube();
        fixture.Build();
        common::Vector_Shutdown( &fixture.source.attributes.edges );
        REQUIRE( common::Vector_Init(
            &fixture.source.attributes.edges, &fixture.allocator ) );
        const common::usize iBefore = state.cAllocationCalls;
        const mesh_source_edge_key_t edge = Edge( 14u, 15u );
        REQUIRE( MeshUv_TryEditSeams(
                     &fixture.source,
                     common::span_t<const mesh_source_edge_key_t>{
                         &edge, 1u },
                     mesh_uv_seam_edit_t::SET ) ==
                 geometry_status_t::OK );
        cEditAllocations = state.cAllocationCalls - iBefore;
        REQUIRE( cEditAllocations > 0u );
    }

    for ( common::usize iFail = 0u; iFail < cEditAllocations; ++iFail ) {
        CAPTURE( iFail, cEditAllocations );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        {
            source_fixture_t fixture{ allocator };
            fixture.AddDefaultCube();
            fixture.Build();
            common::Vector_Shutdown( &fixture.source.attributes.edges );
            REQUIRE( common::Vector_Init(
                &fixture.source.attributes.edges, &fixture.allocator ) );

            mesh_source_description_t before{};
            mesh_source_description_t after{};
            REQUIRE( MeshSourceDescription_Init(
                         &before, common::Allocator_GetSystem(), Id( 1u ) ) ==
                     geometry_status_t::OK );
            REQUIRE( MeshSourceDescription_Init(
                         &after, common::Allocator_GetSystem(), Id( 1u ) ) ==
                     geometry_status_t::OK );
            REQUIRE( MeshSource_TryDescribe(
                         &fixture.source, &before ) ==
                     geometry_status_t::OK );

            state.iFailOnCall = state.cAllocationCalls + iFail;
            common::u32 cChanged = 99u;
            const mesh_source_edge_key_t edge = Edge( 14u, 15u );
            CHECK( MeshUv_TryEditSeams(
                       &fixture.source,
                       common::span_t<const mesh_source_edge_key_t>{
                           &edge, 1u },
                       mesh_uv_seam_edit_t::SET, &cChanged ) ==
                   geometry_status_t::ALLOCATION_FAILED );
            CHECK( cChanged == 0u );
            CHECK( state.cRejectedAllocations == 1u );

            state.iFailOnCall = common::CY_USIZE_MAX;
            REQUIRE( MeshSource_TryDescribe(
                         &fixture.source, &after ) ==
                     geometry_status_t::OK );
            CHECK( MeshSourceDescription_Equal( &before, &after ) );
            MeshSourceDescription_Shutdown( &after );
            MeshSourceDescription_Shutdown( &before );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
