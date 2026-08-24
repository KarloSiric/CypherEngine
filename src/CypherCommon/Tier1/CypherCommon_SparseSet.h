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

/*
================
Sparse Set Contract

sparse[key] stores a dense index, while denseKeys[index] proves which key occupies that index.
That reverse check prevents uninitialized sparse entries from becoming false hits. Erase swaps the
last dense element into the hole, so dense order and all value pointers are unstable.
================
*/

#ifndef CYPHER_COMMON_TIER1_SPARSESET_H
#define CYPHER_COMMON_TIER1_SPARSESET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Vector.h"

namespace cypher::common
{

constexpr u32 CY_SPARSE_SET_INVALID_DENSE_INDEX = CY_U32_MAX; // Empty sparse entry sentinel.

template <typename value_t>
struct sparse_set_t {
    vector_t<u32> sparse{};          // Key-indexed dense positions or the invalid sentinel.
    vector_t<u32> denseKeys{};       // Dense position back to its owning sparse key.
    vector_t<value_t> denseValues{}; // Packed live values parallel to denseKeys.
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
CYPHER_NODISCARD bool_t SparseSet_IsValid(
    const sparse_set_t<value_t> *pSet ) noexcept;

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
CYPHER_NODISCARD const value_t *SparseSet_Find(
    const sparse_set_t<value_t> *pSet,
    u32 key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_Contains(
    const sparse_set_t<value_t> *pSet,
    u32 key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_Erase(
    sparse_set_t<value_t> *pSet,
    u32 key ) noexcept;

template <typename value_t>
CYPHER_NODISCARD span_t<value_t> SparseSet_Values(
    sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
CYPHER_NODISCARD span_t<const value_t> SparseSet_Values(
    const sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
CYPHER_NODISCARD span_t<const u32> SparseSet_Keys(
    const sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
CYPHER_NODISCARD usize SparseSet_Count(
    const sparse_set_t<value_t> *pSet ) noexcept;

template <typename value_t>
CYPHER_NODISCARD bool_t SparseSet_IsEmpty(
    const sparse_set_t<value_t> *pSet ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_SPARSESET_INL
    #include "CypherCommon_SparseSet.inl"
#endif

#endif // CYPHER_COMMON_TIER1_SPARSESET_H
