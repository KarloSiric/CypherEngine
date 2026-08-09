//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RingBuffer.h
//  Purpose: Declares non-owning fixed-capacity circular buffers.
//  Details: RingBuffer uses caller-provided constructed element storage and never
//           allocates or grows. It is not thread-safe.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RINGBUFFER_H
#define CYPHER_COMMON_TIER1_RINGBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t>
struct ring_buffer_t {
    static_assert( is_object_v<type_t>, "ring_buffer_t requires an object type." );
    static_assert( !is_array_v<type_t>, "ring_buffer_t does not store array elements." );

    type_t *pData{ nullptr };
    usize nCapacity{ 0u };
    usize nCount{ 0u };
    usize iHead{ 0u };
};

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_Init(
    ring_buffer_t<type_t> *pBuffer,
    span_t<type_t> storage ) noexcept;

template <typename type_t>
void RingBuffer_Shutdown( ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
void RingBuffer_Clear( ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_IsValid(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_IsFull(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_IsEmpty(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize RingBuffer_Count(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize RingBuffer_Capacity(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_Push(
    ring_buffer_t<type_t> *pBuffer,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_PushOverwrite(
    ring_buffer_t<type_t> *pBuffer,
    const type_t &value,
    type_t *pOverwrittenOut = nullptr ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RingBuffer_Pop(
    ring_buffer_t<type_t> *pBuffer,
    type_t *pValueOut = nullptr ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *RingBuffer_Front(
    ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *RingBuffer_Front(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *RingBuffer_Back(
    ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *RingBuffer_Back(
    const ring_buffer_t<type_t> *pBuffer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *RingBuffer_At(
    ring_buffer_t<type_t> *pBuffer,
    usize iLogicalIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *RingBuffer_At(
    const ring_buffer_t<type_t> *pBuffer,
    usize iLogicalIndex ) noexcept;

} // namespace cypher::common

#include "CypherCommon_RingBuffer.inl"

#endif // CYPHER_COMMON_TIER1_RINGBUFFER_H
