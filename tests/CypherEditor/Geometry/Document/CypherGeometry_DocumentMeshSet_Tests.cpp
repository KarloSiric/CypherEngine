//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshSet_Tests.cpp
//  Purpose: Tests atomic ordered mesh-set publication and batch undo/redo.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentMeshSet.h"
#include "CypherGeometry_MeshSetDelta.h"
#include "CypherGeometry_BrushGenerator.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace cypher::editor::geometry
{

namespace
{

geometry_source_id_t Id( common::u64 value ) noexcept
{
    return geometry_source_id_t{ value };
}

void MakeCubeDescription(
    mesh_source_description_t *pDescription,
    common::u64 root,
    common::f64 offset = 0.0 )
{
    MeshSourceDescription_Clear( pDescription, Id( root ) );
    for ( common::u32 i = 0u; i < 8u; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     pDescription,
                     math::Vec3d_Make(
                         ( ( i & 1u ) != 0u ? 1.0 : 0.0 ) + offset,
                         ( i & 2u ) != 0u ? 1.0 : 0.0,
                         ( i & 4u ) != 0u ? 1.0 : 0.0 ),
                     Id( root + 1u + i ),
                     nullptr ) == geometry_status_t::OK );
    }
    constexpr common::u32 faces[6][4]{
        { 0u, 2u, 3u, 1u }, { 4u, 5u, 7u, 6u },
        { 0u, 1u, 5u, 4u }, { 2u, 6u, 7u, 3u },
        { 0u, 4u, 6u, 2u }, { 1u, 3u, 7u, 5u }
    };
    for ( common::u32 i = 0u; i < 6u; ++i ) {
        mesh_face_attributes_t attributes{};
        attributes.material.value = root + i;
        REQUIRE( MeshSourceDescription_TryAddFace(
                     pDescription,
                     common::span_t<const common::u32>{ faces[i], 4u },
                     Id( root + 9u + i ),
                     attributes,
                     nullptr ) == geometry_status_t::OK );
    }
}

