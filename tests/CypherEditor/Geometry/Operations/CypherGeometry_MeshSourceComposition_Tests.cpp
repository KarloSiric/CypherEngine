//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComposition_Tests.cpp
//  Purpose: Verifies exact, failure-atomic mesh-source composition.
//  Details: Join is tested as an ownership operation rather than welding:
//           disconnected shells and all authored identities/attributes must
//           survive while only the selected root identity remains.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceComposition.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::editor::geometry
{

namespace
{

geometry_source_id_t Id( common::u64 value ) noexcept
{
    return geometry_source_id_t{ value };
}

struct owned_description_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t value{};

    explicit owned_description_t( common::u64 root )
    {
        REQUIRE( MeshSourceDescription_Init(
                     &value, &allocator, Id( root ) ) ==
                 geometry_status_t::OK );
    }

    ~owned_description_t()
    {
        MeshSourceDescription_Shutdown( &value );
    }
};

struct owned_source_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t value{};

    ~owned_source_t()
    {
        MeshSource_Shutdown( &value );
    }
};

void AddCube(
    mesh_source_description_t *pDescription,
    common::u64 vertexIdBase,
    common::u64 faceIdBase,
    common::f64 xOffset,
    common::u64 materialBase )
{
    for ( common::u32 i = 0u; i < 8u; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     pDescription,
                     math::vec3d_t{
                         xOffset + ( ( i & 1u ) != 0u ? 1.0 : 0.0 ),
                         ( i & 2u ) != 0u ? 1.0 : 0.0,
                         ( i & 4u ) != 0u ? 1.0 : 0.0 },
                     Id( vertexIdBase + i ), nullptr ) ==
                 geometry_status_t::OK );
    }

    const common::u32 rings[6][4] = {
        { 0u, 2u, 3u, 1u },
        { 4u, 5u, 7u, 6u },
        { 0u, 1u, 5u, 4u },
        { 2u, 6u, 7u, 3u },
        { 0u, 4u, 6u, 2u },
        { 1u, 3u, 7u, 5u }
    };
    for ( common::u32 iFace = 0u; iFace < 6u; ++iFace ) {
        mesh_face_attributes_t attributes{};
        attributes.material.value = materialBase + iFace;
        attributes.smoothingGroups = 1u << iFace;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     pDescription,
                     common::span_t<const common::u32>{ rings[iFace], 4u },
                     Id( faceIdBase + iFace ), attributes, nullptr ) ==
                 geometry_status_t::OK );
    }
    for ( common::usize i = 0u;
          i < pDescription->corners.nCount;
          ++i ) {
        mesh_corner_attributes_t &corner =
            pDescription->corners.pData[i].attributes;
        corner.uv0 = math::vec2d_t{
            static_cast<common::f64>( i ) + xOffset,
            static_cast<common::f64>( i ) * 0.25 };
        corner.uv1 = math::vec2d_t{
            static_cast<common::f64>( i ) * 0.5,
            xOffset + 3.0 };
        corner.colorRgba = 0x10203040u + static_cast<common::u32>( i );
    }

    mesh_edge_attributes_t edgeAttributes{};
    edgeAttributes.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceDescription_TrySetEdge(
                 pDescription, 0u, 1u, edgeAttributes, 0.75 ) ==
             geometry_status_t::OK );
}

void RequireInputRun(
    const mesh_source_description_t &joined,
    const mesh_source_description_t &input,
    common::usize vertexOffset,
    common::usize cornerOffset,
    common::usize faceOffset,
    common::usize edgeOffset )
{
    for ( common::usize i = 0u; i < input.vertices.nCount; ++i ) {
        const mesh_source_vertex_t &a = joined.vertices.pData[vertexOffset + i];
        const mesh_source_vertex_t &b = input.vertices.pData[i];
        CHECK( a.sourceId.value == b.sourceId.value );
        CHECK( math::Vec3d_EqualsExact( a.position, b.position ) );
    }
    for ( common::usize i = 0u; i < input.corners.nCount; ++i ) {
        const mesh_source_corner_t &a = joined.corners.pData[cornerOffset + i];
        const mesh_source_corner_t &b = input.corners.pData[i];
        CHECK( a.iVertex == vertexOffset + b.iVertex );
        CHECK( a.attributes.uv0.x == b.attributes.uv0.x );
        CHECK( a.attributes.uv0.y == b.attributes.uv0.y );
        CHECK( a.attributes.uv1.x == b.attributes.uv1.x );
        CHECK( a.attributes.uv1.y == b.attributes.uv1.y );
        CHECK( a.attributes.colorRgba == b.attributes.colorRgba );
    }
    for ( common::usize i = 0u; i < input.faces.nCount; ++i ) {
        const mesh_source_face_t &a = joined.faces.pData[faceOffset + i];
        const mesh_source_face_t &b = input.faces.pData[i];
        CHECK( a.iFirstCorner == cornerOffset + b.iFirstCorner );
        CHECK( a.cCorners == b.cCorners );
        CHECK( a.sourceId.value == b.sourceId.value );
        CHECK( a.attributes.material.value == b.attributes.material.value );
        CHECK( a.attributes.smoothingGroups == b.attributes.smoothingGroups );
    }
    for ( common::usize i = 0u; i < input.edges.nCount; ++i ) {
        const mesh_source_edge_t &a = joined.edges.pData[edgeOffset + i];
        const mesh_source_edge_t &b = input.edges.pData[i];
        CHECK( a.iVertexA == vertexOffset + b.iVertexA );
        CHECK( a.iVertexB == vertexOffset + b.iVertexB );
        CHECK( a.attributes.flags == b.attributes.flags );
        CHECK( a.creaseWeight == b.creaseWeight );
    }
}

