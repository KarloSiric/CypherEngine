//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Stream_Tests.cpp
//  Purpose: Tests callback-based byte stream boundary behavior.
//  Details: These tests protect capability validation, exact-transfer looping,
//           malformed callback rejection, output preservation, and closed streams.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stream.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct stream_test_state_t {
    byte data[16]{};
    usize cbSize{ 0u };
    usize iPosition{ 0u };
    usize cbChunk{ CY_USIZE_MAX };
    bool_t bZeroProgress{ CY_FALSE };
    bool_t bMalformedCount{ CY_FALSE };
};

stream_io_result_t TestRead(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept
{
    auto *pState = static_cast<stream_test_state_t *>( pUserData );
    if ( pState->bMalformedCount ) {
        return { stream_status_t::OK, cbRequested + 1u };
    }
    if ( pState->bZeroProgress ) {
        return {};
    }
    const usize cbRemaining = pState->cbSize - pState->iPosition;
    const usize cbAvailable = cbRequested < cbRemaining ? cbRequested : cbRemaining;
    const usize cbTransfer = cbAvailable < pState->cbChunk
        ? cbAvailable
        : pState->cbChunk;
    if ( cbTransfer > 0u ) {
        Cy_MemCopy( pDest, pState->data + pState->iPosition, cbTransfer );
        pState->iPosition += cbTransfer;
    }
    const stream_status_t status =
        cbTransfer < cbRequested && pState->iPosition == pState->cbSize
            ? stream_status_t::END_OF_STREAM
            : stream_status_t::OK;
    return { status, cbTransfer };
}

stream_io_result_t TestWrite(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept
{
    auto *pState = static_cast<stream_test_state_t *>( pUserData );
    if ( pState->bZeroProgress ) {
        return {};
    }
    const usize cbRemaining = sizeof( pState->data ) - pState->iPosition;
    const usize cbAvailable = cbRequested < cbRemaining ? cbRequested : cbRemaining;
    const usize cbTransfer = cbAvailable < pState->cbChunk
        ? cbAvailable
        : pState->cbChunk;
    if ( cbTransfer > 0u ) {
        Cy_MemCopy( pState->data + pState->iPosition, pSource, cbTransfer );
        pState->iPosition += cbTransfer;
        if ( pState->iPosition > pState->cbSize ) {
            pState->cbSize = pState->iPosition;
        }
    }
    const stream_status_t status =
        cbTransfer < cbRequested && pState->iPosition == sizeof( pState->data )
            ? stream_status_t::OUT_OF_RANGE
            : stream_status_t::OK;
    return { status, cbTransfer };
}

stream_status_t TestSeek(
    void *pUserData,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept
{
    auto *pState = static_cast<stream_test_state_t *>( pUserData );
    i64 nBase = 0;
    if ( origin == stream_seek_origin_t::CURRENT ) {
        nBase = static_cast<i64>( pState->iPosition );
    } else if ( origin == stream_seek_origin_t::END ) {
        nBase = static_cast<i64>( pState->cbSize );
    }
    if ( ( nOffset < 0 && nBase < -nOffset ) ||
         ( nOffset > 0 && nBase > static_cast<i64>( pState->cbSize ) - nOffset ) ) {
        return stream_status_t::OUT_OF_RANGE;
    }
    const i64 nPosition = nBase + nOffset;
    if ( nPosition < 0 || static_cast<usize>( nPosition ) > pState->cbSize ) {
        return stream_status_t::OUT_OF_RANGE;
    }
    pState->iPosition = static_cast<usize>( nPosition );
    *pPositionOut = static_cast<u64>( pState->iPosition );
    return stream_status_t::OK;
}

stream_status_t TestTell( void *pUserData, u64 *pValueOut ) noexcept
{
    const auto *pState = static_cast<const stream_test_state_t *>( pUserData );
    *pValueOut = static_cast<u64>( pState->iPosition );
    return stream_status_t::OK;
}

stream_status_t TestSize( void *pUserData, u64 *pValueOut ) noexcept
{
    const auto *pState = static_cast<const stream_test_state_t *>( pUserData );
    *pValueOut = static_cast<u64>( pState->cbSize );
    return stream_status_t::OK;
}

stream_status_t TestFlush( void * ) noexcept
{
    return stream_status_t::OK;
}

const stream_ops_t TEST_OPS{
    &TestRead,
    &TestWrite,
    &TestSeek,
    &TestTell,
    &TestSize,
    &TestFlush
};

stream_t MakeTestStream( stream_test_state_t *pState ) noexcept
{
    return {
        &TEST_OPS,
        pState,
        STREAM_CAPABILITY_READ |
        STREAM_CAPABILITY_WRITE |
        STREAM_CAPABILITY_SEEK |
        STREAM_CAPABILITY_SIZE |
        STREAM_CAPABILITY_FLUSH
    };
}

} // namespace

TEST_CASE( "Stream exact operations consume legal partial progress",
           "[CypherCommon][Tier1][Stream]" )
{
    stream_test_state_t state{};
    state.cbChunk = 2u;
    stream_t stream = MakeTestStream( &state );
    REQUIRE( Stream_IsValid( &stream ) );
    REQUIRE( Stream_HasCapabilities(
        &stream,
        STREAM_CAPABILITY_READ | STREAM_CAPABILITY_WRITE ) );

    const byte source[]{ 1u, 2u, 3u, 4u, 5u };
    REQUIRE( Stream_WriteExact( &stream, source, sizeof( source ) ) == stream_status_t::OK );
    REQUIRE( state.cbSize == sizeof( source ) );

    u64 nPosition = 99u;
    REQUIRE( Stream_Seek(
        &stream,
        0,
        stream_seek_origin_t::BEGIN,
        &nPosition ) == stream_status_t::OK );
    REQUIRE( nPosition == 0u );

    byte output[sizeof( source )]{};
    REQUIRE( Stream_ReadExact( &stream, output, sizeof( output ) ) == stream_status_t::OK );
    for ( usize iByte = 0u; iByte < sizeof( source ); ++iByte ) {
        REQUIRE( output[iByte] == source[iByte] );
    }

    u64 cbSize = 0u;
    REQUIRE( Stream_Tell( &stream, &nPosition ) == stream_status_t::OK );
    REQUIRE( nPosition == sizeof( source ) );
    REQUIRE( Stream_Size( &stream, &cbSize ) == stream_status_t::OK );
    REQUIRE( cbSize == sizeof( source ) );
    REQUIRE( Stream_Flush( &stream ) == stream_status_t::OK );
}

TEST_CASE( "Stream rejects malformed and zero-progress callbacks",
           "[CypherCommon][Tier1][Stream]" )
{
    stream_test_state_t state{};
    state.cbSize = 4u;
    stream_t stream = MakeTestStream( &state );
    byte output[4]{};

    state.bMalformedCount = CY_TRUE;
    const stream_io_result_t malformed = Stream_Read( &stream, output, sizeof( output ) );
    REQUIRE( malformed.status == stream_status_t::IO_ERROR );
    REQUIRE( malformed.cbTransferred == 0u );

    state.bMalformedCount = CY_FALSE;
    state.bZeroProgress = CY_TRUE;
    REQUIRE( Stream_ReadExact( &stream, output, sizeof( output ) ) == stream_status_t::IO_ERROR );
    REQUIRE( Stream_WriteExact( &stream, output, sizeof( output ) ) == stream_status_t::IO_ERROR );
}

TEST_CASE( "Stream reports closed unsupported and invalid operations",
           "[CypherCommon][Tier1][Stream]" )
{
    stream_t closed{};
    byte value = 0u;
    REQUIRE_FALSE( Stream_IsValid( &closed ) );
    REQUIRE( Stream_Read( &closed, &value, 1u ).status == stream_status_t::CLOSED );

    stream_test_state_t state{};
    stream_t readOnly{ &TEST_OPS, &state, STREAM_CAPABILITY_READ };
    REQUIRE( Stream_IsValid( &readOnly ) );
    REQUIRE( Stream_Write( &readOnly, &value, 1u ).status == stream_status_t::UNSUPPORTED );

    u64 output = 77u;
    REQUIRE( Stream_Size( &readOnly, &output ) == stream_status_t::UNSUPPORTED );
    REQUIRE( output == 77u );
    REQUIRE( Stream_Seek(
        &readOnly,
        0,
        static_cast<stream_seek_origin_t>( 0xFFu ) ) == stream_status_t::UNSUPPORTED );
}
