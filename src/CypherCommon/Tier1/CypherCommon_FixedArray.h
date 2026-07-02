//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedArray.h
//  Purpose: Declares CypherCommon Tier1 FixedArray support.
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

#ifndef CYPHER_COMMON_TIER1_FIXEDARRAY_H
#define CYPHER_COMMON_TIER1_FIXEDARRAY_H
#pragma once

/*
================
CypherCommon Fixed Array

Fixed-capacity array declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t, usize capacity>
struct fixed_array_t;

template <typename type_t, usize capacity>
bool_t FixedArray_Push( fixed_array_t<type_t, capacity> *pArray, const type_t &value );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDARRAY_H
