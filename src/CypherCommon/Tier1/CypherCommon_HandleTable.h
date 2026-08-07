//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HandleTable.h
//  Purpose: Declares typed storage addressed by generational handles.
//  Details: Removing a value advances its slot generation so stale handles fail
//           validation instead of resolving to a newly inserted object.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HANDLETABLE_H
#define CYPHER_COMMON_TIER1_HANDLETABLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

template <typename type_t>
struct handle_table_slot_t {
    alignas( type_t ) byte storage[sizeof( type_t )]{};
    u32 nGeneration{ 1u };
    u32 iNextFree{ CY_U32_MAX };
    bool_t bOccupied{ CY_FALSE };
};

template <typename type_t>
struct handle_table_t {
    handle_table_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( handle_table_t );

    handle_table_slot_t<type_t> *pSlots{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
    u32 iFreeHead{ CY_U32_MAX };
    const allocator_t *pAllocator{ nullptr };
};

template <typename type_t>
CYPHER_NODISCARD bool_t HandleTable_Init(
    handle_table_t<type_t> *pTable,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u ) noexcept;

template <typename type_t>
void HandleTable_Shutdown( handle_table_t<type_t> *pTable ) noexcept;

template <typename type_t>
void HandleTable_Clear( handle_table_t<type_t> *pTable ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t HandleTable_Reserve(
    handle_table_t<type_t> *pTable,
    usize nCapacity ) noexcept;

template <typename type_t>
CYPHER_NODISCARD handle32_t HandleTable_Insert(
    handle_table_t<type_t> *pTable,
    const type_t &value ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD handle32_t HandleTable_Emplace(
    handle_table_t<type_t> *pTable,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *HandleTable_Get(
    handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t HandleTable_Remove(
    handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HANDLETABLE_H
