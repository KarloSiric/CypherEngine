//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedMemory.h
//  Purpose: Declares non-owning bounded memory regions.
//  Details: FixedMemory centralizes pointer/range checks over caller-owned storage and
//           performs no allocation or object construction.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FIXEDMEMORY_H
#define CYPHER_COMMON_TIER1_FIXEDMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"

namespace cypher::common
{

struct fixed_memory_t {
    byte *pData{ nullptr };
    usize cbSize{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
fixed_memory_t FixedMemory_FromSpan( byte_span_t memory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_IsValid( fixed_memory_t memory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_ContainsAddress(
    fixed_memory_t memory,
    const void *pAddress ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_ContainsRange(
    fixed_memory_t memory,
    const void *pAddress,
    usize cbRange ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize FixedMemory_OffsetOf(
    fixed_memory_t memory,
    const void *pAddress ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t FixedMemory_Subspan(
    fixed_memory_t memory,
    usize iOffset,
    usize cbSize ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDMEMORY_H
