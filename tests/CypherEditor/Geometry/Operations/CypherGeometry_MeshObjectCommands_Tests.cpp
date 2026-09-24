//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshObjectCommands_Tests.cpp
//  Purpose: Verifies atomic document-level Join and Separate commands.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshObjectCommands.h"

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

void AddCube(
    mesh_source_description_t *pDescription,
    common::u64 vertexIdBase,
    common::u64 faceIdBase,
    common::f64 xOffset,
    common::u64 materialBase )
{
    const common::u32 iVertexBase =
        static_cast<common::u32>( pDescription->vertices.nCount );
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

    constexpr common::u32 localFaces[6][4]{
        { 0u, 2u, 3u, 1u }, { 4u, 5u, 7u, 6u },
        { 0u, 1u, 5u, 4u }, { 2u, 6u, 7u, 3u },
        { 0u, 4u, 6u, 2u }, { 1u, 3u, 7u, 5u }
    };
    for ( common::u32 iFace = 0u; iFace < 6u; ++iFace ) {
        common::u32 corners[4]{};
        for ( common::u32 i = 0u; i < 4u; ++i ) {
            corners[i] = iVertexBase + localFaces[iFace][i];
        }
        mesh_face_attributes_t attributes{};
        attributes.material.value = materialBase + iFace;
        attributes.smoothingGroups = 1u << iFace;
        common::u32 iAddedFace = 0u;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     pDescription,
                     common::span_t<const common::u32>{ corners, 4u },
                     Id( faceIdBase + iFace ), attributes,
                     &iAddedFace ) == geometry_status_t::OK );
        const mesh_source_face_t &face =
            pDescription->faces.pData[iAddedFace];
        for ( common::u32 i = 0u; i < face.cCorners; ++i ) {
            mesh_corner_attributes_t &corner =
                pDescription->corners.pData[face.iFirstCorner + i].attributes;
            corner.uv0 = math::vec2d_t{
                static_cast<common::f64>( faceIdBase + iFace ),
                static_cast<common::f64>( i ) + 0.25 };
            corner.uv1 = math::vec2d_t{
                static_cast<common::f64>( vertexIdBase ),
                static_cast<common::f64>( i ) + 0.5 };
            corner.colorRgba = static_cast<common::u32>(
                0xff000000u | ( ( faceIdBase + iFace + i ) & 0x00ffffffu ) );
        }
    }

    mesh_edge_attributes_t edgeAttributes{};
    edgeAttributes.flags = MESH_EDGE_FLAG_HARD | MESH_EDGE_FLAG_SEAM;
    REQUIRE( MeshSourceDescription_TrySetEdge(
                 pDescription,
                 iVertexBase + 0u,
                 iVertexBase + 1u,
                 edgeAttributes,
                 0.25 + 0.001 * static_cast<common::f64>(
                     materialBase % 100u ) ) ==
             geometry_status_t::OK );
}

