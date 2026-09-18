//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SourceIdRegistry_Tests.cpp
//  Purpose: Verifies document source-ID ownership and deterministic remapping.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SourceIdRegistry.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::editor::geometry
{

namespace
{

struct failing_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

void *RegistryTestAllocate(
    void *pUserData,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }
    return common::Allocator_Allocate(
        common::Allocator_GetSystem(),
        cbSize,
        nAlignment );
}

void RegistryTestFree(
    void *,
    void *pMemory,
    common::usize cbSize,
    common::usize nAlignment ) noexcept
{
    common::Allocator_Free(
        common::Allocator_GetSystem(),
        pMemory,
        cbSize,
        nAlignment );
}

common::allocator_t MakeRegistryTestAllocator(
    failing_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{
        RegistryTestAllocate,
        nullptr,
        RegistryTestFree,
        pState
    };
}

} // namespace

TEST_CASE( "geometry source registry rejects zero and duplicate IDs distinctly",
           "[editor][geometry][core][identity]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_IsValid( &registry ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_IsInitialized( &registry ) );

    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 16u ) == geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 GEOMETRY_SOURCE_ID_INVALID ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 17u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 17u } ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
    REQUIRE( GeometrySourceIdRegistry_Contains(
        &registry,
        geometry_source_id_t{ 17u } ) );

    GeometrySourceIdRegistry_Shutdown( &registry );
    REQUIRE( GeometrySourceIdRegistry_IsValid( &registry ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_IsInitialized( &registry ) );
}

TEST_CASE( "source registry release and clear never rewind allocation",
           "[editor][geometry][core][identity]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 16u ) == geometry_status_t::OK );

    const geometry_source_id_result_t first =
        GeometrySourceIdRegistry_Allocate( &registry );
    const geometry_source_id_result_t second =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( first.id.value == 1u );
    REQUIRE( second.id.value == 2u );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 99u } ) ==
             geometry_status_t::UNSUPPORTED );

    REQUIRE( GeometrySourceIdRegistry_Release( &registry, first.id ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 2u );
    const geometry_source_id_result_t third =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( third.id.value == 3u );

    REQUIRE( GeometrySourceIdRegistry_Clear( &registry ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 0u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 3u );
    const geometry_source_id_result_t afterClear =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( afterClear.id.value == 4u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 4u );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

TEST_CASE( "observed source IDs advance the registry allocator without rewind",
           "[editor][geometry][core][identity]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 16u ) == geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 41u } ) == geometry_status_t::OK );
    REQUIRE( registry.allocator.next.value == 42u );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 7u } ) == geometry_status_t::OK );
    REQUIRE( registry.allocator.next.value == 42u );

    const geometry_source_id_result_t allocated =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( allocated.status == geometry_status_t::OK );
    REQUIRE( allocated.id.value == 42u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 3u );
}

TEST_CASE( "loaded ID registration is a one-time bootstrap phase",
           "[editor][geometry][core][identity][load]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 16u,
                 0u,
                 geometry_source_id_t{ 100u } ) == geometry_status_t::OK );

    // Loaded IDs may arrive in any order below the persisted next/high-water ID.
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 90u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 4u } ) == geometry_status_t::OK );
    REQUIRE( registry.allocator.next.value == 100u );
    REQUIRE( GeometrySourceIdRegistry_SealLoadedIds( &registry ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_SealLoadedIds( &registry ) ==
             geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 17u } ) ==
             geometry_status_t::UNSUPPORTED );
    const geometry_source_id_result_t allocated =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( allocated.status == geometry_status_t::OK );
    REQUIRE( allocated.id.value == 100u );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

