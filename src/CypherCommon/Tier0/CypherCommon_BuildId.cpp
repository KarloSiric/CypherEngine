//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BuildId.cpp
//  Purpose: Implements CypherCommon Tier0 build identity helpers.
//  Details: Build ids feed logs, crash reports, diagnostics, tools, and editor
//           about dialogs with consistent product/version metadata.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BuildId.h"

#include <cstdio>

namespace cypher::common
{
namespace
{

const build_id_t g_engineBuildId = {
    "CypherEngine",
    "CypherEngine",
    "main",
    "local",
    __DATE__,
    __TIME__,
    1u
};

const build_id_t g_gameBuildId = {
    "REAP",
    "REAP",
    "main",
    "local",
    __DATE__,
    __TIME__,
    1u
};

} // namespace

const build_id_t *Cy_BuildId_GetEngine()
{
    return &g_engineBuildId;
}

const build_id_t *Cy_BuildId_GetGame()
{
    return &g_gameBuildId;
}

void Cy_BuildId_Format( const build_id_t &buildId, char *pDest, usize cchDest )
{
    if ( pDest == nullptr || cchDest == 0u ) {
        return;
    }

    std::snprintf( pDest,
                   cchDest,
                   "%s (%s) branch=%s commit=%s version=%u built=%s %s",
                   buildId.pProductName != nullptr ? buildId.pProductName : "",
                   buildId.pInternalName != nullptr ? buildId.pInternalName : "",
                   buildId.pBranchName != nullptr ? buildId.pBranchName : "",
                   buildId.pCommitHash != nullptr ? buildId.pCommitHash : "",
                   buildId.version,
                   buildId.pBuildDate != nullptr ? buildId.pBuildDate : "",
                   buildId.pBuildTime != nullptr ? buildId.pBuildTime : "" );
    pDest[cchDest - 1u] = '\0';
}

} // namespace cypher::common
