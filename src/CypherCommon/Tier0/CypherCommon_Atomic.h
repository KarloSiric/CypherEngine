//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Atomic.h
//  Purpose: Declares CypherCommon Tier0 Atomic support.
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

#ifndef CYPHER_COMMON_TIER0_ATOMIC_H
#define CYPHER_COMMON_TIER0_ATOMIC_H
#pragma once

/*
================
CypherCommon Atomic

Atomic primitive declarations and aliases.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <atomic>

namespace cypher::common
{

template <typename type_t>
using atomic_value_t = std::atomic<type_t>;

using atomic_bool_t = std::atomic<bool_t>;
using atomic_i32_t = std::atomic<i32>;
using atomic_u32_t = std::atomic<u32>;
using atomic_i64_t = std::atomic<i64>;
using atomic_u64_t = std::atomic<u64>;

void Atomic_FenceAcquire();
void Atomic_FenceRelease();
void Atomic_FenceSequential();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ATOMIC_H