void AddDescription(
    geometry_document_t *pDocument,
    const mesh_source_description_t &description )
{
    mesh_source_t mesh{};
    REQUIRE( MeshSource_TryBuild(
                 &description, pDocument->pAllocator, &mesh ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddMesh( pDocument, &mesh ) ==
             geometry_status_t::OK );
    MeshSource_Shutdown( &mesh );
}

struct descriptions_t {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t values[5]{};

    descriptions_t()
    {
        for ( mesh_source_description_t &value : values ) {
            REQUIRE( MeshSourceDescription_Init(
                         &value, &allocator, GEOMETRY_SOURCE_ID_INVALID ) ==
                     geometry_status_t::OK );
        }
        MakeCubeDescription( &values[0], 1000u, 0.0 );
        MakeCubeDescription( &values[1], 2000u, 2.0 );
        MakeCubeDescription( &values[2], 3000u, 4.0 );
        MakeCubeDescription( &values[3], 4000u, 6.0 );
        MakeCubeDescription( &values[4], 5000u, 8.0 );
    }

    ~descriptions_t()
    {
        for ( mesh_source_description_t &value : values ) {
            MeshSourceDescription_Shutdown( &value );
        }
    }
};

bool MeshDescriptionEquals(
    const mesh_source_t *pMesh,
    const mesh_source_description_t &expected,
    const common::allocator_t *pAllocator )
{
    if ( pMesh == nullptr ) {
        return false;
    }
    mesh_source_description_t actual{};
    REQUIRE( MeshSourceDescription_Init(
                 &actual, pAllocator, GEOMETRY_SOURCE_ID_INVALID ) ==
             geometry_status_t::OK );
    REQUIRE( MeshSource_TryDescribe( pMesh, &actual ) ==
             geometry_status_t::OK );
    const bool bEqual = MeshSourceDescription_Equal( &actual, &expected );
    MeshSourceDescription_Shutdown( &actual );
    return bEqual;
}

struct failure_allocator_state_t {
    common::usize cCalls{ 0u };
    common::usize cSuccessful{ 0u };
    common::usize cFrees{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *FailureAllocate(
    void *pUser,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUser );
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
    void *pUser,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failure_allocator_state_t *>( pUser );
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
    mesh_source_t **pStorage{ nullptr };
    common::usize cMeshes{ 0u };
    common::usize cCapacity{ 0u };
    mesh_source_t *pMesh0{ nullptr };
    mesh_source_t *pMesh1{ nullptr };
    const void *pClaimedSlots{ nullptr };
    const void *pLiveSlots{ nullptr };
    common::usize cClaimed{ 0u };
    common::usize cLive{ 0u };
    common::usize cClaimedCapacity{ 0u };
    common::usize cLiveCapacity{ 0u };
    common::u64 nextId{ 0u };
    geometry_revision_t revision{ 0u };
};

document_observation_t Observe(
    const geometry_document_t &document ) noexcept
{
    document_observation_t result{};
    result.pStorage = document.meshes.pData;
    result.cMeshes = document.meshes.nCount;
    result.cCapacity = document.meshes.nCapacity;
    result.pMesh0 = document.meshes.nCount > 0u
        ? document.meshes.pData[0u] : nullptr;
    result.pMesh1 = document.meshes.nCount > 1u
        ? document.meshes.pData[1u] : nullptr;
    result.pClaimedSlots = document.sourceIds.claimedIds.pSlots;
    result.pLiveSlots = document.sourceIds.liveIds.pSlots;
    result.cClaimed = document.sourceIds.claimedIds.nCount;
    result.cLive = document.sourceIds.liveIds.nCount;
    result.cClaimedCapacity = document.sourceIds.claimedIds.nCapacity;
    result.cLiveCapacity = document.sourceIds.liveIds.nCapacity;
    result.nextId = document.sourceIds.allocator.next.value;
    result.revision = document.revision;
    return result;
}

void CheckObservation(
    const geometry_document_t &document,
    const document_observation_t &expected )
{
    CHECK( document.meshes.pData == expected.pStorage );
    CHECK( document.meshes.nCount == expected.cMeshes );
    CHECK( document.meshes.nCapacity == expected.cCapacity );
    CHECK( ( document.meshes.nCount > 0u ? document.meshes.pData[0u]
                                         : nullptr ) == expected.pMesh0 );
    CHECK( ( document.meshes.nCount > 1u ? document.meshes.pData[1u]
                                         : nullptr ) == expected.pMesh1 );
    CHECK( document.sourceIds.claimedIds.pSlots == expected.pClaimedSlots );
    CHECK( document.sourceIds.liveIds.pSlots == expected.pLiveSlots );
    CHECK( document.sourceIds.claimedIds.nCount == expected.cClaimed );
    CHECK( document.sourceIds.liveIds.nCount == expected.cLive );
    CHECK( document.sourceIds.claimedIds.nCapacity ==
           expected.cClaimedCapacity );
    CHECK( document.sourceIds.liveIds.nCapacity == expected.cLiveCapacity );
    CHECK( document.sourceIds.allocator.next.value == expected.nextId );
    CHECK( document.revision == expected.revision );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) );
}

} // namespace