TEST_CASE( "persisted exhausted high-water state survives document load",
           "[editor][geometry][core][identity][load]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 8u,
                 0u,
                 GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 7u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_SealLoadedIds( &registry ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Allocate( &registry ).status ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

TEST_CASE( "retired IDs require the explicit undo restore path",
           "[editor][geometry][core][identity][undo]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 8u ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 11u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 12u } ) == geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Release(
                 &registry,
                 geometry_source_id_t{ 11u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 11u } ) ==
             geometry_status_t::UNSUPPORTED );
    REQUIRE( GeometrySourceIdRegistry_RestoreRetired(
                 &registry,
                 geometry_source_id_t{ 99u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdRegistry_RestoreRetired(
                 &registry,
                 geometry_source_id_t{ 12u } ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometrySourceIdRegistry_RestoreRetired(
                 &registry,
                 geometry_source_id_t{ 11u } ) == geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Clear( &registry ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 0u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 2u );
    REQUIRE( GeometrySourceIdRegistry_RestoreRetired(
                 &registry,
                 geometry_source_id_t{ 12u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

TEST_CASE( "source remaps are canonical across caller order and hash layout",
           "[editor][geometry][core][identity][remap]" )
{
    geometry_source_id_registry_t leftRegistry{};
    geometry_source_id_registry_t rightRegistry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &leftRegistry,
                 common::Allocator_GetSystem(),
                 32u,
                 1u ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &rightRegistry,
                 common::Allocator_GetSystem(),
                 32u,
                 16u ) == geometry_status_t::OK );

    const geometry_source_id_t leftInput[]{
        { 90u },
        { 4u },
        { 27u }
    };
    const geometry_source_id_t rightInput[]{
        { 27u },
        { 90u },
        { 4u }
    };
    for ( const geometry_source_id_t id : leftInput ) {
        REQUIRE( GeometrySourceIdRegistry_Register( &leftRegistry, id ) ==
                 geometry_status_t::OK );
    }
    for ( const geometry_source_id_t id : rightInput ) {
        REQUIRE( GeometrySourceIdRegistry_Register( &rightRegistry, id ) ==
                 geometry_status_t::OK );
    }
    geometry_source_id_remap_t leftRemap{};
    geometry_source_id_remap_t rightRemap{};

    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &leftRegistry,
                 common::Span_FromArray( leftInput ),
                 &leftRemap ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &rightRegistry,
                 common::Span_FromArray( rightInput ),
                 &rightRemap ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRemap_Count( &leftRemap ) == 3u );
    REQUIRE( GeometrySourceIdRemap_Count( &rightRemap ) == 3u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &leftRegistry ) == 6u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &rightRegistry ) == 6u );

    for ( common::usize iEntry = 0u; iEntry < 3u; ++iEntry ) {
        REQUIRE( leftRemap.entries.pData[iEntry].source.value ==
                 rightRemap.entries.pData[iEntry].source.value );
        REQUIRE( leftRemap.entries.pData[iEntry].destination.value ==
                 rightRemap.entries.pData[iEntry].destination.value );
    }
    REQUIRE( leftRemap.entries.pData[0].source.value == 4u );
    REQUIRE( leftRemap.entries.pData[1].source.value == 27u );
    REQUIRE( leftRemap.entries.pData[2].source.value == 90u );

    geometry_source_id_t destination{};
    REQUIRE( GeometrySourceIdRemap_Find(
        &leftRemap,
        geometry_source_id_t{ 27u },
        &destination ) );
    REQUIRE( destination.value == 92u );
    REQUIRE_FALSE( GeometrySourceIdRemap_Find(
        &leftRemap,
        geometry_source_id_t{ 28u },
        &destination ) );

    GeometrySourceIdRemap_Shutdown( &leftRemap );
    GeometrySourceIdRemap_Shutdown( &rightRemap );
}

TEST_CASE( "source remap validation rejects aliased destinations",
           "[editor][geometry][core][identity][remap]" )
{
    geometry_source_id_remap_t remap{};
    REQUIRE( common::Vector_Init(
        &remap.entries,
        common::Allocator_GetSystem(),
        2u ) );
    REQUIRE( common::Vector_Resize( &remap.entries, 2u ) );
    remap.entries.pData[0] = { { 1u }, { 7u } };
    remap.entries.pData[1] = { { 2u }, { 7u } };

    REQUIRE_FALSE( GeometrySourceIdRemap_IsValid( &remap ) );
    REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &remap ) );
    GeometrySourceIdRemap_Shutdown( &remap );
}

TEST_CASE( "source remap rejects invalid and repeated origins atomically",
           "[editor][geometry][core][identity][remap]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 16u ) == geometry_status_t::OK );

    const geometry_source_id_t duplicates[]{ { 8u }, { 2u }, { 8u } };
    geometry_source_id_remap_t remap{};
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( duplicates ),
                 &remap ) == geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 0u );
    REQUIRE( registry.allocator.next.value == 1u );
    REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &remap ) );

    const geometry_source_id_t invalid[]{ { 3u }, {} };
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( invalid ),
                 &remap ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 0u );
    REQUIRE( registry.allocator.next.value == 1u );
    REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &remap ) );
}

