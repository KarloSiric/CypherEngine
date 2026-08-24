//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Buffer.cpp
//  Purpose: Implements non-owning bounded writable byte buffers.
//  Details: Writes validate capacity before mutation, preserve old contents on
//           failure, and support overlapping source ranges through byte moves.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Buffer Implementation Notes

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#include "CypherCommon_Buffer.h"

namespace cypher::common
{

bool_t Buffer_Init( buffer_t *pBuffer, byte_span_t storage ) noexcept
{
    const bool_t bValidBuffer = pBuffer != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Init requires an output object." );
    CY_ASSERT_MSG( bValidStorage, "Buffer_Init requires valid storage." );
    if ( !bValidBuffer || !bValidStorage ) {
        return CY_FALSE;
    }

    pBuffer->pData = storage.pData;
    pBuffer->cbSize = 0u;
    pBuffer->cbCapacity = storage.nCount;
    return CY_TRUE;
}

void Buffer_Clear( buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Clear requires an initialized buffer." );
    if ( bValidBuffer ) {
        pBuffer->cbSize = 0u;
    }
}

bool_t Buffer_IsValid( const buffer_t *pBuffer ) noexcept
{
    return pBuffer != nullptr &&
           ( pBuffer->pData != nullptr || pBuffer->cbCapacity == 0u ) &&
           pBuffer->cbSize <= pBuffer->cbCapacity;
}

bool_t Buffer_IsEmpty( const buffer_t *pBuffer ) noexcept
{
    return !Buffer_IsValid( pBuffer ) || pBuffer->cbSize == 0u;
}

byte *Buffer_Data( buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Data requires an initialized buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

const byte *Buffer_Data( const buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Data requires an initialized buffer." );
    return bValidBuffer ? pBuffer->pData : nullptr;
}

usize Buffer_Size( const buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Size requires an initialized buffer." );
    return bValidBuffer ? pBuffer->cbSize : 0u;
}

usize Buffer_Capacity( const buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Capacity requires an initialized buffer." );
    return bValidBuffer ? pBuffer->cbCapacity : 0u;
}

usize Buffer_Remaining( const buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Remaining requires an initialized buffer." );
    return bValidBuffer ? pBuffer->cbCapacity - pBuffer->cbSize : 0u;
}

bool_t Buffer_Resize( buffer_t *pBuffer, usize cbSize ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Resize requires an initialized buffer." );
    if ( !bValidBuffer || cbSize > pBuffer->cbCapacity ) {
        return CY_FALSE;
    }

    pBuffer->cbSize = cbSize;
    return CY_TRUE;
}

bool_t Buffer_Assign( buffer_t *pBuffer, binary_block_t source ) noexcept
{
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    CY_ASSERT_MSG( bValidSource, "Buffer_Assign requires a valid source block." );
    if ( !Buffer_IsValid( pBuffer ) || !bValidSource ||
         source.cbSize > pBuffer->cbCapacity ) {
        return CY_FALSE;
    }

    if ( source.cbSize > 0u ) {
        Cy_MemMove( pBuffer->pData, source.pData, source.cbSize );
    }
    pBuffer->cbSize = source.cbSize;
    return CY_TRUE;
}

bool_t Buffer_Append(
    buffer_t *pBuffer,
    const void *pData,
    usize cbData ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    const bool_t bValidSource = pData != nullptr || cbData == 0u;
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Append requires an initialized buffer." );
    CY_ASSERT_MSG(
        bValidSource,
        "Buffer_Append requires non-null data for a non-empty source." );
    // Validate the complete append before touching storage; failure is atomic.
    if ( !bValidBuffer || !bValidSource ||
         cbData > pBuffer->cbCapacity - pBuffer->cbSize ) {
        return CY_FALSE;
    }

    if ( cbData > 0u ) {
        // MemMove permits a source range that aliases this buffer.
        Cy_MemMove( pBuffer->pData + pBuffer->cbSize, pData, cbData );
        pBuffer->cbSize += cbData;
    }
    return CY_TRUE;
}

bool_t Buffer_AppendBlock(
    buffer_t *pBuffer,
    binary_block_t source ) noexcept
{
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    CY_ASSERT_MSG(
        bValidSource,
        "Buffer_AppendBlock requires a valid source block." );
    return bValidSource && Buffer_Append( pBuffer, source.pData, source.cbSize );
}

bool_t Buffer_AppendByte( buffer_t *pBuffer, byte value ) noexcept
{
    return Buffer_Append( pBuffer, &value, sizeof( value ) );
}

bool_t Buffer_AppendZero( buffer_t *pBuffer, usize cbData ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "Buffer_AppendZero requires an initialized buffer." );
    if ( !bValidBuffer || cbData > pBuffer->cbCapacity - pBuffer->cbSize ) {
        return CY_FALSE;
    }

    if ( cbData > 0u ) {
        Cy_MemZero( pBuffer->pData + pBuffer->cbSize, cbData );
        pBuffer->cbSize += cbData;
    }
    return CY_TRUE;
}

byte_span_t Buffer_AppendUninitialized(
    buffer_t *pBuffer,
    usize cbData ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "Buffer_AppendUninitialized requires an initialized buffer." );
    if ( !bValidBuffer || cbData > pBuffer->cbCapacity - pBuffer->cbSize ) {
        return {};
    }

    // Size is published with the reservation; caller must initialize the returned
    // bytes before exposing the buffer as immutable data.
    byte *pAppended = pBuffer->pData != nullptr
        ? pBuffer->pData + pBuffer->cbSize
        : nullptr;
    pBuffer->cbSize += cbData;
    return { pAppended, cbData };
}

byte_span_t Buffer_WritableSpan( buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "Buffer_WritableSpan requires an initialized buffer." );
    return bValidBuffer
        ? byte_span_t{ pBuffer->pData, pBuffer->cbSize }
        : byte_span_t{};
}

byte_span_t Buffer_RemainingSpan( buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG(
        bValidBuffer,
        "Buffer_RemainingSpan requires an initialized buffer." );
    if ( !bValidBuffer || pBuffer->pData == nullptr ) {
        return {};
    }

    return {
        pBuffer->pData + pBuffer->cbSize,
        pBuffer->cbCapacity - pBuffer->cbSize
    };
}

binary_block_t Buffer_Block( const buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = Buffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "Buffer_Block requires an initialized buffer." );
    return bValidBuffer
        ? binary_block_t{ pBuffer->pData, pBuffer->cbSize }
        : binary_block_t{};
}

} // namespace cypher::common
