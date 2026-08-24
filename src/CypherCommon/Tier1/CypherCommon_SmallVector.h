//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SmallVector.h
//  Purpose: Declares growable arrays with inline element storage.
//  Details: SmallVector avoids heap allocation up to nInlineCapacity and spills to
//           its explicit allocator only when growth exceeds inline storage.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Small Vector Contract

The first nInlineCapacity objects can live inside the record. Once spilled, pData addresses an
allocator-owned block and inlineStorage contains no live objects. ShrinkToFit may move a small
heap-backed sequence home again, so callers must treat every mutating operation as pointer-invalidating.
================
*/

#ifndef CYPHER_COMMON_TIER1_SMALLVECTOR_H
#define CYPHER_COMMON_TIER1_SMALLVECTOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t, usize nInlineCapacity>
struct small_vector_t {
    static_assert( is_object_v<type_t>, "small_vector_t requires an object type." );
    static_assert( !is_array_v<type_t>, "small_vector_t does not store array elements." );

    small_vector_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( small_vector_t );
    ~small_vector_t() noexcept;

    alignas( type_t ) byte inlineStorage[
        ( nInlineCapacity > 0u ? nInlineCapacity : 1u ) * sizeof( type_t )]{}; // Raw local object storage.
    type_t *pData{ nullptr };                     // Inline storage or the current heap block.
    usize nCount{ 0u };                           // Number of constructed elements.
    usize nCapacity{ nInlineCapacity };            // Slots available at the current storage address.
    const allocator_t *pAllocator{ nullptr };     // Used only when the sequence spills to the heap.
};

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_Init(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    const allocator_t *pAllocator ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_Shutdown(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_Clear(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_IsValid(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD usize SmallVector_Count(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD usize SmallVector_Capacity(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD type_t *SmallVector_Data(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD const type_t *SmallVector_Data(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_IsEmpty(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_Reserve(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize nCapacity ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_Resize(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize nCount ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_ShrinkToFit(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_PushBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    const type_t &value ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_PushBackMove(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    type_t &&value ) noexcept;

template <typename type_t, usize nInlineCapacity, typename... args_t>
CYPHER_NODISCARD type_t *SmallVector_EmplaceBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    args_t &&... args ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_Insert(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex,
    const type_t &value ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_Append(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    span_t<const type_t> values ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_PopBack(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_Erase(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex,
    usize nCount = 1u ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_EraseSwap(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD type_t *SmallVector_At(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD const type_t *SmallVector_At(
    const small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD type_t *SmallVector_Front(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD const type_t *SmallVector_Front(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD type_t *SmallVector_Back(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD const type_t *SmallVector_Back(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD span_t<type_t> SmallVector_Span(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD span_t<const type_t> SmallVector_Span(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD bool_t SmallVector_UsesInlineStorage(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_Move(
    small_vector_t<type_t, nInlineCapacity> *pDest,
    small_vector_t<type_t, nInlineCapacity> *pSource ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_SMALLVECTOR_INL
    #include "CypherCommon_SmallVector.inl"
#endif

#endif // CYPHER_COMMON_TIER1_SMALLVECTOR_H
