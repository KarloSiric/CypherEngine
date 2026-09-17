//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherResource/CypherResource_Tests.cpp
//  Purpose: Tests synchronous runtime resource ownership and lookup.
//  Details: Coverage protects registration, cache identity, reference counts,
//           failure rollback, stale generations, shutdown order, capacity reuse,
//           dependency cycles, callback reentrancy, allocator ownership, nested
//           dependency cleanup, and public output transactions.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::engine::resource;

namespace
{

inline constexpr u32 TEST_PAYLOAD_CAPACITY = 1024u;

struct test_payload_t {
    resource_id_t id{};
    u32 nSerial{ 0u };
};

struct test_backend_t {
    test_payload_t payloads[TEST_PAYLOAD_CAPACITY]{};
    u32 cLoads{ 0u };
    u32 cUnloads{ 0u };
    u32 unloadOrder[TEST_PAYLOAD_CAPACITY]{};
    bool_t bFailLoad{ CY_FALSE };
    bool_t bReturnPayloadOnFailure{ CY_FALSE };
    bool_t bReturnNullOnSuccess{ CY_FALSE };
    resource_manager_t *pManager{ nullptr };
    resource_type_id_t type{ 0u };
    bool_t bAcquireSelf{ CY_FALSE };
    resource_error_t recursiveResult{ resource_error_t::OK };
};

bool_t TestLoad(
    void *pUserData,
    resource_id_t id,
    resource_type_id_t,
    string_view_t path,
    void **ppResourceOut ) noexcept
{
    auto &backend = *static_cast<test_backend_t *>( pUserData );
    const u32 iPayload = backend.cLoads;
    ++backend.cLoads;
    if ( iPayload >= TEST_PAYLOAD_CAPACITY ) {
        *ppResourceOut = nullptr;
        return CY_FALSE;
    }

    test_payload_t &payload = backend.payloads[iPayload];
    payload.id = id;
    payload.nSerial = iPayload + 1u;

    if ( backend.bAcquireSelf ) {
        resource_handle_t recursiveHandle{};
        backend.recursiveResult = Res_Acquire(
            backend.pManager,
            backend.type,
            path,
            &recursiveHandle );
    }

    if ( backend.bFailLoad ) {
        *ppResourceOut = backend.bReturnPayloadOnFailure
            ? &payload
            : nullptr;
        return CY_FALSE;
    }

    *ppResourceOut = backend.bReturnNullOnSuccess ? nullptr : &payload;
    return CY_TRUE;
}

void TestUnload( void *pUserData, void *pResource ) noexcept
{
    auto &backend = *static_cast<test_backend_t *>( pUserData );
    const auto &payload = *static_cast<test_payload_t *>( pResource );
    backend.unloadOrder[backend.cUnloads] = payload.nSerial;
    ++backend.cUnloads;
}

resource_type_id_t TestType( const char *pName = "test_resource" )
{
    return ResourceTypeId_FromName( StringView_FromCString( pName ) );
}

resource_loader_t TestLoader(
    test_backend_t &backend,
    resource_type_id_t type )
{
    backend.type = type;
    return { type, TestLoad, TestUnload, &backend };
}

resource_manager_config_t TestConfig(
    u32 cResources = 8u,
    u32 cTypes = 4u )
{
    resource_manager_config_t config = Res_DefaultConfig();
    config.cResourceCapacity = cResources;
    config.cTypeCapacity = cTypes;
    return config;
}

u32 TestLookupBucket( resource_id_t id, u32 cLookupCapacity )
{
    u64 nValue = id.value;
    nValue ^= nValue >> 33u;
    nValue *= 0xff51afd7ed558ccdull;
    nValue ^= nValue >> 33u;
    nValue *= 0xc4ceb9fe1a85ec53ull;
    nValue ^= nValue >> 33u;
    return static_cast<u32>( nValue ) & ( cLookupCapacity - 1u );
}

struct manager_scope_t {
    resource_manager_t manager{};

    explicit manager_scope_t( const resource_manager_config_t &config )
    {
        REQUIRE( Res_Init( &manager, config ) == resource_error_t::OK );
    }

