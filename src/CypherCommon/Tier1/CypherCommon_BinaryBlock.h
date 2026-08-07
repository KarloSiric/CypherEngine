//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BinaryBlock.h
//  Purpose: Declares immutable borrowed binary blocks.
//  Details: BinaryBlock is a semantic read-only byte view used by resources, hashes,
//           packets, and serialized records. It owns no storage.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BINARYBLOCK_H
#define CYPHER_COMMON_TIER1_BINARYBLOCK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"

namespace cypher::common
{

struct binary_block_t {
    const byte *pData{ nullptr };
    usize cbSize{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t BinaryBlock_FromData(
    const void *pData,
    usize cbSize ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t BinaryBlock_FromSpan( const_byte_span_t bytes ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BinaryBlock_IsValid( binary_block_t block ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BinaryBlock_IsEmpty( binary_block_t block ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t BinaryBlock_Subblock(
    binary_block_t block,
    usize iOffset,
    usize cbSize ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const_byte_span_t BinaryBlock_Span( binary_block_t block ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BINARYBLOCK_H
