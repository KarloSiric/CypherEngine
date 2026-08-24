//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SoaContainer.h
//  Purpose: Declares descriptor-driven structure-of-arrays storage.
//  Details: SoaContainer owns aligned columns for trivially copyable component data.
//           Typed domain wrappers should sit above this low-level untyped primitive.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SOACONTAINER_H
#define CYPHER_COMMON_TIER1_SOACONTAINER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

constexpr usize CY_SOA_MAX_COLUMNS = 16u; // Fixed descriptor ceiling keeps the record allocation-free.

// All columns share one allocation but begin at independently aligned offsets. The container
// stores bytes only; callers own construction rules for non-trivial element types.

struct soa_column_desc_t {
    usize cbElement{ 0u }; // Byte stride of one element in this column.
    usize alignment{ 1u }; // Required base alignment for this column.
};

struct soa_desc_t {
    const soa_column_desc_t *pColumns{ nullptr }; // Borrowed descriptors consumed during Init.
    usize nColumnCount{ 0u };                     // Descriptor count, at most CY_SOA_MAX_COLUMNS.
    const allocator_t *pAllocator{ nullptr };     // Owns the combined column allocation.
    usize nInitialCapacity{ 0u };                 // Initial row capacity.
};

struct soa_container_t {
    soa_container_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( soa_container_t );
    ~soa_container_t() noexcept;

    void *pAllocation{ nullptr };                    // Base address released by pAllocator.
    void *pColumns[CY_SOA_MAX_COLUMNS]{};            // Aligned base of each active column.
    soa_column_desc_t columns[CY_SOA_MAX_COLUMNS]{}; // Owned copy of active descriptors.
    usize nColumnCount{ 0u };                        // Number of valid entries in both arrays.
    usize nCount{ 0u };                              // Logical rows exposed to callers.
    usize nCapacity{ 0u };                           // Rows reserved in every column.
    usize cbAllocation{ 0u };                        // Exact combined allocation byte count.
    usize nAllocationAlignment{ 0u };                // Alignment used to release pAllocation.
    const allocator_t *pAllocator{ nullptr };        // Allocator fixed at initialization.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SoaContainer_Init(
    soa_container_t *pContainer,
    const soa_desc_t &desc ) noexcept;

CYPHER_COMMON_API void SoaContainer_Shutdown(
    soa_container_t *pContainer ) noexcept;

CYPHER_COMMON_API void SoaContainer_Clear(
    soa_container_t *pContainer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SoaContainer_IsValid( const soa_container_t *pContainer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SoaContainer_Reserve(
    soa_container_t *pContainer,
    usize nCapacity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SoaContainer_Resize(
    soa_container_t *pContainer,
    usize nCount ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *SoaContainer_Column(
    soa_container_t *pContainer,
    usize iColumn ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const void *SoaContainer_Column(
    const soa_container_t *pContainer,
    usize iColumn ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *SoaContainer_Element(
    soa_container_t *pContainer,
    usize iColumn,
    usize iElement ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const void *SoaContainer_Element(
    const soa_container_t *pContainer,
    usize iColumn,
    usize iElement ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize SoaContainer_Count( const soa_container_t *pContainer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize SoaContainer_Capacity( const soa_container_t *pContainer ) noexcept;

CYPHER_COMMON_API void SoaContainer_EraseSwap(
    soa_container_t *pContainer,
    usize iElement ) noexcept;

// Transfers storage without copying any column data. Destination must be empty.
CYPHER_COMMON_API void SoaContainer_Move(
    soa_container_t *pDestination,
    soa_container_t *pSource ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SOACONTAINER_H
