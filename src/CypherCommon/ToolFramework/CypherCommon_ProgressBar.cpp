//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ProgressBar.cpp
//  Purpose: Implements tool-facing progress state helpers.
//  Details: CLI tools, Mason panels, and build systems can render this shared
//           state without making progress UI part of the Tier0 runtime.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Progress Bar Implementation Notes

Progress is hierarchical operation state, not terminal drawing. Producers report completed and
total work; hosts choose animation, throttling, and text presentation.
================
*/

#include "CypherCommon_ProgressBar.h"

namespace cypher::common
{

void ProgressBar_Begin( progress_bar_t *pProgress, const char *pTitle, u64 total_work )
{
    if ( pProgress == nullptr ) {
        return;
    }

    // Keep state display-neutral. A host may render this in a terminal, status
    // bar, build monitor, or test without changing producer behavior.
    pProgress->pTitle = pTitle != nullptr ? pTitle : "";
    pProgress->total_work = total_work;
    pProgress->completed_work = 0u;
}

void ProgressBar_Update( progress_bar_t *pProgress, u64 completed_work )
{
    if ( pProgress == nullptr ) {
        return;
    }

    // A non-zero total is bounded; zero represents an indeterminate operation.
    if ( pProgress->total_work != 0u && completed_work > pProgress->total_work ) {
        pProgress->completed_work = pProgress->total_work;
        return;
    }

    pProgress->completed_work = completed_work;
}

void ProgressBar_End( progress_bar_t *pProgress )
{
    if ( pProgress == nullptr ) {
        return;
    }

    pProgress->completed_work = pProgress->total_work;
}

} // namespace cypher::common
