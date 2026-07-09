//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Handle.cpp
//  Purpose: Implements CypherCommon Tier0 handle packing helpers.
//  Details: Handles keep external systems away from private pointers while
//           preserving compact index/generation identity.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Handle.h"

namespace cypher::common
{

handle32_t Cy_Handle32_Make( u32 index, u32 generation )
{
    handle32_t handle{};
    handle.value = ( ( generation & 0xFFFFu ) << 16u ) | ( index & 0xFFFFu );
    return handle;
}

handle64_t Cy_Handle64_Make( u32 index, u32 generation, u32 type )
{
    handle64_t handle{};
    handle.value = ( static_cast<u64>( type & 0xFFFFu ) << 48u ) |
                   ( static_cast<u64>( generation & 0xFFFFu ) << 32u ) |
                   static_cast<u64>( index );
    return handle;
}

handle_parts32_t Cy_Handle32_Unpack( handle32_t handle )
{
    handle_parts32_t parts{};
    parts.index = Cy_Handle32_Index( handle );
    parts.generation = Cy_Handle32_Generation( handle );
    return parts;
}

bool_t Cy_Handle32_IsValid( handle32_t handle )
{
    return handle.value != CY_INVALID_HANDLE;
}

bool_t Cy_Handle64_IsValid( handle64_t handle )
{
    return handle.value != 0u;
}

u32 Cy_Handle32_Index( handle32_t handle )
{
    return handle.value & 0xFFFFu;
}

u32 Cy_Handle32_Generation( handle32_t handle )
{
    return ( handle.value >> 16u ) & 0xFFFFu;
}

} // namespace cypher::common
