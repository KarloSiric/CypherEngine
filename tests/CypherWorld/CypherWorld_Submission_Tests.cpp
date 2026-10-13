//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests bounded view submission lifetime and logical asset preflight.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherWorld/Submission/CypherWorld_Submission.h"
#include "CypherCommon/Mathlib/CypherMath_Intersection.h"

#include <catch2/catch_test_macros.hpp>

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace world = ::cypher::engine::world;

namespace
{

world::world_view_query_t MakeView() noexcept
{
    world::world_view_query_t query{};
    constexpr common::f32 extent = 20.0f;
    query.frustum.planes[0] = math::Plane_Make( { 1.0f, 0.0f, 0.0f }, extent );
    query.frustum.planes[1] = math::Plane_Make( { -1.0f, 0.0f, 0.0f }, extent );
    query.frustum.planes[2] = math::Plane_Make( { 0.0f, 1.0f, 0.0f }, extent );
    query.frustum.planes[3] = math::Plane_Make( { 0.0f, -1.0f, 0.0f }, extent );
    query.frustum.planes[4] = math::Plane_Make( { 0.0f, 0.0f, 1.0f }, extent );
    query.frustum.planes[5] = math::Plane_Make( { 0.0f, 0.0f, -1.0f }, extent );
    query.cameraPosition = { 0.0f, 0.0f, 0.0f };
    query.maxDistance = 100.0f;
    query.layerMask = 1u;
    return query;
}

world::world_object_t MakeObject(
    common::u32 meshSlot,
    common::u32 materialSlot ) noexcept
{
    world::world_object_t object{};
    object.worldBounds = math::Aabb_Make(
        { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } );
    object.layerMask = 1u;
    object.meshSlot = meshSlot;
    object.materialSlot = materialSlot;
    return object;
}

struct counting_allocator_t {
    const common::allocator_t *pSystem{ common::Allocator_GetSystem() };
    common::allocator_t allocator{};
    common::u32 attempts{ 0u };
    common::u32 allocations{ 0u };
    common::u32 frees{ 0u };
    common::u32 failAt{ 0u };

    counting_allocator_t() noexcept
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = [](
            void *pUserData, common::usize size,
            common::usize alignment ) noexcept -> void * {
            auto &self = *static_cast<counting_allocator_t *>( pUserData );
            ++self.attempts;
            if ( self.attempts == self.failAt ) {
                return nullptr;
            }
            void *pMemory = self.pSystem->pfnAllocate(
                self.pSystem->pUserData, size, alignment );
            if ( pMemory != nullptr ) {
                ++self.allocations;
            }
            return pMemory;
        };
        allocator.pfnFree = [](
            void *pUserData, void *pMemory,
            common::usize size, common::usize alignment ) noexcept {
            auto &self = *static_cast<counting_allocator_t *>( pUserData );
            ++self.frees;
            self.pSystem->pfnFree(
                self.pSystem->pUserData, pMemory, size, alignment );
        };
    }
};

// A deliberately small stand-in for the later Render consumer. Every logical
// mesh/material slot must resolve before any draw is recorded.
bool FakeSubmitResolved(
    const world::world_submission_t *pSubmission,
    world::world_submission_token_t token,
    const bool *pMeshes,
    common::u32 meshCount,
    const bool *pMaterials,
    common::u32 materialCount,
    common::u32 *pDrawCount ) noexcept
{
    common::u32 count = 0u;
    if ( world::WorldSubmission_Count( pSubmission, token, &count ) !=
         world::world_submission_error_t::OK ) {
        return false;
    }
    for ( common::u32 i = 0u; i < count; ++i ) {
        world::world_object_snapshot_t record{};
        if ( world::WorldSubmission_Get( pSubmission, token, i, &record ) !=
                 world::world_submission_error_t::OK ||
             record.object.meshSlot >= meshCount ||
             record.object.materialSlot >= materialCount ||
             !pMeshes[record.object.meshSlot] ||
             !pMaterials[record.object.materialSlot] ) {
            return false;
        }
    }
    for ( common::u32 i = 0u; i < count; ++i ) {
        ++*pDrawCount;
    }
    return true;
}

} // namespace

