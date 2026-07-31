//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BuildId.h
//  Purpose: Declares CypherCommon Tier0 BuildId support.
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

#ifndef CYPHER_COMMON_TIER0_BUILDID_H
#define CYPHER_COMMON_TIER0_BUILDID_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Build ID

Build identity declarations used by logs, crash reports, tools, diagnostics
and editor about dialogs.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct build_version_t {
    u32 nMajor;
    u32 nMinor;
    u32 nPatch;
    u32 nBuild;
};

struct build_id_t {
    const char *pszProductName;
    const char *pszInternalName;
    const char *pszVersion;
    const char *pszBranchName;
    const char *pszCommitHash;
    const char *pszBuildDate;
    const char *pszBuildTime;
    build_version_t version;
};

CYPHER_NODISCARD CYPHER_COMMON_API const build_id_t *Cy_BuildIdGetEngine() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API const build_id_t *Cy_BuildIdGetGame() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_BuildIdIsValid(
    const build_id_t *pBuildId ) noexcept;

// Formats identity text and returns the required character count excluding null.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_BuildIdFormat(
    const build_id_t *pBuildId,
    char *pszDst,
    usize cchDst ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BUILDID_H
