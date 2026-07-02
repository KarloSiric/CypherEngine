//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Range.h
//  Purpose: Declares CypherCommon Tier1 Range support.
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

#ifndef CYPHER_COMMON_TIER1_RANGE_H
#define CYPHER_COMMON_TIER1_RANGE_H
#pragma once

/*
================
CypherCommon Range

Index/count and min/max range declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct index_range_t {
    usize first;
    usize count;
};

template <typename type_t>
struct value_range_t {
    type_t min_value;
    type_t max_value;
};

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RANGE_H
