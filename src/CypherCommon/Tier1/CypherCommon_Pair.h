//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Pair.h
//  Purpose: Declares CypherCommon Tier1 Pair support.
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

#ifndef CYPHER_COMMON_TIER1_PAIR_H
#define CYPHER_COMMON_TIER1_PAIR_H
#pragma once

/*
================
CypherCommon Pair

Small two-value aggregate declarations.
================
*/

namespace cypher::common
{

template <typename first_t, typename second_t>
struct pair_t {
    first_t first;
    second_t second;
};

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PAIR_H