void AddDescription(
    geometry_document_t *pDocument,
    const mesh_source_description_t &description )
{
    mesh_source_t source{};
    REQUIRE( MeshSource_TryBuild(
                 &description, pDocument->pAllocator, &source ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddMesh( pDocument, &source ) ==
             geometry_status_t::OK );
    MeshSource_Shutdown( &source );
}

void Describe(
    const mesh_source_t *pSource,
    mesh_source_description_t *pDescription )
{
    REQUIRE( MeshSource_TryDescribe( pSource, pDescription ) ==
             geometry_status_t::OK );
}

bool DescriptionContainsFace(
    const mesh_source_description_t &description,
    geometry_source_id_t faceId ) noexcept
{
    for ( common::usize i = 0u; i < description.faces.nCount; ++i ) {
        if ( description.faces.pData[i].sourceId.value == faceId.value ) {
            return true;
        }
    }
    return false;
}

struct failure_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize cSuccessful{ 0u };
    common::usize cFrees{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailureAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUserData );
    const common::usize iCall = pState->cCalls++;
    if ( iCall == pState->iFailure ) {
        return nullptr;
    }
    void *pMemory = common::Allocator_Allocate(
        common::Allocator_GetSystem(), cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        ++pState->cSuccessful;
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
    ++pState->cFrees;
    common::Allocator_Free(
        common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

common::allocator_t MakeFailureAllocator(
    failure_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        FailureAllocate, nullptr, FailureFree, pState
    };
}

struct document_observation_t {
    mesh_source_t **pMeshStorage{ nullptr };
    common::usize cMeshes{ 0u };
    common::usize cMeshCapacity{ 0u };
    mesh_source_t *pMesh0{ nullptr };
    mesh_source_t *pMesh1{ nullptr };
    const void *pClaimedSlots{ nullptr };
    const void *pLiveSlots{ nullptr };
    common::usize cClaimed{ 0u };
    common::usize cLive{ 0u };
    common::u64 nextId{ 0u };
    geometry_revision_t revision{ 0u };
};

document_observation_t Observe(
    const geometry_document_t &document ) noexcept
{
    document_observation_t result{};
    result.pMeshStorage = document.meshes.pData;
    result.cMeshes = document.meshes.nCount;
    result.cMeshCapacity = document.meshes.nCapacity;
    result.pMesh0 = document.meshes.nCount > 0u
        ? document.meshes.pData[0u] : nullptr;
    result.pMesh1 = document.meshes.nCount > 1u
        ? document.meshes.pData[1u] : nullptr;
    result.pClaimedSlots = document.sourceIds.claimedIds.pSlots;
    result.pLiveSlots = document.sourceIds.liveIds.pSlots;
    result.cClaimed = document.sourceIds.claimedIds.nCount;
    result.cLive = document.sourceIds.liveIds.nCount;
    result.nextId = document.sourceIds.allocator.next.value;
    result.revision = document.revision;
    return result;
}

void CheckObservation(
    const geometry_document_t &document,
    const document_observation_t &expected )
{
    CHECK( document.meshes.pData == expected.pMeshStorage );
    CHECK( document.meshes.nCount == expected.cMeshes );
    CHECK( document.meshes.nCapacity == expected.cMeshCapacity );
    CHECK( ( document.meshes.nCount > 0u
                 ? document.meshes.pData[0u] : nullptr ) == expected.pMesh0 );
    CHECK( ( document.meshes.nCount > 1u
                 ? document.meshes.pData[1u] : nullptr ) == expected.pMesh1 );
    CHECK( document.sourceIds.claimedIds.pSlots == expected.pClaimedSlots );
    CHECK( document.sourceIds.liveIds.pSlots == expected.pLiveSlots );
    CHECK( document.sourceIds.claimedIds.nCount == expected.cClaimed );
    CHECK( document.sourceIds.liveIds.nCount == expected.cLive );
    CHECK( document.sourceIds.allocator.next.value == expected.nextId );
    CHECK( document.revision == expected.revision );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) );
}

void RequireDeltaEmpty( const geometry_mesh_set_delta_t &delta )
{
    CHECK_FALSE( delta.bHasPayload );
    CHECK( delta.before.meshes.nCount == 0u );
    CHECK( delta.after.meshes.nCount == 0u );
}

} // namespace

