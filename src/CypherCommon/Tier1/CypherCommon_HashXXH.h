//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashXXH.h
//  Purpose: Declares CypherCommon Tier1 HashXXH support.
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

#ifndef CYPHER_COMMON_TIER1_HASHXXH_H
#define CYPHER_COMMON_TIER1_HASHXXH_H
#pragma once

/*
================
CypherCommon XX Hash

Fast non-cryptographic hash declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

hash32_t HashXXH32_Data( const void *pData, usize cbData, hash32_t seed );
hash64_t HashXXH64_Data( const void *pData, usize cbData, hash64_t seed );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHXXH_H