TEST_CASE(
    "Ordered mesh-set publication atomically adopts N-to-M results",
    "[geometry][document][mesh-set]" )
{
    descriptions_t descriptions;
    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &descriptions.allocator, policy ) ==
             geometry_status_t::OK );
    AddDescription( &document, descriptions.values[0] );
    AddDescription( &document, descriptions.values[1] );
    AddDescription( &document, descriptions.values[2] );
    mesh_source_t *pA = document.meshes.pData[0u];
    mesh_source_t *pC = document.meshes.pData[2u];
    document.revision = 17u;

    const geometry_source_id_t removals[]{ Id( 2000u ) };
    const mesh_source_description_t *replacements[]{
        &descriptions.values[3], &descriptions.values[4]
    };
    const geometry_source_id_t order[]{
        Id( 5000u ), Id( 1000u ), Id( 3000u ), Id( 4000u )
    };
    REQUIRE( GeometryDocument_TryPublishMeshSetExact(
                 &document,
                 { removals, 1u },
                 { replacements, 2u },
                 { order, 4u } ) == geometry_status_t::OK );

    REQUIRE( GeometryDocument_MeshCount( &document ) == 4u );
    CHECK( document.meshes.pData[1u] == pA );
    CHECK( document.meshes.pData[2u] == pC );
    CHECK( document.meshes.pData[0u]->sourceId.value == 5000u );
    CHECK( document.meshes.pData[3u]->sourceId.value == 4000u );
    CHECK( MeshDescriptionEquals(
        document.meshes.pData[0u], descriptions.values[4],
        document.pAllocator ) );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( 2005u ) ) );
    CHECK( common::HashSet_Contains(
        &document.sourceIds.claimedIds, Id( 2005u ) ) );
    CHECK( document.revision == 17u );

    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Ordered mesh-set publication rejects retained and brush collisions",
    "[geometry][document][mesh-set][identity]" )
{
    descriptions_t descriptions;
    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &descriptions.allocator, policy ) ==
             geometry_status_t::OK );

    geometry_source_id_allocator_t brushIds{};
    brush_solid_t brush{};
    REQUIRE( BrushGenerator_TryMakeBox(
                 &brush,
                 &descriptions.allocator,
                 policy,
                 &brushIds,
                 math::Vec3d_Make( 0.0, 0.0, 0.0 ),
                 math::Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryDocument_TryAddBrush( &document, &brush ) ==
             geometry_status_t::OK );
    AddDescription( &document, descriptions.values[0] );
    const document_observation_t before = Observe( document );

    descriptions.values[3].vertices.pData[0u].sourceId = brush.sourceId;
    const mesh_source_description_t *replacement[]{ &descriptions.values[3] };
    const geometry_source_id_t order[]{ Id( 1000u ), Id( 4000u ) };
    CHECK( GeometryDocument_TryPublishMeshSetExact(
               &document, {}, { replacement, 1u }, { order, 2u } ) ==
           geometry_status_t::IDENTITY_CONFLICT );
    CheckObservation( document, before );

    BrushSolid_Shutdown( &brush );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Mesh-set delta checks order and performs allocation-free inversion",
    "[geometry][transactions][mesh-set][undo]" )
{
    descriptions_t descriptions;
    geometry_policy_t policy{};
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &descriptions.allocator, policy ) ==
             geometry_status_t::OK );
    AddDescription( &document, descriptions.values[0] );
    AddDescription( &document, descriptions.values[1] );
    document.revision = 10u;

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &descriptions.allocator ) == geometry_status_t::OK );
    const mesh_source_description_t *before[]{
        &descriptions.values[0], &descriptions.values[1]
    };
    const mesh_source_description_t *after[]{
        &descriptions.values[1], &descriptions.values[0]
    };
    REQUIRE( GeometryMeshSetDelta_TryAssign(
                 &delta, { before, 2u }, { after, 2u } ) ==
             geometry_status_t::OK );

    mesh_source_t *pA = document.meshes.pData[0u];
    mesh_source_t *pB = document.meshes.pData[1u];
    geometry_revision_t revision = 0u;
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 11u );
    REQUIRE( document.meshes.nCount == 2u );
    CHECK( document.meshes.pData[0u]->sourceId.value == 2000u );
    CHECK( document.meshes.pData[1u]->sourceId.value == 1000u );
    CHECK( document.meshes.pData[0u] == pB );
    CHECK( document.meshes.pData[1u] == pA );
    CHECK( GeometryMeshSetDelta_TryApply(
               &delta, &document, &revision ) ==
           geometry_status_t::STALE_REVISION );
    CHECK( document.revision == 11u );

    auto *pBeforeStorage = delta.before.meshes.pData;
    auto *pAfterStorage = delta.after.meshes.pData;
    GeometryMeshSetDelta_Invert( &delta );
    CHECK( delta.before.meshes.pData == pAfterStorage );
    CHECK( delta.after.meshes.pData == pBeforeStorage );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 12u );
    CHECK( document.meshes.pData[0u]->sourceId.value == 1000u );
    CHECK( document.meshes.pData[1u]->sourceId.value == 2000u );
    CHECK( document.meshes.pData[0u] == pA );
    CHECK( document.meshes.pData[1u] == pB );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 13u );
    CHECK( document.meshes.pData[0u]->sourceId.value == 2000u );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Mesh-set delta canonicalizes valid descriptions before recording",
    "[geometry][transactions][mesh-set][canonical]" )
{
    descriptions_t descriptions;
    mesh_source_description_t permuted{};
    REQUIRE( MeshSourceDescription_Init(
                 &permuted,
                 &descriptions.allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryCopy(
                 &permuted, &descriptions.values[0] ) ==
             geometry_status_t::OK );

    // Same authored mesh, deliberately expressed in a non-canonical vertex
    // order. Corner indices follow their vertices, so topology is unchanged.
    const mesh_source_vertex_t temporary = permuted.vertices.pData[0u];
    permuted.vertices.pData[0u] = permuted.vertices.pData[1u];
    permuted.vertices.pData[1u] = temporary;
    for ( common::usize i = 0u; i < permuted.corners.nCount; ++i ) {
        common::u32 &index = permuted.corners.pData[i].iVertex;
        if ( index == 0u ) {
            index = 1u;
        } else if ( index == 1u ) {
            index = 0u;
        }
    }

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &descriptions.allocator ) == geometry_status_t::OK );
    const mesh_source_description_t *before[]{ &permuted };
    REQUIRE( GeometryMeshSetDelta_TryAssign(
                 &delta, { before, 1u }, {} ) == geometry_status_t::OK );
    REQUIRE( delta.before.meshes.nCount == 1u );
    CHECK( MeshSourceDescription_Equal(
        delta.before.meshes.pData[0u], &descriptions.values[0] ) );

    GeometryMeshSetDelta_Shutdown( &delta );
    MeshSourceDescription_Shutdown( &permuted );
}

