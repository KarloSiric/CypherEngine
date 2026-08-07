//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SparseSet.h
//  Purpose: Declares dense value storage indexed by sparse integer keys.
//  Details: SparseSet provides constant-time lookup and packed iteration. Erase uses
//           swap removal and therefore may reorder dense elements.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SPARSESET_H
#define CYPHER_COMMON_TIER1_SPARSESET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Vector.h"

namespace cypher::common
{

template <typename value_t>
struct sparse_set_t {
    vector_t<u32> sparse{};
    vector_t<u32> denseKeys{};
    vector_t<value_t> denseValues{};
};

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_Init(
    sparse_set_t<value_t> *pSet,
    const allocator_t *pAllocator,
    usize nSparseCapacity = 0u ) noexcept;

template <typename value_t>
void SparseSet_Shutdown( sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
void SparseSet_Clear( sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_ReserveKeys(
    sparse_set_t<value_t> *pSet,
    usize nSparseCapacity ) noexcept;

template <typename value_t>
CYPHER_NODISCARD value_t *SparseSet_Insert(
    sparse_set_t<value_t> *pSet,
    u32 key,
    const value_t &value ) noexcept;

template <typename value_t>
CYPHER_NODISCARD value_t *SparseSet_Find(
    sparse_set_t<value_t> *pSet,
    u32 key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_Erase(
    sparse_set_t<value_t> *pSet,
    u32 key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD span_t<value_t> SparseSet_Values(
    sparse_set_t<value_t> *pSet ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SPARSESET_H
