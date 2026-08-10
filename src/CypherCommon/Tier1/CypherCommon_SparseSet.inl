//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SparseSet.inl
//  Purpose: Implements dense values indexed by sparse unsigned keys.
//  Details: Lookups are constant time and iteration is contiguous. Erase moves the
//           final dense element into the removed slot, so dense order is unstable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SPARSESET_INL
#define CYPHER_COMMON_TIER1_SPARSESET_INL

#ifndef CYPHER_COMMON_TIER1_SPARSESET_H
    #include "CypherCommon_SparseSet.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename value_t>
bool_t SparseSet_IsInitialized( const sparse_set_t<value_t> *pSet ) noexcept
{
    return SparseSet_IsValid( pSet ) && pSet->sparse.pAllocator != nullptr;
}

template <typename value_t>
usize SparseSet_FindDenseIndex(
    const sparse_set_t<value_t> *pSet,
    u32 key ) noexcept
{
    if ( !SparseSet_IsInitialized( pSet ) ||
         static_cast<usize>( key ) >= pSet->sparse.nCount ) {
        return CY_INVALID_SIZE;
    }

    const u32 iDense32 = pSet->sparse.pData[key];
    if ( iDense32 == CY_SPARSE_SET_INVALID_DENSE_INDEX ) {
        return CY_INVALID_SIZE;
    }
    const usize iDense = static_cast<usize>( iDense32 );
    return iDense < pSet->denseKeys.nCount &&
           pSet->denseKeys.pData[iDense] == key
        ? iDense
        : CY_INVALID_SIZE;
}

} // namespace detail

template <typename value_t>
bool_t SparseSet_Init(
    sparse_set_t<value_t> *pSet,
    const allocator_t *pAllocator,
    usize nSparseCapacity ) noexcept
{
    const bool_t bValidDestination = pSet != nullptr &&
        SparseSet_IsValid( pSet ) &&
        pSet->sparse.pAllocator == nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "SparseSet_Init requires a canonical empty destination." );
    if ( !bValidDestination || !Allocator_IsValid( pAllocator ) ) {
        return CY_FALSE;
    }

    if ( !Vector_Init( &pSet->sparse, pAllocator ) ) {
        return CY_FALSE;
    }
    if ( !Vector_Init( &pSet->denseKeys, pAllocator ) ) {
        Vector_Shutdown( &pSet->sparse );
        return CY_FALSE;
    }
    if ( !Vector_Init( &pSet->denseValues, pAllocator ) ) {
        Vector_Shutdown( &pSet->denseKeys );
        Vector_Shutdown( &pSet->sparse );
        return CY_FALSE;
    }
    if ( nSparseCapacity > 0u &&
         !SparseSet_ReserveKeys( pSet, nSparseCapacity ) ) {
        SparseSet_Shutdown( pSet );
        return CY_FALSE;
    }
    return CY_TRUE;
}

template <typename value_t>
void SparseSet_Shutdown( sparse_set_t<value_t> *pSet ) noexcept
{
    if ( pSet == nullptr ) {
        return;
    }
    const bool_t bValidSet = SparseSet_IsValid( pSet );
    CY_ASSERT_MSG( bValidSet, "SparseSet_Shutdown requires a valid set." );
    if ( !bValidSet ) {
        return;
    }

    Vector_Shutdown( &pSet->denseValues );
    Vector_Shutdown( &pSet->denseKeys );
    Vector_Shutdown( &pSet->sparse );
}

template <typename value_t>
void SparseSet_Clear( sparse_set_t<value_t> *pSet ) noexcept
{
    const bool_t bInitialized = detail::SparseSet_IsInitialized( pSet );
    CY_ASSERT_MSG( bInitialized, "SparseSet_Clear requires an initialized set." );
    if ( !bInitialized ) {
        return;
    }

    for ( usize iKey = 0u; iKey < pSet->sparse.nCount; ++iKey ) {
        pSet->sparse.pData[iKey] = CY_SPARSE_SET_INVALID_DENSE_INDEX;
    }
    Vector_Clear( &pSet->denseValues );
    Vector_Clear( &pSet->denseKeys );
}

