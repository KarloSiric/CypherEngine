//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PacketBuffer.h
//  Purpose: Declares non-owning packet payload storage and cursor adapters.
//  Details: PacketBuffer owns no network transport state. It tracks payload size and
//           exposes bounded byte/bit readers and writers over caller storage.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Packet Buffer Contract

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#ifndef CYPHER_COMMON_TIER1_PACKETBUFFER_H
#define CYPHER_COMMON_TIER1_PACKETBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BitReader.h"
#include "CypherCommon_BitWriter.h"
#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"

namespace cypher::common
{

struct packet_buffer_t {
    byte *pData{ nullptr };  // Borrowed packet storage.
    usize cbSize{ 0u };      // Bytes currently committed as packet payload.
    usize cbCapacity{ 0u };  // Writable bytes available at pData.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_Init(
    packet_buffer_t *pPacket,
    byte_span_t storage ) noexcept;

CYPHER_COMMON_API void PacketBuffer_Clear( packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_IsValid( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_IsEmpty( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize PacketBuffer_Size( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize PacketBuffer_Capacity( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_SetSize(
    packet_buffer_t *pPacket,
    usize cbSize ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t PacketBuffer_Block( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_writer_t PacketBuffer_ByteWriter( packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_CommitByteWriter(
    packet_buffer_t *pPacket,
    const byte_writer_t &writer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_reader_t PacketBuffer_ByteReader( const packet_buffer_t *pPacket ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bit_writer_t PacketBuffer_BitWriter(
    packet_buffer_t *pPacket,
    bit_order_t bitOrder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t PacketBuffer_CommitBitWriter(
    packet_buffer_t *pPacket,
    const bit_writer_t &writer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bit_reader_t PacketBuffer_BitReader(
    const packet_buffer_t *pPacket,
    bit_order_t bitOrder ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PACKETBUFFER_H
