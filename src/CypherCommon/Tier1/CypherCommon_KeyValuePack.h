//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValuePack.h
//  Purpose: Declares versioned binary packing for KeyValue documents.
//  Details: Packed data is little-endian, bounds-checked, and self-identifying. A
//           complete validation pass precedes transactional document construction.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#define CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

constexpr fourcc_t CY_KEY_VALUE_PACK_MAGIC = Cy_MakeFourCC( 'C', 'Y', 'K', 'V' );
constexpr format_version_t CY_KEY_VALUE_PACK_VERSION = 1u;

enum class key_value_pack_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    INVALID_MAGIC,
    VERSION_MISMATCH,
    CORRUPT_DATA,
    LIMIT_EXCEEDED,
    OUT_OF_MEMORY
};

struct key_value_pack_limits_t {
    usize nMaxDepth{ 128u };
    usize nMaxNodes{ 1u << 20u };
    usize cbMaxData{ 256u * CY_MIB };
};

struct key_value_pack_result_t {
    key_value_pack_status_t status{ key_value_pack_status_t::OK };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
usize KeyValuePack_RequiredSize( const key_value_t *pRoot ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_pack_result_t KeyValuePack_Write(
    const key_value_t *pRoot,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_pack_result_t KeyValuePack_Read(
    binary_block_t input,
    const key_value_pack_limits_t &limits,
    key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *KeyValuePack_StatusName(
    key_value_pack_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEPACK_H