    ~manager_scope_t()
    {
        if ( Res_IsInitialized( &manager ) ) {
            CHECK( Res_Shutdown( &manager ) == resource_error_t::OK );
        }
    }
};

void *FailAllocate( void *, usize, usize ) noexcept
{
    return nullptr;
}

void NoopFree( void *, void *, usize, usize ) noexcept
{
}

struct dependency_payload_t {
    u32 iNode{ 0u };
    resource_handle_t dependency{};
};

struct dependency_backend_t {
    resource_manager_t *pManager{ nullptr };
    resource_type_id_t type{ 0u };
    dependency_payload_t payloads[2]{ { 0u, {} }, { 1u, {} } };
    u32 cLoads{ 0u };
    u32 cUnloads{ 0u };
    u32 unloadOrder[4]{};
    bool_t bFailParent{ CY_FALSE };
    resource_error_t dependencyGetResult{ resource_error_t::INTERNAL_ERROR };
    resource_error_t dependencyReleaseResult{ resource_error_t::INTERNAL_ERROR };
};

bool_t DependencyLoad(
    void *pUserData,
    resource_id_t,
    resource_type_id_t,
    string_view_t path,
    void **ppResourceOut ) noexcept
{
    auto &backend = *static_cast<dependency_backend_t *>( pUserData );
    const bool_t bParent = StringView_Equals(
        path, StringView_FromCString( "parent" ) );
    dependency_payload_t &payload = backend.payloads[bParent ? 0u : 1u];
    ++backend.cLoads;
    // Even a failed parent transfers its partial payload so its unload callback
    // can release the successfully acquired child.
    *ppResourceOut = &payload;
    if ( bParent ) {
        const resource_error_t result = Res_Acquire(
            backend.pManager, backend.type,
            StringView_FromCString( "child" ), &payload.dependency );
        return result == resource_error_t::OK && !backend.bFailParent;
    }
    return CY_TRUE;
}

void DependencyUnload( void *pUserData, void *pResource ) noexcept
{
    auto &backend = *static_cast<dependency_backend_t *>( pUserData );
    auto &payload = *static_cast<dependency_payload_t *>( pResource );
    backend.unloadOrder[backend.cUnloads++] = payload.iNode;
    if ( ResourceHandle_IsValid( payload.dependency ) ) {
        void *pDependency = nullptr;
        backend.dependencyGetResult = Res_Get(
            backend.pManager, payload.dependency, &pDependency );
        backend.dependencyReleaseResult = Res_Release(
            backend.pManager, payload.dependency );
        payload.dependency = CY_RESOURCE_HANDLE_INVALID;
    }
}

struct lifecycle_probe_t {
    resource_error_t init{ resource_error_t::OK };
    resource_error_t shutdown{ resource_error_t::OK };
    resource_error_t registerType{ resource_error_t::OK };
    resource_error_t unregisterType{ resource_error_t::OK };
    resource_type_slot_t typeSlot{ 1u };
};

struct callback_backend_t {
    resource_manager_t *pManager{ nullptr };
    resource_loader_t loader{};
    lifecycle_probe_t loadProbe{};
    lifecycle_probe_t unloadProbe{};
    resource_handle_t handle{};
    resource_handle_t reacquired{ ResourceHandle_Make( 0u, 1u, 1u ) };
    resource_error_t getDuringUnload{ resource_error_t::OK };
    resource_error_t retainDuringUnload{ resource_error_t::OK };
    resource_error_t releaseDuringUnload{ resource_error_t::OK };
    resource_error_t acquireDuringUnload{ resource_error_t::OK };
    void *pBorrowedDuringUnload{ reinterpret_cast<void *>( 0x1u ) };
    u32 payload{ 42u };
};

void ProbeLifecycle(
    callback_backend_t &backend,
    lifecycle_probe_t &probe ) noexcept
{
    probe.init = Res_Init( backend.pManager, Res_DefaultConfig() );
    probe.shutdown = Res_Shutdown( backend.pManager );
    probe.registerType = Res_RegisterType(
        backend.pManager, backend.loader, &probe.typeSlot );
    probe.unregisterType = Res_UnregisterType(
        backend.pManager, backend.loader.type );
}

bool_t CallbackLoad(
    void *pUserData,
    resource_id_t,
    resource_type_id_t,
    string_view_t,
    void **ppResourceOut ) noexcept
{
    auto &backend = *static_cast<callback_backend_t *>( pUserData );
    ProbeLifecycle( backend, backend.loadProbe );
    *ppResourceOut = &backend.payload;
    return CY_TRUE;
}

void CallbackUnload( void *pUserData, void * ) noexcept
{
    auto &backend = *static_cast<callback_backend_t *>( pUserData );
    ProbeLifecycle( backend, backend.unloadProbe );
    backend.getDuringUnload = Res_Get(
        backend.pManager, backend.handle, &backend.pBorrowedDuringUnload );
    backend.retainDuringUnload = Res_Retain( backend.pManager, backend.handle );
    backend.releaseDuringUnload = Res_Release( backend.pManager, backend.handle );
    backend.acquireDuringUnload = Res_Acquire(
        backend.pManager, backend.loader.type,
        StringView_FromCString( "callback" ), &backend.reacquired );
}

void CheckLifecycleProbe( const lifecycle_probe_t &probe )
{
    CHECK( probe.init == resource_error_t::ALREADY_INITIALIZED );
    CHECK( probe.shutdown == resource_error_t::REENTRANT_LIFECYCLE );
    CHECK( probe.registerType == resource_error_t::REENTRANT_LIFECYCLE );
    CHECK( probe.unregisterType == resource_error_t::REENTRANT_LIFECYCLE );
    CHECK( probe.typeSlot == CY_RESOURCE_TYPE_SLOT_INVALID );
}

struct allocation_probe_t {
    u32 cAllocations{ 0u };
    u32 cFrees{ 0u };
    void *pAllocation{ nullptr };
    usize cbAllocation{ 0u };
    usize nAlignment{ 0u };
    bool_t bFreeMetadataMatched{ CY_FALSE };
};

void *AllocateOnce( void *pUserData, usize cbSize, usize nAlignment ) noexcept
{
    auto &probe = *static_cast<allocation_probe_t *>( pUserData );
    if ( ++probe.cAllocations != 1u ) {
        return nullptr;
    }
    probe.pAllocation = Allocator_Allocate(
        Allocator_GetSystem(), cbSize, nAlignment );
    probe.cbAllocation = cbSize;
    probe.nAlignment = nAlignment;
    return probe.pAllocation;
}

void FreeTracked(
    void *pUserData, void *pMemory, usize cbSize, usize nAlignment ) noexcept
{
    auto &probe = *static_cast<allocation_probe_t *>( pUserData );
    ++probe.cFrees;
    probe.bFreeMetadataMatched = pMemory == probe.pAllocation &&
        cbSize == probe.cbAllocation && nAlignment == probe.nAlignment;
    Allocator_Free( Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "Resource manager initializes transactionally",
           "[CypherEngine][Resource]" )
{
    resource_manager_t manager{};
    REQUIRE_FALSE( Res_IsInitialized( &manager ) );
    REQUIRE( Res_Shutdown( &manager ) ==
             resource_error_t::NOT_INITIALIZED );

    resource_manager_config_t invalid = TestConfig();
    invalid.cResourceCapacity = 0u;
    REQUIRE( Res_Init( &manager, invalid ) ==
             resource_error_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( Res_IsInitialized( &manager ) );

    const allocator_t failingAllocator{
        FailAllocate,
        nullptr,
        NoopFree,
        nullptr
    };
    resource_manager_config_t allocationFailure = TestConfig();
    allocationFailure.pAllocator = &failingAllocator;
    REQUIRE( Res_Init( &manager, allocationFailure ) ==
             resource_error_t::ALLOCATION_FAILED );
    REQUIRE_FALSE( Res_IsInitialized( &manager ) );

    REQUIRE( Res_Init( &manager, TestConfig() ) ==
             resource_error_t::OK );
    REQUIRE( Res_IsInitialized( &manager ) );
    REQUIRE( Res_Init( &manager, TestConfig() ) ==
             resource_error_t::ALREADY_INITIALIZED );

    const resource_manager_stats_t stats = Res_GetStats( &manager );
    REQUIRE( stats.cResourceCapacity == 8u );
    REQUIRE( stats.cTypeCapacity == 4u );
    REQUIRE( stats.cLiveResources == 0u );

    REQUIRE( Res_Shutdown( &manager ) == resource_error_t::OK );
    REQUIRE_FALSE( Res_IsInitialized( &manager ) );
}

TEST_CASE( "Resource types have stable non-reused runtime slots",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 4u, 2u ) };
    test_backend_t backendA{};
    test_backend_t backendB{};
    test_backend_t backendC{};
    const resource_type_id_t typeA = TestType( "shader" );
    const resource_type_id_t typeB = TestType( "texture" );
    const resource_type_id_t typeC = TestType( "material" );

    resource_type_slot_t slotA = CY_RESOURCE_TYPE_SLOT_INVALID;
    resource_type_slot_t slotB = CY_RESOURCE_TYPE_SLOT_INVALID;
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backendA, typeA ), &slotA ) ==
        resource_error_t::OK );
    REQUIRE( slotA == 1u );
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backendB, typeB ), &slotB ) ==
        resource_error_t::OK );
    REQUIRE( slotB == 2u );
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backendC, typeC ) ) ==
        resource_error_t::TYPE_CAPACITY_EXCEEDED );
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backendA, typeA ) ) ==
        resource_error_t::TYPE_ALREADY_REGISTERED );

    resource_loader_t invalidLoader{};
    invalidLoader.type = TestType( "invalid" );
    REQUIRE( Res_RegisterType( &scope.manager, invalidLoader ) ==
             resource_error_t::INVALID_ARGUMENT );

    REQUIRE( Res_UnregisterType( &scope.manager, typeA ) ==
             resource_error_t::OK );
    resource_type_slot_t slotC = CY_RESOURCE_TYPE_SLOT_INVALID;
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backendC, typeC ), &slotC ) ==
        resource_error_t::OK );
    REQUIRE( slotC == 3u );
    REQUIRE( Res_UnregisterType( &scope.manager, typeA ) ==
             resource_error_t::TYPE_NOT_REGISTERED );
}

