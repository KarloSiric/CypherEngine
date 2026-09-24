//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSolidify_Tests.cpp
//  Purpose: Contract tests for bounded open-mesh solidification.
//  Details: Covers both thickness directions, multi-face and warped sheets,
//           identity and surfacing continuity, invalid/closed input rejection,
//           ID exhaustion, policy limits, and every allocator failure point.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSolidify.h"

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_MeshValidation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace cypher::editor::geometry
{

using Catch::Approx;
using math::Vec3d_Make;

namespace
{

geometry_source_id_t Id( common::u64 value ) noexcept
{
    return geometry_source_id_t{ value };
}

struct source_fixture_t {
    common::allocator_t allocator{};
    mesh_source_description_t description{};
    mesh_source_t source{};

    explicit source_fixture_t(
        const common::allocator_t *pAllocator =
            common::Allocator_GetSystem() )
        : allocator( *pAllocator )
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

    common::u32 Vertex(
        double x,
        double y,
        double z,
        common::u64 id )
    {
        common::u32 index = 0u;
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     &description, Vec3d_Make( x, y, z ),
                     Id( id ), &index ) == geometry_status_t::OK );
        return index;
    }

    common::u32 Face(
        std::vector<common::u32> indices,
        common::u64 id,
        common::u64 material = 0u,
        common::u32 smoothingGroups = 1u )
    {
        mesh_face_attributes_t attributes{};
        attributes.material.value = material;
        attributes.smoothingGroups = smoothingGroups;
        common::u32 index = 0u;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     &description,
                     common::span_t<const common::u32>{
                         indices.data(), indices.size() },
                     Id( id ), attributes, &index ) ==
                 geometry_status_t::OK );
        return index;
    }

    void Build()
    {
        REQUIRE( MeshSource_TryBuild(
                     &description, &allocator, &source ) ==
                 geometry_status_t::OK );
    }

    void Quad()
    {
        Vertex( 0.0, 0.0, 0.0, 10u );
        Vertex( 1.0, 0.0, 0.0, 11u );
        Vertex( 1.0, 1.0, 0.0, 12u );
        Vertex( 0.0, 1.0, 0.0, 13u );
        const common::u32 iFace = Face(
            { 0u, 1u, 2u, 3u }, 20u, 77u, 5u );
        const mesh_source_face_t &face =
            description.faces.pData[iFace];
        for ( common::u32 k = 0u; k < face.cCorners; ++k ) {
            mesh_corner_attributes_t &corner =
                description.corners.pData[
                    face.iFirstCorner + k].attributes;
            corner.uv0 = math::vec2d_t{
                0.25 * static_cast<double>( k ),
                1.0 + 0.5 * static_cast<double>( k ) };
            corner.uv1 = math::vec2d_t{
                2.0 + static_cast<double>( k ),
                3.0 - 0.25 * static_cast<double>( k ) };
            corner.colorRgba = 0x11223340u + k;
        }
        mesh_edge_attributes_t edgeAttributes{};
        edgeAttributes.flags =
            MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
        REQUIRE( MeshSourceDescription_TrySetEdge(
                     &description, 0u, 1u, edgeAttributes, 0.75 ) ==
                 geometry_status_t::OK );
        Build();
    }

    void Strip()
    {
        Vertex( 0.0, 0.0, 0.0, 10u );
        Vertex( 1.0, 0.0, 0.0, 11u );
        Vertex( 2.0, 0.0, 0.0, 12u );
        Vertex( 0.0, 1.0, 0.0, 13u );
        Vertex( 1.0, 1.0, 0.0, 14u );
        Vertex( 2.0, 1.0, 0.0, 15u );
        Face( { 0u, 1u, 4u, 3u }, 20u, 101u, 3u );
        Face( { 1u, 2u, 5u, 4u }, 21u, 202u, 6u );
        Build();
    }

    void BentTriangulatedSheet()
    {
        Vertex( 0.0, 0.0, 0.0, 10u );
        Vertex( 1.0, 0.0, 0.0, 11u );
        Vertex( 1.0, 1.0, 1.0, 12u );
        Vertex( 0.0, 1.0, 0.0, 13u );
        Face( { 0u, 1u, 2u }, 20u, 101u, 3u );
        Face( { 0u, 2u, 3u }, 21u, 202u, 6u );
        Build();
    }

    void TwoOppositelyOrientedQuads()
    {
        Vertex( 0.0, 0.0, 0.0, 10u );
        Vertex( 1.0, 0.0, 0.0, 11u );
        Vertex( 1.0, 1.0, 0.0, 12u );
        Vertex( 0.0, 1.0, 0.0, 13u );
        Vertex( 3.0, 0.0, 0.0, 14u );
        Vertex( 4.0, 0.0, 0.0, 15u );
        Vertex( 4.0, 1.0, 0.0, 16u );
        Vertex( 3.0, 1.0, 0.0, 17u );
        Face( { 0u, 1u, 2u, 3u }, 20u, 101u );
        Face( { 4u, 7u, 6u, 5u }, 21u, 202u );
        Build();
    }

    void Annulus()
    {
        Vertex( 0.0, 0.0, 0.0, 10u );
        Vertex( 3.0, 0.0, 0.0, 11u );
        Vertex( 3.0, 3.0, 0.0, 12u );
        Vertex( 0.0, 3.0, 0.0, 13u );
        Vertex( 1.0, 1.0, 0.0, 14u );
        Vertex( 2.0, 1.0, 0.0, 15u );
        Vertex( 2.0, 2.0, 0.0, 16u );
        Vertex( 1.0, 2.0, 0.0, 17u );
        Face( { 0u, 1u, 5u, 4u }, 20u );
        Face( { 1u, 2u, 6u, 5u }, 21u );
        Face( { 2u, 3u, 7u, 6u }, 22u );
        Face( { 3u, 0u, 4u, 7u }, 23u );
        Build();
    }

    void Cube()
    {
        for ( int i = 0; i < 8; ++i ) {
            Vertex(
                ( i & 1 ) ? 1.0 : 0.0,
                ( i & 2 ) ? 1.0 : 0.0,
                ( i & 4 ) ? 1.0 : 0.0,
                10u + static_cast<common::u64>( i ) );
        }
        Face( { 0u, 2u, 3u, 1u }, 20u );
        Face( { 4u, 5u, 7u, 6u }, 21u );
        Face( { 0u, 1u, 5u, 4u }, 22u );
        Face( { 2u, 6u, 7u, 3u }, 23u );
        Face( { 0u, 4u, 6u, 2u }, 24u );
        Face( { 1u, 3u, 7u, 5u }, 25u );
        Build();
    }
};

