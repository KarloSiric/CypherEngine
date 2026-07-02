//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SoaContainer.h
//  Purpose: Declares CypherCommon Tier1 SoaContainer support.
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

#ifndef CYPHER_COMMON_TIER1_SOACONTAINER_H
#define CYPHER_COMMON_TIER1_SOACONTAINER_H
#pragma once

/*
================
CypherCommon SOA Container

Structure-of-arrays container declarations.
================
*/

namespace cypher::common
{

template <typename layout_t>
struct soa_container_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SOACONTAINER_H