template <typename value_t>
bool_t SparseSet_IsValid( const sparse_set_t<value_t> *pSet ) noexcept
{
    if ( pSet == nullptr ||
         !Vector_IsValid( &pSet->sparse ) ||
         !Vector_IsValid( &pSet->denseKeys ) ||
         !Vector_IsValid( &pSet->denseValues ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = pSet->sparse.pAllocator;
    return pSet->denseKeys.pAllocator == pAllocator &&
           pSet->denseValues.pAllocator == pAllocator &&
           pSet->denseKeys.nCount == pSet->denseValues.nCount &&
           pSet->denseKeys.nCount < static_cast<usize>( CY_U32_MAX );
}

template <typename value_t>
bool_t SparseSet_ReserveKeys(
    sparse_set_t<value_t> *pSet,
    usize nSparseCapacity ) noexcept
{
    const bool_t bInitialized = detail::SparseSet_IsInitialized( pSet );
    CY_ASSERT_MSG(
        bInitialized,
        "SparseSet_ReserveKeys requires an initialized set." );
    if ( !bInitialized ||
         nSparseCapacity > static_cast<usize>( CY_U32_MAX ) ) {
        return CY_FALSE;
    }
    if ( nSparseCapacity <= pSet->sparse.nCount ) {
        return CY_TRUE;
    }

    const usize nPreviousCount = pSet->sparse.nCount;
    if ( !Vector_Resize( &pSet->sparse, nSparseCapacity ) ) {
        return CY_FALSE;
    }
    for ( usize iKey = nPreviousCount; iKey < nSparseCapacity; ++iKey ) {
        pSet->sparse.pData[iKey] = CY_SPARSE_SET_INVALID_DENSE_INDEX;
    }
    return CY_TRUE;
}

template <typename value_t>
value_t *SparseSet_Insert(
    sparse_set_t<value_t> *pSet,
    u32 key,
    const value_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_assignable_v<value_t>,
        "SparseSet replacement requires nothrow copy assignment." );

    const bool_t bInitialized = detail::SparseSet_IsInitialized( pSet );
    CY_ASSERT_MSG( bInitialized, "SparseSet_Insert requires an initialized set." );
    if ( !bInitialized || key == CY_U32_MAX ) {
        return nullptr;
    }

    const usize iExisting = detail::SparseSet_FindDenseIndex( pSet, key );
    if ( iExisting != CY_INVALID_SIZE ) {
        pSet->denseValues.pData[iExisting] = value;
        return pSet->denseValues.pData + iExisting;
    }

    if ( !SparseSet_ReserveKeys( pSet, static_cast<usize>( key ) + 1u ) ||
         pSet->denseKeys.nCount >= static_cast<usize>( CY_U32_MAX ) ) {
        return nullptr;
    }

    const u32 iDense = static_cast<u32>( pSet->denseKeys.nCount );
    if ( !Vector_PushBack( &pSet->denseKeys, key ) ) {
        return nullptr;
    }
    if ( !Vector_PushBack( &pSet->denseValues, value ) ) {
        Vector_PopBack( &pSet->denseKeys );
        return nullptr;
    }

    pSet->sparse.pData[key] = iDense;
    return pSet->denseValues.pData + iDense;
}

template <typename value_t>
value_t *SparseSet_Find(
    sparse_set_t<value_t> *pSet,
    u32 key ) noexcept
{
    return const_cast<value_t *>( SparseSet_Find(
        static_cast<const sparse_set_t<value_t> *>( pSet ),
        key ) );
}

template <typename value_t>
const value_t *SparseSet_Find(
    const sparse_set_t<value_t> *pSet,
    u32 key ) noexcept
{
    const usize iDense = detail::SparseSet_FindDenseIndex( pSet, key );
    return iDense != CY_INVALID_SIZE
        ? pSet->denseValues.pData + iDense
        : nullptr;
}

template <typename value_t>
bool_t SparseSet_Contains(
    const sparse_set_t<value_t> *pSet,
    u32 key ) noexcept
{
    return detail::SparseSet_FindDenseIndex( pSet, key ) != CY_INVALID_SIZE;
}

template <typename value_t>
bool_t SparseSet_Erase(
    sparse_set_t<value_t> *pSet,
    u32 key ) noexcept
{
    static_assert(
        std::is_nothrow_move_assignable_v<value_t>,
        "SparseSet erase requires nothrow move assignment." );

    const usize iDense = detail::SparseSet_FindDenseIndex( pSet, key );
    if ( iDense == CY_INVALID_SIZE ) {
        return CY_FALSE;
    }

    const usize iLast = pSet->denseKeys.nCount - 1u;
    if ( iDense != iLast ) {
        const u32 movedKey = pSet->denseKeys.pData[iLast];
        pSet->denseKeys.pData[iDense] = movedKey;
        pSet->denseValues.pData[iDense] =
            static_cast<value_t &&>( pSet->denseValues.pData[iLast] );
        pSet->sparse.pData[movedKey] = static_cast<u32>( iDense );
    }

    Vector_PopBack( &pSet->denseValues );
    Vector_PopBack( &pSet->denseKeys );
    pSet->sparse.pData[key] = CY_SPARSE_SET_INVALID_DENSE_INDEX;
    return CY_TRUE;
}

template <typename value_t>
span_t<value_t> SparseSet_Values( sparse_set_t<value_t> *pSet ) noexcept
{
    return detail::SparseSet_IsInitialized( pSet )
        ? Vector_Span( &pSet->denseValues )
        : span_t<value_t>{};
}

template <typename value_t>
span_t<const value_t> SparseSet_Values(
    const sparse_set_t<value_t> *pSet ) noexcept
{
    return detail::SparseSet_IsInitialized( pSet )
        ? Vector_Span( &pSet->denseValues )
        : span_t<const value_t>{};
}

template <typename value_t>
span_t<const u32> SparseSet_Keys(
    const sparse_set_t<value_t> *pSet ) noexcept
{
    return detail::SparseSet_IsInitialized( pSet )
        ? Vector_Span( &pSet->denseKeys )
        : span_t<const u32>{};
}

template <typename value_t>
usize SparseSet_Count( const sparse_set_t<value_t> *pSet ) noexcept
{
    return detail::SparseSet_IsInitialized( pSet )
        ? pSet->denseValues.nCount
        : 0u;
}

template <typename value_t>
bool_t SparseSet_IsEmpty( const sparse_set_t<value_t> *pSet ) noexcept
{
    return SparseSet_Count( pSet ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SPARSESET_INL
