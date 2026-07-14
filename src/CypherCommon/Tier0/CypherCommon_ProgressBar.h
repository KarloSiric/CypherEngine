//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_ProgressBar.h
//  Purpose: Declares CypherCommon Tier0 ProgressBar support.
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

#ifndef CYPHER_COMMON_TIER0_PROGRESSBAR_H
#define CYPHER_COMMON_TIER0_PROGRESSBAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Progress Bar

Progress reporting declarations for command-line tools.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct progress_bar_t {
    const char *pTitle;
    u64 total_work;
    u64 completed_work;
};

void ProgressBar_Begin( progress_bar_t *pProgress, const char *pTitle, u64 total_work );
void ProgressBar_Update( progress_bar_t *pProgress, u64 completed_work );
void ProgressBar_End( progress_bar_t *pProgress );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROGRESSBAR_H