TEST_CASE( "World submission copies records and invalidates generations" )
{
    world::world_t *pWorld = nullptr;
    world::world_submission_t *pSubmission = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 3u ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Init(
        &pSubmission, common::Allocator_GetSystem(), 3u ) ==
             world::world_submission_error_t::OK );

    const world::world_object_t first = MakeObject( 1u, 1u );
    const world::world_object_t second = MakeObject( 2u, 2u );
    world::world_object_id_t firstId{};
    world::world_object_id_t secondId{};
    REQUIRE( world::World_AddObject( pWorld, &first, &firstId ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &second, &secondId ) == world::world_error_t::OK );
    const world::world_view_query_t query = MakeView();

    world::world_submission_token_t firstToken{};
    REQUIRE( world::WorldSubmission_Build(
        pSubmission, pWorld, &query, &firstToken ) ==
             world::world_submission_error_t::OK );
    REQUIRE( firstToken.generation != 0u );
    common::u32 count = 0u;
    REQUIRE( world::WorldSubmission_Count( pSubmission, firstToken, &count ) ==
             world::world_submission_error_t::OK );
    REQUIRE( count == 2u );

    // The active token survives a rejected attempt to overwrite its storage.
    REQUIRE( world::WorldSubmission_Build(
        pSubmission, pWorld, &query, &firstToken ) ==
             world::world_submission_error_t::ACTIVE_SUBMISSION );
    REQUIRE( world::WorldSubmission_Count( pSubmission, firstToken, &count ) ==
             world::world_submission_error_t::OK );

    REQUIRE( world::World_RemoveObject( pWorld, secondId ) == world::world_error_t::OK );
    world::World_Shutdown( &pWorld );
    world::world_object_snapshot_t record{};
    REQUIRE( world::WorldSubmission_Get( pSubmission, firstToken, 1u, &record ) ==
             world::world_submission_error_t::OK );
    REQUIRE( record.object.meshSlot == 2u );
    REQUIRE( record.id.incarnation == secondId.incarnation );
    REQUIRE( world::WorldSubmission_Get( pSubmission, firstToken, 2u, &record ) ==
             world::world_submission_error_t::INVALID_ARGUMENT );

    world::WorldSubmission_End( pSubmission );
    REQUIRE( world::WorldSubmission_Count( pSubmission, firstToken, &count ) ==
             world::world_submission_error_t::STALE_TOKEN );
    REQUIRE( count == 2u ); // Failed accessor leaves output unchanged.
    world::WorldSubmission_End( pSubmission );

    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    world::world_object_id_t newId{};
    REQUIRE( world::World_AddObject( pWorld, &first, &newId ) == world::world_error_t::OK );
    world::world_submission_token_t nextToken{};
    REQUIRE( world::WorldSubmission_Build(
        pSubmission, pWorld, &query, &nextToken ) ==
             world::world_submission_error_t::OK );
    REQUIRE( nextToken.generation != firstToken.generation );
    REQUIRE( world::WorldSubmission_Get( pSubmission, firstToken, 0u, &record ) ==
             world::world_submission_error_t::STALE_TOKEN );
    REQUIRE( world::WorldSubmission_Get( pSubmission, nextToken, 0u, &record ) ==
             world::world_submission_error_t::OK );
    REQUIRE( record.id.worldSerial == newId.worldSerial );

    world::World_Shutdown( &pWorld );
    world::WorldSubmission_Shutdown( &pSubmission );
    REQUIRE( pSubmission == nullptr );
    world::WorldSubmission_Shutdown( &pSubmission );
}

