//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BinaryBlock.h
//  Purpose: Declares CypherCommon Tier1 BinaryBlock support.
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

#ifndef CYPHER_COMMON_TIER1_BINARYBLOCK_H
#define CYPHER_COMMON_TIER1_BINARYBLOCK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Binary Block

Owned or referenced binary blob declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct binary_block_t {
    void *pData;
    usize cbSize;
};

void BinaryBlock_Clear( binary_block_t *pBlock );

bool_t BinaryBlock_IsEmpty( const binary_block_t *pBlock );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BINARYBLOCK_H
