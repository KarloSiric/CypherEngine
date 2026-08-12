//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherEngine/CypherResource/CypherResource_Tests.cpp
//  Purpose: Tests synchronous runtime resource ownership and lookup.
//  Details: Coverage protects registration, cache identity, reference counts,
//           failure rollback, stale generations, shutdown order, capacity reuse,
//           dependency cycles, and public output transactions.
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
        backend.recursiveResult = CypherResource_Acquire(
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

    *ppResourceOut = &payload;
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
    resource_manager_config_t config = CypherResource_DefaultConfig();
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
        REQUIRE( CypherResource_Init( &manager, config ) == resource_error_t::OK );
    }

    ~manager_scope_t()
    {
        if ( CypherResource_IsInitialized( &manager ) ) {
            CHECK( CypherResource_Shutdown( &manager ) == resource_error_t::OK );
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

} // namespace

TEST_CASE( "Resource manager initializes transactionally",
           "[CypherEngine][Resource]" )
{
    resource_manager_t manager{};
    REQUIRE_FALSE( CypherResource_IsInitialized( &manager ) );
    REQUIRE( CypherResource_Shutdown( &manager ) ==
             resource_error_t::NOT_INITIALIZED );

    resource_manager_config_t invalid = TestConfig();
    invalid.cResourceCapacity = 0u;
    REQUIRE( CypherResource_Init( &manager, invalid ) ==
             resource_error_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( CypherResource_IsInitialized( &manager ) );

    const allocator_t failingAllocator{
        FailAllocate,
        nullptr,
        NoopFree,
        nullptr
    };
    resource_manager_config_t allocationFailure = TestConfig();
    allocationFailure.pAllocator = &failingAllocator;
    REQUIRE( CypherResource_Init( &manager, allocationFailure ) ==
             resource_error_t::ALLOCATION_FAILED );
    REQUIRE_FALSE( CypherResource_IsInitialized( &manager ) );

    REQUIRE( CypherResource_Init( &manager, TestConfig() ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_IsInitialized( &manager ) );
    REQUIRE( CypherResource_Init( &manager, TestConfig() ) ==
             resource_error_t::ALREADY_INITIALIZED );

    const resource_manager_stats_t stats = CypherResource_GetStats( &manager );
    REQUIRE( stats.cResourceCapacity == 8u );
    REQUIRE( stats.cTypeCapacity == 4u );
    REQUIRE( stats.cLiveResources == 0u );

    REQUIRE( CypherResource_Shutdown( &manager ) == resource_error_t::OK );
    REQUIRE_FALSE( CypherResource_IsInitialized( &manager ) );
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
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backendA, typeA ), &slotA ) ==
        resource_error_t::OK );
    REQUIRE( slotA == 1u );
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backendB, typeB ), &slotB ) ==
        resource_error_t::OK );
    REQUIRE( slotB == 2u );
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backendC, typeC ) ) ==
        resource_error_t::TYPE_CAPACITY_EXCEEDED );
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backendA, typeA ) ) ==
        resource_error_t::TYPE_ALREADY_REGISTERED );

    resource_loader_t invalidLoader{};
    invalidLoader.type = TestType( "invalid" );
    REQUIRE( CypherResource_RegisterType( &scope.manager, invalidLoader ) ==
             resource_error_t::INVALID_ARGUMENT );

    REQUIRE( CypherResource_UnregisterType( &scope.manager, typeA ) ==
             resource_error_t::OK );
    resource_type_slot_t slotC = CY_RESOURCE_TYPE_SLOT_INVALID;
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backendC, typeC ), &slotC ) ==
        resource_error_t::OK );
    REQUIRE( slotC == 3u );
    REQUIRE( CypherResource_UnregisterType( &scope.manager, typeA ) ==
             resource_error_t::TYPE_NOT_REGISTERED );
}

