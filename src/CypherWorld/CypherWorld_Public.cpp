//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Public.cpp
//  Purpose: Owns a fixed-capacity table of renderer-neutral world objects.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherWorld_Public.h"

#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

#include <atomic>
#include <new>

namespace cypher::engine::world
{

struct world_object_entry_t {
    world_object_t object{};
    common::u64 incarnation{ 0u };
};

struct world_t {
    common::handle_table_t<world_object_entry_t> objects{};
    const common::allocator_t *pAllocator{ nullptr };
    common::u64 serial{ 0u };
    common::u64 lastIncarnation{ 0u };
    common::u32 capacity{ 0u };
};

namespace
{

std::atomic<common::u64> gLastWorldSerial{ 0u };

common::u64 NextWorldSerial() noexcept
{
    common::u64 observed = gLastWorldSerial.load( std::memory_order_relaxed );
    while ( observed != common::CY_U64_MAX ) {
        if ( gLastWorldSerial.compare_exchange_weak(
                 observed, observed + 1u,
                 std::memory_order_relaxed,
                 std::memory_order_relaxed ) ) {
            return observed + 1u;
        }
    }
    return 0u;
}

common::bool_t IsValidObject( const world_object_t &object ) noexcept
{
    return math::Affine3_IsFinite( object.localToWorld ) &&
           math::Aabb_IsValid( object.worldBounds ) &&
           object.meshSlot != WORLD_ASSET_SLOT_INVALID &&
           object.materialSlot != WORLD_ASSET_SLOT_INVALID;
}

common::bool_t IsLiveId( const world_t *pWorld, world_object_id_t id ) noexcept
{
    if ( id.worldSerial != pWorld->serial || id.incarnation == 0u ||
         !common::Cy_Handle32IsValid( id.slot ) ) {
        return common::CY_FALSE;
    }
    const world_object_entry_t *pEntry = common::HandleTable_Get(
        &pWorld->objects, id.slot );
    return pEntry != nullptr && pEntry->incarnation == id.incarnation;
}

} // namespace

world_error_t World_Init(
    world_t **ppWorld,
    const common::allocator_t *pAllocator,
    common::u32 capacity ) noexcept
{
    if ( ppWorld == nullptr || *ppWorld != nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         capacity == 0u ||
         capacity > common::CY_HANDLE_TABLE_MAX_CAPACITY ) {
        return world_error_t::INVALID_ARGUMENT;
    }

    void *pMemory = common::Allocator_Allocate(
        pAllocator, sizeof( world_t ), alignof( world_t ) );
    if ( pMemory == nullptr ) {
        return world_error_t::OUT_OF_MEMORY;
    }

    world_t *pWorld = ::new ( pMemory ) world_t;
    pWorld->pAllocator = pAllocator;
    if ( !common::HandleTable_Init( &pWorld->objects, pAllocator, capacity ) ) {
        pWorld->~world_t();
        common::Allocator_Free(
            pAllocator, pMemory, sizeof( world_t ), alignof( world_t ) );
        return world_error_t::OUT_OF_MEMORY;
    }

    pWorld->serial = NextWorldSerial();
    if ( pWorld->serial == 0u ) {
        pWorld->~world_t();
        common::Allocator_Free(
            pAllocator, pMemory, sizeof( world_t ), alignof( world_t ) );
        return world_error_t::CAPACITY_EXCEEDED;
    }

    pWorld->capacity = capacity;
    *ppWorld = pWorld;
    return world_error_t::OK;
}

void World_Shutdown( world_t **ppWorld ) noexcept
{
    if ( ppWorld == nullptr || *ppWorld == nullptr ) {
        return;
    }

    world_t *pWorld = *ppWorld;
    const common::allocator_t *pAllocator = pWorld->pAllocator;
    pWorld->~world_t();
    common::Allocator_Free(
        pAllocator, pWorld, sizeof( world_t ), alignof( world_t ) );
    *ppWorld = nullptr;
}

world_error_t World_AddObject(
    world_t *pWorld,
    const world_object_t *pObject,
    world_object_id_t *pOutId ) noexcept
{
    if ( pOutId != nullptr ) {
        *pOutId = WORLD_OBJECT_ID_INVALID;
    }
    if ( pWorld == nullptr || pObject == nullptr || pOutId == nullptr ||
         !IsValidObject( *pObject ) ) {
        return world_error_t::INVALID_ARGUMENT;
    }
    if ( common::HandleTable_Count( &pWorld->objects ) >= pWorld->capacity ) {
        return world_error_t::CAPACITY_EXCEEDED;
    }
    if ( pWorld->lastIncarnation == common::CY_U64_MAX ) {
        return world_error_t::CAPACITY_EXCEEDED;
    }

    const common::u64 incarnation = pWorld->lastIncarnation + 1u;
    const common::handle32_t slot = common::HandleTable_Insert(
        &pWorld->objects, world_object_entry_t{ *pObject, incarnation } );
    if ( !common::Cy_Handle32IsValid( slot ) ) {
        return world_error_t::OUT_OF_MEMORY;
    }
    pWorld->lastIncarnation = incarnation;
    *pOutId = { slot, pWorld->serial, incarnation };
    return world_error_t::OK;
}

world_error_t World_RemoveObject( world_t *pWorld, world_object_id_t id ) noexcept
{
    if ( pWorld == nullptr ) {
        return world_error_t::INVALID_ARGUMENT;
    }
    if ( !IsLiveId( pWorld, id ) ) {
        return world_error_t::STALE_OBJECT_ID;
    }
    const common::bool_t removed = common::HandleTable_Remove( &pWorld->objects, id.slot );
    return removed ? world_error_t::OK : world_error_t::STALE_OBJECT_ID;
}

world_error_t World_GetObject(
    const world_t *pWorld,
    world_object_id_t id,
    world_object_t *pOutObject ) noexcept
{
    if ( pWorld == nullptr || pOutObject == nullptr ) {
        return world_error_t::INVALID_ARGUMENT;
    }
    if ( !IsLiveId( pWorld, id ) ) {
        return world_error_t::STALE_OBJECT_ID;
    }
    *pOutObject = common::HandleTable_Get( &pWorld->objects, id.slot )->object;
    return world_error_t::OK;
}

world_error_t World_UpdateObject(
    world_t *pWorld,
    world_object_id_t id,
    const world_object_t *pObject ) noexcept
{
    if ( pWorld == nullptr || pObject == nullptr || !IsValidObject( *pObject ) ) {
        return world_error_t::INVALID_ARGUMENT;
    }
    if ( !IsLiveId( pWorld, id ) ) {
        return world_error_t::STALE_OBJECT_ID;
    }
    common::HandleTable_Get( &pWorld->objects, id.slot )->object = *pObject;
    return world_error_t::OK;
}

common::u32 World_ObjectCount( const world_t *pWorld ) noexcept
{
    return pWorld == nullptr
        ? 0u
        : static_cast<common::u32>(
              common::HandleTable_Count( &pWorld->objects ) );
}

common::u32 World_ObjectCapacity( const world_t *pWorld ) noexcept
{
    return pWorld == nullptr ? 0u : pWorld->capacity;
}

world_error_t World_SnapshotObjects(
    const world_t *pWorld,
    world_object_snapshot_t *pOutput,
    common::u32 outputCapacity,
    common::u32 *pOutCount ) noexcept
{
    if ( pWorld == nullptr || pOutCount == nullptr ) {
        return world_error_t::INVALID_ARGUMENT;
    }
    const common::u32 count = World_ObjectCount( pWorld );
    if ( outputCapacity < count ) {
        *pOutCount = count;
        return world_error_t::CAPACITY_EXCEEDED;
    }
    if ( count != 0u && pOutput == nullptr ) {
        return world_error_t::INVALID_ARGUMENT;
    }

    common::u32 copied = 0u;
    const common::usize visited = common::HandleTable_ForEach(
        &pWorld->objects,
        [&]( common::handle32_t slot,
             const world_object_entry_t &entry ) noexcept -> common::bool_t {
            pOutput[copied++] = {
                { slot, pWorld->serial, entry.incarnation }, entry.object };
            return common::CY_TRUE;
        } );
    (void)visited;
    *pOutCount = copied;
    return world_error_t::OK;
}

} // namespace cypher::engine::world
