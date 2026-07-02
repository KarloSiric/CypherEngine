//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SystemInfo.h
//  Purpose: Declares CypherCommon Tier0 SystemInfo support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_SYSTEMINFO_H
#define CYPHER_COMMON_TIER0_SYSTEMINFO_H
#pragma once

/*
================
CypherCommon System Info

Basic target/runtime information available without engine subsystems.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"
#include "CypherCommon_Thread.h"

namespace cypher::common
{

constexpr usize CYPHER_DEFAULT_PAGE_SIZE = 4096u;

struct system_info_t {
    u32 logical_thread_count;
    usize pointer_size;
    usize cache_line_size;
    usize default_page_size;
    bool_t is_little_endian;
    bool_t is_64_bit;
};

// Builds a small snapshot of common target/runtime properties.
inline system_info_t GetSystemInfo()
{
    system_info_t info = {};
    info.logical_thread_count = GetLogicalThreadCount();
    info.pointer_size = CYPHER_POINTER_SIZE;
    info.cache_line_size = CY_CACHE_LINE_SIZE;
    info.default_page_size = CYPHER_DEFAULT_PAGE_SIZE;
    info.is_little_endian = CYPHER_ENDIAN_LITTLE != 0;
    info.is_64_bit = CYPHER_TARGET_64BIT != 0;
    return info;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SYSTEMINFO_H
