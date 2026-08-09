//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandBuffer.h
//  Purpose: Declares an allocator-backed FIFO buffer of command lines.
//  Details: CommandBuffer stores text contiguously and returns borrowed line views.
//           Returned views are invalidated by compaction, clear, shutdown, or growth.
//           Enqueued lines may contain tabs but not null, carriage-return, or newline.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMMANDBUFFER_H
#define CYPHER_COMMON_TIER1_COMMANDBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_TextBuffer.h"

namespace cypher::common
{

struct command_buffer_t {
    text_buffer_t text{};
    usize iReadOffset{ 0u };
    usize nCommandCount{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_Init(
    command_buffer_t *pBuffer,
    const allocator_t *pAllocator,
    usize cchInitialCapacity = 0u ) noexcept;

CYPHER_COMMON_API void CommandBuffer_Shutdown(
    command_buffer_t *pBuffer ) noexcept;

CYPHER_COMMON_API void CommandBuffer_Clear(
    command_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_IsValid( const command_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_IsEmpty( const command_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandBuffer_Count( const command_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandBuffer_PendingBytes( const command_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_Enqueue(
    command_buffer_t *pBuffer,
    string_view_t commandLine ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_Peek(
    const command_buffer_t *pBuffer,
    string_view_t *pCommandOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandBuffer_Pop(
    command_buffer_t *pBuffer,
    string_view_t *pCommandOut ) noexcept;

CYPHER_COMMON_API void CommandBuffer_Compact(
    command_buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDBUFFER_H
