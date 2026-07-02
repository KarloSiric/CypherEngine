//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStream.h
//  Purpose: Declares CypherCommon Tier1 MemoryStream support.
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

#ifndef CYPHER_COMMON_TIER1_MEMORYSTREAM_H
#define CYPHER_COMMON_TIER1_MEMORYSTREAM_H
#pragma once

/*
================
CypherCommon Memory Stream

Stream backed by memory declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct memory_stream_t {
    byte *pData;
    usize cbSize;
    usize cbCapacity;
    usize offset;
};

void MemoryStream_Init( memory_stream_t *pStream, void *pData, usize cbCapacity );
usize MemoryStream_Read( memory_stream_t *pStream, void *pDest, usize cbRead );
usize MemoryStream_Write( memory_stream_t *pStream, const void *pSrc, usize cbWrite );
bool_t MemoryStream_Seek( memory_stream_t *pStream, usize offset );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYSTREAM_H