TEST_CASE( "Acquire caches identity and reference counts gate unloading",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig() };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    const string_view_t path = StringView_FromCString(
        "shaders/world/basic.cyshader" );
    resource_handle_t first{};
    REQUIRE( Res_Acquire( &scope.manager, type, path, &first ) ==
             resource_error_t::OK );
    REQUIRE( ResourceHandle_IsValid( first ) );
    REQUIRE( backend.cLoads == 1u );

    void *pPayload = nullptr;
    REQUIRE( Res_Get( &scope.manager, first, &pPayload ) ==
             resource_error_t::OK );
    REQUIRE( pPayload == &backend.payloads[0] );
    REQUIRE( backend.payloads[0].id.value != 0u );

    resource_handle_t second{};
    REQUIRE( Res_Acquire( &scope.manager, type, path, &second ) ==
             resource_error_t::OK );
    REQUIRE( ResourceHandle_Equals( first, second ) );
    REQUIRE( backend.cLoads == 1u );
    REQUIRE( Res_Retain( &scope.manager, first ) ==
             resource_error_t::OK );

    resource_info_t info{};
    REQUIRE( Res_GetInfo( &scope.manager, first, &info ) ==
             resource_error_t::OK );
    REQUIRE( info.state == resource_state_t::READY );
    REQUIRE( info.cReferences == 3u );
    REQUIRE( info.type == type );
    REQUIRE( std::strcmp( info.szVirtualPath,
                          "shaders/world/basic.cyshader" ) == 0 );

    REQUIRE( Res_UnregisterType( &scope.manager, type ) ==
             resource_error_t::TYPE_IN_USE );
    REQUIRE( Res_Release( &scope.manager, first ) ==
             resource_error_t::OK );
    REQUIRE( Res_Release( &scope.manager, second ) ==
             resource_error_t::OK );
    REQUIRE( backend.cUnloads == 0u );
    REQUIRE( Res_IsAlive( &scope.manager, first ) );
    REQUIRE( Res_Release( &scope.manager, first ) ==
             resource_error_t::OK );
    REQUIRE( backend.cUnloads == 1u );
    REQUIRE_FALSE( Res_IsAlive( &scope.manager, first ) );

    pPayload = reinterpret_cast<void *>( 0x1u );
    REQUIRE( Res_Get( &scope.manager, first, &pPayload ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( pPayload == nullptr );

    const resource_manager_stats_t stats =
        Res_GetStats( &scope.manager );
    REQUIRE( stats.cLoadAttempts == 1u );
    REQUIRE( stats.cSuccessfulLoads == 1u );
    REQUIRE( stats.cCacheHits == 1u );
    REQUIRE( stats.cUnloads == 1u );
    REQUIRE( stats.cLiveResources == 0u );
    REQUIRE( stats.cPeakLiveResources == 1u );
}

TEST_CASE( "Recycled slots advance generation and reject stale handles",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t oldHandle{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "materials/old.cymat" ),
        &oldHandle ) == resource_error_t::OK );
    REQUIRE( Res_Release( &scope.manager, oldHandle ) ==
             resource_error_t::OK );

    resource_handle_t newHandle{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "materials/new.cymat" ),
        &newHandle ) == resource_error_t::OK );
    REQUIRE( ResourceHandle_Slot( oldHandle ) == ResourceHandle_Slot( newHandle ) );
    REQUIRE( ResourceHandle_Generation( oldHandle ) !=
             ResourceHandle_Generation( newHandle ) );
    REQUIRE_FALSE( ResourceHandle_Equals( oldHandle, newHandle ) );
    REQUIRE( Res_Retain( &scope.manager, oldHandle ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( Res_Release( &scope.manager, newHandle ) ==
             resource_error_t::OK );
}

TEST_CASE( "Capacity and failed loads roll back reserved slots",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t first{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/first.cytex" ),
        &first ) == resource_error_t::OK );

    resource_handle_t rejected = first;
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/second.cytex" ),
        &rejected ) == resource_error_t::CAPACITY_EXCEEDED );
    REQUIRE_FALSE( ResourceHandle_IsValid( rejected ) );
    REQUIRE( Res_Release( &scope.manager, first ) ==
             resource_error_t::OK );

    backend.bFailLoad = CY_TRUE;
    backend.bReturnPayloadOnFailure = CY_TRUE;
    resource_handle_t failed{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/broken.cytex" ),
        &failed ) == resource_error_t::LOAD_FAILED );
    REQUIRE_FALSE( ResourceHandle_IsValid( failed ) );
    REQUIRE( backend.cUnloads == 2u );

    backend.bFailLoad = CY_FALSE;
    backend.bReturnPayloadOnFailure = CY_FALSE;
    resource_handle_t recovered{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/recovered.cytex" ),
        &recovered ) == resource_error_t::OK );
    REQUIRE( Res_Release( &scope.manager, recovered ) ==
             resource_error_t::OK );

    const resource_manager_stats_t stats =
        Res_GetStats( &scope.manager );
    REQUIRE( stats.cLoadAttempts == 3u );
    REQUIRE( stats.cSuccessfulLoads == 2u );
    REQUIRE( stats.cFailedLoads == 1u );
}

