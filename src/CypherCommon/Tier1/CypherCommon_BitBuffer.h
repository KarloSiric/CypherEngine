//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitBuffer.h
//  Purpose: Declares CypherCommon Tier1 BitBuffer support.
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

#ifndef CYPHER_COMMON_TIER1_BITBUFFER_H
#define CYPHER_COMMON_TIER1_BITBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Bit Buffer

Combined bit reader/writer declarations.
================
*/

#include "CypherCommon_BitReader.h"
#include "CypherCommon_BitWriter.h"

namespace cypher::common
{

struct bit_buffer_t {
    bit_reader_t reader;
    bit_writer_t writer;
};

void BitBuffer_Init( bit_buffer_t *pBuffer, void *pData, usize cbCapacity );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITBUFFER_H