TEST_CASE( "World submission capacity failure publishes no partial view" )
{
    world::world_t *pWorld = nullptr;
    world::world_submission_t *pSubmission = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 2u ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Init(
        &pSubmission, common::Allocator_GetSystem(), 1u ) ==
             world::world_submission_error_t::OK );
    const world::world_object_t first = MakeObject( 0u, 0u );
    const world::world_object_t second = MakeObject( 1u, 1u );
    world::world_object_id_t firstId{};
    world::world_object_id_t secondId{};
    REQUIRE( world::World_AddObject( pWorld, &first, &firstId ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &second, &secondId ) == world::world_error_t::OK );
    const world::world_view_query_t query = MakeView();
    world::world_submission_token_t token{ 99u, 99u };
    REQUIRE( world::WorldSubmission_Build( pSubmission, pWorld, &query, &token ) ==
             world::world_submission_error_t::CAPACITY_EXCEEDED );
    REQUIRE( token.generation == 0u );
    common::u32 count = 123u;
    REQUIRE( world::WorldSubmission_Count( pSubmission, token, &count ) ==
             world::world_submission_error_t::STALE_TOKEN );
    REQUIRE( count == 123u );

    REQUIRE( world::World_RemoveObject( pWorld, secondId ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Build( pSubmission, pWorld, &query, &token ) ==
             world::world_submission_error_t::OK );
    REQUIRE( world::WorldSubmission_Count( pSubmission, token, &count ) ==
             world::world_submission_error_t::OK );
    REQUIRE( count == 1u );
    world::WorldSubmission_End( pSubmission );

    REQUIRE( world::World_RemoveObject( pWorld, firstId ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Build( pSubmission, pWorld, &query, &token ) ==
             world::world_submission_error_t::OK );
    REQUIRE( world::WorldSubmission_Count( pSubmission, token, &count ) ==
             world::world_submission_error_t::OK );
    REQUIRE( count == 0u );
    world::world_object_snapshot_t emptyRecord{};
    REQUIRE( world::WorldSubmission_Get( pSubmission, token, 0u, &emptyRecord ) ==
             world::world_submission_error_t::INVALID_ARGUMENT );
    world::WorldSubmission_End( pSubmission );

    world::world_view_query_t invalidQuery = query;
    invalidQuery.maxDistance = -1.0f;
    token = { 98u, 98u };
    REQUIRE( world::WorldSubmission_Build(
        pSubmission, pWorld, &invalidQuery, &token ) ==
             world::world_submission_error_t::INVALID_ARGUMENT );
    REQUIRE( token.generation == 0u );

    world::World_Shutdown( &pWorld );
    world::WorldSubmission_Shutdown( &pSubmission );
}

TEST_CASE( "World submission allocation failures clean up and builds do not allocate" )
{
    for ( common::u32 failedAttempt : { 1u, 2u } ) {
        counting_allocator_t fixture{};
        fixture.failAt = failedAttempt;
        world::world_submission_t *pSubmission = nullptr;
        REQUIRE( world::WorldSubmission_Init(
            &pSubmission, &fixture.allocator, 2u ) ==
                 world::world_submission_error_t::OUT_OF_MEMORY );
        REQUIRE( pSubmission == nullptr );
        REQUIRE( fixture.allocations == fixture.frees );
    }

    counting_allocator_t fixture{};
    world::world_submission_t *pSubmission = nullptr;
    REQUIRE( world::WorldSubmission_Init(
        &pSubmission, &fixture.allocator, 2u ) ==
             world::world_submission_error_t::OK );
    REQUIRE( fixture.attempts == 2u );
    fixture.failAt = fixture.attempts + 1u;

    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    const world::world_object_t object = MakeObject( 0u, 0u );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &object, &id ) == world::world_error_t::OK );
    const world::world_view_query_t query = MakeView();
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        world::world_submission_token_t token{};
        REQUIRE( world::WorldSubmission_Build(
            pSubmission, pWorld, &query, &token ) ==
                 world::world_submission_error_t::OK );
        world::WorldSubmission_End( pSubmission );
    }
    REQUIRE( fixture.attempts == 2u );

    world::World_Shutdown( &pWorld );
    world::WorldSubmission_Shutdown( &pSubmission );
    REQUIRE( fixture.allocations == fixture.frees );
}

