//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Public.h
//  Purpose: Declares the bounded, renderer-neutral world object table.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_PUBLIC_H
#define CYPHER_ENGINE_WORLD_PUBLIC_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherWorld_Error.h"
#include "CypherWorld_Types.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

namespace cypher::engine::world
{

// The borrowed allocator must outlive the world. Capacity is fixed at creation
// and must be 1..65536. An initially null *ppWorld remains null on failure;
// an existing world is never replaced.
CYPHER_NODISCARD world_error_t World_Init(
    world_t **ppWorld,
    const ::cypher::common::allocator_t *pAllocator,
    ::cypher::common::u32 capacity ) noexcept;

// Safe for a null pointer or an already-empty *ppWorld.
void World_Shutdown( world_t **ppWorld ) noexcept;

// Objects own no meshes or materials. Slots refer to an external logical asset
// table; CY_U32_MAX is reserved and rejected. The bounds are already in world
// space. Callers must update them when the transform changes.
CYPHER_NODISCARD world_error_t World_AddObject(
    world_t *pWorld,
    const world_object_t *pObject,
    world_object_id_t *pOutId ) noexcept;
CYPHER_NODISCARD world_error_t World_RemoveObject(
    world_t *pWorld,
    world_object_id_t id ) noexcept;
CYPHER_NODISCARD world_error_t World_GetObject(
    const world_t *pWorld,
    world_object_id_t id,
    world_object_t *pOutObject ) noexcept;
// Validates the whole replacement before changing the stored record.
CYPHER_NODISCARD world_error_t World_UpdateObject(
    world_t *pWorld,
    world_object_id_t id,
    const world_object_t *pObject ) noexcept;

CYPHER_NODISCARD ::cypher::common::u32 World_ObjectCount( const world_t *pWorld ) noexcept;
CYPHER_NODISCARD ::cypher::common::u32 World_ObjectCapacity( const world_t *pWorld ) noexcept;

// Copies live objects in ascending slot order. An insufficient output capacity
// reports CAPACITY_EXCEEDED, sets *pOutCount to the required count, and writes
// no records. A successful snapshot is independent of later table mutations;
// its IDs still become stale when their live objects are removed.
CYPHER_NODISCARD world_error_t World_SnapshotObjects(
    const world_t *pWorld,
    world_object_snapshot_t *pOutput,
    ::cypher::common::u32 outputCapacity,
    ::cypher::common::u32 *pOutCount ) noexcept;

// Scans live objects in ascending slot order. An object passes when any
// requested layer bit matches, its world bounds touch or intersect the frustum,
// and the closest point on those bounds is within maxDistance of the camera.
// A null output with zero capacity can be used to count matches. Insufficient
// capacity reports the required count and writes no records. Returned values
// are copies; querying and mutating the same world concurrently is unsupported.
CYPHER_NODISCARD world_error_t World_QueryView(
    const world_t *pWorld,
    const world_view_query_t *pQuery,
    world_object_snapshot_t *pOutput,
    ::cypher::common::u32 outputCapacity,
    ::cypher::common::u32 *pOutCount ) noexcept;

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_PUBLIC_H