struct failing_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize cAllocations{ 0u };
    common::usize cFrees{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailingAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cCalls++;
    if ( iCall == pState->iFailure ) {
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cAllocations;
    }
    return pMemory;
}

void FailingFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    ++pState->cFrees;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailingAllocator(
    failing_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        FailingAllocate, nullptr, FailingFree, pState
    };
}

} // namespace

TEST_CASE(
    "Mesh source exact join preserves topology identity and surfacing",
    "[geometry][mesh][composition][join]" )
{
    owned_description_t a( 1u );
    owned_description_t b( 100u );
    AddCube( &a.value, 10u, 20u, 0.0, 1000u );
    AddCube( &b.value, 110u, 120u, 4.0, 2000u );

    owned_source_t sourceA{};
    owned_source_t sourceB{};
    REQUIRE( MeshSource_TryBuild(
                 &a.value, &sourceA.allocator, &sourceA.value ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryBuild(
                 &b.value, &sourceB.allocator, &sourceB.value ) ==
             geometry_status_t::OK );

    owned_description_t beforeA( 900u );
    owned_description_t beforeB( 901u );
    REQUIRE( MeshSource_TryDescribe( &sourceA.value, &beforeA.value ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &sourceB.value, &beforeB.value ) ==
             geometry_status_t::OK );

    const mesh_source_t *inputs[]{ &sourceA.value, &sourceB.value };
    owned_source_t joined{};
    mesh_source_join_report_t report{};
    REQUIRE( MeshSource_TryJoinExact(
                 common::span_t<const mesh_source_t *const>{ inputs, 2u },
                 Id( 1u ), &joined.allocator, &joined.value, &report ) ==
             geometry_status_t::OK );

    CHECK( joined.value.sourceId.value == 1u );
    CHECK( MeshSource_Validate(
               &joined.value, &joined.allocator ).fault ==
           mesh_source_fault_t::NONE );
    CHECK( EditableMesh_VertexCount( &joined.value.mesh ) == 16u );
    CHECK( EditableMesh_FaceCount( &joined.value.mesh ) == 12u );
    CHECK( EditableMesh_ShellCount( &joined.value.mesh ) == 2u );
    CHECK( report.cInputSources == 2u );
    CHECK( report.cVertices == 16u );
    CHECK( report.cFaces == 12u );
    CHECK( report.cCorners == 48u );
    CHECK( report.cAttributedEdges == 2u );
    CHECK( report.cShells == 2u );

    owned_description_t joinedDescription( 902u );
    REQUIRE( MeshSource_TryDescribe(
                 &joined.value, &joinedDescription.value ) ==
             geometry_status_t::OK );
    CHECK( joinedDescription.value.sourceId.value == 1u );
    RequireInputRun(
        joinedDescription.value, beforeA.value, 0u, 0u, 0u, 0u );
    RequireInputRun(
        joinedDescription.value, beforeB.value,
        beforeA.value.vertices.nCount,
        beforeA.value.corners.nCount,
        beforeA.value.faces.nCount,
        beforeA.value.edges.nCount );

    // Caller order is not authored state. Canonical output must therefore be
    // identical when the same ownership set is supplied in reverse order.
    const mesh_source_t *reversedInputs[]{ &sourceB.value, &sourceA.value };
    owned_source_t reversedJoin{};
    REQUIRE( MeshSource_TryJoinExact(
                 common::span_t<const mesh_source_t *const>{
                     reversedInputs, 2u },
                 Id( 1u ), &reversedJoin.allocator,
                 &reversedJoin.value ) == geometry_status_t::OK );
    owned_description_t reversedDescription( 905u );
    REQUIRE( MeshSource_TryDescribe(
                 &reversedJoin.value, &reversedDescription.value ) ==
             geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal(
        &joinedDescription.value, &reversedDescription.value ) );

    // The donor root retires at document publication, while every component
    // identity remains available under the retained root.
    common::vector_t<geometry_source_id_t> joinedIds{};
    REQUIRE( common::Vector_Init( &joinedIds, &joined.allocator ) );
    REQUIRE( MeshSource_TryCollectSourceIds(
                 &joined.value, &joinedIds ) == geometry_status_t::OK );
    CHECK( joinedIds.nCount == 29u );
    bool bFoundDonorRoot = false;
    for ( common::usize i = 0u; i < joinedIds.nCount; ++i ) {
        bFoundDonorRoot = bFoundDonorRoot || joinedIds.pData[i].value == 100u;
    }
    CHECK_FALSE( bFoundDonorRoot );
    common::Vector_Shutdown( &joinedIds );

    owned_description_t afterA( 903u );
    owned_description_t afterB( 904u );
    REQUIRE( MeshSource_TryDescribe( &sourceA.value, &afterA.value ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( &sourceB.value, &afterB.value ) ==
             geometry_status_t::OK );
    CHECK( MeshSourceDescription_Equal( &beforeA.value, &afterA.value ) );
    CHECK( MeshSourceDescription_Equal( &beforeB.value, &afterB.value ) );
}

TEST_CASE(
    "Mesh source exact join rejects ambiguous ownership atomically",
    "[geometry][mesh][composition][join][contract]" )
{
    owned_description_t a( 1u );
    owned_description_t b( 100u );
    AddCube( &a.value, 10u, 20u, 0.0, 1u );
    // Deliberately reuse A's vertex IDs while keeping B internally valid.
    AddCube( &b.value, 10u, 120u, 4.0, 2u );
    owned_source_t sourceA{};
    owned_source_t sourceB{};
    REQUIRE( MeshSource_TryBuild(
                 &a.value, &sourceA.allocator, &sourceA.value ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryBuild(
                 &b.value, &sourceB.allocator, &sourceB.value ) ==
             geometry_status_t::OK );

    const mesh_source_t *colliding[]{ &sourceA.value, &sourceB.value };
    owned_source_t output{};
    CHECK( MeshSource_TryJoinExact(
               common::span_t<const mesh_source_t *const>{ colliding, 2u },
               Id( 1u ), &output.allocator, &output.value ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CHECK_FALSE( MeshSource_IsInitialized( &output.value ) );

    const mesh_source_t *duplicate[]{ &sourceA.value, &sourceA.value };
    CHECK( MeshSource_TryJoinExact(
               common::span_t<const mesh_source_t *const>{ duplicate, 2u },
               Id( 1u ), &output.allocator, &output.value ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( MeshSource_IsInitialized( &output.value ) );

    const mesh_source_t *valid[]{ &sourceA.value, &sourceB.value };
    CHECK( MeshSource_TryJoinExact(
               common::span_t<const mesh_source_t *const>{ valid, 2u },
               Id( 9999u ), &output.allocator, &output.value ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( MeshSource_IsInitialized( &output.value ) );
}

TEST_CASE(
    "Mesh source exact join survives every allocation failure",
    "[geometry][mesh][composition][join][allocation][atomicity]" )
{
    owned_description_t a( 1u );
    owned_description_t b( 100u );
    AddCube( &a.value, 10u, 20u, 0.0, 1u );
    AddCube( &b.value, 110u, 120u, 4.0, 2u );
    owned_source_t sourceA{};
    owned_source_t sourceB{};
    REQUIRE( MeshSource_TryBuild(
                 &a.value, &sourceA.allocator, &sourceA.value ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryBuild(
                 &b.value, &sourceB.allocator, &sourceB.value ) ==
             geometry_status_t::OK );
    const mesh_source_t *inputs[]{ &sourceA.value, &sourceB.value };

    common::usize cOperationAllocations = 0u;
    {
        failing_allocator_state_t state{};
        common::allocator_t allocator = MakeFailingAllocator( &state );
        mesh_source_t output{};
        REQUIRE( MeshSource_TryJoinExact(
                     common::span_t<const mesh_source_t *const>{ inputs, 2u },
                     Id( 1u ), &allocator, &output ) ==
                 geometry_status_t::OK );
        cOperationAllocations = state.cCalls;
        MeshSource_Shutdown( &output );
        CHECK( state.cAllocations == state.cFrees );
    }
    REQUIRE( cOperationAllocations > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        failing_allocator_state_t state{};
        state.iFailure = iFailure;
        common::allocator_t allocator = MakeFailingAllocator( &state );
        mesh_source_t output{};
        mesh_source_join_report_t report{};
        report.cVertices = 999u;
        CHECK( MeshSource_TryJoinExact(
                   common::span_t<const mesh_source_t *const>{ inputs, 2u },
                   Id( 1u ), &allocator, &output, &report ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK_FALSE( MeshSource_IsInitialized( &output ) );
        CHECK_FALSE( GeometrySourceId_IsValid( output.sourceId ) );
        CHECK( report.cInputSources == 0u );
        CHECK( report.cVertices == 0u );
        MeshSource_Shutdown( &output );
        CHECK( state.cAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