TEST_CASE( "Distinct resource churn preserves lookup and slot reuse",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 4u, 1u ) };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    char szPath[64]{};
    resource_handle_t previous{};
    for ( u32 iResource = 0u; iResource < 512u; ++iResource ) {
        const int cchPath = std::snprintf(
            szPath,
            sizeof( szPath ),
            "generated/churn_%u.cyres",
            iResource );
        REQUIRE( cchPath > 0 );
        REQUIRE( static_cast<usize>( cchPath ) < sizeof( szPath ) );

        resource_handle_t handle{};
        REQUIRE( Res_Acquire(
            &scope.manager,
            type,
            { szPath, static_cast<usize>( cchPath ) },
            &handle ) == resource_error_t::OK );
        REQUIRE( ResourceHandle_Slot( handle ) == 0u );
        if ( ResourceHandle_IsValid( previous ) ) {
            REQUIRE_FALSE( ResourceHandle_Equals( handle, previous ) );
            REQUIRE( Res_Retain( &scope.manager, previous ) ==
                     resource_error_t::INVALID_HANDLE );
        }

        previous = handle;
        REQUIRE( Res_Release( &scope.manager, handle ) ==
                 resource_error_t::OK );
    }

    const resource_manager_stats_t stats =
        Res_GetStats( &scope.manager );
    REQUIRE( stats.cSuccessfulLoads == 512u );
    REQUIRE( stats.cUnloads == 512u );
    REQUIRE( stats.cLiveResources == 0u );
}

