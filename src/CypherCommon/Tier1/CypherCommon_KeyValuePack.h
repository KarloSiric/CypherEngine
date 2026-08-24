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

/*
================
Key Value Pack Contract

Defines the compact CYKV representation used for storage and transport. Counts, offsets, and
byte order are validated before any packed data is exposed.
================
*/

#ifndef CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#define CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

constexpr fourcc_t CY_KEY_VALUE_PACK_MAGIC = Cy_MakeFourCC( 'C', 'Y', 'K', 'V' ); // File tag.
constexpr format_version_t CY_KEY_VALUE_PACK_VERSION = 1u; // Binary layout revision.

enum class key_value_pack_status_t : u8 {
    OK = 0u,         // Pack operation completed successfully.
    INVALID_ARGUMENT, // Input tree, byte range, or destination is invalid.
    OUTPUT_TOO_SMALL, // Destination cannot hold the complete packed tree.
    INVALID_MAGIC,    // Input is not a CYKV packed document.
    VERSION_MISMATCH, // Binary layout revision is unsupported.
    CORRUPT_DATA,     // Offsets, types, or lengths violate the format.
    LIMIT_EXCEEDED,   // Caller safety budget rejects the document.
    OUT_OF_MEMORY     // Destination document allocation failed.
};

struct key_value_pack_limits_t {
    usize nMaxDepth{ 128u };       // Maximum reconstructed tree depth.
    usize nMaxNodes{ 1u << 20u };  // Maximum reconstructed semantic nodes.
    usize cbMaxData{ 256u * CY_MIB }; // Maximum copied name and payload bytes.
};

struct key_value_pack_result_t {
    key_value_pack_status_t status{ key_value_pack_status_t::OK }; // Final status.
    usize cbRead{ 0u };     // Source bytes consumed by a successful read.
    usize cbWritten{ 0u };  // Destination bytes published by a successful write.
    usize cbRequired{ 0u }; // Complete output size even when storage is too small.
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
