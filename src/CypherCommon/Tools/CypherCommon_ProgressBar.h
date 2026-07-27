//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tools/CypherCommon_ProgressBar.h
//  Purpose: Declares tool-facing progress state.
//  Details: Progress reporting belongs to command-line tools and editor workflows,
//           not the dependency-light Tier0 runtime contract.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLS_PROGRESSBAR_H
#define CYPHER_COMMON_TOOLS_PROGRESSBAR_H
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

#endif // CYPHER_COMMON_TOOLS_PROGRESSBAR_H