TEST_CASE( "World submission tokens reject a different or recreated storage owner" )
{
    world::world_t *pWorld = nullptr;
    world::world_submission_t *pFirst = nullptr;
    world::world_submission_t *pSecond = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Init(
        &pFirst, common::Allocator_GetSystem(), 1u ) ==
             world::world_submission_error_t::OK );
    REQUIRE( world::WorldSubmission_Init(
        &pSecond, common::Allocator_GetSystem(), 1u ) ==
             world::world_submission_error_t::OK );
    const world::world_object_t object = MakeObject( 0u, 0u );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &object, &id ) == world::world_error_t::OK );
    const world::world_view_query_t query = MakeView();
    world::world_submission_token_t firstToken{};
    world::world_submission_token_t secondToken{};
    REQUIRE( world::WorldSubmission_Build( pFirst, pWorld, &query, &firstToken ) ==
             world::world_submission_error_t::OK );
    REQUIRE( world::WorldSubmission_Build( pSecond, pWorld, &query, &secondToken ) ==
             world::world_submission_error_t::OK );
    common::u32 count = 0u;
    REQUIRE( world::WorldSubmission_Count( pSecond, firstToken, &count ) ==
             world::world_submission_error_t::STALE_TOKEN );
    world::WorldSubmission_Shutdown( &pFirst );
    REQUIRE( world::WorldSubmission_Init(
        &pFirst, common::Allocator_GetSystem(), 1u ) ==
             world::world_submission_error_t::OK );
    world::world_submission_token_t recreatedToken{};
    REQUIRE( world::WorldSubmission_Build(
        pFirst, pWorld, &query, &recreatedToken ) ==
             world::world_submission_error_t::OK );
    REQUIRE( world::WorldSubmission_Count( pFirst, firstToken, &count ) ==
             world::world_submission_error_t::STALE_TOKEN );
    REQUIRE( recreatedToken.storageSerial != firstToken.storageSerial );
    world::World_Shutdown( &pWorld );
    world::WorldSubmission_Shutdown( &pFirst );
    world::WorldSubmission_Shutdown( &pSecond );
}

TEST_CASE( "Submission consumer resolves every logical asset before drawing" )
{
    world::world_t *pWorld = nullptr;
    world::world_submission_t *pSubmission = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 2u ) == world::world_error_t::OK );
    REQUIRE( world::WorldSubmission_Init(
        &pSubmission, common::Allocator_GetSystem(), 2u ) ==
             world::world_submission_error_t::OK );
    const world::world_object_t first = MakeObject( 0u, 0u );
    const world::world_object_t second = MakeObject( 1u, 1u );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &first, &id ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &second, &id ) == world::world_error_t::OK );
    const world::world_view_query_t query = MakeView();
    world::world_submission_token_t token{};
    REQUIRE( world::WorldSubmission_Build( pSubmission, pWorld, &query, &token ) ==
             world::world_submission_error_t::OK );

    const bool meshes[] = { true, true };
    bool materials[] = { true, false };
    common::u32 draws = 0u;
    REQUIRE_FALSE( FakeSubmitResolved(
        pSubmission, token, meshes, 2u, materials, 2u, &draws ) );
    REQUIRE( draws == 0u );
    materials[1] = true;
    REQUIRE( FakeSubmitResolved(
        pSubmission, token, meshes, 2u, materials, 2u, &draws ) );
    REQUIRE( draws == 2u );

    world::World_Shutdown( &pWorld );
    world::WorldSubmission_Shutdown( &pSubmission );
}
