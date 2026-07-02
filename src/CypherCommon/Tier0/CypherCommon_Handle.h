//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Handle.h
//  Purpose: Declares CypherCommon Tier0 Handle support.
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

#ifndef CYPHER_COMMON_TIER0_HANDLE_H
#define CYPHER_COMMON_TIER0_HANDLE_H
#pragma once

/*
================
CypherCommon Handle

Typed handle declarations for resources, files, entities and editor objects.
Handles keep external code away from private subsystem pointers.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct handle32_t {
    u32 value;
};

struct handle64_t {
    u64 value;
};

struct handle_parts32_t {
    u32 index;
    u32 generation;
};

handle32_t Cy_Handle32_Make( u32 index, u32 generation );
handle64_t Cy_Handle64_Make( u32 index, u32 generation, u32 type );
handle_parts32_t Cy_Handle32_Unpack( handle32_t handle );
bool_t Cy_Handle32_IsValid( handle32_t handle );
bool_t Cy_Handle64_IsValid( handle64_t handle );
u32 Cy_Handle32_Index( handle32_t handle );
u32 Cy_Handle32_Generation( handle32_t handle );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_HANDLE_H