TEST_CASE( "Lookup cluster deletion preserves colliding resources",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 4u, 1u ) };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    // Four resource slots produce an eight-entry lookup table. Choose three
    // identities whose home bucket is the final entry so their probe cluster
    // wraps across the end of the table.
    char szPaths[3][64]{};
    u32 cPaths = 0u;
    for ( u32 iCandidate = 0u;
          iCandidate < 1024u && cPaths < 3u;
          ++iCandidate ) {
        char szCandidate[64]{};
        const int cchCandidate = std::snprintf(
            szCandidate,
            sizeof( szCandidate ),
            "generated/collision_%u.cyres",
            iCandidate );
        REQUIRE( cchCandidate > 0 );
        REQUIRE( static_cast<usize>( cchCandidate ) < sizeof( szCandidate ) );

        const string_view_t candidate{
            szCandidate,
            static_cast<usize>( cchCandidate )
        };
        const resource_id_t id = ResourceId_FromPath( candidate, type );
        if ( TestLookupBucket( id, 8u ) == 7u ) {
            std::memcpy(
                szPaths[cPaths],
                szCandidate,
                static_cast<usize>( cchCandidate ) + 1u );
            ++cPaths;
        }
    }
    REQUIRE( cPaths == 3u );

    resource_handle_t handles[3]{};
    for ( u32 iPath = 0u; iPath < 3u; ++iPath ) {
        REQUIRE( Res_Acquire(
            &scope.manager,
            type,
            StringView_FromCString( szPaths[iPath] ),
            &handles[iPath] ) == resource_error_t::OK );
    }

    REQUIRE( Res_Release( &scope.manager, handles[1] ) ==
             resource_error_t::OK );
    REQUIRE( Res_IsAlive( &scope.manager, handles[0] ) );
    REQUIRE( Res_IsAlive( &scope.manager, handles[2] ) );

    void *pResource = nullptr;
    REQUIRE( Res_Get( &scope.manager, handles[2], &pResource ) ==
             resource_error_t::OK );
    REQUIRE( pResource != nullptr );
    REQUIRE( Res_Release( &scope.manager, handles[0] ) ==
             resource_error_t::OK );
    REQUIRE( Res_Release( &scope.manager, handles[2] ) ==
             resource_error_t::OK );
}

