//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Stats.h
//  Purpose: Defines the bounded process-wide registry for runtime statistics.
//  Details: Registrations receive stable IDs; values may change concurrently while
//           names and categories remain fixed until the registry is cleared.
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
constexpr stat_id_t CY_STAT_ID_INVALID = 0u; // Registration IDs begin at one.
constexpr usize CY_STATS_MAX_COUNT = 1024u; // Fixed registry capacity; no growth allocation.
constexpr usize CY_STAT_NAME_MAX = 64u;      // Includes the null terminator.
constexpr usize CY_STAT_CATEGORY_MAX = 48u;  // Includes the null terminator.
constexpr usize CY_STAT_DESCRIPTION_MAX = 128u; // Includes the null terminator.

enum class stat_value_type_t : u8 {
    I64 = 0u,
    U64,
    F64
};

struct stat_value_t {
    stat_value_type_t type = stat_value_type_t::I64; // Selects the active union member.
    union {
        i64 i64Value; // Signed counter or gauge.
        u64 u64Value; // Unsigned counter or gauge.
        f64 f64Value; // Floating-point gauge.
    };
};

struct stat_desc_t {
    const char *pszName;        // Required registration name; copied into registry storage.
    const char *pszCategory;    // Optional grouping name; copied into registry storage.
    const char *pszDescription; // Optional explanation; copied into registry storage.
    stat_value_type_t type = stat_value_type_t::I64; // Type accepted by future updates.
};

struct stat_snapshot_t {
    stat_id_t id;                              // Stable registration ID.
    char szName[CY_STAT_NAME_MAX];             // Owned null-terminated name copy.
    char szCategory[CY_STAT_CATEGORY_MAX];     // Owned null-terminated category copy.
    char szDescription[CY_STAT_DESCRIPTION_MAX]; // Owned null-terminated description copy.
    stat_value_t value;                        // Value observed while taking the snapshot.
};

struct stats_registry_info_t {
    usize nRegisteredCount;    // Live registrations currently addressable by ID.
    usize nCapacity;           // Fixed maximum registration count.
    u64 nDroppedRegistrations; // Failed registrations since the last full clear.
};

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsRegister(
    const stat_desc_t &desc,
    stat_id_t *pOutId ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API stat_id_t Cy_StatsFind(
    const char *pszName ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsSetI64(
    stat_id_t id,
    i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsSetU64(
    stat_id_t id,
    u64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsSetF64(
    stat_id_t id,
    f64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsAddI64(
    stat_id_t id,
    i64 delta ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsAddU64(
    stat_id_t id,
    u64 delta ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsAddF64(
    stat_id_t id,
    f64 delta ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsGet(
    stat_id_t id,
    stat_value_t *pOutValue ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsGetByName(
    const char *pszName,
    stat_value_t *pOutValue ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StatsGetSnapshot(
    usize nIndex,
    stat_snapshot_t *pOutSnapshot ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API stats_registry_info_t
Cy_StatsGetRegistryInfo() noexcept;

// Zeroes values while preserving registrations and stable IDs.
CYPHER_COMMON_API void Cy_StatsResetValues() noexcept;

// Clears all registrations. Call only while stat users are quiescent.
CYPHER_COMMON_API void Cy_StatsClearRegistry() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_STATS_H
