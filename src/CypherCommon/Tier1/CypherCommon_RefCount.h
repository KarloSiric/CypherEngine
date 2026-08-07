//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RefCount.h
//  Purpose: Declares intrusive atomic reference counters.
//  Details: RefCount tracks lifetime only; object destruction remains the owner's
//           explicit responsibility when Release reports the final reference.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_REFCOUNT_H
#define CYPHER_COMMON_TIER1_REFCOUNT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct ref_count_t {
    atomic_u32_t nReferences{ 0u };
};

CYPHER_COMMON_API void RefCount_Init(
    ref_count_t *pRefCount,
    u32 nInitialReferences = 1u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 RefCount_AddRef( ref_count_t *pRefCount ) noexcept;

// Returns the remaining count; zero means the caller owns destruction.
CYPHER_NODISCARD CYPHER_COMMON_API
u32 RefCount_Release( ref_count_t *pRefCount ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 RefCount_Load( const ref_count_t *pRefCount ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_REFCOUNT_H