TEST_CASE( "Recursive acquisition of the loading identity reports a cycle",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig() };
    test_backend_t backend{};
    backend.pManager = &scope.manager;
    backend.bAcquireSelf = CY_TRUE;
    backend.bFailLoad = CY_TRUE;
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t handle{};
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "models/cyclic.cymesh" ),
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE( backend.recursiveResult == resource_error_t::DEPENDENCY_CYCLE );
    REQUIRE_FALSE( ResourceHandle_IsValid( handle ) );
    REQUIRE( Res_GetStats( &scope.manager ).cLiveResources == 0u );
}

TEST_CASE( "Shutdown force-unloads resources in reverse load order",
           "[CypherEngine][Resource]" )
{
    resource_manager_t manager{};
    REQUIRE( Res_Init( &manager, TestConfig() ) == resource_error_t::OK );
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t first{};
    resource_handle_t second{};
    resource_handle_t duplicate{};
    REQUIRE( Res_Acquire(
        &manager, type, StringView_FromCString( "a" ), &first ) ==
        resource_error_t::OK );
    REQUIRE( Res_Acquire(
        &manager, type, StringView_FromCString( "b" ), &second ) ==
        resource_error_t::OK );
    REQUIRE( Res_Acquire(
        &manager, type, StringView_FromCString( "a" ), &duplicate ) ==
        resource_error_t::OK );

    REQUIRE( Res_Shutdown( &manager ) == resource_error_t::OK );
    REQUIRE( backend.cUnloads == 2u );
    REQUIRE( backend.unloadOrder[0] == 2u );
    REQUIRE( backend.unloadOrder[1] == 1u );
    REQUIRE_FALSE( Res_IsInitialized( &manager ) );
}

TEST_CASE( "Public operations reject invalid paths and reset outputs",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig() };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t output = ResourceHandle_Make( 1u, 1u, 1u );
    REQUIRE( Res_Acquire(
        &scope.manager, type, {}, &output ) ==
        resource_error_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    const char embeddedNull[]{ 'a', '\0', 'b' };
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        { embeddedNull, sizeof( embeddedNull ) },
        &output ) == resource_error_t::INVALID_ARGUMENT );

    char tooLong[CYPHER_RESOURCE_PATH_BUFFER_SIZE + 1u]{};
    std::memset( tooLong, 'x', sizeof( tooLong ) );
    REQUIRE( Res_Acquire(
        &scope.manager,
        type,
        { tooLong, sizeof( tooLong ) },
        &output ) == resource_error_t::PATH_TOO_LONG );

    REQUIRE( Res_Acquire(
        &scope.manager,
        TestType( "missing" ),
        StringView_FromCString( "missing" ),
        &output ) == resource_error_t::TYPE_NOT_REGISTERED );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    REQUIRE( std::strcmp(
        Res_ErrorName( resource_error_t::INVALID_HANDLE ),
        "INVALID_HANDLE" ) == 0 );
}

TEST_CASE( "Dependent resources remain usable during parent teardown",
           "[CypherEngine][Resource][Lifetime]" )
{
    dependency_backend_t backend{};
    manager_scope_t scope{ TestConfig( 2u, 1u ) };
    backend.pManager = &scope.manager;
    backend.type = TestType();
    const resource_loader_t loader{
        backend.type, DependencyLoad, DependencyUnload, &backend
    };
    REQUIRE( Res_RegisterType( &scope.manager, loader ) == resource_error_t::OK );

    resource_handle_t parent{};
    REQUIRE( Res_Acquire(
        &scope.manager, backend.type,
        StringView_FromCString( "parent" ), &parent ) == resource_error_t::OK );
    REQUIRE( backend.cLoads == 2u );
    REQUIRE( Res_GetStats( &scope.manager ).cLiveResources == 2u );
    const resource_handle_t child = backend.payloads[0].dependency;
    REQUIRE( Res_IsAlive( &scope.manager, child ) );

    SECTION( "Final release recursively unloads the owned dependency" )
    {
        REQUIRE( Res_Release( &scope.manager, parent ) == resource_error_t::OK );
        const resource_manager_stats_t stats = Res_GetStats( &scope.manager );
        REQUIRE( stats.cLiveResources == 0u );
        REQUIRE( stats.cPeakLiveResources == 2u );
        REQUIRE( stats.cUnloads == 2u );
    }
    SECTION( "Forced shutdown permits releasing dependencies in unload callbacks" )
    {
        REQUIRE( Res_Shutdown( &scope.manager ) == resource_error_t::OK );
    }

    REQUIRE( backend.cUnloads == 2u );
    REQUIRE( backend.unloadOrder[0] == 0u );
    REQUIRE( backend.unloadOrder[1] == 1u );
    REQUIRE( backend.dependencyGetResult == resource_error_t::OK );
    REQUIRE( backend.dependencyReleaseResult == resource_error_t::OK );
    REQUIRE_FALSE( Res_IsAlive( &scope.manager, parent ) );
    REQUIRE_FALSE( Res_IsAlive( &scope.manager, child ) );
}

