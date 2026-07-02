//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ChecksumCRC32.h
//  Purpose: Declares CypherCommon Tier1 ChecksumCRC32 support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
#define CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
#pragma once

/*
================
CypherCommon CRC32

CRC32 checksum declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

crc32_t ChecksumCRC32_Data( const void *pData, usize cbData );
crc32_t ChecksumCRC32_Update( crc32_t crc, const void *pData, usize cbData );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHECKSUMCRC32_H
