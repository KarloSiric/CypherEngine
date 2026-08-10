//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ResourceId.h
//  Purpose: Declares compact deterministic resource identifiers.
//  Details: Resource IDs derive from normalized virtual paths plus resource type. The
//           asset database must retain canonical text to detect theoretical hash collisions.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RESOURCEID_H
#define CYPHER_COMMON_TIER1_RESOURCEID_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

using resource_type_id_t = u32;

constexpr usize CY_RESOURCE_ID_STRING_LENGTH = 16u;
constexpr usize CY_RESOURCE_ID_STRING_CAPACITY = CY_RESOURCE_ID_STRING_LENGTH + 1u;

struct resource_id_t {
    u64 value{ 0u };
};

constexpr resource_id_t CY_RESOURCE_ID_INVALID{};

// Type names are ASCII case-insensitive. Empty or non-ASCII names are invalid.
CYPHER_NODISCARD CYPHER_COMMON_API
resource_type_id_t ResourceTypeId_FromName( string_view_t typeName ) noexcept;

// normalizedVirtualPath must already satisfy the VFS canonical-path policy.
CYPHER_NODISCARD CYPHER_COMMON_API
resource_id_t ResourceId_FromPath(
    string_view_t normalizedVirtualPath,
    resource_type_id_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceId_IsValid( resource_id_t id ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceId_Equals( resource_id_t left, resource_id_t right ) noexcept;

// Writes 16 lowercase hexadecimal digits and a null terminator.
CYPHER_NODISCARD CYPHER_COMMON_API
usize ResourceId_ToString(
    resource_id_t id,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ResourceId_FromString(
    string_view_t text,
    resource_id_t *pIdOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RESOURCEID_H
