//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Types.h
//  Purpose: Defines backend-neutral values for the bounded world object table.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_TYPES_H
#define CYPHER_ENGINE_WORLD_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Mathlib/CypherMath_Bounds.h"
#include "CypherCommon/Mathlib/CypherMath_Frustum.h"
#include "CypherCommon/Tier0/CypherCommon_Handle.h"

namespace cypher::engine::world
{

struct world_t; // Opaque owner; initialized and destroyed through World_Init/Shutdown.

struct world_object_id_t {
    ::cypher::common::handle32_t slot{}; // Generational table slot.
    ::cypher::common::u64 worldSerial{ 0u }; // Rejects IDs from another or destroyed world.
    ::cypher::common::u64 incarnation{ 0u }; // Rejects old IDs even when slot generation wraps.
};

inline constexpr world_object_id_t WORLD_OBJECT_ID_INVALID{};
inline constexpr ::cypher::common::u32 WORLD_ASSET_SLOT_INVALID = ::cypher::common::CY_U32_MAX;

struct world_object_t {
    ::cypher::math::affine3_t localToWorld{ ::cypher::math::CY_AFFINE3_IDENTITY };
    ::cypher::math::aabb_t worldBounds{}; // Caller-maintained, world-space AABB for later view queries.
    ::cypher::common::flags32_t layerMask{ 0u }; // Zero excludes the object from every layer query.
    ::cypher::common::u32 meshSlot{ WORLD_ASSET_SLOT_INVALID }; // Logical asset slot, never a GPU handle.
    ::cypher::common::u32 materialSlot{ WORLD_ASSET_SLOT_INVALID };
};

struct world_object_snapshot_t {
    world_object_id_t id{};
    world_object_t object{};
};

// A bounded, CPU-only query over the current world object table. The frustum
// contains six inward-facing unit planes. Distance is measured from the camera
// to the closest point on each object's world-space bounds, inclusively.
struct world_view_query_t {
    ::cypher::math::frustum_t frustum{};
    ::cypher::math::vec3_t cameraPosition{};
    ::cypher::common::f32 maxDistance{ ::cypher::common::CY_F32_MAX };
    ::cypher::common::flags32_t layerMask{ ~::cypher::common::flags32_t{ 0u } };
    ::cypher::common::f32 frustumTolerance{ 0.0f };
};

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_TYPES_H