struct scoped_description_t {
    mesh_source_description_t value{};

    explicit scoped_description_t(
        const common::allocator_t *pAllocator,
        geometry_source_id_t id = Id( 999u ) )
    {
        REQUIRE( MeshSourceDescription_Init(
                     &value, pAllocator, id ) == geometry_status_t::OK );
    }

    ~scoped_description_t()
    {
        MeshSourceDescription_Shutdown( &value );
    }

    void Capture( const mesh_source_t *pSource )
    {
        REQUIRE( MeshSource_TryDescribe(
                     pSource, &value ) == geometry_status_t::OK );
    }
};

const mesh_source_face_t *FindFace(
    const mesh_source_description_t &description,
    common::u64 id ) noexcept
{
    for ( common::usize i = 0u;
          i < description.faces.nCount; ++i ) {
        if ( description.faces.pData[i].sourceId.value == id ) {
            return &description.faces.pData[i];
        }
    }
    return nullptr;
}

const mesh_source_vertex_t *FindVertex(
    const mesh_source_description_t &description,
    common::u64 id ) noexcept
{
    for ( common::usize i = 0u;
          i < description.vertices.nCount; ++i ) {
        if ( description.vertices.pData[i].sourceId.value == id ) {
            return &description.vertices.pData[i];
        }
    }
    return nullptr;
}

const mesh_source_corner_t *FindCorner(
    const mesh_source_description_t &description,
    const mesh_source_face_t &face,
    common::u64 vertexId ) noexcept
{
    for ( common::u32 k = 0u; k < face.cCorners; ++k ) {
        const mesh_source_corner_t &corner =
            description.corners.pData[face.iFirstCorner + k];
        if ( description.vertices.pData[corner.iVertex].sourceId.value ==
             vertexId ) {
            return &corner;
        }
    }
    return nullptr;
}

