//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SmallVector.inl
//  Purpose: Implements growable arrays with inline element storage.
//  Details: Inline elements avoid allocation, heap spill retains explicit
//           allocator ownership, and moves distinguish embedded from heap data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SMALLVECTOR_INL
#define CYPHER_COMMON_TIER1_SMALLVECTOR_INL

#ifndef CYPHER_COMMON_TIER1_SMALLVECTOR_H
    #include "CypherCommon_SmallVector.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContainerOps.inl"

namespace cypher::common
{

namespace detail
{

template <typename type_t, usize nInlineCapacity>
type_t *SmallVector_InlineData(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    if constexpr ( nInlineCapacity == 0u ) {
        return nullptr;
    } else {
        return reinterpret_cast<type_t *>( pVector->inlineStorage );
    }
}

template <typename type_t, usize nInlineCapacity>
const type_t *SmallVector_InlineData(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    if constexpr ( nInlineCapacity == 0u ) {
        return nullptr;
    } else {
        return reinterpret_cast<const type_t *>( pVector->inlineStorage );
    }
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_IsCanonicalEmpty(
    const small_vector_t<type_t, nInlineCapacity> &vector ) noexcept
{
    return vector.pData == nullptr &&
           vector.nCount == 0u &&
           vector.nCapacity == nInlineCapacity &&
           vector.pAllocator == nullptr;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_UsesInlineUnchecked(
    const small_vector_t<type_t, nInlineCapacity> &vector ) noexcept
{
    if constexpr ( nInlineCapacity == 0u ) {
        return CY_FALSE;
    } else {
        return vector.pData == SmallVector_InlineData( &vector );
    }
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_Reset(
    small_vector_t<type_t, nInlineCapacity> &vector ) noexcept
{
    vector.pData = nullptr;
    vector.nCount = 0u;
    vector.nCapacity = nInlineCapacity;
    vector.pAllocator = nullptr;
}

template <typename type_t>
bool_t SmallVector_CalculateGrowth(
    usize nCurrentCapacity,
    usize nRequiredCapacity,
    usize &nCapacityOut ) noexcept
{
    constexpr usize nMinimumCapacity = 8u;
    constexpr usize nMaximumCapacity = CY_USIZE_MAX / sizeof( type_t );
    if ( nRequiredCapacity > nMaximumCapacity ) {
        nCapacityOut = 0u;
        return CY_FALSE;
    }

    usize nCandidate = nCurrentCapacity < nMinimumCapacity
        ? nMinimumCapacity
        : nCurrentCapacity;
    if ( nCandidate < nRequiredCapacity ) {
        const usize nHalf = nCandidate / 2u;
        nCandidate = nCandidate > nMaximumCapacity - nHalf
            ? nMaximumCapacity
            : nCandidate + nHalf;
    }
    if ( nCandidate < nRequiredCapacity ) {
        nCandidate = nRequiredCapacity;
    }

    nCapacityOut = nCandidate;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_EnsureAdditional(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize nAdditionalCount ) noexcept
{
    if ( nAdditionalCount > CY_USIZE_MAX - pVector->nCount ) {
        CY_ASSERT_MSG( CY_FALSE, "SmallVector count overflowed." );
        return CY_FALSE;
    }

    const usize nRequiredCapacity = pVector->nCount + nAdditionalCount;
    if ( nRequiredCapacity <= pVector->nCapacity ) {
        return CY_TRUE;
    }

    usize nGrowthCapacity = 0u;
    const bool_t bValidGrowth = SmallVector_CalculateGrowth<type_t>(
        pVector->nCapacity,
        nRequiredCapacity,
        nGrowthCapacity );
    CY_ASSERT_MSG(
        bValidGrowth,
        "SmallVector growth exceeds addressable storage." );
    return bValidGrowth && SmallVector_Reserve( pVector, nGrowthCapacity );
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_ElementIsInternal(
    const small_vector_t<type_t, nInlineCapacity> &vector,
    const type_t *pValue ) noexcept
{
    if ( vector.pData == nullptr || pValue == nullptr || vector.nCount == 0u ) {
        return CY_FALSE;
    }

    usize cbLiveSize = 0u;
    if ( !Cy_TryArrayByteCount<type_t>( vector.nCount, cbLiveSize ) ) {
        return CY_FALSE;
    }
    const uintptr nBegin = reinterpret_cast<uintptr>( vector.pData );
    const uintptr nValue = reinterpret_cast<uintptr>( pValue );
    return nValue >= nBegin &&
           nValue - nBegin < cbLiveSize &&
           ( nValue - nBegin ) % sizeof( type_t ) == 0u;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_RebaseAppendSource(
    const small_vector_t<type_t, nInlineCapacity> &vector,
    span_t<const type_t> values,
    bool_t &bInternalOut,
    usize &iSourceOut ) noexcept
{
    bInternalOut = CY_FALSE;
    iSourceOut = 0u;
    if ( values.nCount == 0u || vector.pData == nullptr ) {
        return CY_TRUE;
    }

    usize cbStorageSize = 0u;
    usize cbSourceSize = 0u;
    const bool_t bValidSizes =
        Cy_TryArrayByteCount<type_t>( vector.nCapacity, cbStorageSize ) &&
        Cy_TryArrayByteCount<type_t>( values.nCount, cbSourceSize );
    if ( !bValidSizes ) {
        return CY_FALSE;
    }

    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nStorageBegin = reinterpret_cast<uintptr>( vector.pData );
    const uintptr nSourceBegin = reinterpret_cast<uintptr>( values.pData );
    if ( nStorageBegin > nMaximumAddress - cbStorageSize ||
         nSourceBegin > nMaximumAddress - cbSourceSize ) {
        return CY_FALSE;
    }
    const uintptr nStorageEnd = nStorageBegin + cbStorageSize;
    const uintptr nSourceEnd = nSourceBegin + cbSourceSize;
    const bool_t bOverlapsStorage =
        nSourceBegin < nStorageEnd && nStorageBegin < nSourceEnd;
    if ( !bOverlapsStorage ) {
        return CY_TRUE;
    }

    usize cbLiveSize = 0u;
    const bool_t bValidLiveSize =
        Cy_TryArrayByteCount<type_t>( vector.nCount, cbLiveSize );
    const bool_t bSourceStartsInStorage = nSourceBegin >= nStorageBegin;
    const bool_t bAlignedSource =
        bSourceStartsInStorage &&
        ( nSourceBegin - nStorageBegin ) % sizeof( type_t ) == 0u;
    const bool_t bInsideLiveElements =
        bAlignedSource &&
        nSourceEnd <= nStorageBegin + cbLiveSize;
    CY_ASSERT_MSG(
        bValidLiveSize && bInsideLiveElements,
        "SmallVector_Append source may only overlap constructed elements." );
    if ( !bValidLiveSize || !bInsideLiveElements ) {
        return CY_FALSE;
    }

    bInternalOut = CY_TRUE;
    iSourceOut = ( nSourceBegin - nStorageBegin ) / sizeof( type_t );
    return CY_TRUE;
}

} // namespace detail

template <typename type_t, usize nInlineCapacity>
small_vector_t<type_t, nInlineCapacity>::~small_vector_t() noexcept
{
    SmallVector_Shutdown( this );
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_Init(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    const allocator_t *pAllocator ) noexcept
{
    const bool_t bValidDestination =
        pVector != nullptr && detail::SmallVector_IsCanonicalEmpty( *pVector );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "SmallVector_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "SmallVector_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    pVector->pData = detail::SmallVector_InlineData( pVector );
    pVector->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_Shutdown(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_Shutdown requires a valid vector." );
    if ( !bValidVector ) {
        return;
    }

    Container_DestroyRange( pVector->pData, pVector->nCount );
    if ( pVector->pData != nullptr &&
         !detail::SmallVector_UsesInlineUnchecked( *pVector ) ) {
        Allocator_FreeArrayStorage(
            pVector->pAllocator,
            pVector->pData,
            pVector->nCapacity );
    }
    detail::SmallVector_Reset( *pVector );
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_Clear(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Clear requires a valid vector." );
    if ( !bValidVector ) {
        return;
    }

    Container_DestroyRange( pVector->pData, pVector->nCount );
    pVector->nCount = 0u;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_IsValid(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    if ( pVector == nullptr ) {
        return CY_FALSE;
    }
    if ( detail::SmallVector_IsCanonicalEmpty( *pVector ) ) {
        return CY_TRUE;
    }
    if ( !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    if ( detail::SmallVector_UsesInlineUnchecked( *pVector ) ) {
        return pVector->nCapacity == nInlineCapacity &&
               pVector->nCount <= nInlineCapacity;
    }
    if ( pVector->pData == nullptr ) {
        return nInlineCapacity == 0u &&
               pVector->nCount == 0u &&
               pVector->nCapacity == 0u;
    }

    usize cbCapacity = 0u;
    return pVector->nCapacity > nInlineCapacity &&
           pVector->nCount <= pVector->nCapacity &&
           Cy_TryArrayByteCount<type_t>( pVector->nCapacity, cbCapacity ) &&
           Cy_AlignIsPointerAligned( pVector->pData, alignof( type_t ) );
}

template <typename type_t, usize nInlineCapacity>
usize SmallVector_Count(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Count requires a valid vector." );
    return bValidVector ? pVector->nCount : 0u;
}

template <typename type_t, usize nInlineCapacity>
usize SmallVector_Capacity(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_Capacity requires a valid vector." );
    return bValidVector ? pVector->nCapacity : 0u;
}

template <typename type_t, usize nInlineCapacity>
type_t *SmallVector_Data(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Data requires a valid vector." );
    return bValidVector ? pVector->pData : nullptr;
}

template <typename type_t, usize nInlineCapacity>
const type_t *SmallVector_Data(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Data requires a valid vector." );
    return bValidVector ? pVector->pData : nullptr;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_IsEmpty(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_IsEmpty requires a valid vector." );
    return bValidVector ? pVector->nCount == 0u : CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_Reserve(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize nCapacity ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Reserve requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "SmallVector_Reserve requires an initialized allocator binding." );
    if ( !bValidVector || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( nCapacity <= pVector->nCapacity ) {
        return CY_TRUE;
    }

    type_t *pNewData = Allocator_AllocateArrayStorage<type_t>(
        pVector->pAllocator,
        nCapacity );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    Container_RelocateConstructRange(
        pNewData,
        pVector->pData,
        pVector->nCount );
    Container_DestroyRange( pVector->pData, pVector->nCount );
    if ( pVector->pData != nullptr &&
         !detail::SmallVector_UsesInlineUnchecked( *pVector ) ) {
        Allocator_FreeArrayStorage(
            pVector->pAllocator,
            pVector->pData,
            pVector->nCapacity );
    }

    pVector->pData = pNewData;
    pVector->nCapacity = nCapacity;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_Resize(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize nCount ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Resize requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "SmallVector_Resize requires an initialized allocator binding." );
    if ( !bValidVector || !bAllocatorBound ) {
        return CY_FALSE;
    }

    if ( nCount < pVector->nCount ) {
        Container_DestroyRange(
            pVector->pData + nCount,
            pVector->nCount - nCount );
        pVector->nCount = nCount;
        return CY_TRUE;
    }
    if ( nCount == pVector->nCount ) {
        return CY_TRUE;
    }

    const usize nAdditional = nCount - pVector->nCount;
    if ( !detail::SmallVector_EnsureAdditional( pVector, nAdditional ) ) {
        return CY_FALSE;
    }
    Container_DefaultConstructRange(
        pVector->pData + pVector->nCount,
        nAdditional );
    pVector->nCount = nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_ShrinkToFit(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_ShrinkToFit requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "SmallVector_ShrinkToFit requires an initialized allocator binding." );
    if ( !bValidVector || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( detail::SmallVector_UsesInlineUnchecked( *pVector ) ) {
        return CY_TRUE;
    }

    if ( pVector->nCount <= nInlineCapacity ) {
        type_t *pInlineData = detail::SmallVector_InlineData( pVector );
        Container_RelocateConstructRange(
            pInlineData,
            pVector->pData,
            pVector->nCount );
        Container_DestroyRange( pVector->pData, pVector->nCount );
        Allocator_FreeArrayStorage(
            pVector->pAllocator,
            pVector->pData,
            pVector->nCapacity );
        pVector->pData = pInlineData;
        pVector->nCapacity = nInlineCapacity;
        return CY_TRUE;
    }
    if ( pVector->nCount == pVector->nCapacity ) {
        return CY_TRUE;
    }

    type_t *pNewData = Allocator_AllocateArrayStorage<type_t>(
        pVector->pAllocator,
        pVector->nCount );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }
    Container_RelocateConstructRange(
        pNewData,
        pVector->pData,
        pVector->nCount );
    Container_DestroyRange( pVector->pData, pVector->nCount );
    Allocator_FreeArrayStorage(
        pVector->pAllocator,
        pVector->pData,
        pVector->nCapacity );
    pVector->pData = pNewData;
    pVector->nCapacity = pVector->nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_PushBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "SmallVector_PushBack requires nothrow copy construction." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_PushBack requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pVector->nCount == pVector->nCapacity;
    const bool_t bInternalValue =
        detail::SmallVector_ElementIsInternal( *pVector, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( value );
        if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( temporary );
    } else {
        if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( value );
    }

    ++pVector->nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_PushBackMove(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    type_t &&value ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "SmallVector_PushBackMove requires nothrow move construction." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_PushBackMove requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pVector->nCount == pVector->nCapacity;
    const bool_t bInternalValue =
        detail::SmallVector_ElementIsInternal( *pVector, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( static_cast<type_t &&>( value ) );
        if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( temporary ) );
    } else {
        if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( value ) );
    }

    ++pVector->nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity, typename... args_t>
type_t *SmallVector_EmplaceBack(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    args_t &&... args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t...>,
        "SmallVector_EmplaceBack construction must not throw." );
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "SmallVector_EmplaceBack requires nothrow move construction." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_EmplaceBack requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return nullptr;
    }

    type_t temporary( static_cast<args_t &&>( args )... );
    if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
        return nullptr;
    }
    type_t *pElement = pVector->pData + pVector->nCount;
    ::new ( static_cast<void *>( pElement ) )
        type_t( static_cast<type_t &&>( temporary ) );
    ++pVector->nCount;
    return pElement;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_Insert(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "SmallVector_Insert requires nothrow copy construction." );
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "SmallVector_Insert requires nothrow move construction." );
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "SmallVector_Insert requires nothrow move assignment." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex <= pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "SmallVector_Insert requires a valid vector." );
    CY_ASSERT_MSG(
        bIndexInRange,
        "SmallVector_Insert index is outside the vector." );
    if ( !bIndexInRange || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    type_t temporary( value );
    if ( !detail::SmallVector_EnsureAdditional( pVector, 1u ) ) {
        return CY_FALSE;
    }
    if ( iIndex == pVector->nCount ) {
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( temporary ) );
    } else {
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( pVector->pData[pVector->nCount - 1u] ) );
        const usize nShiftCount = pVector->nCount - iIndex - 1u;
        Container_MoveAssignRangeBackward(
            pVector->pData + iIndex + 1u,
            pVector->pData + iIndex,
            nShiftCount );
        pVector->pData[iIndex] = static_cast<type_t &&>( temporary );
    }
    ++pVector->nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_Append(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    span_t<const type_t> values ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "SmallVector_Append requires nothrow copy construction." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bValidValues = Span_IsValid( values );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Append requires a valid vector." );
    CY_ASSERT_MSG(
        bValidValues,
        "SmallVector_Append requires a valid source span." );
    if ( !bValidVector || !bValidValues ||
         !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( values.nCount == 0u ) {
        return CY_TRUE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSource = 0u;
    if ( !detail::SmallVector_RebaseAppendSource(
             *pVector,
             values,
             bInternalSource,
             iSource ) ) {
        return CY_FALSE;
    }
    if ( !detail::SmallVector_EnsureAdditional( pVector, values.nCount ) ) {
        return CY_FALSE;
    }

    const type_t *pSource = bInternalSource
        ? pVector->pData + iSource
        : values.pData;
    Container_CopyConstructRange(
        pVector->pData + pVector->nCount,
        pSource,
        values.nCount );
    pVector->nCount += values.nCount;
    return CY_TRUE;
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_PopBack(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bNotEmpty = bValidVector && pVector->nCount > 0u;
    CY_ASSERT_MSG( bValidVector, "SmallVector_PopBack requires a valid vector." );
    CY_ASSERT_MSG(
        bNotEmpty,
        "SmallVector_PopBack requires a non-empty vector." );
    if ( !bNotEmpty ) {
        return;
    }

    --pVector->nCount;
    Container_DestroyRange( pVector->pData + pVector->nCount, 1u );
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_Erase(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex,
    usize nCount ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bStartInRange = bValidVector && iIndex <= pVector->nCount;
    const bool_t bRangeInBounds =
        bStartInRange && nCount <= pVector->nCount - iIndex;
    CY_ASSERT_MSG( bValidVector, "SmallVector_Erase requires a valid vector." );
    CY_ASSERT_MSG(
        bRangeInBounds,
        "SmallVector_Erase range is outside the vector." );
    if ( !bRangeInBounds || nCount == 0u ) {
        return;
    }

    const usize nTailCount = pVector->nCount - iIndex - nCount;
    Container_MoveAssignRangeForward(
        pVector->pData + iIndex,
        pVector->pData + iIndex + nCount,
        nTailCount );
    Container_DestroyRange(
        pVector->pData + pVector->nCount - nCount,
        nCount );
    pVector->nCount -= nCount;
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_EraseSwap(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept
{
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "SmallVector_EraseSwap requires nothrow move assignment." );

    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "SmallVector_EraseSwap requires a valid vector." );
    CY_ASSERT_MSG(
        bIndexInRange,
        "SmallVector_EraseSwap index is outside the vector." );
    if ( !bIndexInRange ) {
        return;
    }

    const usize iLast = pVector->nCount - 1u;
    if ( iIndex != iLast ) {
        pVector->pData[iIndex] =
            static_cast<type_t &&>( pVector->pData[iLast] );
    }
    Container_DestroyRange( pVector->pData + iLast, 1u );
    --pVector->nCount;
}

template <typename type_t, usize nInlineCapacity>
type_t *SmallVector_At(
    small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "SmallVector_At requires a valid vector." );
    CY_ASSERT_MSG(
        bIndexInRange,
        "SmallVector_At index is outside the vector." );
    return bIndexInRange ? pVector->pData + iIndex : nullptr;
}

template <typename type_t, usize nInlineCapacity>
const type_t *SmallVector_At(
    const small_vector_t<type_t, nInlineCapacity> *pVector,
    usize iIndex ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "SmallVector_At requires a valid vector." );
    CY_ASSERT_MSG(
        bIndexInRange,
        "SmallVector_At index is outside the vector." );
    return bIndexInRange ? pVector->pData + iIndex : nullptr;
}

template <typename type_t, usize nInlineCapacity>
type_t *SmallVector_Front(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    return SmallVector_At( pVector, 0u );
}

template <typename type_t, usize nInlineCapacity>
const type_t *SmallVector_Front(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    return SmallVector_At( pVector, 0u );
}

template <typename type_t, usize nInlineCapacity>
type_t *SmallVector_Back(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const usize nCount = SmallVector_Count( pVector );
    return nCount > 0u ? SmallVector_At( pVector, nCount - 1u ) : nullptr;
}

template <typename type_t, usize nInlineCapacity>
const type_t *SmallVector_Back(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const usize nCount = SmallVector_Count( pVector );
    return nCount > 0u ? SmallVector_At( pVector, nCount - 1u ) : nullptr;
}

template <typename type_t, usize nInlineCapacity>
span_t<type_t> SmallVector_Span(
    small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Span requires a valid vector." );
    return bValidVector
        ? span_t<type_t>{ pVector->pData, pVector->nCount }
        : span_t<type_t>{};
}

template <typename type_t, usize nInlineCapacity>
span_t<const type_t> SmallVector_Span(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "SmallVector_Span requires a valid vector." );
    return bValidVector
        ? span_t<const type_t>{ pVector->pData, pVector->nCount }
        : span_t<const type_t>{};
}

template <typename type_t, usize nInlineCapacity>
bool_t SmallVector_UsesInlineStorage(
    const small_vector_t<type_t, nInlineCapacity> *pVector ) noexcept
{
    const bool_t bValidVector = SmallVector_IsValid( pVector );
    CY_ASSERT_MSG(
        bValidVector,
        "SmallVector_UsesInlineStorage requires a valid vector." );
    return bValidVector &&
           detail::SmallVector_UsesInlineUnchecked( *pVector );
}

template <typename type_t, usize nInlineCapacity>
void SmallVector_Move(
    small_vector_t<type_t, nInlineCapacity> *pDest,
    small_vector_t<type_t, nInlineCapacity> *pSource ) noexcept
{
    const bool_t bDistinctVectors =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctVectors && detail::SmallVector_IsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctVectors && SmallVector_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctVectors && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "SmallVector_Move requires distinct vectors and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    if ( pSource->pAllocator == nullptr ) {
        return;
    }

    pDest->pAllocator = pSource->pAllocator;
    if ( pSource->pData != nullptr &&
         !detail::SmallVector_UsesInlineUnchecked( *pSource ) ) {
        pDest->pData = pSource->pData;
        pDest->nCount = pSource->nCount;
        pDest->nCapacity = pSource->nCapacity;
        detail::SmallVector_Reset( *pSource );
        return;
    }

    pDest->pData = detail::SmallVector_InlineData( pDest );
    pDest->nCount = pSource->nCount;
    pDest->nCapacity = nInlineCapacity;
    Container_RelocateConstructRange(
        pDest->pData,
        pSource->pData,
        pSource->nCount );
    Container_DestroyRange( pSource->pData, pSource->nCount );
    detail::SmallVector_Reset( *pSource );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SMALLVECTOR_INL
