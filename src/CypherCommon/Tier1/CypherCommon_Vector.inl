//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Vector.inl
//  Purpose: Implements allocator-backed contiguous growable arrays.
//  Details: Growth is overflow checked and transactional. Element relocation,
//           insertion, erasure, and self-append preserve explicit lifetimes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_VECTOR_INL
#define CYPHER_COMMON_TIER1_VECTOR_INL

#ifndef CYPHER_COMMON_TIER1_VECTOR_H
    #include "CypherCommon_Vector.h"
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
bool_t Vector_IsCanonicalEmpty( const vector_t<type_t> &vector ) noexcept
{
    return vector.pData == nullptr &&
           vector.nCount == 0u &&
           vector.nCapacity == 0u &&
           vector.pAllocator == nullptr;
}

template <typename type_t>
void Vector_Reset( vector_t<type_t> &vector ) noexcept
{
    vector.pData = nullptr;
    vector.nCount = 0u;
    vector.nCapacity = 0u;
    vector.pAllocator = nullptr;
}

template <typename type_t>
bool_t Vector_CalculateGrowth(
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
        if ( nCandidate > nMaximumCapacity - nHalf ) {
            nCandidate = nMaximumCapacity;
        } else {
            nCandidate += nHalf;
        }
    }
    if ( nCandidate < nRequiredCapacity ) {
        nCandidate = nRequiredCapacity;
    }

    nCapacityOut = nCandidate;
    return CY_TRUE;
}

template <typename type_t>
bool_t Vector_ElementIsInternal(
    const vector_t<type_t> &vector,
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

template <typename type_t>
bool_t Vector_EnsureAdditional(
    vector_t<type_t> *pVector,
    usize nAdditionalCount ) noexcept
{
    if ( nAdditionalCount > CY_USIZE_MAX - pVector->nCount ) {
        CY_ASSERT_MSG( CY_FALSE, "Vector count overflowed." );
        return CY_FALSE;
    }

    const usize nRequiredCapacity = pVector->nCount + nAdditionalCount;
    if ( nRequiredCapacity <= pVector->nCapacity ) {
        return CY_TRUE;
    }

    usize nGrowthCapacity = 0u;
    const bool_t bValidGrowth = Vector_CalculateGrowth<type_t>(
        pVector->nCapacity,
        nRequiredCapacity,
        nGrowthCapacity );
    CY_ASSERT_MSG( bValidGrowth, "Vector growth exceeds addressable storage." );
    return bValidGrowth && Vector_Reserve( pVector, nGrowthCapacity );
}

template <typename type_t>
bool_t Vector_RebaseAppendSource(
    const vector_t<type_t> &vector,
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

    const uintptr nStorageBegin = reinterpret_cast<uintptr>( vector.pData );
    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    if ( nStorageBegin > nMaximumAddress - cbStorageSize ) {
        return CY_FALSE;
    }
    const uintptr nStorageEnd = nStorageBegin + cbStorageSize;
    const uintptr nSourceBegin = reinterpret_cast<uintptr>( values.pData );
    if ( nSourceBegin > nMaximumAddress - cbSourceSize ) {
        return CY_FALSE;
    }
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
        "Vector_Append source may only overlap constructed vector elements." );
    if ( !bValidLiveSize || !bInsideLiveElements ) {
        return CY_FALSE;
    }

    bInternalOut = CY_TRUE;
    iSourceOut = ( nSourceBegin - nStorageBegin ) / sizeof( type_t );
    return CY_TRUE;
}

} // namespace detail

template <typename type_t>
vector_t<type_t>::~vector_t() noexcept
{
    Vector_Shutdown( this );
}