TEST_CASE( "Failed parent loads clean dependencies and allow retrying the same identity",
           "[CypherEngine][Resource][Lifetime]" )
{
    dependency_backend_t backend{};
    manager_scope_t scope{ TestConfig( 2u, 1u ) };
    backend.pManager = &scope.manager;
    backend.type = TestType();
    backend.bFailParent = CY_TRUE;
    const resource_loader_t loader{
        backend.type, DependencyLoad, DependencyUnload, &backend
    };
    REQUIRE( Res_RegisterType( &scope.manager, loader ) == resource_error_t::OK );

    resource_handle_t parent = ResourceHandle_Make( 0u, 1u, 1u );
    const string_view_t path = StringView_FromCString( "parent" );
    REQUIRE( Res_Acquire( &scope.manager, backend.type, path, &parent ) ==
             resource_error_t::LOAD_FAILED );
    REQUIRE_FALSE( ResourceHandle_IsValid( parent ) );
    REQUIRE( backend.cUnloads == 2u );
    REQUIRE( backend.dependencyGetResult == resource_error_t::OK );
    REQUIRE( backend.dependencyReleaseResult == resource_error_t::OK );
    const resource_manager_stats_t failedStats = Res_GetStats( &scope.manager );
    REQUIRE( failedStats.cLiveResources == 0u );
    REQUIRE( failedStats.cLoadAttempts == 2u );
    REQUIRE( failedStats.cSuccessfulLoads == 1u );
    REQUIRE( failedStats.cFailedLoads == 1u );
    // Failed parent cleanup is not a published-resource unload statistic.
    REQUIRE( failedStats.cUnloads == 1u );

    backend.bFailParent = CY_FALSE;
    REQUIRE( Res_Acquire( &scope.manager, backend.type, path, &parent ) ==
             resource_error_t::OK );
    REQUIRE( Res_Release( &scope.manager, parent ) == resource_error_t::OK );
    REQUIRE( backend.cLoads == 4u );
    REQUIRE( backend.cUnloads == 4u );
    REQUIRE( Res_GetStats( &scope.manager ).cLiveResources == 0u );
}

TEST_CASE( "Callbacks cannot mutate manager lifecycle or resurrect an unloading resource",
           "[CypherEngine][Resource][Lifetime]" )
{
    callback_backend_t backend{};
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    backend.pManager = &scope.manager;
    backend.loader = { TestType(), CallbackLoad, CallbackUnload, &backend };
    REQUIRE( Res_RegisterType( &scope.manager, backend.loader ) ==
             resource_error_t::OK );
    REQUIRE( Res_Acquire(
        &scope.manager, backend.loader.type,
        StringView_FromCString( "callback" ), &backend.handle ) ==
        resource_error_t::OK );
    CheckLifecycleProbe( backend.loadProbe );

    SECTION( "Final release" )
    {
        REQUIRE( Res_Release( &scope.manager, backend.handle ) ==
                 resource_error_t::OK );
        REQUIRE( Res_GetStats( &scope.manager ).cRegisteredTypes == 1u );
        REQUIRE( Res_GetStats( &scope.manager ).cUnloads == 1u );
    }
    SECTION( "Forced shutdown" )
    {
        REQUIRE( Res_Shutdown( &scope.manager ) == resource_error_t::OK );
    }

    CheckLifecycleProbe( backend.unloadProbe );
    REQUIRE( backend.getDuringUnload == resource_error_t::RESOURCE_BUSY );
    REQUIRE( backend.retainDuringUnload == resource_error_t::RESOURCE_BUSY );
    REQUIRE( backend.releaseDuringUnload == resource_error_t::RESOURCE_BUSY );
    REQUIRE( backend.acquireDuringUnload == resource_error_t::RESOURCE_BUSY );
    REQUIRE( backend.pBorrowedDuringUnload == nullptr );
    REQUIRE_FALSE( ResourceHandle_IsValid( backend.reacquired ) );
}

