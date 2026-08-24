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

// A fixed-memory record describes writable storage but does not own or initialize it. Address
// queries use half-open ranges so one-past-the-end is accepted only for an empty subrange.

struct fixed_memory_t {
    byte *pData{ nullptr }; // Borrowed first byte; nullptr is valid only for an empty region.
    usize cbSize{ 0u };     // Total writable bytes in the represented region.
};

// Creates a writable view over caller-owned storage.
CYPHER_NODISCARD CYPHER_COMMON_API
fixed_memory_t FixedMemory_Make( void *pData, usize cbSize ) noexcept;

// Converts a writable byte span into fixed-memory form.
CYPHER_NODISCARD CYPHER_COMMON_API
fixed_memory_t FixedMemory_FromSpan( byte_span_t memory ) noexcept;

// Checks the pointer/size invariant: only non-empty regions require data.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_IsValid( fixed_memory_t memory ) noexcept;

// Returns true when the represented region contains no bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_IsEmpty( fixed_memory_t memory ) noexcept;

// Returns the writable base address after validating the region.
CYPHER_NODISCARD CYPHER_COMMON_API
byte *FixedMemory_Data( fixed_memory_t memory ) noexcept;

// Returns the represented byte count after validating the region.
CYPHER_NODISCARD CYPHER_COMMON_API
usize FixedMemory_Size( fixed_memory_t memory ) noexcept;

// Converts the region back to a writable byte span.
CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t FixedMemory_Span( fixed_memory_t memory ) noexcept;

// Tests the half-open address range [pData, pData + cbSize).
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_ContainsAddress(
    fixed_memory_t memory,
    const void *pAddress ) noexcept;

// Tests a byte range. A zero-byte range may begin one byte past the region.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FixedMemory_ContainsRange(
    fixed_memory_t memory,
    const void *pAddress,
    usize cbRange ) noexcept;

// Returns the byte offset of an address, or CY_INVALID_SIZE when not contained.
CYPHER_NODISCARD CYPHER_COMMON_API
usize FixedMemory_OffsetOf(
    fixed_memory_t memory,
    const void *pAddress ) noexcept;

// Returns a bounded subspan; an oversized count is clamped to available bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t FixedMemory_Subspan(
    fixed_memory_t memory,
    usize iOffset,
    usize cbSize ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDMEMORY_H
