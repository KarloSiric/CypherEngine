//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests fixed-capacity world object lifetime and snapshot behavior.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

// Exercise the public World header after another engine namespace named common
// has been declared; its types must still refer to ::cypher::common.
#include "CypherCommon/Engine/CypherCommon.h"
#include "CypherWorld/CypherWorld_Public.h"
#include "CypherCommon/Mathlib/CypherMath_Intersection.h"
#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace world = ::cypher::engine::world;

namespace
{

world::world_object_t MakeObject( common::u32 meshSlot ) noexcept
{
    world::world_object_t object{};
    object.localToWorld = math::Affine3_FromTranslation(
        { static_cast<common::f32>( meshSlot ), 2.0f, 3.0f } );
    object.worldBounds = math::Aabb_Make(
        { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } );
    object.layerMask = 1u;
    object.meshSlot = meshSlot;
    object.materialSlot = meshSlot + 100u;
    return object;
}

bool MakePerspectiveView( world::world_view_query_t *pQuery ) noexcept
{
    math::mat4_t projection{};
    if ( !math::Mat4_TryPerspectiveRH(
             math::Angle_FromDegrees( 90.0f ), 1.0f, 1.0f, 10.0f,
             math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
             &projection ) ) {
        return false;
    }
    return math::Frustum_TryFromViewProjection(
        projection, math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        0.000001f, &pQuery->frustum );
}

math::frustum_t MakeBoxFrustum( common::f32 extent ) noexcept
{
    math::frustum_t result{};
    result.planes[0] = math::Plane_Make( { 1.0f, 0.0f, 0.0f }, extent );
    result.planes[1] = math::Plane_Make( { -1.0f, 0.0f, 0.0f }, extent );
    result.planes[2] = math::Plane_Make( { 0.0f, 1.0f, 0.0f }, extent );
    result.planes[3] = math::Plane_Make( { 0.0f, -1.0f, 0.0f }, extent );
    result.planes[4] = math::Plane_Make( { 0.0f, 0.0f, 1.0f }, extent );
    result.planes[5] = math::Plane_Make( { 0.0f, 0.0f, -1.0f }, extent );
    return result;
}

struct failing_allocator_t {
    const common::allocator_t *pSystem{ common::Allocator_GetSystem() };
    common::allocator_t allocator{};
    common::u32 attempts{ 0u };
    common::u32 successfulAllocations{ 0u };
    common::u32 frees{ 0u };
    common::u32 failAtAttempt{ 0u };

    failing_allocator_t() noexcept
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = [](
            void *pUserData, common::usize size,
            common::usize alignment ) noexcept -> void * {
            auto &self = *static_cast<failing_allocator_t *>( pUserData );
            ++self.attempts;
            if ( self.attempts == self.failAtAttempt ) {
                return nullptr;
            }
            void *pResult = self.pSystem->pfnAllocate(
                self.pSystem->pUserData, size, alignment );
            if ( pResult != nullptr ) {
                ++self.successfulAllocations;
            }
            return pResult;
        };
        allocator.pfnFree = [](
            void *pUserData, void *pMemory,
            common::usize size, common::usize alignment ) noexcept {
            auto &self = *static_cast<failing_allocator_t *>( pUserData );
            ++self.frees;
            self.pSystem->pfnFree(
                self.pSystem->pUserData, pMemory, size, alignment );
        };
    }
};

} // namespace

