//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Diff.h
//  Purpose: Declares CypherCommon Tier1 Diff support.
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

#ifndef CYPHER_COMMON_TIER1_DIFF_H
#define CYPHER_COMMON_TIER1_DIFF_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Diff

Binary/text diff declaration surface.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct diff_result_t;

bool_t Diff_Binary( const void *pOldData, usize cbOldData, const void *pNewData, usize cbNewData, diff_result_t *pOutDiff );
bool_t Diff_Apply( const void *pOldData, usize cbOldData, const diff_result_t *pDiff, void *pDest, usize cbDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DIFF_H
