//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PacketBuffer.cpp
//  Purpose: Implements borrowed packet storage and binary cursor adapters.
//  Details: Cursor commits require matching storage provenance and a successful
//           cursor state, preventing foreign or partially failed writers from
//           changing the packet's published payload size.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Packet Buffer Implementation Notes

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#include "CypherCommon_PacketBuffer.h"

namespace cypher::common
{

bool_t PacketBuffer_Init(
    packet_buffer_t *pPacket,
    byte_span_t storage ) noexcept
{
    const bool_t bValidPacket = pPacket != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_Init requires a packet object." );
    CY_ASSERT_MSG( bValidStorage, "PacketBuffer_Init requires valid storage." );
    if ( !bValidPacket || !bValidStorage ) {
        return CY_FALSE;
    }

    pPacket->pData = storage.pData; // Borrowed storage; PacketBuffer never frees it.
    pPacket->cbSize = 0u;
    pPacket->cbCapacity = storage.nCount;
    return CY_TRUE;
}

void PacketBuffer_Clear( packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_Clear requires a valid packet." );
    if ( bValidPacket ) {
        pPacket->cbSize = 0u;
    }
}

bool_t PacketBuffer_IsValid( const packet_buffer_t *pPacket ) noexcept
{
    return pPacket != nullptr &&
           ( pPacket->pData != nullptr || pPacket->cbCapacity == 0u ) &&
           pPacket->cbSize <= pPacket->cbCapacity;
}

bool_t PacketBuffer_IsEmpty( const packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_IsEmpty requires a valid packet." );
    return !bValidPacket || pPacket->cbSize == 0u;
}

usize PacketBuffer_Size( const packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_Size requires a valid packet." );
    return bValidPacket ? pPacket->cbSize : 0u;
}

usize PacketBuffer_Capacity( const packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_Capacity requires a valid packet." );
    return bValidPacket ? pPacket->cbCapacity : 0u;
}

bool_t PacketBuffer_SetSize(
    packet_buffer_t *pPacket,
    usize cbSize ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_SetSize requires a valid packet." );
    if ( !bValidPacket || cbSize > pPacket->cbCapacity ) {
        return CY_FALSE;
    }
    pPacket->cbSize = cbSize;
    return CY_TRUE;
}

binary_block_t PacketBuffer_Block( const packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_Block requires a valid packet." );
    if ( !bValidPacket || pPacket->cbSize == 0u ) {
        return {};
    }
    return { pPacket->pData, pPacket->cbSize };
}

byte_writer_t PacketBuffer_ByteWriter( packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_ByteWriter requires a valid packet." );
    byte_writer_t writer{};
    if ( bValidPacket ) {
        // Writers see full capacity, but bytes become packet payload only after commit.
        const bool_t bInitialized = ByteWriter_Init(
            &writer,
            { pPacket->pData, pPacket->cbCapacity } );
        CY_ASSERT_MSG( bInitialized, "PacketBuffer byte writer initialization failed." );
        (void)bInitialized;
    }
    return writer;
}

bool_t PacketBuffer_CommitByteWriter(
    packet_buffer_t *pPacket,
    const byte_writer_t &writer ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    const bool_t bValidWriter = ByteWriter_IsValid( &writer );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer byte commit requires a valid packet." );
    CY_ASSERT_MSG( bValidWriter, "PacketBuffer byte commit requires a valid writer." );
    // Identity and capacity checks prevent committing a writer created for a
    // different packet; failed cursors never publish partial data.
    if ( !bValidPacket || !bValidWriter ||
         writer.status != byte_cursor_status_t::OK ||
         writer.pData != pPacket->pData ||
         writer.cbCapacity != pPacket->cbCapacity ) {
        return CY_FALSE;
    }
    pPacket->cbSize = writer.cbHighWater;
    return CY_TRUE;
}

byte_reader_t PacketBuffer_ByteReader( const packet_buffer_t *pPacket ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_ByteReader requires a valid packet." );
    byte_reader_t reader{};
    if ( bValidPacket ) {
        const bool_t bInitialized = ByteReader_Init(
            &reader,
            PacketBuffer_Block( pPacket ) );
        CY_ASSERT_MSG( bInitialized, "PacketBuffer byte reader initialization failed." );
        (void)bInitialized;
    }
    return reader;
}

bit_writer_t PacketBuffer_BitWriter(
    packet_buffer_t *pPacket,
    bit_order_t bitOrder ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    const bool_t bBitCapacityFits =
        bValidPacket && pPacket->cbCapacity <= CY_USIZE_MAX / 8u;
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_BitWriter requires a valid packet." );
    CY_ASSERT_MSG( bBitCapacityFits, "PacketBuffer bit capacity overflowed." );
    bit_writer_t writer{};
    if ( bBitCapacityFits ) {
        const bool_t bInitialized = BitWriter_Init(
            &writer,
            { pPacket->pData, pPacket->cbCapacity },
            bitOrder );
        CY_ASSERT_MSG( bInitialized, "PacketBuffer bit writer initialization failed." );
        (void)bInitialized;
    }
    return writer;
}

bool_t PacketBuffer_CommitBitWriter(
    packet_buffer_t *pPacket,
    const bit_writer_t &writer ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    const bool_t bValidWriter = BitWriter_IsValid( &writer );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer bit commit requires a valid packet." );
    CY_ASSERT_MSG( bValidWriter, "PacketBuffer bit commit requires a valid writer." );
    // Bit writers carry capacity in bits, so provenance includes the exact
    // byte-to-bit capacity conversion as well as the backing pointer.
    if ( !bValidPacket || !bValidWriter ||
         writer.status != bit_cursor_status_t::OK ||
         writer.pData != pPacket->pData ||
         pPacket->cbCapacity > CY_USIZE_MAX / 8u ||
         writer.nBitCapacity != pPacket->cbCapacity * 8u ) {
        return CY_FALSE;
    }
    pPacket->cbSize = BitWriter_Block( &writer ).cbSize;
    return CY_TRUE;
}

bit_reader_t PacketBuffer_BitReader(
    const packet_buffer_t *pPacket,
    bit_order_t bitOrder ) noexcept
{
    const bool_t bValidPacket = PacketBuffer_IsValid( pPacket );
    CY_ASSERT_MSG( bValidPacket, "PacketBuffer_BitReader requires a valid packet." );
    bit_reader_t reader{};
    if ( bValidPacket ) {
        const bool_t bBitSizeFits = pPacket->cbSize <= CY_USIZE_MAX / 8u;
        CY_ASSERT_MSG( bBitSizeFits, "PacketBuffer bit size overflowed." );
        if ( bBitSizeFits ) {
            const bool_t bInitialized = BitReader_Init(
                &reader,
                PacketBuffer_Block( pPacket ),
                pPacket->cbSize * 8u,
                bitOrder );
            CY_ASSERT_MSG( bInitialized, "PacketBuffer bit reader initialization failed." );
            (void)bInitialized;
        }
    }
    return reader;
}

} // namespace cypher::common