const mesh_source_edge_t *FindEdge(
    const mesh_source_description_t &description,
    common::u64 vertexA,
    common::u64 vertexB ) noexcept
{
    const common::u64 low = std::min( vertexA, vertexB );
    const common::u64 high = std::max( vertexA, vertexB );
    for ( common::usize i = 0u;
          i < description.edges.nCount; ++i ) {
        const mesh_source_edge_t &edge = description.edges.pData[i];
        const common::u64 a =
            description.vertices.pData[edge.iVertexA].sourceId.value;
        const common::u64 b =
            description.vertices.pData[edge.iVertexB].sourceId.value;
        if ( std::min( a, b ) == low && std::max( a, b ) == high ) {
            return &edge;
        }
    }
    return nullptr;
}

bool ResultIsEmpty( const mesh_source_t &source ) noexcept
{
    return !MeshSource_IsInitialized( &source ) &&
           !GeometrySourceId_IsValid( source.sourceId ) &&
           source.mesh.pAllocator == nullptr &&
           source.attributes.corners.pAllocator == nullptr &&
           source.attributes.faces.pAllocator == nullptr &&
           source.attributes.edges.pAllocator == nullptr &&
           source.vertexIds.pAllocator == nullptr &&
           source.faceIds.pAllocator == nullptr;
}

struct failure_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cSuccessfulAllocations{ 0u };
    common::usize cFrees{ 0u };
    common::usize cRejectedAllocations{ 0u };
};

void *FailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    ++pState->cAllocationCalls;
    if ( pState->cAllocationCalls == pState->iFailOnCall ) {
        ++pState->cRejectedAllocations;
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessfulAllocations;
    }
    return pMemory;
}

void FailureFree(
    void *pUserData,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        ++pState->cFrees;
    }
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailureAllocator(
    failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        FailureAllocate, nullptr, FailureFree, pState };
}

} // namespace

