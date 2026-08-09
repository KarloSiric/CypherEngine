//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Array.inl
//  Purpose: Implements allocator-backed exact-size arrays.
//  Details: Array operations preserve object lifetimes, allocator provenance,
//           alignment, and the original value on ordinary allocation failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_ARRAY_INL
#define CYPHER_COMMON_TIER1_ARRAY_INL

#ifndef CYPHER_COMMON_TIER1_ARRAY_H
    #include "CypherCommon_Array.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContainerOps.inl"

namespace cypher::common
{

namespace detail
{

template <typename type_t>
bool_t Array_IsCanonicalEmpty( const array_t<type_t> &array ) noexcept
{
    return array.pData == nullptr &&
           array.nCount == 0u &&
           array.pAllocator == nullptr;
}

template <typename type_t>
void Array_Reset( array_t<type_t> &array ) noexcept
{
    array.pData = nullptr;
    array.nCount = 0u;
    array.pAllocator = nullptr;
}

} // namespace detail

template <typename type_t>
array_t<type_t>::~array_t() noexcept
{
    Array_Shutdown( this );
}

template <typename type_t>
bool_t Array_Init(
    array_t<type_t> *pArray,
    const allocator_t *pAllocator,
    usize nCount ) noexcept
{
    const bool_t bValidDestination =
        pArray != nullptr && detail::Array_IsCanonicalEmpty( *pArray );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "Array_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "Array_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    usize cbSize = 0u;
    const bool_t bValidByteCount =
        Cy_TryArrayByteCount<type_t>( nCount, cbSize );
    CY_ASSERT_MSG( bValidByteCount, "Array_Init byte count overflowed." );
    if ( !bValidByteCount ) {
        return CY_FALSE;
    }

    if ( nCount == 0u ) {
        pArray->pAllocator = pAllocator;
        return CY_TRUE;
    }

    type_t *pData = Allocator_AllocateArrayStorage<type_t>(
        pAllocator,
        nCount );
    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    Container_DefaultConstructRange( pData, nCount );
    pArray->pData = pData;
    pArray->nCount = nCount;
    pArray->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename type_t>
void Array_Shutdown( array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Shutdown requires a valid array." );
    if ( !bValidArray ) {
        return;
    }

    if ( pArray->pData != nullptr ) {
        Container_DestroyRange( pArray->pData, pArray->nCount );
        Allocator_FreeArrayStorage(
            pArray->pAllocator,
            pArray->pData,
            pArray->nCount );
    }

    detail::Array_Reset( *pArray );
}

template <typename type_t>
bool_t Array_Resize(
    array_t<type_t> *pArray,
    usize nCount ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    const bool_t bAllocatorBound =
        bValidArray && Allocator_IsValid( pArray->pAllocator );
    CY_ASSERT_MSG( bValidArray, "Array_Resize requires a valid array." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Array_Resize requires an initialized allocator binding." );
    if ( !bValidArray || !bAllocatorBound ) {
        return CY_FALSE;
    }

    if ( nCount == pArray->nCount ) {
        return CY_TRUE;
    }
    if ( nCount == 0u ) {
        Array_Clear( pArray );
        return CY_TRUE;
    }

    usize cbSize = 0u;
    const bool_t bValidByteCount =
        Cy_TryArrayByteCount<type_t>( nCount, cbSize );
    CY_ASSERT_MSG( bValidByteCount, "Array_Resize byte count overflowed." );
    if ( !bValidByteCount ) {
        return CY_FALSE;
    }

    type_t *pNewData = Allocator_AllocateArrayStorage<type_t>(
        pArray->pAllocator,
        nCount );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    const usize nPreserved = pArray->nCount < nCount
        ? pArray->nCount
        : nCount;
    Container_RelocateConstructRange(
        pNewData,
        pArray->pData,
        nPreserved );
    Container_DefaultConstructRange(
        pNewData + nPreserved,
        nCount - nPreserved );

    if ( pArray->pData != nullptr ) {
        Container_DestroyRange( pArray->pData, pArray->nCount );
        Allocator_FreeArrayStorage(
            pArray->pAllocator,
            pArray->pData,
            pArray->nCount );
    }

    pArray->pData = pNewData;
    pArray->nCount = nCount;
    return CY_TRUE;
}

template <typename type_t>
void Array_Clear( array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Clear requires a valid array." );
    if ( !bValidArray ) {
        return;
    }

    if ( pArray->pData != nullptr ) {
        Container_DestroyRange( pArray->pData, pArray->nCount );
        Allocator_FreeArrayStorage(
            pArray->pAllocator,
            pArray->pData,
            pArray->nCount );
    }

    pArray->pData = nullptr;
    pArray->nCount = 0u;
}

template <typename type_t>
bool_t Array_IsValid( const array_t<type_t> *pArray ) noexcept
{
    if ( pArray == nullptr ) {
        return CY_FALSE;
    }

    if ( pArray->pData == nullptr ) {
        return pArray->nCount == 0u &&
               ( pArray->pAllocator == nullptr ||
                 Allocator_IsValid( pArray->pAllocator ) );
    }

    usize cbSize = 0u;
    return pArray->nCount > 0u &&
           Cy_TryArrayByteCount<type_t>( pArray->nCount, cbSize ) &&
           Allocator_IsValid( pArray->pAllocator ) &&
           Cy_AlignIsPointerAligned( pArray->pData, alignof( type_t ) );
}

template <typename type_t>
usize Array_Count( const array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Count requires a valid array." );
    return bValidArray ? pArray->nCount : 0u;
}

template <typename type_t>
bool_t Array_IsEmpty( const array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_IsEmpty requires a valid array." );
    return bValidArray ? pArray->nCount == 0u : CY_TRUE;
}

template <typename type_t>
type_t *Array_Data( array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Data requires a valid array." );
    return bValidArray ? pArray->pData : nullptr;
}

template <typename type_t>
const type_t *Array_Data( const array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Data requires a valid array." );
    return bValidArray ? pArray->pData : nullptr;
}

template <typename type_t>
type_t *Array_At(
    array_t<type_t> *pArray,
    usize iIndex ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    const bool_t bIndexInRange = bValidArray && iIndex < pArray->nCount;
    CY_ASSERT_MSG( bValidArray, "Array_At requires a valid array." );
    CY_ASSERT_MSG( bIndexInRange, "Array_At index is outside the array." );
    return bIndexInRange ? pArray->pData + iIndex : nullptr;
}

template <typename type_t>
const type_t *Array_At(
    const array_t<type_t> *pArray,
    usize iIndex ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    const bool_t bIndexInRange = bValidArray && iIndex < pArray->nCount;
    CY_ASSERT_MSG( bValidArray, "Array_At requires a valid array." );
    CY_ASSERT_MSG( bIndexInRange, "Array_At index is outside the array." );
    return bIndexInRange ? pArray->pData + iIndex : nullptr;
}

template <typename type_t>
type_t *Array_Front( array_t<type_t> *pArray ) noexcept
{
    return Array_At( pArray, 0u );
}

template <typename type_t>
const type_t *Array_Front( const array_t<type_t> *pArray ) noexcept
{
    return Array_At( pArray, 0u );
}

template <typename type_t>
type_t *Array_Back( array_t<type_t> *pArray ) noexcept
{
    const usize nCount = Array_Count( pArray );
    return nCount > 0u ? Array_At( pArray, nCount - 1u ) : nullptr;
}

template <typename type_t>
const type_t *Array_Back( const array_t<type_t> *pArray ) noexcept
{
    const usize nCount = Array_Count( pArray );
    return nCount > 0u ? Array_At( pArray, nCount - 1u ) : nullptr;
}

template <typename type_t>
span_t<type_t> Array_Span( array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Span requires a valid array." );
    return bValidArray
        ? span_t<type_t>{ pArray->pData, pArray->nCount }
        : span_t<type_t>{};
}

template <typename type_t>
span_t<const type_t> Array_Span(
    const array_t<type_t> *pArray ) noexcept
{
    const bool_t bValidArray = Array_IsValid( pArray );
    CY_ASSERT_MSG( bValidArray, "Array_Span requires a valid array." );
    return bValidArray
        ? span_t<const type_t>{ pArray->pData, pArray->nCount }
        : span_t<const type_t>{};
}

template <typename type_t>
void Array_Move(
    array_t<type_t> *pDest,
    array_t<type_t> *pSource ) noexcept
{
    const bool_t bDistinctArrays =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctArrays && detail::Array_IsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctArrays && Array_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctArrays && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "Array_Move requires distinct arrays and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    pDest->pData = pSource->pData;
    pDest->nCount = pSource->nCount;
    pDest->pAllocator = pSource->pAllocator;
    detail::Array_Reset( *pSource );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAY_INL
