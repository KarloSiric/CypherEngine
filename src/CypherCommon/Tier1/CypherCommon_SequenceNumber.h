//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SequenceNumber.h
//  Purpose: Declares CypherCommon Tier1 SequenceNumber support.
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

#ifndef CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
#define CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
#pragma once

/*
================
CypherCommon Sequence Number

Wrapping sequence number declarations for networking.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

bool_t SequenceNumber_IsNewer16( u16 a, u16 b );
bool_t SequenceNumber_IsNewer32( u32 a, u32 b );
i32 SequenceNumber_Diff16( u16 a, u16 b );
i32 SequenceNumber_Diff32( u32 a, u32 b );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