TEST_CASE(
    "Solidify closes a textured quad and preserves the authored layer",
    "[geometry][meshsolidify]" )
{
    source_fixture_t fixture;
    fixture.Quad();
    scoped_description_t sourceBefore( &fixture.allocator );
    scoped_description_t sourceAfter( &fixture.allocator );
    scoped_description_t outputDescription( &fixture.allocator );
    sourceBefore.Capture( &fixture.source );

    geometry_source_id_allocator_t ids{ Id( 100u ) };
    mesh_source_t output{};
    mesh_solidify_report_t report{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &ids,
                 geometry_policy_t{}, 1.0, &output, &report ) ==
             geometry_status_t::OK );
    sourceAfter.Capture( &fixture.source );
    REQUIRE( MeshSourceDescription_Equal(
        &sourceBefore.value, &sourceAfter.value ) );

    CHECK( report.cSourceVertices == 4u );
    CHECK( report.cSourceFaces == 1u );
    CHECK( report.cBoundaryEdges == 4u );
    CHECK( report.cOffsetVertices == 4u );
    CHECK( report.cOffsetFaces == 1u );
    CHECK( report.cSideFaces == 4u );
    CHECK( report.cTriangulatedSideWalls == 0u );
    CHECK( report.cNewSourceIds == 9u );
    CHECK( ids.next.value == 109u );
    CHECK( EditableMesh_VertexCount( &output.mesh ) == 8u );
    CHECK( EditableMesh_FaceCount( &output.mesh ) == 6u );
    CHECK( EditableMesh_EdgeCount( &output.mesh ) == 12u );
    CHECK( MeshBoundary_CountBoundaryEdges( &output.mesh ) == 0u );
    const mesh_validation_result_t validation =
        MeshValidation_Validate( &output.mesh );
    CHECK( validation.status == geometry_status_t::OK );
    CHECK( validation.fSignedVolume == Approx( 1.0 ) );
    CHECK( MeshSource_Validate(
               &output, &fixture.allocator ).fault ==
           mesh_source_fault_t::NONE );

    outputDescription.Capture( &output );
    for ( common::u64 i = 0u; i < 4u; ++i ) {
        const mesh_source_vertex_t *pOriginal =
            FindVertex( outputDescription.value, 10u + i );
        const mesh_source_vertex_t *pOffset =
            FindVertex( outputDescription.value, 100u + i );
        REQUIRE( pOriginal != nullptr );
        REQUIRE( pOffset != nullptr );
        CHECK( pOriginal->position.z == 0.0 );
        CHECK( pOffset->position.z == Approx( 1.0 ) );
    }

    const mesh_source_face_t *pOriginalFace =
        FindFace( outputDescription.value, 20u );
    const mesh_source_face_t *pOffsetFace =
        FindFace( outputDescription.value, 104u );
    REQUIRE( pOriginalFace != nullptr );
    REQUIRE( pOffsetFace != nullptr );
    CHECK( pOriginalFace->attributes.material.value == 77u );
    CHECK( pOriginalFace->attributes.smoothingGroups == 5u );
    CHECK( pOffsetFace->attributes.material.value == 77u );
    CHECK( pOffsetFace->attributes.smoothingGroups == 5u );
    for ( common::u64 i = 0u; i < 4u; ++i ) {
        const mesh_source_corner_t *pCorner = FindCorner(
            outputDescription.value, *pOriginalFace, 10u + i );
        REQUIRE( pCorner != nullptr );
        CHECK( pCorner->attributes.uv0.x ==
               Approx( 0.25 * static_cast<double>( i ) ) );
        CHECK( pCorner->attributes.uv1.x ==
               Approx( 2.0 + static_cast<double>( i ) ) );
        CHECK( pCorner->attributes.colorRgba == 0x11223340u + i );
    }
    for ( common::u64 faceId = 105u; faceId <= 108u; ++faceId ) {
        const mesh_source_face_t *pSide =
            FindFace( outputDescription.value, faceId );
        REQUIRE( pSide != nullptr );
        CHECK( pSide->attributes.material.value == 77u );
        CHECK( pSide->attributes.smoothingGroups == 0u );
    }

    const mesh_source_edge_t *pOriginalEdge = FindEdge(
        outputDescription.value, 10u, 11u );
    const mesh_source_edge_t *pOffsetEdge = FindEdge(
        outputDescription.value, 100u, 101u );
    REQUIRE( pOriginalEdge != nullptr );
    REQUIRE( pOffsetEdge != nullptr );
    CHECK( pOriginalEdge->attributes.flags ==
           ( MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM ) );
    CHECK( pOriginalEdge->creaseWeight == Approx( 0.75 ) );
    CHECK( pOffsetEdge->attributes.flags ==
           pOriginalEdge->attributes.flags );
    CHECK( pOffsetEdge->creaseWeight == Approx( 0.75 ) );

    geometry_mesh_face_handle_t hOriginal{};
    geometry_mesh_face_handle_t hOffset{};
    REQUIRE( MeshSource_TryFindFace(
        &output, Id( 20u ), &hOriginal ) );
    REQUIRE( MeshSource_TryFindFace(
        &output, Id( 104u ), &hOffset ) );
    CHECK( EditableMesh_GetFace(
               &output.mesh, hOriginal )->normal.z < -0.99 );
    CHECK( EditableMesh_GetFace(
               &output.mesh, hOffset )->normal.z > 0.99 );

    MeshSource_Shutdown( &output );
}

TEST_CASE(
    "Solidify supports negative thickness without reversing the volume",
    "[geometry][meshsolidify]" )
{
    source_fixture_t fixture;
    fixture.Quad();
    geometry_source_id_allocator_t ids{ Id( 200u ) };
    mesh_source_t output{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &ids,
                 geometry_policy_t{}, -0.5, &output, nullptr ) ==
             geometry_status_t::OK );
    const mesh_validation_result_t validation =
        MeshValidation_Validate( &output.mesh );
    REQUIRE( validation.status == geometry_status_t::OK );
    CHECK( validation.fSignedVolume == Approx( 0.5 ) );

    scoped_description_t described( &fixture.allocator );
    described.Capture( &output );
    for ( common::u64 i = 0u; i < 4u; ++i ) {
        const mesh_source_vertex_t *pOriginal =
            FindVertex( described.value, 10u + i );
        const mesh_source_vertex_t *pOffset =
            FindVertex( described.value, 200u + i );
        REQUIRE( pOriginal != nullptr );
        REQUIRE( pOffset != nullptr );
        CHECK( pOriginal->position.z == 0.0 );
        CHECK( pOffset->position.z == Approx( -0.5 ) );
    }
    geometry_mesh_face_handle_t hOriginal{};
    geometry_mesh_face_handle_t hOffset{};
    REQUIRE( MeshSource_TryFindFace(
        &output, Id( 20u ), &hOriginal ) );
    REQUIRE( MeshSource_TryFindFace(
        &output, Id( 204u ), &hOffset ) );
    CHECK( EditableMesh_GetFace(
               &output.mesh, hOriginal )->normal.z > 0.99 );
    CHECK( EditableMesh_GetFace(
               &output.mesh, hOffset )->normal.z < -0.99 );
    MeshSource_Shutdown( &output );
}