TEST_CASE(
    "Mesh-set delta rebuilds only changed roots and retains unrelated objects",
    "[geometry][transactions][mesh-set][minimal-diff]" )
{
    descriptions_t descriptions;
    mesh_source_description_t modified{};
    REQUIRE( MeshSourceDescription_Init(
                 &modified, &descriptions.allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryCopy(
                 &modified, &descriptions.values[0] ) ==
             geometry_status_t::OK );
    modified.vertices.pData[0u].position.x -= 0.25;

    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &descriptions.allocator,
                 geometry_policy_t{} ) == geometry_status_t::OK );
    AddDescription( &document, descriptions.values[0] );
    AddDescription( &document, descriptions.values[1] );
    const std::uintptr_t originalChangedAddress =
        reinterpret_cast<std::uintptr_t>( document.meshes.pData[0u] );
    mesh_source_t *pOriginalRetained = document.meshes.pData[1u];

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &descriptions.allocator ) == geometry_status_t::OK );
    const mesh_source_description_t *before[]{
        &descriptions.values[0], &descriptions.values[1]
    };
    const mesh_source_description_t *after[]{
        &descriptions.values[1], &modified
    };
    REQUIRE( GeometryMeshSetDelta_TryAssign(
                 &delta, { before, 2u }, { after, 2u } ) ==
             geometry_status_t::OK );

    geometry_revision_t revision = 0u;
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, &revision ) == geometry_status_t::OK );
    CHECK( revision == 1u );
    REQUIRE( document.meshes.nCount == 2u );
    CHECK( document.meshes.pData[0u] == pOriginalRetained );
    CHECK( reinterpret_cast<std::uintptr_t>( document.meshes.pData[1u] ) !=
           originalChangedAddress );
    CHECK( MeshDescriptionEquals(
        document.meshes.pData[1u], modified, document.pAllocator ) );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
    MeshSourceDescription_Shutdown( &modified );
}

