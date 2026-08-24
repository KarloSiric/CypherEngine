//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitBuffer.cpp
//  Purpose: Implements non-owning fixed-capacity bit storage.
//  Details: Logical growth and shrink operations initialize every affected bit so
//           serialized padding never exposes stale caller storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitBuffer.h"

namespace cypher::common
{

namespace
{

// Logical bits are packed least-significant bit first inside each byte. Range
// writes preserve all bits outside the requested half-open interval.

usize BitsToBytes( usize nBits ) noexcept
{
    return ( nBits / 8u ) + ( ( nBits % 8u ) != 0u ? 1u : 0u );
}

void SetBitUnchecked( bit_buffer_t *pBuffer, usize iBit, bool_t value ) noexcept
{
    byte &target = pBuffer->pData[iBit / 8u];
    const byte mask = static_cast<byte>( 1u << ( iBit % 8u ) );
    if ( value ) {
        target = static_cast<byte>( target | mask );
    } else {
        target = static_cast<byte>( target & static_cast<byte>( ~mask ) );
    }
}

void ApplyByteMask( byte *pTarget, byte mask, bool_t value ) noexcept
{
    if ( value ) {
        *pTarget = static_cast<byte>( *pTarget | mask );
    } else {
        *pTarget = static_cast<byte>( *pTarget & static_cast<byte>( ~mask ) );
    }
}

void FillRangeUnchecked(
    bit_buffer_t *pBuffer,
    usize iFirst,
    usize iEnd,
    bool_t value ) noexcept
{
    if ( iFirst >= iEnd ) {
        return;
    }

    const usize iFirstByte = iFirst / 8u;
    const usize iLastByte = ( iEnd - 1u ) / 8u;
    const usize iFirstBit = iFirst % 8u;
    const usize iEndBit = iEnd % 8u;

    if ( iFirstByte == iLastByte ) {
        // A single-byte range needs one mask so neighboring bits survive.
        const usize nBitCount = iEnd - iFirst;
        const u32 nMask = ( ( 1u << nBitCount ) - 1u ) << iFirstBit;
        ApplyByteMask(
            pBuffer->pData + iFirstByte,
            static_cast<byte>( nMask ),
            value );
        return;
    }

    usize iWholeByteBegin = iFirstByte;
    if ( iFirstBit != 0u ) {
        ApplyByteMask(
            pBuffer->pData + iFirstByte,
            static_cast<byte>( 0xFFu << iFirstBit ),
            value );
        iWholeByteBegin = iFirstByte + 1u;
    }

    const usize iWholeByteEnd = iEnd / 8u;
    if ( iWholeByteBegin < iWholeByteEnd ) {
        // Only complete interior bytes can use the bulk fill path.
        Cy_MemSet(
            pBuffer->pData + iWholeByteBegin,
            value ? 0xFFu : 0x00u,
            iWholeByteEnd - iWholeByteBegin );
    }

    if ( iEndBit != 0u ) {
        ApplyByteMask(
            pBuffer->pData + iWholeByteEnd,
            static_cast<byte>( ( 1u << iEndBit ) - 1u ),
            value );
    }
}

void ClearPaddingUnchecked( bit_buffer_t *pBuffer ) noexcept
{
    const usize nBitsInLastByte = pBuffer->nBitSize % 8u;
    if ( nBitsInLastByte == 0u ) {
        return;
    }

    // Never expose stale padding when the logical block is serialized.
    const usize iLastByte = pBuffer->nBitSize / 8u;
    const byte mask = static_cast<byte>( ( 1u << nBitsInLastByte ) - 1u );
    pBuffer->pData[iLastByte] = static_cast<byte>(
        pBuffer->pData[iLastByte] & mask );
}

} // namespace

bool_t BitBuffer_Init( bit_buffer_t *pBuffer, byte_span_t storage ) noexcept
{
    const bool_t bValidBuffer = pBuffer != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    const bool_t bCapacityFits = storage.nCount <= CY_USIZE_MAX / 8u;
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Init requires a buffer object." );
    CY_ASSERT_MSG( bValidStorage, "BitBuffer_Init requires valid storage." );
    CY_ASSERT_MSG( bCapacityFits, "BitBuffer_Init bit capacity overflowed." );
    if ( !bValidBuffer || !bValidStorage || !bCapacityFits ) {
        return CY_FALSE;
    }

    pBuffer->pData = storage.pData;
    pBuffer->nBitSize = 0u;
    pBuffer->nBitCapacity = storage.nCount * 8u;
    return CY_TRUE;
}

void BitBuffer_Clear( bit_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Clear requires a valid buffer." );
    if ( !bValidBuffer ) {
        return;
    }

    const usize cbUsed = BitsToBytes( pBuffer->nBitSize );
    if ( cbUsed > 0u ) {
        Cy_MemZero( pBuffer->pData, cbUsed );
    }
    pBuffer->nBitSize = 0u;
}

bool_t BitBuffer_IsValid( const bit_buffer_t *pBuffer ) noexcept
{
    return pBuffer != nullptr &&
           ( pBuffer->pData != nullptr || pBuffer->nBitCapacity == 0u ) &&
           ( pBuffer->nBitCapacity % 8u ) == 0u &&
           pBuffer->nBitSize <= pBuffer->nBitCapacity;
}

bool_t BitBuffer_IsEmpty( const bit_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_IsEmpty requires a valid buffer." );
    return !bValidBuffer || pBuffer->nBitSize == 0u;
}

usize BitBuffer_Size( const bit_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Size requires a valid buffer." );
    return bValidBuffer ? pBuffer->nBitSize : 0u;
}

usize BitBuffer_Capacity( const bit_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Capacity requires a valid buffer." );
    return bValidBuffer ? pBuffer->nBitCapacity : 0u;
}

bool_t BitBuffer_Resize(
    bit_buffer_t *pBuffer,
    usize nBitSize,
    bool_t bFillValue ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Resize requires a valid buffer." );
    if ( !bValidBuffer || nBitSize > pBuffer->nBitCapacity ) {
        return CY_FALSE;
    }
    if ( nBitSize == pBuffer->nBitSize ) {
        return CY_TRUE;
    }

    if ( nBitSize > pBuffer->nBitSize ) {
        FillRangeUnchecked(
            pBuffer,
            pBuffer->nBitSize,
            nBitSize,
            bFillValue );
    } else {
        FillRangeUnchecked(
            pBuffer,
            nBitSize,
            pBuffer->nBitSize,
            CY_FALSE );
    }
    pBuffer->nBitSize = nBitSize;
    ClearPaddingUnchecked( pBuffer );
    return CY_TRUE;
}

bool_t BitBuffer_Get(
    const bit_buffer_t *pBuffer,
    usize iBit,
    bool_t *pValueOut ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Get requires a valid buffer." );
    CY_ASSERT_MSG( bValidOutput, "BitBuffer_Get requires output." );
    if ( !bValidBuffer || !bValidOutput || iBit >= pBuffer->nBitSize ) {
        return CY_FALSE;
    }

    const byte mask = static_cast<byte>( 1u << ( iBit % 8u ) );
    *pValueOut = ( pBuffer->pData[iBit / 8u] & mask ) != 0u;
    return CY_TRUE;
}

bool_t BitBuffer_Set(
    bit_buffer_t *pBuffer,
    usize iBit,
    bool_t value ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Set requires a valid buffer." );
    if ( !bValidBuffer || iBit >= pBuffer->nBitSize ) {
        return CY_FALSE;
    }

    SetBitUnchecked( pBuffer, iBit, value );
    return CY_TRUE;
}

binary_block_t BitBuffer_Block( const bit_buffer_t *pBuffer ) noexcept
{
    const bool_t bValidBuffer = BitBuffer_IsValid( pBuffer );
    CY_ASSERT_MSG( bValidBuffer, "BitBuffer_Block requires a valid buffer." );
    if ( !bValidBuffer || pBuffer->nBitSize == 0u ) {
        return {};
    }
    return { pBuffer->pData, BitsToBytes( pBuffer->nBitSize ) };
}

} // namespace cypher::common
