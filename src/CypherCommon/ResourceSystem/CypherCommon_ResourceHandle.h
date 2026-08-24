//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ResourceSystem/CypherCommon_ResourceHandle.h
//  Purpose: Declares compact generation-checked runtime resource handles.
//  Details: Handles pack a 16-bit manager slot, 32-bit generation, and 16-bit
//           runtime type without exposing private resource pointers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RESOURCESYSTEM_RESOURCEHANDLE_H
#define CYPHER_COMMON_RESOURCESYSTEM_RESOURCEHANDLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Handle.h"

namespace cypher::common
{

using resource_slot_t = u32;       // Logical slot; only low 16 bits are packed.
using resource_generation_t = u32; // Reuse counter preventing stale-handle access.
using resource_type_slot_t = u32;  // Runtime resource-manager type index.

constexpr u32 CY_RESOURCE_SLOT_BITS = 16u;       // Low packed field width.
constexpr u32 CY_RESOURCE_GENERATION_BITS = 32u; // Middle packed field width.
constexpr u32 CY_RESOURCE_TYPE_SLOT_BITS = 16u;  // High packed field width.
constexpr u32 CY_RESOURCE_GENERATION_SHIFT = CY_RESOURCE_SLOT_BITS; // Bit 16.
constexpr u32 CY_RESOURCE_TYPE_SLOT_SHIFT =
    CY_RESOURCE_SLOT_BITS + CY_RESOURCE_GENERATION_BITS;
constexpr resource_slot_t CY_RESOURCE_SLOT_MAX = 0xFFFFu; // Largest packed slot.
constexpr resource_generation_t CY_RESOURCE_GENERATION_INVALID = 0u; // Sentinel.
constexpr resource_generation_t CY_RESOURCE_GENERATION_FIRST = 1u; // Initial live value.
constexpr resource_generation_t CY_RESOURCE_GENERATION_MAX = CY_U32_MAX; // Wrap bound.

constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_INVALID = 0u; // No type.
constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_MAX = 0xFFFFu; // Packed maximum.

static_assert(
    CY_RESOURCE_SLOT_BITS +
    CY_RESOURCE_GENERATION_BITS +
    CY_RESOURCE_TYPE_SLOT_BITS == 64u,
    "Resource handle fields must occupy exactly 64 bits." );

struct resource_handle_t {
    u64 value{ 0u }; // Opaque packed type, generation, and slot fields.
};

struct resource_handle_parts_t {
    resource_slot_t iSlot{};             // Manager record index.
    resource_generation_t nGeneration{}; // Expected record generation.
    resource_type_slot_t iTypeSlot{};    // Owning resource-manager type.
};

constexpr resource_handle_t CY_RESOURCE_HANDLE_INVALID{};
static_assert(
    sizeof( resource_handle_t ) == sizeof( u64 ),
    "Resource handles must remain exactly 64 bits." );

// Creates a handle after validating the generation and runtime type fields.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_TryMake(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot,
    resource_handle_t *pHandleOut ) noexcept;

// Creates a handle from trusted manager-owned fields and asserts on misuse.
CYPHER_NODISCARD CYPHER_COMMON_API
resource_handle_t ResourceHandle_Make(
    resource_slot_t iSlot,
    resource_generation_t nGeneration,
    resource_type_slot_t iTypeSlot ) noexcept;

// Checks packed structure only; the resource manager must still verify liveness.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_IsValid( resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceHandle_Equals(
    resource_handle_t left,
    resource_handle_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_handle_parts_t ResourceHandle_Unpack(
    resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_slot_t ResourceHandle_Slot( resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_generation_t ResourceHandle_Generation(
    resource_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
resource_type_slot_t ResourceHandle_TypeSlot(
    resource_handle_t handle ) noexcept;
} // namespace cypher::common

#endif // CYPHER_COMMON_RESOURCESYSTEM_RESOURCEHANDLE_H