TEST_CASE(
    "Mesh-set delta undo restores retired identities without rewinding high water",
    "[geometry][transactions][mesh-set][identity]" )
{
    descriptions_t descriptions;
    geometry_document_t document{};
    REQUIRE( GeometryDocument_Init(
                 &document, &descriptions.allocator,
                 geometry_policy_t{} ) == geometry_status_t::OK );
    AddDescription( &document, descriptions.values[0] );
    AddDescription( &document, descriptions.values[1] );
    mesh_source_t *pRetained = document.meshes.pData[1u];

    const common::u64 highWater =
        document.sourceIds.allocator.next.value;
    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &document.sourceIds );
    REQUIRE( highWater > 2014u );

    geometry_mesh_set_delta_t delta{};
    REQUIRE( GeometryMeshSetDelta_Init(
                 &delta, &descriptions.allocator ) == geometry_status_t::OK );
    const mesh_source_description_t *before[]{
        &descriptions.values[0], &descriptions.values[1]
    };
    const mesh_source_description_t *after[]{ &descriptions.values[1] };
    REQUIRE( GeometryMeshSetDelta_TryAssign(
                 &delta, { before, 2u }, { after, 1u } ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, nullptr ) == geometry_status_t::OK );
    REQUIRE( document.meshes.nCount == 1u );
    CHECK( document.meshes.pData[0u] == pRetained );
    CHECK_FALSE( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( 1005u ) ) );
    CHECK( common::HashSet_Contains(
        &document.sourceIds.claimedIds, Id( 1005u ) ) );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &document.sourceIds ) == cClaimed );
    CHECK( document.sourceIds.allocator.next.value == highWater );

    GeometryMeshSetDelta_Invert( &delta );
    REQUIRE( GeometryMeshSetDelta_TryApply(
                 &delta, &document, nullptr ) == geometry_status_t::OK );
    REQUIRE( document.meshes.nCount == 2u );
    CHECK( document.meshes.pData[1u] == pRetained );
    CHECK( GeometrySourceIdRegistry_Contains(
        &document.sourceIds, Id( 1005u ) ) );
    CHECK( GeometrySourceIdRegistry_ClaimedCount(
               &document.sourceIds ) == cClaimed );
    CHECK( document.sourceIds.allocator.next.value == highWater );
    CHECK( GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) );

    GeometryMeshSetDelta_Shutdown( &delta );
    GeometryDocument_Shutdown( &document );
}

TEST_CASE(
    "Mesh-set delta accepts coherent exact and empty no-op payloads",
    "[geometry][transactions][mesh-set][no-op]" )
{
    descriptions_t descriptions;

    SECTION( "an exact mesh set retains its object and publishes a revision" ) {
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &descriptions.allocator,
                     geometry_policy_t{} ) == geometry_status_t::OK );
        AddDescription( &document, descriptions.values[0] );
        mesh_source_t *pOriginal = document.meshes.pData[0u];

        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &descriptions.allocator ) ==
                 geometry_status_t::OK );
        const mesh_source_description_t *set[]{ &descriptions.values[0] };
        REQUIRE( GeometryMeshSetDelta_TryAssign(
                     &delta, { set, 1u }, { set, 1u } ) ==
                 geometry_status_t::OK );

        geometry_revision_t revision = 99u;
        REQUIRE( GeometryMeshSetDelta_TryApply(
                     &delta, &document, &revision ) ==
                 geometry_status_t::OK );
        CHECK( revision == 1u );
        REQUIRE( document.meshes.nCount == 1u );
        CHECK( document.meshes.pData[0u] == pOriginal );

        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
    }

    SECTION( "the empty set is a valid complete document state" ) {
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &descriptions.allocator,
                     geometry_policy_t{} ) == geometry_status_t::OK );
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &descriptions.allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometryMeshSetDelta_TryAssign(
                     &delta, {}, {} ) == geometry_status_t::OK );

        geometry_revision_t revision = 99u;
        REQUIRE( GeometryMeshSetDelta_TryApply(
                     &delta, &document, &revision ) ==
                 geometry_status_t::OK );
        CHECK( revision == 1u );
        CHECK( document.meshes.nCount == 0u );
        CHECK( GeometrySourceIdRegistry_Count( &document.sourceIds ) == 0u );
        CHECK( document.sourceIds.allocator.next.value == 1u );

        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
    }
}

