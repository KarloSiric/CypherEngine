//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RingBuffer.inl
//  Purpose: Implements non-owning fixed-capacity circular buffers.
//  Details: Slots are caller-constructed objects. Operations assign logical values
//           without allocation or lifetime changes and reject aliased output storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RINGBUFFER_INL
#define CYPHER_COMMON_TIER1_RINGBUFFER_INL
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <limits>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

inline usize RingBuffer_PhysicalIndex(
    usize iHead,
    usize iLogicalIndex,
    usize nCapacity ) noexcept
{
    const usize nUntilWrap = nCapacity - iHead;
    return iLogicalIndex < nUntilWrap
        ? iHead + iLogicalIndex
        : iLogicalIndex - nUntilWrap;
}

template <typename type_t>
bool_t RingBuffer_PointerIsInternal(
    const ring_buffer_t<type_t> &buffer,
    const type_t *pValue ) noexcept
{
    if ( pValue == nullptr || buffer.pData == nullptr ) {
        return CY_FALSE;
    }

    usize cbStorage = 0u;
    if ( !Cy_TryArrayByteCount<type_t>( buffer.nCapacity, cbStorage ) ) {
        return CY_TRUE;
    }
    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nBegin = reinterpret_cast<uintptr>( buffer.pData );
    if ( nBegin > nMaximumAddress - cbStorage ) {
        return CY_TRUE;
    }
    const uintptr nValue = reinterpret_cast<uintptr>( pValue );
    return nValue >= nBegin && nValue < nBegin + cbStorage;
}

template <typename type_t>
bool_t RingBuffer_AssignOut(
    type_t *pValueOut,
    type_t &value ) noexcept
{
    if ( pValueOut == nullptr ) {
        return CY_TRUE;
    }
    if constexpr ( std::is_nothrow_copy_assignable_v<type_t> ) {
        *pValueOut = value;
        return CY_TRUE;
    } else if constexpr ( std::is_nothrow_move_assignable_v<type_t> ) {
        *pValueOut = static_cast<type_t &&>( value );
        return CY_TRUE;
    } else {
        CY_ASSERT_MSG(
            CY_FALSE,
            "RingBuffer output requires nothrow copy or move assignment." );
        return CY_FALSE;
    }
}

} // namespace detail

template <typename type_t>
bool_t RingBuffer_Init(
    ring_buffer_t<type_t> *pBuffer,
    span_t<type_t> storage ) noexcept
{
    const bool_t bValidBuffer = pBuffer != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    const bool_t bEmptyDestination =
        bValidBuffer &&
        pBuffer->pData == nullptr &&
        pBuffer->nCapacity == 0u &&
        pBuffer->nCount == 0u &&
        pBuffer->iHead == 0u;
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_Init requires a buffer object." );
    CY_ASSERT_MSG(
        bValidStorage,
        "RingBuffer_Init requires a valid storage span." );
    CY_ASSERT_MSG(
        bEmptyDestination,
        "RingBuffer_Init requires an empty destination." );
    if ( !bValidBuffer || !bValidStorage || !bEmptyDestination ) {
        return CY_FALSE;
    }

    pBuffer->pData = storage.pData;
    pBuffer->nCapacity = storage.nCount;
    return CY_TRUE;
}

template <typename type_t>
void RingBuffer_Shutdown( ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_Shutdown requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    pBuffer->pData = nullptr;
    pBuffer->nCapacity = 0u;
    pBuffer->nCount = 0u;
    pBuffer->iHead = 0u;
}

template <typename type_t>
void RingBuffer_Clear( ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_Clear requires a valid buffer." );
    if ( bValidBuffer ) {
        pBuffer->nCount = 0u;
        pBuffer->iHead = 0u;
    }
}

template <typename type_t>
bool_t RingBuffer_IsValid( const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    if ( pBuffer == nullptr ) {
        return CY_FALSE;
    }
    if ( pBuffer->pData == nullptr ) {
        return pBuffer->nCapacity == 0u &&
               pBuffer->nCount == 0u &&
               pBuffer->iHead == 0u;
    }

    return pBuffer->nCapacity > 0u &&
           pBuffer->nCount <= pBuffer->nCapacity &&
           pBuffer->iHead < pBuffer->nCapacity &&
           ( pBuffer->nCount > 0u || pBuffer->iHead == 0u );
}

template <typename type_t>
bool_t RingBuffer_IsFull(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_IsFull requires a valid buffer." );
    return bValidBuffer &&
           pBuffer->nCapacity > 0u &&
           pBuffer->nCount == pBuffer->nCapacity;
}

template <typename type_t>
bool_t RingBuffer_IsEmpty(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_IsEmpty requires a valid buffer." );
    return bValidBuffer ? pBuffer->nCount == 0u : CY_TRUE;
}

template <typename type_t>
usize RingBuffer_Count(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_Count requires a valid buffer." );
    return bValidBuffer ? pBuffer->nCount : 0u;
}

template <typename type_t>
usize RingBuffer_Capacity(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_Capacity requires a valid buffer." );
    return bValidBuffer ? pBuffer->nCapacity : 0u;
}

