//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ScratchBuffer.cpp
//  Purpose: Implements scoped local-first temporary byte storage.
//  Details: Local storage is aligned in place when possible; otherwise one explicit
//           allocator-backed ownership record is released automatically or manually.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Scratch Buffer Implementation Notes

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#include "CypherCommon_ScratchBuffer.h"

#include "CypherCommon_FixedMemory.h"

namespace cypher::common
{

namespace
{

bool_t IsCanonicalEmpty( const scratch_buffer_t &buffer ) noexcept
{
    return buffer.pData == nullptr &&
           buffer.cbSize == 0u &&
           buffer.nAlignment == 0u &&
           buffer.localStorage.pData == nullptr &&
           buffer.localStorage.nCount == 0u &&
           Allocator_OwnedIsValid( &buffer.fallback ) &&
           buffer.fallback.pData == nullptr;
}

byte *FindLocalStorage(
    byte_span_t storage,
    usize cbSize,
    usize nAlignment ) noexcept
{
    if ( storage.pData == nullptr || cbSize == 0u ) {
        return nullptr;
    }

    uintptr nAlignedAddress = 0u;
    if ( !Cy_AlignPointerUpChecked(
             storage.pData,
             nAlignment,
             nAlignedAddress ) ) {
        return nullptr;
    }

    const usize cbPadding = static_cast<usize>(
        nAlignedAddress - reinterpret_cast<uintptr>( storage.pData ) );
    // Alignment padding consumes part of the caller's local span and must be
    // included when deciding whether the requested payload still fits.
    if ( cbPadding > storage.nCount || cbSize > storage.nCount - cbPadding ) {
        return nullptr;
    }

    return reinterpret_cast<byte *>( nAlignedAddress );
}

} // namespace

scratch_buffer_t::~scratch_buffer_t() noexcept
{
    ScratchBuffer_Release( this );
}

bool_t ScratchBuffer_Acquire(
    scratch_buffer_t *pBuffer,
    byte_span_t localStorage,
    const allocator_t *pFallbackAllocator,
    usize cbSize,
    usize nAlignment ) noexcept
{
    const bool_t bValidDestination =
        pBuffer != nullptr && IsCanonicalEmpty( *pBuffer );
    const bool_t bValidLocalStorage = Span_IsValid( localStorage );
    const bool_t bValidAlignment = Cy_AlignIsPowerOfTwo( nAlignment );
    CY_ASSERT_MSG(
        bValidDestination,
        "ScratchBuffer_Acquire requires an empty destination." );
    CY_ASSERT_MSG(
        bValidLocalStorage,
        "ScratchBuffer_Acquire requires valid local storage." );
    CY_ASSERT_MSG(
        bValidAlignment,
        "ScratchBuffer_Acquire requires power-of-two alignment." );
    if ( !bValidDestination || !bValidLocalStorage || !bValidAlignment ) {
        return CY_FALSE;
    }

    if ( cbSize == 0u ) {
        return CY_TRUE;
    }

    // Prefer borrowed local storage; ownership remains empty on this fast path.
    byte *pLocalData = FindLocalStorage(
        localStorage,
        cbSize,
        nAlignment );
    if ( pLocalData != nullptr ) {
        pBuffer->pData = pLocalData;
        pBuffer->cbSize = cbSize;
        pBuffer->nAlignment = nAlignment;
        pBuffer->localStorage = localStorage;
        return CY_TRUE;
    }

    // The fallback record captures allocator, size, and alignment so Release
    // does not need the original acquisition arguments.
    const bool_t bValidFallback = Allocator_IsValid( pFallbackAllocator );
    CY_ASSERT_MSG(
        bValidFallback,
        "ScratchBuffer fallback requires a valid allocator." );
    if ( !bValidFallback ||
         !Allocator_AllocateOwned(
             &pBuffer->fallback,
             pFallbackAllocator,
             cbSize,
             nAlignment ) ) {
        return CY_FALSE;
    }

    pBuffer->pData = static_cast<byte *>( pBuffer->fallback.pData );
    pBuffer->cbSize = cbSize;
    pBuffer->nAlignment = nAlignment;
    pBuffer->localStorage = localStorage;
    return CY_TRUE;
}

bool_t ScratchBuffer_AcquireZeroed(
    scratch_buffer_t *pBuffer,
    byte_span_t localStorage,
    const allocator_t *pFallbackAllocator,
    usize cbSize,
    usize nAlignment ) noexcept
{
    if ( !ScratchBuffer_Acquire(
             pBuffer,
             localStorage,
             pFallbackAllocator,
             cbSize,
             nAlignment ) ) {
        return CY_FALSE;
    }

    if ( pBuffer->pData != nullptr ) {
        Cy_MemZero( pBuffer->pData, pBuffer->cbSize );
    }
    return CY_TRUE;
}

void ScratchBuffer_Release( scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBufferObject = pBuffer != nullptr;
    CY_ASSERT_MSG(
        bValidBufferObject,
        "ScratchBuffer_Release requires a buffer object." );
    if ( !bValidBufferObject ) {
        return;
    }

    Allocator_FreeOwned( &pBuffer->fallback ); // No-op for buffers backed by local storage.
    pBuffer->pData = nullptr;
    pBuffer->cbSize = 0u;
    pBuffer->nAlignment = 0u;
    pBuffer->localStorage = {};
}

bool_t ScratchBuffer_IsValid( const scratch_buffer_t *pBuffer ) noexcept
{
    if ( pBuffer == nullptr || !Allocator_OwnedIsValid( &pBuffer->fallback ) ) {
        return CY_FALSE;
    }

    if ( IsCanonicalEmpty( *pBuffer ) ) {
        return CY_TRUE;
    }

    if ( pBuffer->pData == nullptr ||
         pBuffer->cbSize == 0u ||
         !Cy_AlignIsPowerOfTwo( pBuffer->nAlignment ) ||
         !Cy_AlignIsPointerAligned( pBuffer->pData, pBuffer->nAlignment ) ||
         !Span_IsValid( pBuffer->localStorage ) ) {
        return CY_FALSE;
    }

    if ( pBuffer->fallback.pData != nullptr ) {
        return pBuffer->pData == pBuffer->fallback.pData &&
               pBuffer->cbSize == pBuffer->fallback.cbSize &&
               pBuffer->nAlignment == pBuffer->fallback.nAlignment;
    }

    return FixedMemory_ContainsRange(
        FixedMemory_FromSpan( pBuffer->localStorage ),
        pBuffer->pData,
        pBuffer->cbSize );
}

byte *ScratchBuffer_Data( scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Data requires a valid buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

const byte *ScratchBuffer_Data( const scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Data requires a valid buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

usize ScratchBuffer_Size( const scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Size requires a valid buffer." );
    return bValidBuffer ? pBuffer->cbSize : 0u;
}

byte_span_t ScratchBuffer_Span( scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Span requires a valid buffer." );
    return bValidBuffer
        ? byte_span_t{ pBuffer->pData, pBuffer->cbSize }
        : byte_span_t{};
}

binary_block_t ScratchBuffer_Block(
    const scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Block requires a valid buffer." );
    return bValidBuffer
        ? binary_block_t{ pBuffer->pData, pBuffer->cbSize }
        : binary_block_t{};
}

bool_t ScratchBuffer_UsesLocalStorage(
    const scratch_buffer_t *pBuffer ) noexcept
{
    return ScratchBuffer_IsValid( pBuffer ) &&
           pBuffer->pData != nullptr &&
           pBuffer->fallback.pData == nullptr;
}

bool_t ScratchBuffer_UsesFallbackAllocation(
    const scratch_buffer_t *pBuffer ) noexcept
{
    return ScratchBuffer_IsValid( pBuffer ) &&
           pBuffer->fallback.pData != nullptr;
}

void ScratchBuffer_Clear( scratch_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = ScratchBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "ScratchBuffer_Clear requires a valid buffer." );
    if ( bValidBuffer && pBuffer->pData != nullptr ) {
        Cy_MemZero( pBuffer->pData, pBuffer->cbSize );
    }
}

} // namespace cypher::common