TEST_CASE(
    "Every mesh-set delta apply allocation failure preserves document and delta",
    "[geometry][transactions][mesh-set][allocation]" )
{
    descriptions_t descriptions;
    mesh_source_description_t modified{};
    REQUIRE( MeshSourceDescription_Init(
                 &modified, &descriptions.allocator,
                 GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( MeshSourceDescription_TryCopy(
                 &modified, &descriptions.values[0] ) ==
             geometry_status_t::OK );
    modified.vertices.pData[0u].position.x -= 0.25;
    const mesh_source_description_t *before[]{
        &descriptions.values[0], &descriptions.values[1]
    };
    const mesh_source_description_t *after[]{
        &descriptions.values[1], &modified
    };

    common::usize cAllocations = 0u;
    {
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, geometry_policy_t{} ) ==
                 geometry_status_t::OK );
        AddDescription( &document, descriptions.values[0] );
        AddDescription( &document, descriptions.values[1] );
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        REQUIRE( GeometryMeshSetDelta_TryAssign(
                     &delta, { before, 2u }, { after, 2u } ) ==
                 geometry_status_t::OK );

        const common::usize iBegin = state.cCalls;
        REQUIRE( GeometryMeshSetDelta_TryApply(
                     &delta, &document, nullptr ) ==
                 geometry_status_t::OK );
        cAllocations = state.cCalls - iBegin;
        REQUIRE( cAllocations > 0u );

        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        REQUIRE( state.cSuccessful == state.cFrees );
    }

    for ( common::usize iFailure = 0u;
          iFailure < cAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cAllocations );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, geometry_policy_t{} ) ==
                 geometry_status_t::OK );
        AddDescription( &document, descriptions.values[0] );
        AddDescription( &document, descriptions.values[1] );
        document.revision = 71u;
        geometry_mesh_set_delta_t delta{};
        REQUIRE( GeometryMeshSetDelta_Init(
                     &delta, &allocator ) == geometry_status_t::OK );
        REQUIRE( GeometryMeshSetDelta_TryAssign(
                     &delta, { before, 2u }, { after, 2u } ) ==
                 geometry_status_t::OK );

        const document_observation_t documentBefore = Observe( document );
        auto *pBeforeStorage = delta.before.meshes.pData;
        auto *pAfterStorage = delta.after.meshes.pData;
        mesh_source_description_t *pBefore0 =
            delta.before.meshes.pData[0u];
        mesh_source_description_t *pBefore1 =
            delta.before.meshes.pData[1u];
        mesh_source_description_t *pAfter0 =
            delta.after.meshes.pData[0u];
        mesh_source_description_t *pAfter1 =
            delta.after.meshes.pData[1u];
        const common::usize iFailureCall = state.cCalls + iFailure;
        state.iFailure = iFailureCall;
        geometry_revision_t revision = 999u;

        CHECK( GeometryMeshSetDelta_TryApply(
                   &delta, &document, &revision ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cCalls == iFailureCall + 1u );
        CHECK( revision == 999u );
        CheckObservation( document, documentBefore );
        CHECK( delta.bHasPayload );
        CHECK( delta.before.meshes.pData == pBeforeStorage );
        CHECK( delta.after.meshes.pData == pAfterStorage );
        REQUIRE( delta.before.meshes.nCount == 2u );
        REQUIRE( delta.after.meshes.nCount == 2u );
        CHECK( delta.before.meshes.pData[0u] == pBefore0 );
        CHECK( delta.before.meshes.pData[1u] == pBefore1 );
        CHECK( delta.after.meshes.pData[0u] == pAfter0 );
        CHECK( delta.after.meshes.pData[1u] == pAfter1 );
        CHECK( MeshSourceDescription_Equal(
            delta.before.meshes.pData[0u], &descriptions.values[0] ) );
        CHECK( MeshSourceDescription_Equal(
            delta.before.meshes.pData[1u], &descriptions.values[1] ) );
        CHECK( MeshSourceDescription_Equal(
            delta.after.meshes.pData[0u], &descriptions.values[1] ) );
        CHECK( MeshSourceDescription_Equal(
            delta.after.meshes.pData[1u], &modified ) );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryMeshSetDelta_Shutdown( &delta );
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }

    MeshSourceDescription_Shutdown( &modified );
}