TEST_CASE( "World object table is bounded and stale IDs do not resolve" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 2u ) == world::world_error_t::OK );
    REQUIRE( world::World_ObjectCapacity( pWorld ) == 2u );

    const world::world_object_t first = MakeObject( 1u );
    const world::world_object_t second = MakeObject( 2u );
    const world::world_object_t third = MakeObject( 3u );
    world::world_object_id_t firstId{};
    world::world_object_id_t secondId{};
    world::world_object_id_t thirdId{};
    REQUIRE( world::World_AddObject( pWorld, &first, &firstId ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &second, &secondId ) == world::world_error_t::OK );
    thirdId = firstId;
    REQUIRE( world::World_AddObject( pWorld, &third, &thirdId ) ==
             world::world_error_t::CAPACITY_EXCEEDED );
    REQUIRE( thirdId.slot.value == 0u );
    REQUIRE( thirdId.worldSerial == 0u );
    REQUIRE( thirdId.incarnation == 0u );
    REQUIRE( world::World_ObjectCount( pWorld ) == 2u );

    world::world_object_snapshot_t output[2]{};
    output[0].object.meshSlot = 999u;
    common::u32 required = 0u;
    REQUIRE( world::World_SnapshotObjects( pWorld, output, 1u, &required ) ==
             world::world_error_t::CAPACITY_EXCEEDED );
    REQUIRE( required == 2u );
    REQUIRE( output[0].object.meshSlot == 999u );
    REQUIRE( world::World_SnapshotObjects( pWorld, output, 2u, &required ) ==
             world::world_error_t::OK );
    REQUIRE( output[0].id.slot.value == firstId.slot.value );
    REQUIRE( output[1].id.slot.value == secondId.slot.value );

    REQUIRE( world::World_RemoveObject( pWorld, firstId ) == world::world_error_t::OK );
    REQUIRE( world::World_RemoveObject( pWorld, firstId ) ==
             world::world_error_t::STALE_OBJECT_ID );
    world::world_object_t readback{};
    REQUIRE( world::World_GetObject( pWorld, firstId, &readback ) ==
             world::world_error_t::STALE_OBJECT_ID );
    REQUIRE( world::World_AddObject( pWorld, &third, &thirdId ) == world::world_error_t::OK );
    REQUIRE( common::Cy_Handle32Index( thirdId.slot ) ==
             common::Cy_Handle32Index( firstId.slot ) );
    REQUIRE( thirdId.slot.value != firstId.slot.value );
    REQUIRE( thirdId.incarnation != firstId.incarnation );
    REQUIRE( world::World_SnapshotObjects( pWorld, output, 2u, &required ) ==
             world::world_error_t::OK );
    REQUIRE( output[0].id.slot.value == thirdId.slot.value );
    REQUIRE( output[1].id.slot.value == secondId.slot.value );
    REQUIRE( output[0].object.meshSlot == 3u );

    world::World_Shutdown( &pWorld );
    REQUIRE( pWorld == nullptr );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World object incarnation survives packed slot generation wrap" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    const world::world_object_t object = MakeObject( 11u );
    world::world_object_id_t original{};
    REQUIRE( world::World_AddObject( pWorld, &object, &original ) ==
             world::world_error_t::OK );
    REQUIRE( world::World_RemoveObject( pWorld, original ) ==
             world::world_error_t::OK );

    world::world_object_id_t current{};
    for ( common::u32 reuse = 0u;
          reuse < common::CY_HANDLE32_GENERATION_MAX;
          ++reuse ) {
        REQUIRE( world::World_AddObject( pWorld, &object, &current ) ==
                 world::world_error_t::OK );
        if ( reuse + 1u < common::CY_HANDLE32_GENERATION_MAX ) {
            REQUIRE( world::World_RemoveObject( pWorld, current ) ==
                     world::world_error_t::OK );
        }
    }

    // The packed slot and generation alias the original after one full cycle.
    REQUIRE( current.slot.value == original.slot.value );
    REQUIRE( current.incarnation != original.incarnation );
    world::world_object_t readback{};
    REQUIRE( world::World_GetObject( pWorld, original, &readback ) ==
             world::world_error_t::STALE_OBJECT_ID );
    REQUIRE( world::World_RemoveObject( pWorld, original ) ==
             world::world_error_t::STALE_OBJECT_ID );
    REQUIRE( world::World_GetObject( pWorld, current, &readback ) ==
             world::world_error_t::OK );
    REQUIRE( readback.meshSlot == 11u );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World replacement validates transform, world bounds, and logical slots atomically" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    world::world_object_t initial = MakeObject( 4u );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &initial, &id ) == world::world_error_t::OK );

    world::world_object_t invalid = initial;
    invalid.localToWorld.m[9] = std::numeric_limits<common::f32>::quiet_NaN();
    REQUIRE( world::World_UpdateObject( pWorld, id, &invalid ) ==
             world::world_error_t::INVALID_ARGUMENT );
    invalid = initial;
    invalid.worldBounds.maximum.x = 0.0f;
    REQUIRE( world::World_UpdateObject( pWorld, id, &invalid ) ==
             world::world_error_t::INVALID_ARGUMENT );
    invalid = initial;
    invalid.worldBounds.maximum.z = std::numeric_limits<common::f32>::infinity();
    REQUIRE( world::World_UpdateObject( pWorld, id, &invalid ) ==
             world::world_error_t::INVALID_ARGUMENT );
    invalid = initial;
    invalid.meshSlot = world::WORLD_ASSET_SLOT_INVALID;
    REQUIRE( world::World_UpdateObject( pWorld, id, &invalid ) ==
             world::world_error_t::INVALID_ARGUMENT );

    world::world_object_t readback{};
    REQUIRE( world::World_GetObject( pWorld, id, &readback ) == world::world_error_t::OK );
    REQUIRE( readback.meshSlot == 4u );
    REQUIRE( readback.localToWorld.m[9] == 4.0f );
    REQUIRE( readback.worldBounds.maximum.z == 6.0f );

    world::world_object_t replacement = MakeObject( 5u );
    replacement.layerMask = 0u;
    replacement.worldBounds = math::Aabb_Make(
        { 8.0f, 9.0f, 10.0f }, { 8.0f, 9.0f, 10.0f } );
    REQUIRE( world::World_UpdateObject( pWorld, id, &replacement ) ==
             world::world_error_t::OK );
    REQUIRE( world::World_GetObject( pWorld, id, &readback ) == world::world_error_t::OK );
    REQUIRE( readback.layerMask == 0u );
    REQUIRE( readback.meshSlot == 5u );
    REQUIRE( readback.worldBounds.minimum.x == 8.0f );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World IDs are tied to one live world instance" )
{
    world::world_t *pFirst = nullptr;
    world::world_t *pSecond = nullptr;
    const auto *pAllocator = common::Allocator_GetSystem();
    REQUIRE( world::World_Init( &pFirst, pAllocator, 1u ) == world::world_error_t::OK );
    REQUIRE( world::World_Init( &pSecond, pAllocator, 1u ) == world::world_error_t::OK );
    world::world_object_t object = MakeObject( 7u );
    world::world_object_id_t firstId{};
    world::world_object_id_t secondId{};
    REQUIRE( world::World_AddObject( pFirst, &object, &firstId ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pSecond, &object, &secondId ) == world::world_error_t::OK );
    REQUIRE( firstId.slot.value == secondId.slot.value );
    REQUIRE( firstId.worldSerial != secondId.worldSerial );
    world::world_object_t readback{};
    REQUIRE( world::World_GetObject( pSecond, firstId, &readback ) ==
             world::world_error_t::STALE_OBJECT_ID );
    world::World_Shutdown( &pFirst );
    REQUIRE( world::World_Init( &pFirst, pAllocator, 1u ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pFirst, &object, &secondId ) == world::world_error_t::OK );
    REQUIRE( world::World_GetObject( pFirst, firstId, &readback ) ==
             world::world_error_t::STALE_OBJECT_ID );
    world::World_Shutdown( &pSecond );
    world::World_Shutdown( &pFirst );
}

