//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Handle.h
//  Purpose: Declares CypherCommon Tier0 Handle support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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

/*
================
CypherCommon Handle

Typed handle declarations for resources, files, entities and editor objects.
Handles keep external code away from private subsystem pointers.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

constexpr u32 CY_HANDLE32_INDEX_BITS = 16u;
constexpr u32 CY_HANDLE32_GENERATION_BITS = 16u;
constexpr u32 CY_HANDLE32_INDEX_MAX = 0xFFFFu;
constexpr u32 CY_HANDLE32_GENERATION_MAX = 0xFFFFu;

constexpr u32 CY_HANDLE64_INDEX_BITS = 32u;
constexpr u32 CY_HANDLE64_GENERATION_BITS = 16u;
constexpr u32 CY_HANDLE64_TYPE_BITS = 16u;
constexpr u32 CY_HANDLE64_GENERATION_MAX = 0xFFFFu;
constexpr u32 CY_HANDLE64_TYPE_MAX = 0xFFFFu;

struct handle32_t {
    u32 value = 0u;
};

struct handle64_t {
    u64 value = 0u;
};

struct handle_parts32_t {
    u32 nIndex;
    u32 nGeneration;
};

struct handle_parts64_t {
    u32 nIndex;
    u32 nGeneration;
    u32 nType;
};

constexpr handle32_t CY_HANDLE32_INVALID{};
constexpr handle64_t CY_HANDLE64_INVALID{};

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle32TryMake(
    u32 nIndex,
    u32 nGeneration,
    handle32_t *pOutHandle ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_Handle64TryMake(
    u32 nIndex,
    u32 nGeneration,
    u32 nType,
    handle64_t *pOutHandle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API handle32_t Cy_Handle32Make(
    u32 nIndex,
    u32 nGeneration ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API handle64_t Cy_Handle64Make(
    u32 nIndex,
    u32 nGeneration,
    u32 nType ) noexcept;

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