TEST_CASE( "invalid zero remap input takes precedence over domain capacity",
           "[editor][geometry][core][identity][remap]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 1u ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 1u } ) == geometry_status_t::OK );

    const geometry_source_id_t invalid[]{ GEOMETRY_SOURCE_ID_INVALID };
    geometry_source_id_remap_t remap{};
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( invalid ),
                 &remap ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 1u );
    REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &remap ) );
}

TEST_CASE( "source remap consumes the final identities without wrapping",
           "[editor][geometry][core][identity][remap]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 4u,
                 0u,
                 geometry_source_id_t{ common::CY_U64_MAX - 1u } ) ==
             geometry_status_t::OK );

    const geometry_source_id_t firstBatch[]{ { 8u }, { 3u } };
    geometry_source_id_remap_t firstRemap{};
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( firstBatch ),
                 &firstRemap ) == geometry_status_t::OK );
    REQUIRE( firstRemap.entries.pData[0].destination.value ==
             common::CY_U64_MAX - 1u );
    REQUIRE( firstRemap.entries.pData[1].destination.value ==
             common::CY_U64_MAX );
    REQUIRE( GeometrySourceIdAllocator_IsExhausted( &registry.allocator ) );

    const geometry_source_id_t secondBatch[]{ { 17u } };
    geometry_source_id_remap_t secondRemap{};
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( secondBatch ),
                 &secondRemap ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 2u );
    REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &secondRemap ) );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );

    GeometrySourceIdRemap_Shutdown( &firstRemap );
}

TEST_CASE( "source registry enforces its hard lifetime identity limit",
           "[editor][geometry][core][identity]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 2u ) == geometry_status_t::OK );

    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 10u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 20u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 30u } ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( GeometrySourceIdRegistry_Release(
                 &registry,
                 geometry_source_id_t{ 10u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Allocate( &registry ).status ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 1u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 2u );
    REQUIRE( GeometrySourceIdRegistry_Reserve( &registry, 3u ) ==
             geometry_status_t::LIMIT_EXCEEDED );

    const geometry_source_id_t oneMore[]{ { 9u } };
    geometry_source_id_remap_t remap{};
    REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                 &registry,
                 common::Span_FromArray( oneMore ),
                 &remap ) == geometry_status_t::LIMIT_EXCEEDED );
}

TEST_CASE( "deep source registry validation audits live claim provenance",
           "[editor][geometry][core][identity][validation]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 4u ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 7u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_Release(
                 &registry,
                 geometry_source_id_t{ 7u } ) == geometry_status_t::OK );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );

    // Simulate structurally plausible corruption that the O(1) readiness check
    // intentionally does not scan for.
    REQUIRE( common::HashSet_Insert(
        &registry.liveIds,
        geometry_source_id_t{ 8u } ) );
    REQUIRE( GeometrySourceIdRegistry_IsValid( &registry ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
    REQUIRE( common::HashSet_Erase(
        &registry.liveIds,
        geometry_source_id_t{ 8u } ) );
    REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
}

TEST_CASE( "source registry never revives the terminal identity",
           "[editor][geometry][core][identity]" )
{
    geometry_source_id_registry_t registry{};
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 common::Allocator_GetSystem(),
                 2u,
                 0u,
                 geometry_source_id_t{ common::CY_U64_MAX } ) ==
             geometry_status_t::OK );

    const geometry_source_id_result_t terminal =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( terminal.status == geometry_status_t::OK );
    REQUIRE( terminal.id.value == common::CY_U64_MAX );
    REQUIRE( GeometrySourceIdRegistry_Release( &registry, terminal.id ) ==
             geometry_status_t::OK );

    const geometry_source_id_result_t afterRelease =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( afterRelease.status ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE_FALSE( GeometrySourceId_IsValid( afterRelease.id ) );
}

