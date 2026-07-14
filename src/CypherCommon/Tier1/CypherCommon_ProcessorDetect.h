//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ProcessorDetect.h
//  Purpose: Declares CypherCommon Tier1 ProcessorDetect support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PROCESSORDETECT_H
#define CYPHER_COMMON_TIER1_PROCESSORDETECT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Processor Detect

Higher-level processor detection declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct processor_info_t {
    char name[128];
    u32 logical_thread_count;
    u32 physical_core_count;
};

bool_t ProcessorDetect_GetInfo( processor_info_t *pOutInfo );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PROCESSORDETECT_H
