//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Array.h
//  Purpose: Declares allocator-backed exact-size arrays.
//  Details: Array owns exactly nCount constructed elements and carries no spare
//           capacity. Use vector_t when repeated growth is expected.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_ARRAY_H
#define CYPHER_COMMON_TIER1_ARRAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t>
struct array_t {
    array_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( array_t );

    type_t *pData{ nullptr };
    usize nCount{ 0u };
    const allocator_t *pAllocator{ nullptr };
};

template <typename type_t>
CYPHER_NODISCARD bool_t Array_Init(
    array_t<type_t> *pArray,
    const allocator_t *pAllocator,
    usize nCount ) noexcept;

template <typename type_t>
void Array_Shutdown( array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Array_Resize(
    array_t<type_t> *pArray,
    usize nCount ) noexcept;

// Destroys all elements and releases storage while retaining the allocator binding.
template <typename type_t>
void Array_Clear( array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Array_IsValid(
    const array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Array_Count(
    const array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Array_At(
    array_t<type_t> *pArray,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Array_At(
    const array_t<type_t> *pArray,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Array_Span(
    array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<const type_t> Array_Span(
    const array_t<type_t> *pArray ) noexcept;

template <typename type_t>
void Array_Move(
    array_t<type_t> *pDest,
    array_t<type_t> *pSource ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAY_H