TEST_CASE( "Acquire caches identity and reference counts gate unloading",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig() };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    const string_view_t path = StringView_FromCString(
        "shaders/world/basic.cyshader" );
    resource_handle_t first{};
    REQUIRE( CypherResource_Acquire( &scope.manager, type, path, &first ) ==
             resource_error_t::OK );
    REQUIRE( ResourceHandle_IsValid( first ) );
    REQUIRE( backend.cLoads == 1u );

    void *pPayload = nullptr;
    REQUIRE( CypherResource_Get( &scope.manager, first, &pPayload ) ==
             resource_error_t::OK );
    REQUIRE( pPayload == &backend.payloads[0] );
    REQUIRE( backend.payloads[0].id.value != 0u );

    resource_handle_t second{};
    REQUIRE( CypherResource_Acquire( &scope.manager, type, path, &second ) ==
             resource_error_t::OK );
    REQUIRE( ResourceHandle_Equals( first, second ) );
    REQUIRE( backend.cLoads == 1u );
    REQUIRE( CypherResource_Retain( &scope.manager, first ) ==
             resource_error_t::OK );

    resource_info_t info{};
    REQUIRE( CypherResource_GetInfo( &scope.manager, first, &info ) ==
             resource_error_t::OK );
    REQUIRE( info.state == resource_state_t::READY );
    REQUIRE( info.cReferences == 3u );
    REQUIRE( info.type == type );
    REQUIRE( std::strcmp( info.szVirtualPath,
                          "shaders/world/basic.cyshader" ) == 0 );

    REQUIRE( CypherResource_UnregisterType( &scope.manager, type ) ==
             resource_error_t::TYPE_IN_USE );
    REQUIRE( CypherResource_Release( &scope.manager, first ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, second ) ==
             resource_error_t::OK );
    REQUIRE( backend.cUnloads == 0u );
    REQUIRE( CypherResource_IsAlive( &scope.manager, first ) );
    REQUIRE( CypherResource_Release( &scope.manager, first ) ==
             resource_error_t::OK );
    REQUIRE( backend.cUnloads == 1u );
    REQUIRE_FALSE( CypherResource_IsAlive( &scope.manager, first ) );

    pPayload = reinterpret_cast<void *>( 0x1u );
    REQUIRE( CypherResource_Get( &scope.manager, first, &pPayload ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( pPayload == nullptr );

    const resource_manager_stats_t stats =
        CypherResource_GetStats( &scope.manager );
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
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t oldHandle{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "materials/old.cymat" ),
        &oldHandle ) == resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, oldHandle ) ==
             resource_error_t::OK );

    resource_handle_t newHandle{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "materials/new.cymat" ),
        &newHandle ) == resource_error_t::OK );
    REQUIRE( ResourceHandle_Slot( oldHandle ) == ResourceHandle_Slot( newHandle ) );
    REQUIRE( ResourceHandle_Generation( oldHandle ) !=
             ResourceHandle_Generation( newHandle ) );
    REQUIRE_FALSE( ResourceHandle_Equals( oldHandle, newHandle ) );
    REQUIRE( CypherResource_Retain( &scope.manager, oldHandle ) ==
             resource_error_t::INVALID_HANDLE );
    REQUIRE( CypherResource_Release( &scope.manager, newHandle ) ==
             resource_error_t::OK );
}

