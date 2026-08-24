//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BuildId.h
//  Purpose: Declares immutable engine/game build identity records.
//  Details: Pointers refer to static process-lifetime strings generated at build
//           time. Logs and crash reports may retain them without copying.
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
    u32 nMajor; // Breaking product generation.
    u32 nMinor; // Backward-compatible feature generation.
    u32 nPatch; // Corrective release generation.
    u32 nBuild; // Build-system sequence or zero when unavailable.
};

struct build_id_t {
    const char *pszProductName;  // User-facing engine or game name.
    const char *pszInternalName; // Stable executable/module identifier.
    const char *pszVersion;      // Preformatted semantic version string.
    const char *pszBranchName;   // Source-control branch, if embedded.
    const char *pszCommitHash;   // Source revision, if embedded.
    const char *pszBuildDate;    // Compiler-provided build date.
    const char *pszBuildTime;    // Compiler-provided build time.
    build_version_t version;     // Numeric form for machine comparisons.
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