TEST_CASE( "source registry initialization and reserve failures are atomic",
           "[editor][geometry][core][identity][allocation]" )
{
    failing_allocator_state_t state{};
    common::allocator_t allocator = MakeRegistryTestAllocator( &state );
    geometry_source_id_registry_t registry{};

    state.iFailure = 0u;
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 &allocator,
                 32u,
                 1u ) == geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( GeometrySourceIdRegistry_IsValid( &registry ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_IsInitialized( &registry ) );

    state.iFailure = state.cAllocationCalls + 1u;
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 &allocator,
                 32u,
                 1u ) == geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( GeometrySourceIdRegistry_IsValid( &registry ) );
    REQUIRE_FALSE( GeometrySourceIdRegistry_IsInitialized( &registry ) );

    state.iFailure = common::CY_USIZE_MAX;
    REQUIRE( GeometrySourceIdRegistry_Init(
                 &registry,
                 &allocator,
                 32u,
                 6u ) == geometry_status_t::OK );
    for ( common::u64 value = 1u; value <= 6u; ++value ) {
        REQUIRE( GeometrySourceIdRegistry_Register(
                     &registry,
                     geometry_source_id_t{ value } ) ==
                 geometry_status_t::OK );
    }

    const common::usize cCapacityBefore = registry.liveIds.nCapacity;
    const geometry_source_id_t nextBefore = registry.allocator.next;
    state.iFailure = state.cAllocationCalls;
    REQUIRE( GeometrySourceIdRegistry_Register(
                 &registry,
                 geometry_source_id_t{ 77u } ) ==
             geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 6u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 6u );
    REQUIRE( registry.liveIds.nCapacity == cCapacityBefore );
    REQUIRE( registry.allocator.next.value == nextBefore.value );
    REQUIRE_FALSE( GeometrySourceIdRegistry_Contains(
        &registry,
        geometry_source_id_t{ 77u } ) );

    state.iFailure = state.cAllocationCalls;
    const geometry_source_id_result_t allocation =
        GeometrySourceIdRegistry_Allocate( &registry );
    REQUIRE( allocation.status == geometry_status_t::ALLOCATION_FAILED );
    REQUIRE_FALSE( GeometrySourceId_IsValid( allocation.id ) );
    REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 6u );
    REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 6u );
    REQUIRE( registry.liveIds.nCapacity == cCapacityBefore );
    REQUIRE( registry.allocator.next.value == nextBefore.value );
}

TEST_CASE( "every source remap allocation failure preserves identity state",
           "[editor][geometry][core][identity][allocation][remap]" )
{
    const geometry_source_id_t sourceIds[]{ { 9u }, { 3u }, { 21u } };

    // Ordered input, output entries, claimed-set reserve, then live-set reserve.
    for ( common::usize iFailure = 0u; iFailure < 4u; ++iFailure ) {
        failing_allocator_state_t state{};
        common::allocator_t allocator = MakeRegistryTestAllocator( &state );
        geometry_source_id_registry_t registry{};
        REQUIRE( GeometrySourceIdRegistry_Init(
                     &registry,
                     &allocator,
                     32u ) == geometry_status_t::OK );
        geometry_source_id_remap_t remap{};

        state.iFailure = state.cAllocationCalls + iFailure;
        REQUIRE( GeometrySourceIdRegistry_CreateDeterministicRemap(
                     &registry,
                     common::Span_FromArray( sourceIds ),
                     &remap ) == geometry_status_t::ALLOCATION_FAILED );
        REQUIRE( GeometrySourceIdRegistry_Count( &registry ) == 0u );
        REQUIRE( GeometrySourceIdRegistry_ClaimedCount( &registry ) == 0u );
        REQUIRE( registry.allocator.next.value == 1u );
        REQUIRE( registry.bLoadRegistrationOpen );
        REQUIRE( GeometrySourceIdRegistry_ValidateDeep( &registry ) );
        REQUIRE( GeometrySourceIdRemap_IsValid( &remap ) );
        REQUIRE_FALSE( GeometrySourceIdRemap_IsInitialized( &remap ) );

        GeometrySourceIdRegistry_Shutdown( &registry );
    }
}

} // namespace cypher::editor::geometry