TEST_CASE( "Capacity and failed loads roll back reserved slots",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig( 1u, 1u ) };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t first{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/first.cytex" ),
        &first ) == resource_error_t::OK );

    resource_handle_t rejected = first;
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/second.cytex" ),
        &rejected ) == resource_error_t::CAPACITY_EXCEEDED );
    REQUIRE_FALSE( ResourceHandle_IsValid( rejected ) );
    REQUIRE( CypherResource_Release( &scope.manager, first ) ==
             resource_error_t::OK );

    backend.bFailLoad = CY_TRUE;
    backend.bReturnPayloadOnFailure = CY_TRUE;
    resource_handle_t failed{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/broken.cytex" ),
        &failed ) == resource_error_t::LOAD_FAILED );
    REQUIRE_FALSE( ResourceHandle_IsValid( failed ) );
    REQUIRE( backend.cUnloads == 2u );

    backend.bFailLoad = CY_FALSE;
    backend.bReturnPayloadOnFailure = CY_FALSE;
    resource_handle_t recovered{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "textures/recovered.cytex" ),
        &recovered ) == resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, recovered ) ==
             resource_error_t::OK );

    const resource_manager_stats_t stats =
        CypherResource_GetStats( &scope.manager );
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
    REQUIRE( CypherResource_RegisterType(
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
        REQUIRE( CypherResource_Acquire(
            &scope.manager,
            type,
            { szPath, static_cast<usize>( cchPath ) },
            &handle ) == resource_error_t::OK );
        REQUIRE( ResourceHandle_Slot( handle ) == 0u );
        if ( ResourceHandle_IsValid( previous ) ) {
            REQUIRE_FALSE( ResourceHandle_Equals( handle, previous ) );
            REQUIRE( CypherResource_Retain( &scope.manager, previous ) ==
                     resource_error_t::INVALID_HANDLE );
        }

        previous = handle;
        REQUIRE( CypherResource_Release( &scope.manager, handle ) ==
                 resource_error_t::OK );
    }

    const resource_manager_stats_t stats =
        CypherResource_GetStats( &scope.manager );
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
    REQUIRE( CypherResource_RegisterType(
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
        REQUIRE( CypherResource_Acquire(
            &scope.manager,
            type,
            StringView_FromCString( szPaths[iPath] ),
            &handles[iPath] ) == resource_error_t::OK );
    }

    REQUIRE( CypherResource_Release( &scope.manager, handles[1] ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_IsAlive( &scope.manager, handles[0] ) );
    REQUIRE( CypherResource_IsAlive( &scope.manager, handles[2] ) );

    void *pResource = nullptr;
    REQUIRE( CypherResource_Get( &scope.manager, handles[2], &pResource ) ==
             resource_error_t::OK );
    REQUIRE( pResource != nullptr );
    REQUIRE( CypherResource_Release( &scope.manager, handles[0] ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, handles[2] ) ==
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
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t handle{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        StringView_FromCString( "models/cyclic.cymesh" ),
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE( backend.recursiveResult == resource_error_t::DEPENDENCY_CYCLE );
    REQUIRE_FALSE( ResourceHandle_IsValid( handle ) );
    REQUIRE( CypherResource_GetStats( &scope.manager ).cLiveResources == 0u );
}

TEST_CASE( "Shutdown force-unloads resources in reverse load order",
           "[CypherEngine][Resource]" )
{
    resource_manager_t manager{};
    REQUIRE( CypherResource_Init( &manager, TestConfig() ) == resource_error_t::OK );
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( CypherResource_RegisterType(
        &manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t first{};
    resource_handle_t second{};
    resource_handle_t duplicate{};
    REQUIRE( CypherResource_Acquire(
        &manager, type, StringView_FromCString( "a" ), &first ) ==
        resource_error_t::OK );
    REQUIRE( CypherResource_Acquire(
        &manager, type, StringView_FromCString( "b" ), &second ) ==
        resource_error_t::OK );
    REQUIRE( CypherResource_Acquire(
        &manager, type, StringView_FromCString( "a" ), &duplicate ) ==
        resource_error_t::OK );

    REQUIRE( CypherResource_Shutdown( &manager ) == resource_error_t::OK );
    REQUIRE( backend.cUnloads == 2u );
    REQUIRE( backend.unloadOrder[0] == 2u );
    REQUIRE( backend.unloadOrder[1] == 1u );
    REQUIRE_FALSE( CypherResource_IsInitialized( &manager ) );
}

TEST_CASE( "Public operations reject invalid paths and reset outputs",
           "[CypherEngine][Resource]" )
{
    manager_scope_t scope{ TestConfig() };
    test_backend_t backend{};
    const resource_type_id_t type = TestType();
    REQUIRE( CypherResource_RegisterType(
        &scope.manager, TestLoader( backend, type ) ) == resource_error_t::OK );

    resource_handle_t output = ResourceHandle_Make( 1u, 1u, 1u );
    REQUIRE( CypherResource_Acquire(
        &scope.manager, type, {}, &output ) ==
        resource_error_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    const char embeddedNull[]{ 'a', '\0', 'b' };
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        { embeddedNull, sizeof( embeddedNull ) },
        &output ) == resource_error_t::INVALID_ARGUMENT );

    char tooLong[CYPHER_RESOURCE_PATH_BUFFER_SIZE + 1u]{};
    std::memset( tooLong, 'x', sizeof( tooLong ) );
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        type,
        { tooLong, sizeof( tooLong ) },
        &output ) == resource_error_t::PATH_TOO_LONG );

    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        TestType( "missing" ),
        StringView_FromCString( "missing" ),
        &output ) == resource_error_t::TYPE_NOT_REGISTERED );
    REQUIRE_FALSE( ResourceHandle_IsValid( output ) );

    REQUIRE( std::strcmp(
        CypherResource_ErrorName( resource_error_t::INVALID_HANDLE ),
        "INVALID_HANDLE" ) == 0 );
}
