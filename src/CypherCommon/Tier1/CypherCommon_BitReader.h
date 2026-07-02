//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitReader.h
//  Purpose: Declares CypherCommon Tier1 BitReader support.
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

#ifndef CYPHER_COMMON_TIER1_BITREADER_H
#define CYPHER_COMMON_TIER1_BITREADER_H
#pragma once

/*
================
CypherCommon Bit Reader

Bit-level reader declarations for network and package data.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct bit_reader_t {
    const byte *pData;
    usize cbSize;
    usize bit_offset;
};

void BitReader_Init( bit_reader_t *pReader, const void *pData, usize cbSize );
bool_t BitReader_ReadBits( bit_reader_t *pReader, u32 bit_count, u64 *pOutValue );
bool_t BitReader_ReadBool( bit_reader_t *pReader, bool_t *pOutValue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITREADER_H
