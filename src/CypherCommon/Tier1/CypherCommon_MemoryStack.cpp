//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStack.cpp
//  Purpose: Implements linear stack allocation over caller-owned memory.
//  Details: Allocations align absolute addresses, fail transactionally on capacity
//           exhaustion, and rewind in constant time through byte-offset markers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryStack.h"

namespace cypher::common
{

bool_t MemoryStack_Init(
    memory_stack_t *pStack,
    byte_span_t memory ) noexcept
{
    const bool_t bValidStack = pStack != nullptr;
    const bool_t bValidMemory = Span_IsValid( memory );
    CY_ASSERT_MSG( bValidStack, "MemoryStack_Init requires an output object." );
    CY_ASSERT_MSG( bValidMemory, "MemoryStack_Init requires valid storage." );
    if ( !bValidStack || !bValidMemory ) {
        return CY_FALSE;
    }

    pStack->memory = FixedMemory_FromSpan( memory );
    pStack->iOffset = 0u;
    pStack->cbHighWater = 0u;
    return CY_TRUE;
}

void MemoryStack_Reset( memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "MemoryStack_Reset requires an initialized stack." );
    if ( bValidStack ) {
        pStack->iOffset = 0u;
    }
}

bool_t MemoryStack_IsValid( const memory_stack_t *pStack ) noexcept
{
    return pStack != nullptr &&
           FixedMemory_IsValid( pStack->memory ) &&
           pStack->iOffset <= pStack->memory.cbSize &&
           pStack->cbHighWater <= pStack->memory.cbSize &&
           pStack->iOffset <= pStack->cbHighWater;
}

void *MemoryStack_Allocate(
    memory_stack_t *pStack,
    usize cbSize,
    usize nAlignment ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( nAlignment );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_Allocate requires an initialized stack." );
    CY_ASSERT_MSG(
        bValidAlignment,
        "MemoryStack_Allocate requires power-of-two alignment." );
    if ( !bValidStack || !bValidAlignment || cbSize == 0u ||
         pStack->memory.pData == nullptr ) {
        return nullptr;
    }

    byte *pCurrent = pStack->memory.pData + pStack->iOffset;
    uintptr nAlignedAddress = 0u;
    if ( !Cy_AlignPointerUpChecked( pCurrent, nAlignment, nAlignedAddress ) ) {
        return nullptr;
    }

    const usize cbPadding = static_cast<usize>(
        nAlignedAddress - reinterpret_cast<uintptr>( pCurrent ) );
    const usize cbRemaining = pStack->memory.cbSize - pStack->iOffset;
    if ( cbPadding > cbRemaining || cbSize > cbRemaining - cbPadding ) {
        return nullptr;
    }

    pStack->iOffset += cbPadding + cbSize;
    if ( pStack->iOffset > pStack->cbHighWater ) {
        pStack->cbHighWater = pStack->iOffset;
    }
    return reinterpret_cast<void *>( nAlignedAddress );
}

void *MemoryStack_AllocateZeroed(
    memory_stack_t *pStack,
    usize cbSize,
    usize nAlignment ) noexcept
{
    void *pMemory = MemoryStack_Allocate( pStack, cbSize, nAlignment );
    if ( pMemory != nullptr ) {
        Cy_MemZero( pMemory, cbSize );
    }
    return pMemory;
}

memory_stack_marker_t MemoryStack_Mark(
    const memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "MemoryStack_Mark requires an initialized stack." );
    return bValidStack ? pStack->iOffset : 0u;
}

bool_t MemoryStack_Restore(
    memory_stack_t *pStack,
    memory_stack_marker_t marker ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_Restore requires an initialized stack." );
    if ( !bValidStack ) {
        return CY_FALSE;
    }

    const bool_t bValidMarker = marker <= pStack->iOffset;
    CY_ASSERT_MSG(
        bValidMarker,
        "MemoryStack_Restore cannot advance to a future marker." );
    if ( !bValidMarker ) {
        return CY_FALSE;
    }

    pStack->iOffset = marker;
    return CY_TRUE;
}

usize MemoryStack_Capacity( const memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_Capacity requires an initialized stack." );
    return bValidStack ? pStack->memory.cbSize : 0u;
}

usize MemoryStack_Used( const memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "MemoryStack_Used requires an initialized stack." );
    return bValidStack ? pStack->iOffset : 0u;
}

usize MemoryStack_Remaining( const memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_Remaining requires an initialized stack." );
    return bValidStack ? pStack->memory.cbSize - pStack->iOffset : 0u;
}

usize MemoryStack_HighWater( const memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_HighWater requires an initialized stack." );
    return bValidStack ? pStack->cbHighWater : 0u;
}

void MemoryStack_ClearHighWater( memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_ClearHighWater requires an initialized stack." );
    if ( bValidStack ) {
        pStack->cbHighWater = pStack->iOffset;
    }
}

byte_span_t MemoryStack_AllocatedSpan( memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_AllocatedSpan requires an initialized stack." );
    return bValidStack
        ? byte_span_t{ pStack->memory.pData, pStack->iOffset }
        : byte_span_t{};
}

byte_span_t MemoryStack_RemainingSpan( memory_stack_t *pStack ) noexcept
{
    const bool_t bValidStack = MemoryStack_IsValid( pStack );
    CY_ASSERT_MSG(
        bValidStack,
        "MemoryStack_RemainingSpan requires an initialized stack." );
    if ( !bValidStack || pStack->memory.pData == nullptr ) {
        return {};
    }

    return {
        pStack->memory.pData + pStack->iOffset,
        pStack->memory.cbSize - pStack->iOffset
    };
}

bool_t MemoryStack_Owns(
    const memory_stack_t *pStack,
    const void *pAddress ) noexcept
{
    return MemoryStack_IsValid( pStack ) &&
           FixedMemory_ContainsAddress( pStack->memory, pAddress );
}

} // namespace cypher::common