template <typename type_t>
bool_t Vector_Init(
    vector_t<type_t> *pVector,
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pVector != nullptr && detail::Vector_IsCanonicalEmpty( *pVector );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "Vector_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "Vector_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    if ( nInitialCapacity == 0u ) {
        pVector->pAllocator = pAllocator;
        return CY_TRUE;
    }

    type_t *pData = Allocator_AllocateArrayStorage<type_t>(
        pAllocator,
        nInitialCapacity );
    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    pVector->pData = pData;
    pVector->nCapacity = nInitialCapacity;
    pVector->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename type_t>
void Vector_Shutdown( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Shutdown requires a valid vector." );
    if ( !bValidVector ) {
        return;
    }

    Container_DestroyRange( pVector->pData, pVector->nCount );
    if ( pVector->pData != nullptr ) {
        Allocator_FreeArrayStorage(
            pVector->pAllocator,
            pVector->pData,
            pVector->nCapacity );
    }
    detail::Vector_Reset( *pVector );
}

template <typename type_t>
void Vector_Clear( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Clear requires a valid vector." );
    if ( !bValidVector ) {
        return;
    }

    Container_DestroyRange( pVector->pData, pVector->nCount );
    pVector->nCount = 0u;
}

template <typename type_t>
bool_t Vector_IsValid( const vector_t<type_t> *pVector ) noexcept
{
    if ( pVector == nullptr ) {
        return CY_FALSE;
    }

    if ( pVector->pData == nullptr ) {
        return pVector->nCount == 0u &&
               pVector->nCapacity == 0u &&
               ( pVector->pAllocator == nullptr ||
                 Allocator_IsValid( pVector->pAllocator ) );
    }

    usize cbCapacity = 0u;
    return pVector->nCapacity > 0u &&
           pVector->nCount <= pVector->nCapacity &&
           Cy_TryArrayByteCount<type_t>( pVector->nCapacity, cbCapacity ) &&
           Allocator_IsValid( pVector->pAllocator ) &&
           Cy_AlignIsPointerAligned( pVector->pData, alignof( type_t ) );
}

template <typename type_t>
bool_t Vector_IsEmpty( const vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_IsEmpty requires a valid vector." );
    return bValidVector ? pVector->nCount == 0u : CY_TRUE;
}

template <typename type_t>
usize Vector_Count( const vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Count requires a valid vector." );
    return bValidVector ? pVector->nCount : 0u;
}

template <typename type_t>
usize Vector_Capacity( const vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Capacity requires a valid vector." );
    return bValidVector ? pVector->nCapacity : 0u;
}

template <typename type_t>
type_t *Vector_Data( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Data requires a valid vector." );
    return bValidVector ? pVector->pData : nullptr;
}

template <typename type_t>
const type_t *Vector_Data( const vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Data requires a valid vector." );
    return bValidVector ? pVector->pData : nullptr;
}

template <typename type_t>
bool_t Vector_Reserve(
    vector_t<type_t> *pVector,
    usize nCapacity ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG( bValidVector, "Vector_Reserve requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Vector_Reserve requires an initialized allocator binding." );
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
    if ( pVector->pData != nullptr ) {
        Allocator_FreeArrayStorage(
            pVector->pAllocator,
            pVector->pData,
            pVector->nCapacity );
    }

    pVector->pData = pNewData;
    pVector->nCapacity = nCapacity;
    return CY_TRUE;
}

template <typename type_t>
bool_t Vector_Resize(
    vector_t<type_t> *pVector,
    usize nCount ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG( bValidVector, "Vector_Resize requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Vector_Resize requires an initialized allocator binding." );
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
    if ( !detail::Vector_EnsureAdditional( pVector, nAdditional ) ) {
        return CY_FALSE;
    }

    Container_DefaultConstructRange(
        pVector->pData + pVector->nCount,
        nAdditional );
    pVector->nCount = nCount;
    return CY_TRUE;
}

template <typename type_t>
bool_t Vector_ShrinkToFit( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bAllocatorBound =
        bValidVector && Allocator_IsValid( pVector->pAllocator );
    CY_ASSERT_MSG( bValidVector, "Vector_ShrinkToFit requires a valid vector." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Vector_ShrinkToFit requires an initialized allocator binding." );
    if ( !bValidVector || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( pVector->nCount == pVector->nCapacity ) {
        return CY_TRUE;
    }
    if ( pVector->nCount == 0u ) {
        if ( pVector->pData != nullptr ) {
            Allocator_FreeArrayStorage(
                pVector->pAllocator,
                pVector->pData,
                pVector->nCapacity );
        }
        pVector->pData = nullptr;
        pVector->nCapacity = 0u;
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

template <typename type_t>
bool_t Vector_PushBack(
    vector_t<type_t> *pVector,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "Vector_PushBack requires nothrow copy construction." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_PushBack requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pVector->nCount == pVector->nCapacity;
    const bool_t bInternalValue =
        detail::Vector_ElementIsInternal( *pVector, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( value );
        if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( temporary );
    } else {
        if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( value );
    }

    ++pVector->nCount;
    return CY_TRUE;
}

template <typename type_t>
bool_t Vector_PushBackMove(
    vector_t<type_t> *pVector,
    type_t &&value ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "Vector_PushBackMove requires nothrow move construction." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_PushBackMove requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pVector->nCount == pVector->nCapacity;
    const bool_t bInternalValue =
        detail::Vector_ElementIsInternal( *pVector, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( static_cast<type_t &&>( value ) );
        if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( temporary ) );
    } else {
        if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
            return CY_FALSE;
        }
        ::new ( static_cast<void *>( pVector->pData + pVector->nCount ) )
            type_t( static_cast<type_t &&>( value ) );
    }

    ++pVector->nCount;
    return CY_TRUE;
}

template <typename type_t, typename... args_t>
type_t *Vector_EmplaceBack(
    vector_t<type_t> *pVector,
    args_t &&... args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t...>,
        "Vector_EmplaceBack construction must not throw." );
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "Vector_EmplaceBack requires nothrow move construction." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_EmplaceBack requires a valid vector." );
    if ( !bValidVector || !Allocator_IsValid( pVector->pAllocator ) ) {
        return nullptr;
    }

    // Construct first so arguments that reference vector elements remain valid.
    type_t temporary( static_cast<args_t &&>( args )... );
    if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
        return nullptr;
    }

    type_t *pElement = pVector->pData + pVector->nCount;
    ::new ( static_cast<void *>( pElement ) )
        type_t( static_cast<type_t &&>( temporary ) );
    ++pVector->nCount;
    return pElement;
}

template <typename type_t>
bool_t Vector_Insert(
    vector_t<type_t> *pVector,
    usize iIndex,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "Vector_Insert requires nothrow copy construction." );
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "Vector_Insert requires nothrow move construction." );
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "Vector_Insert requires nothrow move assignment." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex <= pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "Vector_Insert requires a valid vector." );
    CY_ASSERT_MSG( bIndexInRange, "Vector_Insert index is outside the vector." );
    if ( !bIndexInRange || !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }

    type_t temporary( value );
    if ( !detail::Vector_EnsureAdditional( pVector, 1u ) ) {
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

template <typename type_t>
bool_t Vector_Append(
    vector_t<type_t> *pVector,
    span_t<const type_t> values ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "Vector_Append requires nothrow copy construction." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bValidValues = Span_IsValid( values );
    CY_ASSERT_MSG( bValidVector, "Vector_Append requires a valid vector." );
    CY_ASSERT_MSG( bValidValues, "Vector_Append requires a valid source span." );
    if ( !bValidVector || !bValidValues ||
         !Allocator_IsValid( pVector->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( values.nCount == 0u ) {
        return CY_TRUE;
    }

    bool_t bInternalSource = CY_FALSE;
    usize iSource = 0u;
    if ( !detail::Vector_RebaseAppendSource(
             *pVector,
             values,
             bInternalSource,
             iSource ) ) {
        return CY_FALSE;
    }
    if ( !detail::Vector_EnsureAdditional( pVector, values.nCount ) ) {
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

template <typename type_t>
void Vector_PopBack( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bNotEmpty = bValidVector && pVector->nCount > 0u;
    CY_ASSERT_MSG( bValidVector, "Vector_PopBack requires a valid vector." );
    CY_ASSERT_MSG( bNotEmpty, "Vector_PopBack requires a non-empty vector." );
    if ( !bNotEmpty ) {
        return;
    }

    --pVector->nCount;
    Container_DestroyRange( pVector->pData + pVector->nCount, 1u );
}

template <typename type_t>
void Vector_Erase(
    vector_t<type_t> *pVector,
    usize iIndex,
    usize nCount ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bStartInRange = bValidVector && iIndex <= pVector->nCount;
    const bool_t bRangeInBounds =
        bStartInRange && nCount <= pVector->nCount - iIndex;
    CY_ASSERT_MSG( bValidVector, "Vector_Erase requires a valid vector." );
    CY_ASSERT_MSG( bRangeInBounds, "Vector_Erase range is outside the vector." );
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

template <typename type_t>
void Vector_EraseSwap(
    vector_t<type_t> *pVector,
    usize iIndex ) noexcept
{
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "Vector_EraseSwap requires nothrow move assignment." );

    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "Vector_EraseSwap requires a valid vector." );
    CY_ASSERT_MSG(
        bIndexInRange,
        "Vector_EraseSwap index is outside the vector." );
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

template <typename type_t>
type_t *Vector_At(
    vector_t<type_t> *pVector,
    usize iIndex ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "Vector_At requires a valid vector." );
    CY_ASSERT_MSG( bIndexInRange, "Vector_At index is outside the vector." );
    return bIndexInRange ? pVector->pData + iIndex : nullptr;
}

template <typename type_t>
const type_t *Vector_At(
    const vector_t<type_t> *pVector,
    usize iIndex ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    const bool_t bIndexInRange =
        bValidVector && iIndex < pVector->nCount;
    CY_ASSERT_MSG( bValidVector, "Vector_At requires a valid vector." );
    CY_ASSERT_MSG( bIndexInRange, "Vector_At index is outside the vector." );
    return bIndexInRange ? pVector->pData + iIndex : nullptr;
}

template <typename type_t>
type_t *Vector_Front( vector_t<type_t> *pVector ) noexcept
{
    return Vector_At( pVector, 0u );
}

template <typename type_t>
const type_t *Vector_Front( const vector_t<type_t> *pVector ) noexcept
{
    return Vector_At( pVector, 0u );
}

template <typename type_t>
type_t *Vector_Back( vector_t<type_t> *pVector ) noexcept
{
    const usize nCount = Vector_Count( pVector );
    return nCount > 0u ? Vector_At( pVector, nCount - 1u ) : nullptr;
}

template <typename type_t>
const type_t *Vector_Back( const vector_t<type_t> *pVector ) noexcept
{
    const usize nCount = Vector_Count( pVector );
    return nCount > 0u ? Vector_At( pVector, nCount - 1u ) : nullptr;
}

template <typename type_t>
span_t<type_t> Vector_Span( vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Span requires a valid vector." );
    return bValidVector
        ? span_t<type_t>{ pVector->pData, pVector->nCount }
        : span_t<type_t>{};
}

template <typename type_t>
span_t<const type_t> Vector_Span(
    const vector_t<type_t> *pVector ) noexcept
{
    const bool_t bValidVector = Vector_IsValid( pVector );
    CY_ASSERT_MSG( bValidVector, "Vector_Span requires a valid vector." );
    return bValidVector
        ? span_t<const type_t>{ pVector->pData, pVector->nCount }
        : span_t<const type_t>{};
}

template <typename type_t>
void Vector_Move(
    vector_t<type_t> *pDest,
    vector_t<type_t> *pSource ) noexcept
{
    const bool_t bDistinctVectors =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctVectors && detail::Vector_IsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctVectors && Vector_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctVectors && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "Vector_Move requires distinct vectors and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    pDest->pData = pSource->pData;
    pDest->nCount = pSource->nCount;
    pDest->nCapacity = pSource->nCapacity;
    pDest->pAllocator = pSource->pAllocator;
    detail::Vector_Reset( *pSource );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_VECTOR_INL
