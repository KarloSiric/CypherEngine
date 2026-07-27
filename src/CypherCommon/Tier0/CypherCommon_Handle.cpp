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

bool_t Cy_Handle32TryMake(
    u32 nIndex,
    u32 nGeneration,
    handle32_t *pOutHandle ) noexcept
{
    if ( pOutHandle == nullptr ) {
        return CY_FALSE;
    }
    *pOutHandle = CY_HANDLE32_INVALID;
    if ( nIndex > CY_HANDLE32_INDEX_MAX ||
         nGeneration > CY_HANDLE32_GENERATION_MAX ) {
        return CY_FALSE;
    }

    pOutHandle->value = ( nGeneration << CY_HANDLE32_INDEX_BITS ) | nIndex;
    return pOutHandle->value != CY_HANDLE32_INVALID.value;
}

bool_t Cy_Handle64TryMake(
    u32 nIndex,
    u32 nGeneration,
    u32 nType,
    handle64_t *pOutHandle ) noexcept
{
    if ( pOutHandle == nullptr ) {
        return CY_FALSE;
    }
    *pOutHandle = CY_HANDLE64_INVALID;
    if ( nGeneration > CY_HANDLE64_GENERATION_MAX ||
         nType > CY_HANDLE64_TYPE_MAX ) {
        return CY_FALSE;
    }

    pOutHandle->value =
        ( static_cast<u64>( nType ) << 48u ) |
        ( static_cast<u64>( nGeneration ) << CY_HANDLE64_INDEX_BITS ) |
        static_cast<u64>( nIndex );
    return pOutHandle->value != CY_HANDLE64_INVALID.value;
}

handle32_t Cy_Handle32Make( u32 nIndex, u32 nGeneration ) noexcept
{
    handle32_t handle{};
    static_cast<void>(
        Cy_Handle32TryMake( nIndex, nGeneration, &handle ) );
    return handle;
}

handle64_t Cy_Handle64Make(
    u32 nIndex,
    u32 nGeneration,
    u32 nType ) noexcept
{
    handle64_t handle{};
    static_cast<void>(
        Cy_Handle64TryMake( nIndex, nGeneration, nType, &handle ) );
    return handle;
}

handle_parts32_t Cy_Handle32Unpack( handle32_t handle ) noexcept
{
    return {
        Cy_Handle32Index( handle ),
        Cy_Handle32Generation( handle )
    };
}

handle_parts64_t Cy_Handle64Unpack( handle64_t handle ) noexcept
{
    return {
        Cy_Handle64Index( handle ),
        Cy_Handle64Generation( handle ),
        Cy_Handle64Type( handle )
    };
}

bool_t Cy_Handle32IsValid( handle32_t handle ) noexcept
{
    return handle.value != CY_HANDLE32_INVALID.value;
}

bool_t Cy_Handle64IsValid( handle64_t handle ) noexcept
{
    return handle.value != CY_HANDLE64_INVALID.value;
}

u32 Cy_Handle32Index( handle32_t handle ) noexcept
{
    return handle.value & CY_HANDLE32_INDEX_MAX;
}

u32 Cy_Handle32Generation( handle32_t handle ) noexcept
{
    return handle.value >> CY_HANDLE32_INDEX_BITS;
}

u32 Cy_Handle64Index( handle64_t handle ) noexcept
{
    return static_cast<u32>( handle.value & 0xFFFFFFFFull );
}

u32 Cy_Handle64Generation( handle64_t handle ) noexcept
{
    return static_cast<u32>(
        ( handle.value >> CY_HANDLE64_INDEX_BITS ) &
        static_cast<u64>( CY_HANDLE64_GENERATION_MAX ) );
}

u32 Cy_Handle64Type( handle64_t handle ) noexcept
{
    return static_cast<u32>(
        ( handle.value >> 48u ) &
        static_cast<u64>( CY_HANDLE64_TYPE_MAX ) );
}

} // namespace cypher::common