TEST_CASE(
    "Solidify closes a multi-face sheet without walling its interior edge",
    "[geometry][meshsolidify]" )
{
    source_fixture_t fixture;
    fixture.Strip();
    geometry_source_id_allocator_t ids{ Id( 1000u ) };
    mesh_source_t output{};
    mesh_solidify_report_t report{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &ids,
                 geometry_policy_t{}, 0.5, &output, &report ) ==
             geometry_status_t::OK );
    CHECK( report.cBoundaryEdges == 6u );
    CHECK( report.cSideFaces == 6u );
    CHECK( EditableMesh_VertexCount( &output.mesh ) == 12u );
    CHECK( EditableMesh_FaceCount( &output.mesh ) == 10u );
    CHECK( EditableMesh_EdgeCount( &output.mesh ) == 20u );
    CHECK( MeshBoundary_CountBoundaryEdges( &output.mesh ) == 0u );
    CHECK( MeshValidation_Validate(
               &output.mesh ).fSignedVolume == Approx( 1.0 ) );

    scoped_description_t described( &fixture.allocator );
    described.Capture( &output );
    REQUIRE( FindFace( described.value, 20u ) != nullptr );
    REQUIRE( FindFace( described.value, 21u ) != nullptr );
    common::u32 cMaterial101Sides = 0u;
    common::u32 cMaterial202Sides = 0u;
    for ( common::usize i = 0u;
          i < described.value.faces.nCount; ++i ) {
        const mesh_source_face_t &face =
            described.value.faces.pData[i];
        if ( face.attributes.smoothingGroups != 0u ) {
            continue;
        }
        cMaterial101Sides +=
            face.attributes.material.value == 101u ? 1u : 0u;
        cMaterial202Sides +=
            face.attributes.material.value == 202u ? 1u : 0u;
    }
    CHECK( cMaterial101Sides == 3u );
    CHECK( cMaterial202Sides == 3u );
    MeshSource_Shutdown( &output );
}

