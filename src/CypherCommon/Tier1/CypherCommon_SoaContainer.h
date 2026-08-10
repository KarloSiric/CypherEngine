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

constexpr usize CY_SOA_MAX_COLUMNS = 16u;

struct soa_column_desc_t {
    usize cbElement{ 0u };
    usize alignment{ 1u };
};

struct soa_desc_t {
    const soa_column_desc_t *pColumns{ nullptr };
    usize nColumnCount{ 0u };
    const allocator_t *pAllocator{ nullptr };
    usize nInitialCapacity{ 0u };
};

struct soa_container_t {
    soa_container_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( soa_container_t );
    ~soa_container_t() noexcept;

    void *pAllocation{ nullptr };
    void *pColumns[CY_SOA_MAX_COLUMNS]{};
    soa_column_desc_t columns[CY_SOA_MAX_COLUMNS]{};
    usize nColumnCount{ 0u };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
    usize cbAllocation{ 0u };
    usize nAllocationAlignment{ 0u };
    const allocator_t *pAllocator{ nullptr };
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