TEST_CASE(
    "Every mesh-set publication allocation failure preserves exact document state",
    "[geometry][document][mesh-set][allocation]" )
{
    descriptions_t descriptions;
    geometry_policy_t policy{};
    const geometry_source_id_t removals[]{ Id( 1000u ) };
    const mesh_source_description_t *replacements[]{
        &descriptions.values[3], &descriptions.values[4]
    };
    const geometry_source_id_t order[]{
        Id( 2000u ), Id( 4000u ), Id( 5000u )
    };

    failure_allocator_state_t baselineState{};
    common::allocator_t baselineAllocator =
        MakeFailureAllocator( &baselineState );
    geometry_document_t baseline{};
    REQUIRE( GeometryDocument_Init(
                 &baseline, &baselineAllocator, policy ) ==
             geometry_status_t::OK );
    AddDescription( &baseline, descriptions.values[0] );
    AddDescription( &baseline, descriptions.values[1] );
    const common::usize iBegin = baselineState.cCalls;
    REQUIRE( GeometryDocument_TryPublishMeshSetExact(
                 &baseline,
                 { removals, 1u },
                 { replacements, 2u },
                 { order, 3u } ) == geometry_status_t::OK );
    const common::usize cAllocations = baselineState.cCalls - iBegin;
    REQUIRE( cAllocations > 0u );
    GeometryDocument_Shutdown( &baseline );
    REQUIRE( baselineState.cSuccessful == baselineState.cFrees );

    for ( common::usize iFailure = 0u;
          iFailure < cAllocations;
          ++iFailure ) {
        CAPTURE( iFailure, cAllocations );
        failure_allocator_state_t state{};
        common::allocator_t allocator = MakeFailureAllocator( &state );
        geometry_document_t document{};
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        AddDescription( &document, descriptions.values[0] );
        AddDescription( &document, descriptions.values[1] );
        document.revision = 71u;
        const document_observation_t before = Observe( document );
        const common::usize iFailureCall = state.cCalls + iFailure;
        state.iFailure = iFailureCall;

        CHECK( GeometryDocument_TryPublishMeshSetExact(
                   &document,
                   { removals, 1u },
                   { replacements, 2u },
                   { order, 3u } ) ==
               geometry_status_t::ALLOCATION_FAILED );
        CHECK( state.cCalls == iFailureCall + 1u );
        CheckObservation( document, before );

        state.iFailure = common::CY_USIZE_MAX;
        GeometryDocument_Shutdown( &document );
        CHECK( state.cSuccessful == state.cFrees );
    }
}

} // namespace cypher::editor::geometry
