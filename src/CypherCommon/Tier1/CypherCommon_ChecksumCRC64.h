//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ChecksumCRC64.h
//  Purpose: Declares CRC-64/ECMA-182 checksum helpers.
//  Details: The polynomial and initial/final convention are fixed by this contract so
//           package and cache checksums remain portable across platforms and builds.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Checksum CRC64 Contract

This algorithm is deterministic over an explicit byte range. It is suitable for lookup, change
detection, or corruption checks, but must not be used as a cryptographic authenticator.
================
*/

#ifndef CYPHER_COMMON_TIER1_CHECKSUMCRC64_H
#define CYPHER_COMMON_TIER1_CHECKSUMCRC64_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

using crc64_t = u64;
constexpr crc64_t CY_CRC64_INITIAL = 0x0000000000000000ull;
constexpr crc64_t CY_CRC64_POLYNOMIAL = 0x42F0E1EBA9EA3693ull;
constexpr crc64_t CY_CRC64_FINAL_XOR = 0x0000000000000000ull;

CYPHER_NODISCARD CYPHER_COMMON_API
crc64_t ChecksumCRC64_Update( crc64_t state, binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
crc64_t ChecksumCRC64_Finalize( crc64_t state ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
crc64_t ChecksumCRC64_Data( binary_block_t data ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHECKSUMCRC64_H