template <typename type_t>
bool_t RingBuffer_Push(
    ring_buffer_t<type_t> *pBuffer,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_assignable_v<type_t>,
        "RingBuffer_Push requires nothrow copy assignment." );

    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_Push requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->nCapacity == 0u ||
         pBuffer->nCount == pBuffer->nCapacity ) {
        return CY_FALSE;
    }

    const usize iTail = detail::RingBuffer_PhysicalIndex(
        pBuffer->iHead,
        pBuffer->nCount,
        pBuffer->nCapacity );
    pBuffer->pData[iTail] = value;
    ++pBuffer->nCount;
    return CY_TRUE;
}

template <typename type_t>
bool_t RingBuffer_PushOverwrite(
    ring_buffer_t<type_t> *pBuffer,
    const type_t &value,
    type_t *pOverwrittenOut ) noexcept
{
    static_assert(
        std::is_nothrow_copy_assignable_v<type_t>,
        "RingBuffer_PushOverwrite requires nothrow copy assignment." );

    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "RingBuffer_PushOverwrite requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->nCapacity == 0u ) {
        return CY_FALSE;
    }
    if ( pBuffer->nCount < pBuffer->nCapacity ) {
        return RingBuffer_Push( pBuffer, value );
    }

    const bool_t bExternalOutput =
        !detail::RingBuffer_PointerIsInternal( *pBuffer, pOverwrittenOut );
    CY_ASSERT_MSG(
        bExternalOutput,
        "RingBuffer overwrite output may not alias ring storage." );
    if ( !bExternalOutput ) {
        return CY_FALSE;
    }

    type_t &oldest = pBuffer->pData[pBuffer->iHead];
    if ( !detail::RingBuffer_AssignOut( pOverwrittenOut, oldest ) ) {
        return CY_FALSE;
    }
    if ( &value != &oldest ) {
        oldest = value;
    }
    pBuffer->iHead = detail::RingBuffer_PhysicalIndex(
        pBuffer->iHead,
        1u,
        pBuffer->nCapacity );
    return CY_TRUE;
}

template <typename type_t>
bool_t RingBuffer_Pop(
    ring_buffer_t<type_t> *pBuffer,
    type_t *pValueOut ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_Pop requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->nCount == 0u ) {
        return CY_FALSE;
    }

    const bool_t bExternalOutput =
        !detail::RingBuffer_PointerIsInternal( *pBuffer, pValueOut );
    CY_ASSERT_MSG(
        bExternalOutput,
        "RingBuffer_Pop output may not alias ring storage." );
    if ( !bExternalOutput ) {
        return CY_FALSE;
    }

    type_t &front = pBuffer->pData[pBuffer->iHead];
    if ( !detail::RingBuffer_AssignOut( pValueOut, front ) ) {
        return CY_FALSE;
    }
    --pBuffer->nCount;
    pBuffer->iHead = pBuffer->nCount > 0u
        ? detail::RingBuffer_PhysicalIndex(
            pBuffer->iHead,
            1u,
            pBuffer->nCapacity )
        : 0u;
    return CY_TRUE;
}

template <typename type_t>
type_t *RingBuffer_Front(
    ring_buffer_t<type_t> *pBuffer ) noexcept
{
    return RingBuffer_At( pBuffer, 0u );
}

template <typename type_t>
const type_t *RingBuffer_Front(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    return RingBuffer_At( pBuffer, 0u );
}

template <typename type_t>
type_t *RingBuffer_Back(
    ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_Back requires a valid buffer." );
    return bValidBuffer && pBuffer->nCount > 0u
        ? RingBuffer_At( pBuffer, pBuffer->nCount - 1u )
        : nullptr;
}

template <typename type_t>
const type_t *RingBuffer_Back(
    const ring_buffer_t<type_t> *pBuffer ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_Back requires a valid buffer." );
    return bValidBuffer && pBuffer->nCount > 0u
        ? RingBuffer_At( pBuffer, pBuffer->nCount - 1u )
        : nullptr;
}

template <typename type_t>
type_t *RingBuffer_At(
    ring_buffer_t<type_t> *pBuffer,
    usize iLogicalIndex ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    const bool_t bIndexInRange =
        bValidBuffer && iLogicalIndex < pBuffer->nCount;
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_At requires a valid buffer." );
    if ( bValidBuffer && pBuffer->nCount > 0u ) {
        CY_ASSERT_MSG(
            bIndexInRange,
            "RingBuffer_At index is outside the logical range." );
    }
    return bIndexInRange
        ? pBuffer->pData + detail::RingBuffer_PhysicalIndex(
            pBuffer->iHead,
            iLogicalIndex,
            pBuffer->nCapacity )
        : nullptr;
}

template <typename type_t>
const type_t *RingBuffer_At(
    const ring_buffer_t<type_t> *pBuffer,
    usize iLogicalIndex ) noexcept
{
    const bool_t bValidBuffer = RingBuffer_IsValid( pBuffer );
    const bool_t bIndexInRange =
        bValidBuffer && iLogicalIndex < pBuffer->nCount;
    CY_ASSERT_MSG( bValidBuffer, "RingBuffer_At requires a valid buffer." );
    if ( bValidBuffer && pBuffer->nCount > 0u ) {
        CY_ASSERT_MSG(
            bIndexInRange,
            "RingBuffer_At index is outside the logical range." );
    }
    return bIndexInRange
        ? pBuffer->pData + detail::RingBuffer_PhysicalIndex(
            pBuffer->iHead,
            iLogicalIndex,
            pBuffer->nCapacity )
        : nullptr;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RINGBUFFER_INL
