//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UniqueId.h
//  Purpose: Declares portable 128-bit UUID values and conversion.
//  Details: Random creation requires a secure platform random source. Parsing and
//           formatting use canonical lowercase RFC 4122 text with hyphens.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_UNIQUEID_H
#define CYPHER_COMMON_TIER1_UNIQUEID_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr usize CY_UNIQUE_ID_BYTE_COUNT = 16u;
constexpr usize CY_UNIQUE_ID_STRING_LENGTH = 36u;
constexpr usize CY_UNIQUE_ID_STRING_CAPACITY = CY_UNIQUE_ID_STRING_LENGTH + 1u;

struct unique_id_t {
    byte bytes[CY_UNIQUE_ID_BYTE_COUNT]{};
};

constexpr unique_id_t CY_UNIQUE_ID_INVALID{};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UniqueId_CreateRandom( unique_id_t *pIdOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UniqueId_FromBytes(
    const_byte_span_t bytes,
    unique_id_t *pIdOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UniqueId_FromString(
    string_view_t text,
    unique_id_t *pIdOut ) noexcept;

// Writes canonical lowercase 8-4-4-4-12 UUID text and a null terminator.
// Returns 36 on success and zero when the destination contract is not satisfied.
CYPHER_NODISCARD CYPHER_COMMON_API
usize UniqueId_ToString(
    unique_id_t id,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UniqueId_IsValid( unique_id_t id ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t UniqueId_Equals( unique_id_t left, unique_id_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i32 UniqueId_Compare( unique_id_t left, unique_id_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNIQUEID_H