TEST_CASE(
    "Solidify triangulates warped boundary walls deterministically",
    "[geometry][meshsolidify][triangulation]" )
{
    source_fixture_t fixture;
    fixture.BentTriangulatedSheet();
    geometry_source_id_allocator_t ids{ Id( 1000u ) };
    mesh_source_t output{};
    mesh_solidify_report_t report{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &ids,
                 geometry_policy_t{}, 0.25, &output, &report ) ==
             geometry_status_t::OK );

    CHECK( report.cSourceVertices == 4u );
    CHECK( report.cSourceFaces == 2u );
    CHECK( report.cBoundaryEdges == 4u );
    CHECK( report.cOffsetVertices == 4u );
    CHECK( report.cOffsetFaces == 2u );
    CHECK( report.cSideFaces == 8u );
    CHECK( report.cTriangulatedSideWalls == 4u );
    CHECK( report.cNewSourceIds == 14u );
    CHECK( ids.next.value == 1014u );
    CHECK( EditableMesh_VertexCount( &output.mesh ) == 8u );
    CHECK( EditableMesh_HalfEdgeCount( &output.mesh ) == 36u );
    CHECK( EditableMesh_EdgeCount( &output.mesh ) == 18u );
    CHECK( EditableMesh_FaceCount( &output.mesh ) == 12u );
    CHECK( EditableMesh_ShellCount( &output.mesh ) == 1u );
    CHECK( MeshBoundary_CountBoundaryEdges( &output.mesh ) == 0u );

    const mesh_validation_result_t validation =
        MeshValidation_Validate( &output.mesh );
    CHECK( validation.status == geometry_status_t::OK );
    CHECK( validation.fSignedVolume > 0.0 );
    CHECK( MeshSource_Validate(
               &output, &fixture.allocator ).fault ==
           mesh_source_fault_t::NONE );

    scoped_description_t described( &fixture.allocator );
    described.Capture( &output );

    geometry_source_id_allocator_t replayIds{ Id( 1000u ) };
    mesh_source_t replayOutput{};
    mesh_solidify_report_t replayReport{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &replayIds,
                 geometry_policy_t{}, 0.25, &replayOutput,
                 &replayReport ) == geometry_status_t::OK );
    scoped_description_t replayDescription( &fixture.allocator );
    replayDescription.Capture( &replayOutput );
    CHECK( MeshSourceDescription_Equal(
        &described.value, &replayDescription.value ) );
    CHECK( replayReport.cTriangulatedSideWalls ==
           report.cTriangulatedSideWalls );
    CHECK( replayReport.cNewSourceIds == report.cNewSourceIds );
    CHECK( replayIds.next.value == ids.next.value );

    for ( common::u64 faceId : { 20u, 21u, 1004u, 1005u } ) {
        REQUIRE( FindFace( described.value, faceId ) != nullptr );
    }
    CHECK( FindFace( described.value, 20u )->attributes.material.value ==
           101u );
    CHECK( FindFace( described.value, 20u )->attributes.smoothingGroups ==
           3u );
    CHECK( FindFace( described.value, 21u )->attributes.material.value ==
           202u );
    CHECK( FindFace( described.value, 21u )->attributes.smoothingGroups ==
           6u );
    CHECK( FindFace( described.value, 1004u )->attributes.material.value ==
           101u );
    CHECK( FindFace( described.value, 1004u )->attributes.smoothingGroups ==
           3u );
    CHECK( FindFace( described.value, 1005u )->attributes.material.value ==
           202u );
    CHECK( FindFace( described.value, 1005u )->attributes.smoothingGroups ==
           6u );

    common::u32 cMaterial101Sides = 0u;
    common::u32 cMaterial202Sides = 0u;
    for ( common::u64 faceId = 1006u; faceId <= 1013u; ++faceId ) {
        const mesh_source_face_t *pSide =
            FindFace( described.value, faceId );
        REQUIRE( pSide != nullptr );
        CHECK( pSide->attributes.smoothingGroups == 0u );
        cMaterial101Sides +=
            pSide->attributes.material.value == 101u ? 1u : 0u;
        cMaterial202Sides +=
            pSide->attributes.material.value == 202u ? 1u : 0u;
    }
    CHECK( cMaterial101Sides == 4u );
    CHECK( cMaterial202Sides == 4u );

    MeshSource_Shutdown( &replayOutput );
    MeshSource_Shutdown( &output );
}

TEST_CASE(
    "Solidify orients every disconnected shell as a positive volume",
    "[geometry][meshsolidify]" )
{
    source_fixture_t fixture;
    fixture.TwoOppositelyOrientedQuads();
    geometry_source_id_allocator_t ids{ Id( 1000u ) };
    mesh_source_t output{};
    mesh_solidify_report_t report{};
    REQUIRE( MeshSolidify_TryBuild(
                 &fixture.source, &fixture.allocator, &ids,
                 geometry_policy_t{}, 0.5, &output, &report ) ==
             geometry_status_t::OK );
    CHECK( report.cBoundaryEdges == 8u );
    CHECK( report.cNewSourceIds == 18u );
    CHECK( ids.next.value == 1018u );
    CHECK( EditableMesh_VertexCount( &output.mesh ) == 16u );
    CHECK( EditableMesh_HalfEdgeCount( &output.mesh ) == 48u );
    CHECK( EditableMesh_EdgeCount( &output.mesh ) == 24u );
    CHECK( EditableMesh_FaceCount( &output.mesh ) == 12u );
    CHECK( EditableMesh_ShellCount( &output.mesh ) == 2u );
    CHECK( MeshBoundary_CountBoundaryEdges( &output.mesh ) == 0u );
    const mesh_validation_result_t validation =
        MeshValidation_Validate( &output.mesh );
    CHECK( validation.status == geometry_status_t::OK );
    CHECK( validation.fSignedVolume == Approx( 1.0 ) );
    MeshSource_Shutdown( &output );
}

