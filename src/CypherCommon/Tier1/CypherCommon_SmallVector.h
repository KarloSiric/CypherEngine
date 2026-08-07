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
    small_vector_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( small_vector_t );

    alignas( type_t ) byte inlineStorage[
        ( nInlineCapacity > 0u ? nInlineCapacity : 1u ) * sizeof( type_t )]{};
    type_t *pData{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ nInlineCapacity };
    const allocator_t *pAllocator{ nullptr };
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
CYPHER_NODISCARD usize SmallVector_Count(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

template <typename type_t, usize nInlineCapacity>
CYPHER_NODISCARD usize SmallVector_Capacity(
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
CYPHER_NODISCARD bool_t SmallVector_PushBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    const type_t &value ) noexcept;

template <typename type_t, usize nInlineCapacity, typename... args_t>
CYPHER_NODISCARD type_t *SmallVector_EmplaceBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    args_t &&... args ) noexcept;

template <typename type_t, usize nInlineCapacity>
void SmallVector_PopBack(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept;

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

#endif // CYPHER_COMMON_TIER1_SMALLVECTOR_H
