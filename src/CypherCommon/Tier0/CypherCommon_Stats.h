//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Stats.h
//  Purpose: Declares CypherCommon Tier0 Stats support.
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

#ifndef CYPHER_COMMON_TIER0_STATS_H
#define CYPHER_COMMON_TIER0_STATS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Stats

Named runtime statistics declarations for memory, VFS, pak loading, renderer,
networking and tooling diagnostics.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using stat_id_t = u32;
constexpr stat_id_t CY_STAT_ID_INVALID = 0u;
constexpr usize CY_STATS_MAX_COUNT = 1024u;
constexpr usize CY_STAT_NAME_MAX = 64u;
constexpr usize CY_STAT_CATEGORY_MAX = 48u;
constexpr usize CY_STAT_DESCRIPTION_MAX = 128u;

enum class stat_value_type_t : u8 {
    I64 = 0u,
    U64,
    F64
};

struct stat_value_t {
    stat_value_type_t type = stat_value_type_t::I64;
    union {
        i64 i64Value;
        u64 u64Value;
        f64 f64Value;
    };
};

struct stat_desc_t {
    const char *pszName;
    const char *pszCategory;
    const char *pszDescription;
    stat_value_type_t type = stat_value_type_t::I64;
};

struct stat_snapshot_t {
    stat_id_t id;
    char szName[CY_STAT_NAME_MAX];
    char szCategory[CY_STAT_CATEGORY_MAX];
    char szDescription[CY_STAT_DESCRIPTION_MAX];
    stat_value_t value;
};

struct stats_registry_info_t {
    usize nRegisteredCount;
    usize nCapacity;
    u64 nDroppedRegistrations;
};

[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsRegister(
    const stat_desc_t &desc,
    stat_id_t *pOutId ) noexcept;
[[nodiscard]] CYPHER_COMMON_API stat_id_t Cy_StatsFind(
    const char *pszName ) noexcept;

[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsSetI64(
    stat_id_t id,
    i64 value ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsSetU64(
    stat_id_t id,
    u64 value ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsSetF64(
    stat_id_t id,
    f64 value ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsAddI64(
    stat_id_t id,
    i64 delta ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsAddU64(
    stat_id_t id,
    u64 delta ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsAddF64(
    stat_id_t id,
    f64 delta ) noexcept;

[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsGet(
    stat_id_t id,
    stat_value_t *pOutValue ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsGetByName(
    const char *pszName,
    stat_value_t *pOutValue ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_StatsGetSnapshot(
    usize nIndex,
    stat_snapshot_t *pOutSnapshot ) noexcept;
[[nodiscard]] CYPHER_COMMON_API stats_registry_info_t
Cy_StatsGetRegistryInfo() noexcept;

// Zeroes values while preserving registrations and stable IDs.
CYPHER_COMMON_API void Cy_StatsResetValues() noexcept;

// Clears all registrations. Call only while stat users are quiescent.
CYPHER_COMMON_API void Cy_StatsClearRegistry() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_STATS_H
