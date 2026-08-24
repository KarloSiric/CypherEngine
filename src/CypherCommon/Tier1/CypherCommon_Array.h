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
    static_assert( is_object_v<type_t>, "array_t requires an object type." );
    static_assert( !is_array_v<type_t>, "array_t does not store array elements." );

    array_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( array_t );
    ~array_t() noexcept;

    type_t *pData{ nullptr };                     // Contiguous storage for exactly nCount live objects.
    usize nCount{ 0u };                           // Constructed element count; also the allocation extent.
    const allocator_t *pAllocator{ nullptr };     // Allocator bound for the array's complete lifetime.
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
CYPHER_NODISCARD bool_t Array_IsEmpty(
    const array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Array_Data(
    array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Array_Data(
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
CYPHER_NODISCARD type_t *Array_Front(
    array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Array_Front(
    const array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Array_Back(
    array_t<type_t> *pArray ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Array_Back(
    const array_t<type_t> *pArray ) noexcept;

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

#ifndef CYPHER_COMMON_TIER1_ARRAY_INL
    #include "CypherCommon_Array.inl"
#endif

#endif // CYPHER_COMMON_TIER1_ARRAY_H