TEST_CASE( "World initialization frees partial allocations on failure" )
{
    for ( common::u32 failedAttempt : { 1u, 2u } ) {
        failing_allocator_t fixture{};
        fixture.failAtAttempt = failedAttempt;
        world::world_t *pWorld = nullptr;
        REQUIRE( world::World_Init( &pWorld, &fixture.allocator, 8u ) ==
                 world::world_error_t::OUT_OF_MEMORY );
        REQUIRE( pWorld == nullptr );
        REQUIRE( fixture.successfulAllocations == fixture.frees );
    }

    failing_allocator_t fixture{};
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init( &pWorld, &fixture.allocator, 2u ) ==
             world::world_error_t::OK );
    fixture.failAtAttempt = fixture.attempts + 1u;
    const world::world_object_t object = MakeObject( 1u );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &object, &id ) == world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &object, &id ) == world::world_error_t::OK );
    REQUIRE( fixture.attempts == 2u ); // No allocation during bounded inserts.
    world::World_Shutdown( &pWorld );
    REQUIRE( fixture.successfulAllocations == fixture.frees );
}

TEST_CASE( "World view query filters layers, frustum, and distance without partial output" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 8u ) == world::world_error_t::OK );
    world::world_view_query_t query{};
    REQUIRE( MakePerspectiveView( &query ) );
    query.cameraPosition = { 0.0f, 0.0f, 0.0f };
    query.maxDistance = 6.0f;
    query.layerMask = 1u;

    const math::aabb_t boxes[] = {
        math::Aabb_Make( { -0.25f, -0.25f, -3.25f },
                         { 0.25f, 0.25f, -2.75f } ), // Inside.
        math::Aabb_Make( { -0.25f, -0.25f, -4.25f },
                         { 0.25f, 0.25f, -3.75f } ), // Wrong layer.
        math::Aabb_Make( { -0.25f, -0.25f, -1.1f },
                         { 0.25f, 0.25f, -0.9f } ), // Crosses near plane.
        math::Aabb_Make( { -0.25f, -0.25f, -20.25f },
                         { 0.25f, 0.25f, -19.75f } ), // Past far plane.
        math::Aabb_Make( { 19.75f, -0.25f, -5.25f },
                         { 20.25f, 0.25f, -4.75f } ), // Outside side plane.
        math::Aabb_Make( { -0.25f, -0.25f, -8.25f },
                         { 0.25f, 0.25f, -7.75f } ) // Beyond distance.
    };
    world::world_object_id_t ids[6]{};
    for ( common::u32 i = 0u; i < 6u; ++i ) {
        world::world_object_t object = MakeObject( i + 1u );
        object.worldBounds = boxes[i];
        object.layerMask = i == 1u ? 2u : 1u;
        REQUIRE( world::World_AddObject( pWorld, &object, &ids[i] ) ==
                 world::world_error_t::OK );
    }

    world::world_object_snapshot_t output[2]{};
    output[0].object.meshSlot = 999u;
    common::u32 count = 777u;
    REQUIRE( world::World_QueryView( pWorld, &query, output, 1u, &count ) ==
             world::world_error_t::CAPACITY_EXCEEDED );
    REQUIRE( count == 2u );
    REQUIRE( output[0].object.meshSlot == 999u );
    REQUIRE( world::World_QueryView( pWorld, &query, output, 2u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 2u );
    REQUIRE( output[0].id.slot.value == ids[0].slot.value );
    REQUIRE( output[1].id.slot.value == ids[2].slot.value );
    REQUIRE( output[0].object.meshSlot == 1u );
    REQUIRE( output[1].object.meshSlot == 3u );

    world::world_object_t moved{};
    REQUIRE( world::World_GetObject( pWorld, ids[0], &moved ) ==
             world::world_error_t::OK );
    moved.worldBounds = math::Aabb_Make(
        { -0.25f, -0.25f, -9.25f }, { 0.25f, 0.25f, -8.75f } );
    REQUIRE( world::World_UpdateObject( pWorld, ids[0], &moved ) ==
             world::world_error_t::OK );
    REQUIRE( world::World_QueryView( pWorld, &query, output, 2u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 1u );
    REQUIRE( output[0].id.slot.value == ids[2].slot.value );
    REQUIRE( world::World_RemoveObject( pWorld, ids[2] ) ==
             world::world_error_t::OK );
    REQUIRE( world::World_QueryView( pWorld, &query, nullptr, 0u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 0u );
    // The previous copy does not alias the removed table entry.
    REQUIRE( output[0].object.meshSlot == 3u );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World view query uses inclusive bounds distance without f32 overflow" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 4u ) == world::world_error_t::OK );
    world::world_view_query_t query{};
    query.frustum = MakeBoxFrustum( 1.0e31f );
    query.layerMask = 1u;
    query.maxDistance = 2.0e30f;
    REQUIRE( math::Frustum_IsValid(
        query.frustum, math::CY_FRUSTUM_PLANE_UNIT_TOLERANCE ) );

    world::world_object_t nearObject = MakeObject( 1u );
    nearObject.worldBounds = math::Aabb_Make(
        { 1.0e30f, 0.0f, 0.0f }, { 1.0e30f, 0.0f, 0.0f } );
    world::world_object_t farObject = MakeObject( 2u );
    farObject.worldBounds = math::Aabb_Make(
        { 3.0e30f, 0.0f, 0.0f }, { 3.0e30f, 0.0f, 0.0f } );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &nearObject, &id ) ==
             world::world_error_t::OK );
    REQUIRE( world::World_AddObject( pWorld, &farObject, &id ) ==
             world::world_error_t::OK );
    world::world_object_snapshot_t output[2]{};
    common::u32 count = 0u;
    REQUIRE( world::World_QueryView( pWorld, &query, output, 2u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 1u );
    REQUIRE( output[0].object.meshSlot == 1u );

    query.maxDistance = 0.0f;
    query.cameraPosition = { 1.0e30f, 0.0f, 0.0f };
    REQUIRE( world::World_QueryView( pWorld, &query, output, 2u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 1u );
    query.layerMask = 0u;
    REQUIRE( world::World_QueryView( pWorld, &query, nullptr, 0u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 0u );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World view query rejects invalid descriptions without changing output" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 1u ) == world::world_error_t::OK );
    world::world_object_t object = MakeObject( 1u );
    object.worldBounds = math::Aabb_Make(
        { -0.25f, -0.25f, -2.25f }, { 0.25f, 0.25f, -1.75f } );
    world::world_object_id_t id{};
    REQUIRE( world::World_AddObject( pWorld, &object, &id ) ==
             world::world_error_t::OK );
    world::world_view_query_t query{};
    REQUIRE( MakePerspectiveView( &query ) );
    world::world_object_snapshot_t output{};
    output.object.meshSlot = 1234u;
    common::u32 count = 987u;

    REQUIRE( world::World_QueryView( pWorld, &query, nullptr, 0u, &count ) ==
             world::world_error_t::CAPACITY_EXCEEDED );
    REQUIRE( count == 1u );
    count = 987u;
    REQUIRE( world::World_QueryView( pWorld, &query, nullptr, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    REQUIRE( count == 987u );
    query.cameraPosition.x = std::numeric_limits<common::f32>::quiet_NaN();
    REQUIRE( world::World_QueryView( pWorld, &query, &output, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    query.cameraPosition.x = 0.0f;
    query.maxDistance = -1.0f;
    REQUIRE( world::World_QueryView( pWorld, &query, &output, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    query.maxDistance = std::numeric_limits<common::f32>::infinity();
    REQUIRE( world::World_QueryView( pWorld, &query, &output, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    query.maxDistance = 10.0f;
    query.frustumTolerance = -0.01f;
    REQUIRE( world::World_QueryView( pWorld, &query, &output, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    query.frustumTolerance = 0.0f;
    query.frustum.planes[0].normal.x = 2.0f;
    REQUIRE( world::World_QueryView( pWorld, &query, &output, 1u, &count ) ==
             world::world_error_t::INVALID_ARGUMENT );
    REQUIRE( count == 987u );
    REQUIRE( output.object.meshSlot == 1234u );
    world::World_Shutdown( &pWorld );
}

TEST_CASE( "World view query keeps inclusive boundaries and slot order after reuse" )
{
    world::world_t *pWorld = nullptr;
    REQUIRE( world::World_Init(
        &pWorld, common::Allocator_GetSystem(), 3u ) == world::world_error_t::OK );
    world::world_view_query_t query{};
    query.frustum = MakeBoxFrustum( 10.0f );
    query.maxDistance = 10.0f;
    query.layerMask = 1u;

    world::world_object_id_t ids[3]{};
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        world::world_object_t object = MakeObject( i + 1u );
        const common::f32 x = i == 0u ? 5.0f :
                              i == 1u ? 9.0f : 6.0f;
        object.worldBounds = math::Aabb_Make( { x, 0.0f, 0.0f },
                                               { x, 0.0f, 0.0f } );
        object.layerMask = i == 0u ? 3u : 1u;
        REQUIRE( world::World_AddObject( pWorld, &object, &ids[i] ) ==
                 world::world_error_t::OK );
    }
    REQUIRE( world::World_RemoveObject( pWorld, ids[1] ) ==
             world::world_error_t::OK );
    world::world_object_t boundary = MakeObject( 4u );
    boundary.worldBounds = math::Aabb_Make( { 10.0f, 0.0f, 0.0f },
                                            { 10.0f, 0.0f, 0.0f } );
    boundary.layerMask = 1u;
    world::world_object_id_t reused{};
    REQUIRE( world::World_AddObject( pWorld, &boundary, &reused ) ==
             world::world_error_t::OK );
    REQUIRE( common::Cy_Handle32Index( reused.slot ) ==
             common::Cy_Handle32Index( ids[1].slot ) );

    world::world_object_snapshot_t output[3]{};
    common::u32 count = 0u;
    REQUIRE( world::World_QueryView( pWorld, &query, output, 3u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 3u );
    REQUIRE( output[0].object.meshSlot == 1u );
    REQUIRE( output[1].object.meshSlot == 4u ); // Reused slot sorts before slot 2.
    REQUIRE( output[2].object.meshSlot == 3u );

    query.maxDistance = 5.0f; // Exact closest-point distance is included.
    REQUIRE( world::World_QueryView( pWorld, &query, output, 3u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 1u );
    REQUIRE( output[0].object.meshSlot == 1u );
    query.maxDistance = 10.0f;
    query.layerMask = 2u; // Any shared bit accepts layer mask 3.
    REQUIRE( world::World_QueryView( pWorld, &query, output, 3u, &count ) ==
             world::world_error_t::OK );
    REQUIRE( count == 1u );
    REQUIRE( output[0].object.meshSlot == 1u );
    world::World_Shutdown( &pWorld );
}
