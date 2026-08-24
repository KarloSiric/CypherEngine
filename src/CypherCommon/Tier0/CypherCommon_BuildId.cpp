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

#ifndef CYPHER_ENGINE_VERSION_MAJOR
    #define CYPHER_ENGINE_VERSION_MAJOR 0
#endif
#ifndef CYPHER_ENGINE_VERSION_MINOR
    #define CYPHER_ENGINE_VERSION_MINOR 0
#endif
#ifndef CYPHER_ENGINE_VERSION_PATCH
    #define CYPHER_ENGINE_VERSION_PATCH 0
#endif
#ifndef CYPHER_ENGINE_VERSION_STRING
    #define CYPHER_ENGINE_VERSION_STRING "0.0.0"
#endif
#ifndef CYPHER_BUILD_BRANCH
    #define CYPHER_BUILD_BRANCH "local"
#endif
#ifndef CYPHER_BUILD_COMMIT
    #define CYPHER_BUILD_COMMIT "local"
#endif
#ifndef CYPHER_BUILD_NUMBER
    #define CYPHER_BUILD_NUMBER 0
#endif

namespace cypher::common
{
namespace
{

//-----------------------------------------------------------------------------
// Build identities
//
// These records describe the binaries being compiled, not assets loaded at
// runtime. Keep their formatting stable enough for crash reports and diagnostics.
//-----------------------------------------------------------------------------

const build_id_t g_engineBuildId = {
    "CypherEngine",
    "CypherEngine",
    CYPHER_ENGINE_VERSION_STRING,
    CYPHER_BUILD_BRANCH,
    CYPHER_BUILD_COMMIT,
    __DATE__,
    __TIME__,
    {
        CYPHER_ENGINE_VERSION_MAJOR,
        CYPHER_ENGINE_VERSION_MINOR,
        CYPHER_ENGINE_VERSION_PATCH,
        CYPHER_BUILD_NUMBER
    }
};

const build_id_t g_gameBuildId = {
    "REAP",
    "REAP",
    CYPHER_ENGINE_VERSION_STRING,
    CYPHER_BUILD_BRANCH,
    CYPHER_BUILD_COMMIT,
    __DATE__,
    __TIME__,
    {
        CYPHER_ENGINE_VERSION_MAJOR,
        CYPHER_ENGINE_VERSION_MINOR,
        CYPHER_ENGINE_VERSION_PATCH,
        CYPHER_BUILD_NUMBER
    }
};

} // namespace

const build_id_t *Cy_BuildIdGetEngine() noexcept
{
    return &g_engineBuildId;
}

const build_id_t *Cy_BuildIdGetGame() noexcept
{
    return &g_gameBuildId;
}

bool_t Cy_BuildIdIsValid( const build_id_t *pBuildId ) noexcept
{
    return pBuildId != nullptr &&
           pBuildId->pszProductName != nullptr &&
           pBuildId->pszProductName[0] != '\0' &&
           pBuildId->pszInternalName != nullptr &&
           pBuildId->pszInternalName[0] != '\0' &&
           pBuildId->pszVersion != nullptr &&
           pBuildId->pszVersion[0] != '\0' &&
           pBuildId->pszBranchName != nullptr &&
           pBuildId->pszCommitHash != nullptr;
}

usize Cy_BuildIdFormat(
    const build_id_t *pBuildId,
    char *pszDst,
    usize cchDst ) noexcept
{
    if ( pszDst != nullptr && cchDst > 0u ) {
        pszDst[0] = '\0';
    }
    if ( !Cy_BuildIdIsValid( pBuildId ) ) {
        return 0u;
    }

    const int cchRequired = std::snprintf(
        nullptr,
        0u,
        "%s (%s) version=%s build=%u branch=%s commit=%s built=%s %s",
        pBuildId->pszProductName,
        pBuildId->pszInternalName,
        pBuildId->pszVersion,
        pBuildId->version.nBuild,
        pBuildId->pszBranchName,
        pBuildId->pszCommitHash,
        pBuildId->pszBuildDate != nullptr ? pBuildId->pszBuildDate : "",
        pBuildId->pszBuildTime != nullptr ? pBuildId->pszBuildTime : "" );
    if ( cchRequired < 0 ) {
        return 0u;
    }

    if ( pszDst != nullptr && cchDst > 0u ) {
        std::snprintf(
            pszDst,
            cchDst,
            "%s (%s) version=%s build=%u branch=%s commit=%s built=%s %s",
            pBuildId->pszProductName,
            pBuildId->pszInternalName,
            pBuildId->pszVersion,
            pBuildId->version.nBuild,
            pBuildId->pszBranchName,
            pBuildId->pszCommitHash,
            pBuildId->pszBuildDate != nullptr ? pBuildId->pszBuildDate : "",
            pBuildId->pszBuildTime != nullptr ? pBuildId->pszBuildTime : "" );
        pszDst[cchDst - 1u] = '\0';
    }
    return static_cast<usize>( cchRequired );
}

} // namespace cypher::common