TEST_CASE(
    "Document mesh Join preserves exact authored state and supports undo redo",
    "[geometry][document][mesh-command][join][undo]" )
{
    owned_description_t a( 100u );
    owned_description_t unrelated( 1000u );
    owned_description_t b( 2000u );
    owned_description_t c( 3000u );
    AddCube( &a.value, 110u, 120u, 0.0, 10u );
    AddCube( &unrelated.value, 1010u, 1020u, 20.0, 20u );
    AddCube( &b.value, 2010u, 2020u, 4.0, 30u );
    AddCube( &c.value, 3010u, 3020u, 8.0, 40u );

    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &a.allocator, policy ) == geometry_status_t::OK );
    AddDescription( &document, a.value );
    AddDescription( &document, unrelated.value );
    AddDescription( &document, b.value );
    AddDescription( &document, c.value );
    document.revision = 40u;
    mesh_source_t *pUnrelated = document.meshes.pData[1u];

    const mesh_source_t *joinInputs[]{
        document.meshes.pData[0u],
        document.meshes.pData[2u],
        document.meshes.pData[3u]
    };
    mesh_source_t expectedJoined{};
    REQUIRE( MeshSource_TryJoinExact(
                 { joinInputs, 3u }, Id( 2000u ), &a.allocator,
                 &expectedJoined ) == geometry_status_t::OK );
    owned_description_t expected( 9000u );
    Describe( &expectedJoined, &expected.value );
    MeshSource_Shutdown( &expectedJoined );

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &a.allocator ) == geometry_status_t::OK );
    const geometry_source_id_t roots[]{ Id( 3000u ), Id( 100u ), Id( 2000u ) };
    mesh_object_command_report_t report{};
    geometry_revision_t revision = 0u;
    REQUIRE( GeometryMeshObjectCommand_TryJoinExact(
                 &document, { roots, 3u }, Id( 2000u ), &delta,
                 &report, &revision ) == geometry_status_t::OK );

    REQUIRE( document.meshes.nCount == 2u );
    CHECK( document.meshes.pData[0u] == pUnrelated );
    CHECK( document.meshes.pData[0u]->sourceId.value == 1000u );
    CHECK( document.meshes.pData[1u]->sourceId.value == 2000u );
    owned_description_t actual( 9001u );
    Describe( document.meshes.pData[1u], &actual.value );
    CHECK( MeshSourceDescription_Equal( &actual.value, &expected.value ) );
    CHECK( report.retainedRootId.value == 2000u );
    CHECK( report.cInputObjects == 3u );
    CHECK( report.cOutputObjects == 1u );
    CHECK( report.cNewRoots == 0u );
    CHECK( report.cVertices == 24u );
    CHECK( report.cFaces == 18u );
    CHECK( report.cShells == 3u );
    CHECK( revision == 41u );
    CHECK( document.revision == 41u );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( 100u ) ) );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( 3000u ) ) );

    CHECK( GeometryMeshSetDelta_TryApply(
               &delta, &document, &revision ) ==
           geometry_status_t::STALE_REVISION );
    CHECK( document.revision == 41u );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 42u );
    REQUIRE( document.meshes.nCount == 4u );
    CHECK( document.meshes.pData[1u] == pUnrelated );
    CHECK( document.meshes.pData[0u]->sourceId.value == 100u );
    CHECK( document.meshes.pData[2u]->sourceId.value == 2000u );
    CHECK( document.meshes.pData[3u]->sourceId.value == 3000u );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 43u );
    CHECK( document.meshes.pData[0u] == pUnrelated );
    CHECK( document.meshes.pData[1u]->sourceId.value == 2000u );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document mesh Separate retains an explicit shell and orders fresh roots",
    "[geometry][document][mesh-command][separate][undo]" )
{
    owned_description_t unrelatedA( 100u );
    owned_description_t multi( 1000u );
    owned_description_t unrelatedB( 9000u );
    AddCube( &unrelatedA.value, 110u, 120u, -20.0, 10u );
    // Face minima intentionally differ from geometric and insertion order.
    AddCube( &multi.value, 2010u, 7000u, 0.0, 100u );
    AddCube( &multi.value, 3010u, 4000u, 4.0, 200u );
    AddCube( &multi.value, 5010u, 6000u, 8.0, 300u );
    AddCube( &unrelatedB.value, 9010u, 9020u, 20.0, 20u );

    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &multi.allocator, policy ) ==
             geometry_status_t::OK );
    AddDescription( &document, unrelatedA.value );
    AddDescription( &document, multi.value );
    AddDescription( &document, unrelatedB.value );
    document.revision = 90u;
    mesh_source_t *pUnrelatedA = document.meshes.pData[0u];
    mesh_source_t *pUnrelatedB = document.meshes.pData[2u];
    owned_description_t canonicalMulti( 20004u );
    Describe( document.meshes.pData[1u], &canonicalMulti.value );
    const common::u64 firstFresh = document.sourceIds.allocator.next.value;

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &multi.allocator ) == geometry_status_t::OK );
    mesh_object_command_report_t report{};
    geometry_revision_t revision = 0u;
    REQUIRE( GeometryMeshObjectCommand_TrySeparateByShell(
                 &document, Id( 1000u ), Id( 4002u ), &delta,
                 &report, &revision ) == geometry_status_t::OK );

    REQUIRE( document.meshes.nCount == 5u );
    CHECK( document.meshes.pData[0u] == pUnrelatedA );
    CHECK( document.meshes.pData[4u] == pUnrelatedB );
    CHECK( document.meshes.pData[1u]->sourceId.value == 1000u );
    CHECK( document.meshes.pData[2u]->sourceId.value == firstFresh );
    CHECK( document.meshes.pData[3u]->sourceId.value == firstFresh + 1u );

    owned_description_t retained( 20000u );
    owned_description_t first( 20001u );
    owned_description_t second( 20002u );
    Describe( document.meshes.pData[1u], &retained.value );
    Describe( document.meshes.pData[2u], &first.value );
    Describe( document.meshes.pData[3u], &second.value );
    CHECK( DescriptionContainsFace( retained.value, Id( 4000u ) ) );
    CHECK( DescriptionContainsFace( first.value, Id( 6000u ) ) );
    CHECK( DescriptionContainsFace( second.value, Id( 7000u ) ) );
    CHECK( retained.value.vertices.nCount == 8u );
    CHECK( retained.value.faces.nCount == 6u );
    CHECK( first.value.vertices.nCount == 8u );
    CHECK( first.value.faces.nCount == 6u );
    CHECK( second.value.vertices.nCount == 8u );
    CHECK( second.value.faces.nCount == 6u );
    CHECK( retained.value.edges.nCount == 1u );
    CHECK( first.value.edges.nCount == 1u );
    CHECK( second.value.edges.nCount == 1u );
    CHECK( report.retainedRootId.value == 1000u );
    CHECK( report.firstNewRootId.value == firstFresh );
    CHECK( report.cInputObjects == 1u );
    CHECK( report.cOutputObjects == 3u );
    CHECK( report.cNewRoots == 2u );
    CHECK( report.cVertices == 24u );
    CHECK( report.cFaces == 18u );
    CHECK( report.cShells == 3u );
    CHECK( revision == 91u );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 92u );
    REQUIRE( document.meshes.nCount == 3u );
    CHECK( document.meshes.pData[0u] == pUnrelatedA );
    CHECK( document.meshes.pData[2u] == pUnrelatedB );
    owned_description_t restored( 20003u );
    Describe( document.meshes.pData[1u], &restored.value );
    CHECK( MeshSourceDescription_Equal(
        &restored.value, &canonicalMulti.value ) );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( firstFresh ) ) );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 93u );
    REQUIRE( document.meshes.nCount == 5u );
    CHECK( document.meshes.pData[2u]->sourceId.value == firstFresh );
    CHECK( document.meshes.pData[3u]->sourceId.value == firstFresh + 1u );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document mesh commands reject invalid and degenerate requests atomically",
    "[geometry][document][mesh-command][contract]" )
{
    owned_description_t a( 100u );
    owned_description_t b( 1000u );
    AddCube( &a.value, 110u, 120u, 0.0, 1u );
    AddCube( &b.value, 1010u, 1020u, 4.0, 2u );
    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &a.allocator, policy ) == geometry_status_t::OK );
    AddDescription( &document, a.value );
    AddDescription( &document, b.value );
    document.revision = 7u;
    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &a.allocator ) == geometry_status_t::OK );
    const document_observation_t before = Observe( document );

    const geometry_source_id_t duplicate[]{ Id( 100u ), Id( 100u ) };
    CHECK( GeometryMeshObjectCommand_TryJoinExact(
               &document, { duplicate, 2u }, Id( 100u ), &delta,
               nullptr, nullptr ) == geometry_status_t::IDENTITY_CONFLICT );
    CheckObservation( document, before );
    RequireDeltaEmpty( delta );

    CHECK( GeometryMeshObjectCommand_TrySeparateByShell(
               &document, Id( 100u ), Id( 120u ), &delta,
               nullptr, nullptr ) == geometry_status_t::DEGENERATE );
    CheckObservation( document, before );
    RequireDeltaEmpty( delta );

    CHECK( GeometryMeshObjectCommand_TrySeparateByShell(
               &document, Id( 99999u ), Id( 120u ), &delta,
               nullptr, nullptr ) == geometry_status_t::INVALID_HANDLE );
    CheckObservation( document, before );
    RequireDeltaEmpty( delta );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Document Join survives every command allocation failure",
    "[geometry][document][mesh-command][join][allocation][atomicity]" )
{
    owned_description_t a( 100u );
    owned_description_t b( 1000u );
    AddCube( &a.value, 110u, 120u, 0.0, 1u );
    AddCube( &b.value, 1010u, 1020u, 4.0, 2u );
    geometry_policy_t policy{};
    const geometry_source_id_t roots[]{ Id( 100u ), Id( 1000u ) };

    common::usize cOperationCalls = 0u;
    {
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) == geometry_status_t::OK );
        AddDescription( &document, a.value );
        AddDescription( &document, b.value );
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        const common::usize iBegin = state.cCalls;
        REQUIRE( GeometryMeshObjectCommand_TryJoinExact(
                     &document, { roots, 2u }, Id( 100u ), &delta,
                     nullptr, nullptr ) == geometry_status_t::OK );
        cOperationCalls = state.cCalls - iBegin;
        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }
    REQUIRE( cOperationCalls > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationCalls;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationCalls );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) == geometry_status_t::OK );
        AddDescription( &document, a.value );
        AddDescription( &document, b.value );
        document.revision = 55u;
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        const document_observation_t before = Observe( document );
        const common::usize iFailureCall = state.cCalls + iFailure;
        state.iFailure = iFailureCall;
        geometry_revision_t revision = 444u;

        CHECK( GeometryMeshObjectCommand_TryJoinExact(
                   &document, { roots, 2u }, Id( 100u ), &delta,
                   nullptr, &revision ) == geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cCalls == iFailureCall + 1u );
        CHECK( revision == 444u );
        CheckObservation( document, before );
        RequireDeltaEmpty( delta );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }
}

