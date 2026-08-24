//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ProcessorDetect.h
//  Purpose: Preserves compatibility vocabulary for the Tier0 CPU detector.
//  Details: Processor detection is a Tier0 platform service. This header deliberately
//           defines no second detector, cache, initialization path, or feature source.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Processor Detect Contract

Processor facts are descriptive capabilities, not permission to execute an instruction. Runtime
dispatch must also respect operating-system support and the compiled code path.
================
*/

#ifndef CYPHER_COMMON_TIER1_PROCESSORDETECT_H
#define CYPHER_COMMON_TIER1_PROCESSORDETECT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CPUDetect.h"

namespace cypher::common
{

using processor_info_t = cy_cpu_detect_info_t;
using processor_vendor_t = cy_cpu_vendor_t;
using processor_feature_flags_t = cy_cpu_feature_flags_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PROCESSORDETECT_H