TEST_CASE(
    "Solidify rejects unsupported and malformed inputs atomically",
    "[geometry][meshsolidify][contract]" )
{
    SECTION( "closed shells require an inside-outside policy" ) {
        source_fixture_t fixture;
        fixture.Cube();
        scoped_description_t before( &fixture.allocator );
        scoped_description_t after( &fixture.allocator );
        before.Capture( &fixture.source );
        geometry_source_id_allocator_t ids{ Id( 500u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 0.25, &output, nullptr ) ==
               geometry_status_t::UNSUPPORTED );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( ids.next.value == 500u );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "annular sheets need higher-genus result validation" ) {
        source_fixture_t fixture;
        fixture.Annulus();
        scoped_description_t before( &fixture.allocator );
        scoped_description_t after( &fixture.allocator );
        before.Capture( &fixture.source );
        geometry_source_id_allocator_t ids{ Id( 500u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 0.25, &output, nullptr ) ==
               geometry_status_t::UNSUPPORTED );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( ids.next.value == 500u );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "zero, sub-policy, and non-finite thickness" ) {
        source_fixture_t fixture;
        fixture.Quad();
        scoped_description_t before( &fixture.allocator );
        scoped_description_t after( &fixture.allocator );
        before.Capture( &fixture.source );
        const geometry_policy_t policy{};
        geometry_source_id_allocator_t ids{ Id( 500u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy, 0.0, &output, nullptr ) ==
               geometry_status_t::DEGENERATE );
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy,
                   0.5 * policy.numerical.fMinimumEdgeLength,
                   &output, nullptr ) == geometry_status_t::DEGENERATE );
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy,
                   -policy.numerical.fMinimumEdgeLength,
                   &output, nullptr ) == geometry_status_t::DEGENERATE );
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy,
                   std::numeric_limits<double>::quiet_NaN(),
                   &output, nullptr ) ==
               geometry_status_t::NUMERIC_FAILURE );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( ids.next.value == 500u );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "dirty output root is not overwritten" ) {
        source_fixture_t fixture;
        fixture.Quad();
        scoped_description_t before( &fixture.allocator );
        scoped_description_t after( &fixture.allocator );
        before.Capture( &fixture.source );
        geometry_source_id_allocator_t ids{ Id( 500u ) };
        mesh_source_t output{};
        output.sourceId = Id( 999u );

        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 0.25, &output, nullptr ) ==
               geometry_status_t::ALREADY_INITIALIZED );
        after.Capture( &fixture.source );
        CHECK( MeshSourceDescription_Equal(
            &before.value, &after.value ) );
        CHECK( ids.next.value == 500u );
        CHECK( output.sourceId.value == 999u );
        CHECK_FALSE( MeshSource_IsInitialized( &output ) );
    }

    SECTION( "broken twin reciprocity" ) {
        source_fixture_t fixture;
        fixture.Strip();
        geometry_mesh_edge_handle_t hShared{};
        REQUIRE( MeshSourceEdit_TryFindEdge(
            &fixture.source, Id( 11u ), Id( 14u ),
            &hShared ) );
        mesh_edge_record_t *pEdge = GenerationPool_Get(
            &fixture.source.mesh.edges, hShared );
        REQUIRE( pEdge != nullptr );
        mesh_half_edge_record_t *pHalfEdge = GenerationPool_Get(
            &fixture.source.mesh.halfEdges, pEdge->hHalfEdge );
        REQUIRE( pHalfEdge != nullptr );
        const geometry_mesh_half_edge_handle_t oldTwin =
            pHalfEdge->hTwin;
        REQUIRE( GeometryHandle_IsValid( oldTwin ) );
        pHalfEdge->hTwin =
            GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;

        geometry_source_id_allocator_t ids{ Id( 500u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 0.25, &output, nullptr ) ==
               geometry_status_t::INVALID_TOPOLOGY );
        CHECK_FALSE( GeometryHandle_IsValid( pHalfEdge->hTwin ) );
        CHECK( ids.next.value == 500u );
        CHECK( ResultIsEmpty( output ) );
    }
}

