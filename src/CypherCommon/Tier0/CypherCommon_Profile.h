//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Profile.h
//  Purpose: Declares CypherCommon Tier0 Profile support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_PROFILE_H
#define CYPHER_COMMON_TIER0_PROFILE_H
#pragma once

/*
================
CypherCommon Profile

Profiling zone and counter declarations used to measure engine runtime,
renderer, VFS, tools and editor performance.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

using profile_token_t = u64;

enum profile_flags_t : flags32_t {
    PROFILE_FLAG_NONE = 0u,
    PROFILE_FLAG_CPU = CYPHER_BIT32( 0 ),
    PROFILE_FLAG_GPU = CYPHER_BIT32( 1 ),
    PROFILE_FLAG_TOOL = CYPHER_BIT32( 2 )
};

struct profile_zone_desc_t {
    const char *pName;
    const char *pCategory;
    source_location_t location;
    flags32_t flags;
};

profile_token_t Cy_ProfileBeginZone( const profile_zone_desc_t &desc );
void Cy_ProfileEndZone( profile_token_t token );
void Cy_ProfileCounterAdd( const char *pName, i64 value );
void Cy_ProfileCounterSet( const char *pName, i64 value );
void Cy_ProfileFrameBegin();
void Cy_ProfileFrameEnd();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROFILE_H
