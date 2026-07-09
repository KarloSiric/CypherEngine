//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_ProgressBar.cpp
//  Purpose: Implements CypherCommon Tier0 progress state helpers.
//  Details: Tier0 tracks progress state only. CLI tools, Mason panels, and build
//           systems can choose how to render that state.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ProgressBar.h"

namespace cypher::common
{

void ProgressBar_Begin( progress_bar_t *pProgress, const char *pTitle, u64 total_work )
{
    if ( pProgress == nullptr ) {
        return;
    }

    pProgress->pTitle = pTitle != nullptr ? pTitle : "";
    pProgress->total_work = total_work;
    pProgress->completed_work = 0u;
}

void ProgressBar_Update( progress_bar_t *pProgress, u64 completed_work )
{
    if ( pProgress == nullptr ) {
        return;
    }

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
