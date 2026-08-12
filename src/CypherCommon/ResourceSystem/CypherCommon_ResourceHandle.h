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

using resource_slot_t = u32;
using resource_generation_t = u32;
using resource_type_slot_t = u32;

constexpr u32 CY_RESOURCE_SLOT_BITS = 16u;
constexpr u32 CY_RESOURCE_GENERATION_BITS = 32u;
constexpr u32 CY_RESOURCE_TYPE_SLOT_BITS = 16u;
constexpr u32 CY_RESOURCE_GENERATION_SHIFT = CY_RESOURCE_SLOT_BITS;
constexpr u32 CY_RESOURCE_TYPE_SLOT_SHIFT =
    CY_RESOURCE_SLOT_BITS + CY_RESOURCE_GENERATION_BITS;
constexpr resource_slot_t CY_RESOURCE_SLOT_MAX = 0xFFFFu;
constexpr resource_generation_t CY_RESOURCE_GENERATION_INVALID = 0u;
constexpr resource_generation_t CY_RESOURCE_GENERATION_FIRST = 1u;
constexpr resource_generation_t CY_RESOURCE_GENERATION_MAX = CY_U32_MAX;

constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_INVALID = 0u;
constexpr resource_type_slot_t CY_RESOURCE_TYPE_SLOT_MAX = 0xFFFFu;

static_assert(
    CY_RESOURCE_SLOT_BITS +
    CY_RESOURCE_GENERATION_BITS +
    CY_RESOURCE_TYPE_SLOT_BITS == 64u,
    "Resource handle fields must occupy exactly 64 bits." );

struct resource_handle_t {
    u64 value{ 0u };
};

struct resource_handle_parts_t {
    resource_slot_t iSlot{};
    resource_generation_t nGeneration{};
    resource_type_slot_t iTypeSlot{};
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
