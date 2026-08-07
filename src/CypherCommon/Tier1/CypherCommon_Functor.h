//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Functor.h
//  Purpose: Declares default comparison and hashing function objects.
//  Details: Containers accept these stateless policies explicitly so projects can
//           supply domain-aware ordering, equality, and hashing without virtual calls.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FUNCTOR_H
#define CYPHER_COMMON_TIER1_FUNCTOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct less_t {
    CYPHER_NODISCARD bool_t operator()(
        const type_t &left,
        const type_t &right ) const noexcept;
};

template <typename type_t>
struct equal_t {
    CYPHER_NODISCARD bool_t operator()(
        const type_t &left,
        const type_t &right ) const noexcept;
};

template <typename type_t>
struct hash_functor_t {
    CYPHER_NODISCARD hash64_t operator()(
        const type_t &value ) const noexcept;
};

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FUNCTOR_H
