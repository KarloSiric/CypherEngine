//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUDetect.h
//  Purpose: Declares CypherCommon Tier0 CPUDetect support.
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

#ifndef CYPHER_COMMON_TIER0_CPUDETECT_H
#define CYPHER_COMMON_TIER0_CPUDETECT_H
#pragma once

/*
================
CypherCommon CPU Detect

CPU feature detection declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct cpu_features_t {
    bool_t has_sse2;
    bool_t has_sse3;
    bool_t has_sse41;
    bool_t has_sse42;
    bool_t has_avx;
    bool_t has_avx2;
    bool_t has_neon;
};

cpu_features_t CPUDetect_GetFeatures();
const char *CPUDetect_GetBrandString();

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CPUDETECT_H
