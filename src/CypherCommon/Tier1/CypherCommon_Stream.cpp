//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stream.cpp
//  Purpose: Implements the callback-based byte stream interface.
//  Details: Wrappers validate capabilities and callback results at the boundary.
//           Exact transfers loop over legal partial progress and reject zero-progress
//           success so a broken backend cannot hang an engine or tool thread.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stream.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_STREAM_CAPABILITY_MASK =
    STREAM_CAPABILITY_READ |
    STREAM_CAPABILITY_WRITE |
    STREAM_CAPABILITY_SEEK |
    STREAM_CAPABILITY_SIZE |
    STREAM_CAPABILITY_FLUSH;

bool_t IsStreamStatusValid( stream_status_t status ) noexcept
{
    switch ( status ) {
        case stream_status_t::OK:
        case stream_status_t::END_OF_STREAM:
        case stream_status_t::INVALID_ARGUMENT:
        case stream_status_t::UNSUPPORTED:
        case stream_status_t::IO_ERROR:
        case stream_status_t::OUT_OF_RANGE:
        case stream_status_t::CLOSED:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t IsSeekOriginValid( stream_seek_origin_t origin ) noexcept
{
    switch ( origin ) {
        case stream_seek_origin_t::BEGIN:
        case stream_seek_origin_t::CURRENT:
        case stream_seek_origin_t::END:
            return CY_TRUE;
    }
    return CY_FALSE;
}

stream_status_t StreamContractStatus( const stream_t *pStream ) noexcept
{
    if ( pStream == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( pStream->pOps == nullptr ) {
        return stream_status_t::CLOSED;
    }
    if ( ( pStream->capabilities & ~CY_STREAM_CAPABILITY_MASK ) != 0u ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    // A capability bit is a promise that the corresponding backend entry point
    // exists. Validate the table once at every public boundary before dispatch.
    if ( ( pStream->capabilities & STREAM_CAPABILITY_READ ) != 0u &&
         pStream->pOps->pfnRead == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( ( pStream->capabilities & STREAM_CAPABILITY_WRITE ) != 0u &&
         pStream->pOps->pfnWrite == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( ( pStream->capabilities & STREAM_CAPABILITY_SEEK ) != 0u &&
         ( pStream->pOps->pfnSeek == nullptr ||
           pStream->pOps->pfnTell == nullptr ) ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( ( pStream->capabilities & STREAM_CAPABILITY_SIZE ) != 0u &&
         pStream->pOps->pfnSize == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( ( pStream->capabilities & STREAM_CAPABILITY_FLUSH ) != 0u &&
         pStream->pOps->pfnFlush == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    return stream_status_t::OK;
}

stream_io_result_t ValidateIoResult(
    stream_io_result_t result,
    usize cbRequested ) noexcept
{
    // Backends are untrusted at this boundary: impossible status values or byte
    // counts are converted into a deterministic engine-level I/O failure.
    if ( !IsStreamStatusValid( result.status ) ||
         result.cbTransferred > cbRequested ) {
        return { stream_status_t::IO_ERROR, 0u };
    }
    return result;
}

} // namespace

bool_t Stream_IsValid( const stream_t *pStream ) noexcept
{
    return StreamContractStatus( pStream ) == stream_status_t::OK;
}

bool_t Stream_HasCapabilities(
    const stream_t *pStream,
    flags32_t capabilities ) noexcept
{
    if ( !Stream_IsValid( pStream ) ||
         ( capabilities & ~CY_STREAM_CAPABILITY_MASK ) != 0u ) {
        return CY_FALSE;
    }
    return ( pStream->capabilities & capabilities ) == capabilities;
}

stream_io_result_t Stream_Read(
    stream_t *pStream,
    void *pDest,
    usize cbRequested ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return { contractStatus, 0u };
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_READ ) ) {
        return { stream_status_t::UNSUPPORTED, 0u };
    }
    if ( pDest == nullptr && cbRequested != 0u ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }
    return ValidateIoResult(
        pStream->pOps->pfnRead( pStream->pUserData, pDest, cbRequested ),
        cbRequested );
}

stream_status_t Stream_ReadExact(
    stream_t *pStream,
    void *pDest,
    usize cbRequired ) noexcept
{
    if ( pDest == nullptr && cbRequired != 0u ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    usize cbTotal = 0u;
    while ( cbTotal < cbRequired ) {
        stream_io_result_t result = Stream_Read(
            pStream,
            static_cast<byte *>( pDest ) + cbTotal,
            cbRequired - cbTotal );
        cbTotal += result.cbTransferred;
        if ( cbTotal == cbRequired ) {
            return stream_status_t::OK;
        }
        if ( result.status != stream_status_t::OK ) {
            return result.status;
        }
        // OK without progress violates the stream contract and would otherwise
        // leave an exact transfer spinning forever.
        if ( result.cbTransferred == 0u ) {
            return stream_status_t::IO_ERROR;
        }
    }
    return stream_status_t::OK;
}

stream_io_result_t Stream_Write(
    stream_t *pStream,
    const void *pSource,
    usize cbRequested ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return { contractStatus, 0u };
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_WRITE ) ) {
        return { stream_status_t::UNSUPPORTED, 0u };
    }
    if ( pSource == nullptr && cbRequested != 0u ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }
    return ValidateIoResult(
        pStream->pOps->pfnWrite( pStream->pUserData, pSource, cbRequested ),
        cbRequested );
}

stream_status_t Stream_WriteExact(
    stream_t *pStream,
    const void *pSource,
    usize cbRequired ) noexcept
{
    if ( pSource == nullptr && cbRequired != 0u ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    usize cbTotal = 0u;
    while ( cbTotal < cbRequired ) {
        stream_io_result_t result = Stream_Write(
            pStream,
            static_cast<const byte *>( pSource ) + cbTotal,
            cbRequired - cbTotal );
        cbTotal += result.cbTransferred;
        if ( cbTotal == cbRequired ) {
            return stream_status_t::OK;
        }
        if ( result.status != stream_status_t::OK ) {
            return result.status;
        }
        // Treat a zero-progress success as a broken backend, not a retry signal.
        if ( result.cbTransferred == 0u ) {
            return stream_status_t::IO_ERROR;
        }
    }
    return stream_status_t::OK;
}

stream_status_t Stream_Seek(
    stream_t *pStream,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return contractStatus;
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_SEEK ) ) {
        return stream_status_t::UNSUPPORTED;
    }
    if ( !IsSeekOriginValid( origin ) ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    u64 nPosition = 0u;
    const stream_status_t status = pStream->pOps->pfnSeek(
        pStream->pUserData,
        nOffset,
        origin,
        &nPosition );
    if ( !IsStreamStatusValid( status ) ) {
        return stream_status_t::IO_ERROR;
    }
    if ( status == stream_status_t::OK && pPositionOut != nullptr ) {
        *pPositionOut = nPosition;
    }
    return status;
}

stream_status_t Stream_Tell( stream_t *pStream, u64 *pPositionOut ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return contractStatus;
    }
    if ( pPositionOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_SEEK ) ) {
        return stream_status_t::UNSUPPORTED;
    }

    u64 nPosition = 0u;
    const stream_status_t status = pStream->pOps->pfnTell(
        pStream->pUserData,
        &nPosition );
    if ( !IsStreamStatusValid( status ) ) {
        return stream_status_t::IO_ERROR;
    }
    if ( status == stream_status_t::OK ) {
        *pPositionOut = nPosition;
    }
    return status;
}

stream_status_t Stream_Size( stream_t *pStream, u64 *pSizeOut ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return contractStatus;
    }
    if ( pSizeOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_SIZE ) ) {
        return stream_status_t::UNSUPPORTED;
    }

    u64 cbSize = 0u;
    const stream_status_t status = pStream->pOps->pfnSize(
        pStream->pUserData,
        &cbSize );
    if ( !IsStreamStatusValid( status ) ) {
        return stream_status_t::IO_ERROR;
    }
    if ( status == stream_status_t::OK ) {
        *pSizeOut = cbSize;
    }
    return status;
}

stream_status_t Stream_Flush( stream_t *pStream ) noexcept
{
    const stream_status_t contractStatus = StreamContractStatus( pStream );
    if ( contractStatus != stream_status_t::OK ) {
        return contractStatus;
    }
    if ( !Stream_HasCapabilities( pStream, STREAM_CAPABILITY_FLUSH ) ) {
        return stream_status_t::UNSUPPORTED;
    }

    const stream_status_t status = pStream->pOps->pfnFlush( pStream->pUserData );
    return IsStreamStatusValid( status ) ? status : stream_status_t::IO_ERROR;
}

} // namespace cypher::common
