//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStream.cpp
//  Purpose: Implements stream adapters over borrowed memory.
//  Details: Read-only streams expose immutable initialized bytes. Writable streams
//           track a logical high-water size, disallow uninitialized seek gaps, and
//           zero any bytes exposed through explicit logical growth.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryStream.h"

namespace cypher::common
{

namespace
{

stream_io_result_t MemoryStreamRead(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept
{
    auto *pMemoryStream = static_cast<memory_stream_t *>( pUserData );
    if ( !MemoryStream_IsValid( pMemoryStream ) ||
         ( pDest == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbRemaining = pMemoryStream->cbSize - pMemoryStream->iPosition;
    const usize cbTransfer = cbRequested < cbRemaining
        ? cbRequested
        : cbRemaining;
    if ( cbTransfer > 0u ) {
        Cy_MemMove(
            pDest,
            pMemoryStream->pReadData + pMemoryStream->iPosition,
            cbTransfer );
        pMemoryStream->iPosition += cbTransfer;
    }
    return {
        cbTransfer == cbRequested
            ? stream_status_t::OK
            : stream_status_t::END_OF_STREAM,
        cbTransfer
    };
}

stream_io_result_t MemoryStreamWrite(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept
{
    auto *pMemoryStream = static_cast<memory_stream_t *>( pUserData );
    if ( !MemoryStream_IsValid( pMemoryStream ) ||
         ( pSource == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( !pMemoryStream->bWritable ) {
        return { stream_status_t::UNSUPPORTED, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbRemaining =
        pMemoryStream->cbCapacity - pMemoryStream->iPosition;
    const usize cbTransfer = cbRequested < cbRemaining
        ? cbRequested
        : cbRemaining;
    if ( cbTransfer > 0u ) {
        Cy_MemMove(
            pMemoryStream->pWriteData + pMemoryStream->iPosition,
            pSource,
            cbTransfer );
        pMemoryStream->iPosition += cbTransfer;
        if ( pMemoryStream->iPosition > pMemoryStream->cbSize ) {
            pMemoryStream->cbSize = pMemoryStream->iPosition;
        }
    }
    return {
        cbTransfer == cbRequested
            ? stream_status_t::OK
            : stream_status_t::OUT_OF_RANGE,
        cbTransfer
    };
}

stream_status_t MemoryStreamSeek(
    void *pUserData,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept
{
    auto *pMemoryStream = static_cast<memory_stream_t *>( pUserData );
    if ( !MemoryStream_IsValid( pMemoryStream ) || pPositionOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    usize iBase = 0u;
    switch ( origin ) {
        case stream_seek_origin_t::BEGIN:
            break;
        case stream_seek_origin_t::CURRENT:
            iBase = pMemoryStream->iPosition;
            break;
        case stream_seek_origin_t::END:
            iBase = pMemoryStream->cbSize;
            break;
        default:
            return stream_status_t::INVALID_ARGUMENT;
    }

    usize iPosition = 0u;
    if ( nOffset >= 0 ) {
        const u64 nPositiveOffset = static_cast<u64>( nOffset );
        if ( nPositiveOffset > CY_USIZE_MAX ||
             static_cast<usize>( nPositiveOffset ) > pMemoryStream->cbSize - iBase ) {
            return stream_status_t::OUT_OF_RANGE;
        }
        iPosition = iBase + static_cast<usize>( nPositiveOffset );
    } else {
        const u64 nMagnitude =
            static_cast<u64>( -( nOffset + 1 ) ) + 1u;
        if ( nMagnitude > iBase ) {
            return stream_status_t::OUT_OF_RANGE;
        }
        iPosition = iBase - static_cast<usize>( nMagnitude );
    }

    pMemoryStream->iPosition = iPosition;
    *pPositionOut = static_cast<u64>( iPosition );
    return stream_status_t::OK;
}

stream_status_t MemoryStreamTell(
    void *pUserData,
    u64 *pValueOut ) noexcept
{
    const auto *pMemoryStream = static_cast<const memory_stream_t *>( pUserData );
    if ( !MemoryStream_IsValid( pMemoryStream ) || pValueOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    *pValueOut = static_cast<u64>( pMemoryStream->iPosition );
    return stream_status_t::OK;
}

stream_status_t MemoryStreamSize(
    void *pUserData,
    u64 *pValueOut ) noexcept
{
    const auto *pMemoryStream = static_cast<const memory_stream_t *>( pUserData );
    if ( !MemoryStream_IsValid( pMemoryStream ) || pValueOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    *pValueOut = static_cast<u64>( pMemoryStream->cbSize );
    return stream_status_t::OK;
}

stream_status_t MemoryStreamFlush( void *pUserData ) noexcept
{
    return MemoryStream_IsValid(
        static_cast<const memory_stream_t *>( pUserData ) )
            ? stream_status_t::OK
            : stream_status_t::INVALID_ARGUMENT;
}

const stream_ops_t MEMORY_STREAM_OPS{
    &MemoryStreamRead,
    &MemoryStreamWrite,
    &MemoryStreamSeek,
    &MemoryStreamTell,
    &MemoryStreamSize,
    &MemoryStreamFlush
};

} // namespace

bool_t MemoryStream_InitRead(
    memory_stream_t *pMemoryStream,
    binary_block_t source ) noexcept
{
    const bool_t bValidObject = pMemoryStream != nullptr;
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    CY_ASSERT_MSG( bValidObject, "MemoryStream_InitRead requires an object." );
    CY_ASSERT_MSG( bValidSource, "MemoryStream_InitRead requires a valid source." );
    if ( !bValidObject || !bValidSource ) {
        return CY_FALSE;
    }

    pMemoryStream->pReadData = source.pData;
    pMemoryStream->pWriteData = nullptr;
    pMemoryStream->cbSize = source.cbSize;
    pMemoryStream->cbCapacity = source.cbSize;
    pMemoryStream->iPosition = 0u;
    pMemoryStream->bWritable = CY_FALSE;
    return CY_TRUE;
}

bool_t MemoryStream_InitWrite(
    memory_stream_t *pMemoryStream,
    byte_span_t storage,
    usize cbInitialSize ) noexcept
{
    const bool_t bValidObject = pMemoryStream != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    const bool_t bInitialSizeFits = cbInitialSize <= storage.nCount;
    CY_ASSERT_MSG( bValidObject, "MemoryStream_InitWrite requires an object." );
    CY_ASSERT_MSG( bValidStorage, "MemoryStream_InitWrite requires valid storage." );
    CY_ASSERT_MSG( bInitialSizeFits, "MemoryStream initial size exceeds capacity." );
    if ( !bValidObject || !bValidStorage || !bInitialSizeFits ) {
        return CY_FALSE;
    }

    pMemoryStream->pReadData = storage.pData;
    pMemoryStream->pWriteData = storage.pData;
    pMemoryStream->cbSize = cbInitialSize;
    pMemoryStream->cbCapacity = storage.nCount;
    pMemoryStream->iPosition = 0u;
    pMemoryStream->bWritable = CY_TRUE;
    return CY_TRUE;
}

void MemoryStream_Reset( memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_Reset requires a valid stream." );
    if ( bValidStream ) {
        pMemoryStream->iPosition = 0u;
    }
}

bool_t MemoryStream_IsValid( const memory_stream_t *pMemoryStream ) noexcept
{
    if ( pMemoryStream == nullptr ||
         pMemoryStream->cbSize > pMemoryStream->cbCapacity ||
         pMemoryStream->iPosition > pMemoryStream->cbSize ||
         ( pMemoryStream->pReadData == nullptr &&
           pMemoryStream->cbCapacity != 0u ) ) {
        return CY_FALSE;
    }
    if ( pMemoryStream->bWritable ) {
        return pMemoryStream->pWriteData == pMemoryStream->pReadData;
    }
    return pMemoryStream->pWriteData == nullptr &&
           pMemoryStream->cbSize == pMemoryStream->cbCapacity;
}

bool_t MemoryStream_IsWritable( const memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_IsWritable requires a valid stream." );
    return bValidStream && pMemoryStream->bWritable;
}

usize MemoryStream_Size( const memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_Size requires a valid stream." );
    return bValidStream ? pMemoryStream->cbSize : 0u;
}

usize MemoryStream_Capacity( const memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_Capacity requires a valid stream." );
    return bValidStream ? pMemoryStream->cbCapacity : 0u;
}

usize MemoryStream_Position( const memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_Position requires a valid stream." );
    return bValidStream ? pMemoryStream->iPosition : 0u;
}

bool_t MemoryStream_SetSize(
    memory_stream_t *pMemoryStream,
    usize cbSize ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_SetSize requires a valid stream." );
    if ( !bValidStream || !pMemoryStream->bWritable ||
         cbSize > pMemoryStream->cbCapacity ) {
        return CY_FALSE;
    }

    if ( cbSize > pMemoryStream->cbSize ) {
        Cy_MemZero(
            pMemoryStream->pWriteData + pMemoryStream->cbSize,
            cbSize - pMemoryStream->cbSize );
    }
    pMemoryStream->cbSize = cbSize;
    if ( pMemoryStream->iPosition > cbSize ) {
        pMemoryStream->iPosition = cbSize;
    }
    return CY_TRUE;
}

bool_t MemoryStream_Clear( memory_stream_t *pMemoryStream ) noexcept
{
    return MemoryStream_SetSize( pMemoryStream, 0u );
}

stream_t MemoryStream_AsStream( memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_AsStream requires a valid stream." );
    if ( !bValidStream ) {
        return {};
    }

    flags32_t capabilities =
        STREAM_CAPABILITY_READ |
        STREAM_CAPABILITY_SEEK |
        STREAM_CAPABILITY_SIZE;
    if ( pMemoryStream->bWritable ) {
        capabilities |= STREAM_CAPABILITY_WRITE | STREAM_CAPABILITY_FLUSH;
    }
    return { &MEMORY_STREAM_OPS, pMemoryStream, capabilities };
}

binary_block_t MemoryStream_Block(
    const memory_stream_t *pMemoryStream ) noexcept
{
    const bool_t bValidStream = MemoryStream_IsValid( pMemoryStream );
    CY_ASSERT_MSG( bValidStream, "MemoryStream_Block requires a valid stream." );
    if ( !bValidStream || pMemoryStream->cbSize == 0u ) {
        return {};
    }
    return { pMemoryStream->pReadData, pMemoryStream->cbSize };
}

} // namespace cypher::common

