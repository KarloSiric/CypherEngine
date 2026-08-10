//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Set.inl
//  Purpose: Implements deterministic ordered unique-key sets.
//  Details: Set stores one zero-sized marker per RBTree node and preserves strict
//           ordering without duplicating balancing or allocator logic.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SET_INL
#define CYPHER_COMMON_TIER1_SET_INL

#ifndef CYPHER_COMMON_TIER1_SET_H
    #include "CypherCommon_Set.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename key_t, typename compare_t>
bool_t Set_Init(
    set_t<key_t, compare_t> *pSet,
    const allocator_t *pAllocator,
    compare_t compare ) noexcept
{
    return RBTree_Init(
        pSet,
        pAllocator,
        static_cast<compare_t &&>( compare ) );
}

template <typename key_t, typename compare_t>
void Set_Shutdown( set_t<key_t, compare_t> *pSet ) noexcept
{
    RBTree_Shutdown( pSet );
}

template <typename key_t, typename compare_t>
void Set_Clear( set_t<key_t, compare_t> *pSet ) noexcept
{
    RBTree_Clear( pSet );
}

template <typename key_t, typename compare_t>
bool_t Set_Insert(
    set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept
{
    return RBTree_Insert( pSet, key, set_unit_t{} ).bInserted;
}

template <typename key_t, typename compare_t>
bool_t Set_Contains(
    const set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept
{
    return RBTree_Find( pSet, key ) != nullptr;
}

template <typename key_t, typename compare_t>
bool_t Set_Erase(
    set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept
{
    return RBTree_Erase( pSet, key );
}

template <typename key_t, typename compare_t>
usize Set_Count( const set_t<key_t, compare_t> *pSet ) noexcept
{
    return RBTree_Count( pSet );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SET_INL
