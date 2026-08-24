//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Handle.h
//  Purpose: Defines compact generational handles shared by low-level subsystems.
//  Details: Handles contain identity only; the owning table controls storage and lifetime.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_HANDLE_H
#define CYPHER_COMMON_TIER0_HANDLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

// WARNING: These bit counts define the packed handle ABI and must total 32 bits.
constexpr u32 CY_HANDLE32_INDEX_BITS = 16u;      // Low bits: owner-table slot.
constexpr u32 CY_HANDLE32_GENERATION_BITS = 16u; // High bits: slot reuse counter.
constexpr u32 CY_HANDLE32_INDEX_MAX = 0xFFFFu;
constexpr u32 CY_HANDLE32_GENERATION_MAX = 0xFFFFu;

// WARNING: These bit counts define the packed handle ABI and must total 64 bits.
constexpr u32 CY_HANDLE64_INDEX_BITS = 32u;      // Low bits: owner-table slot.
constexpr u32 CY_HANDLE64_GENERATION_BITS = 16u; // Middle bits: slot reuse counter.
constexpr u32 CY_HANDLE64_TYPE_BITS = 16u;       // High bits: owning resource family.
constexpr u32 CY_HANDLE64_GENERATION_MAX = 0xFFFFu;
constexpr u32 CY_HANDLE64_TYPE_MAX = 0xFFFFu;

struct handle32_t {
    u32 value = 0u; // Packed index/generation; zero is invalid.
};

struct handle64_t {
    u64 value = 0u; // Packed index/generation/type; zero is invalid.
};

struct handle_parts32_t {
    u32 nIndex;      // Slot in the owning table.
    u32 nGeneration; // Generation observed when the handle was created.
};

struct handle_parts64_t {
    u32 nIndex;      // Slot in the owning table.
    u32 nGeneration; // Generation observed when the handle was created.
    u32 nType;       // Owning resource or subsystem family.
};

constexpr handle32_t CY_HANDLE32_INVALID{}; // Packed zero sentinel.
constexpr handle64_t CY_HANDLE64_INVALID{}; // Packed zero sentinel.

// TryMake rejects out-of-range components and leaves no partially valid handle.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle32TryMake(
    u32 nIndex,
    u32 nGeneration,
    handle32_t *pOutHandle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle64TryMake(
    u32 nIndex,
    u32 nGeneration,
    u32 nType,
    handle64_t *pOutHandle ) noexcept;
// Make asserts the same preconditions as TryMake and returns the packed value.
CYPHER_NODISCARD CYPHER_COMMON_API handle32_t Cy_Handle32Make(
    u32 nIndex,
    u32 nGeneration ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API handle64_t Cy_Handle64Make(
    u32 nIndex,
    u32 nGeneration,
    u32 nType ) noexcept;
// Unpack and component queries do not validate that the owning slot is still alive.
CYPHER_NODISCARD CYPHER_COMMON_API handle_parts32_t Cy_Handle32Unpack(
    handle32_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API handle_parts64_t Cy_Handle64Unpack(
    handle64_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle32IsValid(
    handle32_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle64IsValid(
    handle64_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_Handle32Index(
    handle32_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_Handle32Generation(
    handle32_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_Handle64Index(
    handle64_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_Handle64Generation(
    handle64_t handle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_Handle64Type(
    handle64_t handle ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_HANDLE_H
