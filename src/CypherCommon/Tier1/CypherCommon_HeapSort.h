//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HeapSort.h
//  Purpose: Declares CypherCommon Tier1 HeapSort support.
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

#ifndef CYPHER_COMMON_TIER1_HEAPSORT_H
#define CYPHER_COMMON_TIER1_HEAPSORT_H
#pragma once

/*
================
CypherCommon Heap Sort

Heap sort declarations.
================
*/

#include "CypherCommon_Sort.h"

namespace cypher::common
{

void HeapSort_Sort( void *pData, usize count, usize cbElement, sort_compare_t compare, void *pUserData );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HEAPSORT_H
