//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ChecksumCRC32.h
//  Purpose: Declares standard CRC-32 checksum helpers.
//  Details: CRC detects accidental corruption in files and packets; it provides no
//           authentication and must not be treated as a cryptographic integrity check.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
#define CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

constexpr crc32_t CY_CRC32_INITIAL = 0xFFFFFFFFu;
constexpr crc32_t CY_CRC32_POLYNOMIAL_REFLECTED = 0xEDB88320u;
constexpr crc32_t CY_CRC32_FINAL_XOR = 0xFFFFFFFFu;

CYPHER_NODISCARD CYPHER_COMMON_API
crc32_t ChecksumCRC32_Update( crc32_t state, binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
crc32_t ChecksumCRC32_Finalize( crc32_t state ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
crc32_t ChecksumCRC32_Data( binary_block_t data ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
