//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Vector.h
//  Purpose: Declares allocator-backed contiguous growable arrays.
//  Details: Vector provides explicit capacity control, contiguous storage, and
//           amortized growth without exceptions or hidden allocator selection.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_VECTOR_H
#define CYPHER_COMMON_TIER1_VECTOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t>
struct vector_t {
    vector_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( vector_t );

    type_t *pData{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
    const allocator_t *pAllocator{ nullptr };
};

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_Init(
    vector_t<type_t> *pVector,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u ) noexcept;

template <typename type_t>
void Vector_Shutdown( vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
void Vector_Clear( vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_IsValid(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_IsEmpty(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Vector_Count(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Vector_Capacity(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_Reserve(
    vector_t<type_t> *pVector,
    usize nCapacity ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_Resize(
    vector_t<type_t> *pVector,
    usize nCount ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_ShrinkToFit(
    vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_PushBack(
    vector_t<type_t> *pVector,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_PushBackMove(
    vector_t<type_t> *pVector,
    type_t &&value ) noexcept;

template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *Vector_EmplaceBack(
    vector_t<type_t> *pVector,
    args_t &&... args ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_Insert(
    vector_t<type_t> *pVector,
    usize iIndex,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Vector_Append(
    vector_t<type_t> *pVector,
    span_t<const type_t> values ) noexcept;

template <typename type_t>
void Vector_PopBack( vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
void Vector_Erase(
    vector_t<type_t> *pVector,
    usize iIndex,
    usize nCount = 1u ) noexcept;

template <typename type_t>
void Vector_EraseSwap(
    vector_t<type_t> *pVector,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Vector_At(
    vector_t<type_t> *pVector,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Vector_At(
    const vector_t<type_t> *pVector,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Vector_Front(
    vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Vector_Front(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Vector_Back(
    vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Vector_Back(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Vector_Span(
    vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<const type_t> Vector_Span(
    const vector_t<type_t> *pVector ) noexcept;

template <typename type_t>
void Vector_Move(
    vector_t<type_t> *pDest,
    vector_t<type_t> *pSource ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_VECTOR_H