TEST_CASE(
    "Document Separate survives every command allocation failure",
    "[geometry][document][mesh-command][separate][allocation][atomicity]" )
{
    owned_description_t multi( 100u );
    AddCube( &multi.value, 110u, 120u, 0.0, 1u );
    AddCube( &multi.value, 1010u, 1020u, 4.0, 2u );
    geometry_policy_t policy{};

    common::usize cOperationCalls = 0u;
    {
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) == geometry_status_t::OK );
        AddDescription( &document, multi.value );
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        const common::usize iBegin = state.cCalls;
        REQUIRE( GeometryMeshObjectCommand_TrySeparateByShell(
                     &document, Id( 100u ), Id( 120u ), &delta,
                     nullptr, nullptr ) == geometry_status_t::OK );
        cOperationCalls = state.cCalls - iBegin;
        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }
    REQUIRE( cOperationCalls > 0u );

    for ( common::usize iFailure = 0u;
          iFailure < cOperationCalls;
          ++iFailure ) {
        CAPTURE( iFailure, cOperationCalls );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) == geometry_status_t::OK );
        AddDescription( &document, multi.value );
        document.revision = 88u;
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        const document_observation_t before = Observe( document );
        const common::usize iFailureCall = state.cCalls + iFailure;
        state.iFailure = iFailureCall;
        geometry_revision_t revision = 555u;

        CHECK( GeometryMeshObjectCommand_TrySeparateByShell(
                   &document, Id( 100u ), Id( 120u ), &delta,
                   nullptr, &revision ) == geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cCalls == iFailureCall + 1u );
        CHECK( revision == 555u );
        CheckObservation( document, before );
        RequireDeltaEmpty( delta );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
