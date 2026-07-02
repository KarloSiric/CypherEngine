//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitWriter.h
//  Purpose: Declares CypherCommon Tier1 BitWriter support.
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

#ifndef CYPHER_COMMON_TIER1_BITWRITER_H
#define CYPHER_COMMON_TIER1_BITWRITER_H
#pragma once

/*
================
CypherCommon Bit Writer

Bit-level writer declarations for network and package data.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct bit_writer_t {
    byte *pData;
    usize cbCapacity;
    usize bit_offset;
};

void BitWriter_Init( bit_writer_t *pWriter, void *pData, usize cbCapacity );
bool_t BitWriter_WriteBits( bit_writer_t *pWriter, u64 value, u32 bit_count );
bool_t BitWriter_WriteBool( bit_writer_t *pWriter, bool_t value );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITWRITER_H