TEST_CASE( "A loader reporting success without a payload rolls back its identity",
           "[CypherEngine][Resource]" )
{
    test_backend_t backend{};
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType( &scope.manager, TestLoader( backend, type ) ) ==
             resource_error_t::OK );
    backend.bReturnNullOnSuccess = CY_TRUE;
    resource_handle_t handle = ResourceHandle_Make( 0u, 1u, 1u );
    const string_view_t path = StringView_FromCString( "empty" );
    REQUIRE( Res_Acquire( &scope.manager, type, path, &handle ) ==
             resource_error_t::LOAD_FAILED );
    REQUIRE_FALSE( ResourceHandle_IsValid( handle ) );
    REQUIRE( backend.cUnloads == 0u );
    REQUIRE( Res_GetStats( &scope.manager ).cFailedLoads == 1u );
    REQUIRE( Res_GetStats( &scope.manager ).cLiveResources == 0u );

    backend.bReturnNullOnSuccess = CY_FALSE;
    REQUIRE( Res_Acquire( &scope.manager, type, path, &handle ) ==
             resource_error_t::OK );
    REQUIRE( backend.cLoads == 2u );
    REQUIRE( Res_Release( &scope.manager, handle ) == resource_error_t::OK );
    REQUIRE( backend.cUnloads == 1u );
}

TEST_CASE( "Resource manager uses one allocation with matching release metadata",
           "[CypherEngine][Resource][Allocation]" )
{
    allocation_probe_t probe{};
    const allocator_t allocator{ AllocateOnce, nullptr, FreeTracked, &probe };
    test_backend_t backend{};
    resource_manager_config_t config = TestConfig( 1u, 1u );
    config.pAllocator = &allocator;
    manager_scope_t scope{ config };
    REQUIRE( probe.cAllocations == 1u );
    REQUIRE( scope.manager.pAllocator == &allocator );
    REQUIRE( scope.manager.pImplementation == probe.pAllocation );
    REQUIRE( scope.manager.cbAllocation == probe.cbAllocation );

    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType( &scope.manager, TestLoader( backend, type ) ) ==
             resource_error_t::OK );
    const string_view_t path = StringView_FromCString( "bounded" );
    for ( u32 iLoad = 0u; iLoad < 16u; ++iLoad ) {
        resource_handle_t owner{};
        resource_handle_t cached{};
        REQUIRE( Res_Acquire( &scope.manager, type, path, &owner ) ==
                 resource_error_t::OK );
        REQUIRE( Res_Acquire( &scope.manager, type, path, &cached ) ==
                 resource_error_t::OK );
        REQUIRE( ResourceHandle_Equals( owner, cached ) );
        REQUIRE( Res_Release( &scope.manager, cached ) == resource_error_t::OK );
        REQUIRE( Res_Release( &scope.manager, owner ) == resource_error_t::OK );
    }
    REQUIRE( probe.cAllocations == 1u );
    REQUIRE( probe.cFrees == 0u );
    REQUIRE( Res_Shutdown( &scope.manager ) == resource_error_t::OK );
    REQUIRE( probe.cFrees == 1u );
    REQUIRE( probe.bFreeMetadataMatched );
    REQUIRE( scope.manager.pImplementation == nullptr );
    REQUIRE( scope.manager.pAllocator == nullptr );
    REQUIRE( scope.manager.cbAllocation == 0u );
}

TEST_CASE( "Wrong-type handles preserve live payloads and clear rejected outputs",
           "[CypherEngine][Resource]" )
{
    test_backend_t backend{};
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    const resource_type_id_t type = TestType();
    REQUIRE( Res_RegisterType( &scope.manager, TestLoader( backend, type ) ) ==
             resource_error_t::OK );
    resource_handle_t handle{};
    REQUIRE( Res_Acquire(
        &scope.manager, type, StringView_FromCString( "typed" ), &handle ) ==
        resource_error_t::OK );
    const resource_handle_t wrongType = ResourceHandle_Make(
        ResourceHandle_Slot( handle ), ResourceHandle_Generation( handle ),
        ResourceHandle_TypeSlot( handle ) + 1u );
    REQUIRE( Res_Retain( &scope.manager, wrongType ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( Res_Release( &scope.manager, wrongType ) ==
             resource_error_t::INVALID_HANDLE );
    void *pPayload = reinterpret_cast<void *>( 0x1u );
    REQUIRE( Res_Get( &scope.manager, wrongType, &pPayload ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( pPayload == nullptr );
    resource_info_t info{};
    info.cReferences = 42u;
    REQUIRE( Res_GetInfo( &scope.manager, wrongType, &info ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( info.cReferences == 0u );
    REQUIRE_FALSE( ResourceHandle_IsValid( info.handle ) );
    REQUIRE( Res_IsAlive( &scope.manager, handle ) );
    REQUIRE( Res_GetInfo( &scope.manager, handle, &info ) == resource_error_t::OK );
    REQUIRE( info.cReferences == 1u );
    REQUIRE( backend.cUnloads == 0u );
    REQUIRE( Res_Release( &scope.manager, handle ) == resource_error_t::OK );
}