TEST_CASE(
    "Solidify respects limits and identity allocation atomically",
    "[geometry][meshsolidify][contract]" )
{
    source_fixture_t fixture;
    fixture.Quad();

    SECTION( "policy limit" ) {
        geometry_policy_t policy{};
        policy.limits.cVerticesMax = 7u;
        geometry_source_id_allocator_t ids{ Id( 100u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy, 1.0, &output, nullptr ) ==
               geometry_status_t::LIMIT_EXCEEDED );
        CHECK( ids.next.value == 100u );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "edge policy limit uses the exact closed result count" ) {
        geometry_policy_t policy{};
        policy.limits.cEdgesMax = 11u;
        geometry_source_id_allocator_t ids{ Id( 100u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   policy, 1.0, &output, nullptr ) ==
               geometry_status_t::LIMIT_EXCEEDED );
        CHECK( ids.next.value == 100u );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "exhausted allocator" ) {
        geometry_source_id_allocator_t ids{
            Id( common::CY_U64_MAX ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 1.0, &output, nullptr ) ==
               geometry_status_t::INSUFFICIENT_CAPACITY );
        CHECK( ids.next.value == common::CY_U64_MAX );
        CHECK( ResultIsEmpty( output ) );
    }

    SECTION( "fresh IDs may not collide with retained identities" ) {
        geometry_source_id_allocator_t ids{ Id( 10u ) };
        mesh_source_t output{};
        CHECK( MeshSolidify_TryBuild(
                   &fixture.source, &fixture.allocator, &ids,
                   geometry_policy_t{}, 1.0, &output, nullptr ) ==
               geometry_status_t::IDENTITY_CONFLICT );
        CHECK( ids.next.value == 10u );
        CHECK( ResultIsEmpty( output ) );
    }
}

TEST_CASE(
    "Every solidify allocation failure is leak-free and atomic",
    "[geometry][meshsolidify][allocation][contract]" )
{
    common::usize cOperationAllocations = 0u;
    {
        failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeFailureAllocator( &state );
        {
            source_fixture_t fixture( &allocator );
            fixture.Quad();
            geometry_source_id_allocator_t ids{ Id( 100u ) };
            mesh_source_t output{};
            const common::usize iBegin = state.cAllocationCalls;
            REQUIRE( MeshSolidify_TryBuild(
                         &fixture.source, &allocator, &ids,
                         geometry_policy_t{}, 1.0,
                         &output, nullptr ) == geometry_status_t::OK );
            cOperationAllocations =
                state.cAllocationCalls - iBegin;
            REQUIRE( cOperationAllocations > 0u );
            MeshSource_Shutdown( &output );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }

    for ( common::usize iFailure = 1u;
          iFailure <= cOperationAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationAllocations );
        failure_allocator_state_t state{};
        const common::allocator_t allocator =
            MakeFailureAllocator( &state );
        {
            source_fixture_t fixture( &allocator );
            fixture.Quad();
            scoped_description_t before( &allocator );
            scoped_description_t after( &allocator );
            before.Capture( &fixture.source );
            geometry_source_id_allocator_t ids{ Id( 100u ) };
            mesh_source_t output{};

            state.iFailOnCall =
                state.cAllocationCalls + iFailure;
            const geometry_status_t status = MeshSolidify_TryBuild(
                &fixture.source, &allocator, &ids,
                geometry_policy_t{}, 1.0, &output, nullptr );
            state.iFailOnCall = common::CY_INVALID_SIZE;

            CHECK( state.cRejectedAllocations == 1u );
            CHECK( status == geometry_status_t::ALLOCATION_FAILED );
            CHECK( ids.next.value == 100u );
            CHECK( ResultIsEmpty( output ) );
            after.Capture( &fixture.source );
            CHECK( MeshSourceDescription_Equal(
                &before.value, &after.value ) );
            MeshSource_Shutdown( &output );
        }
        CHECK( state.cSuccessfulAllocations == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
