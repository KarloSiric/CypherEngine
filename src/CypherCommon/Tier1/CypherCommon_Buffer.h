//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Buffer.h
//  Purpose: Declares CypherCommon Tier1 Buffer support.
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

#ifndef CYPHER_COMMON_TIER1_BUFFER_H
#define CYPHER_COMMON_TIER1_BUFFER_H
#pragma once

/*
================
CypherCommon Buffer

Mutable byte buffer declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct buffer_t {
    byte *pData;
    usize cbSize;
    usize cbCapacity;
};

void Buffer_Init( buffer_t *pBuffer, void *pMemory, usize cbCapacity );
bool_t Buffer_Reserve( buffer_t *pBuffer, usize cbCapacity );
bool_t Buffer_Append( buffer_t *pBuffer, const void *pData, usize cbData );
void Buffer_Clear( buffer_t *pBuffer );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BUFFER_H
