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
#pragma once

/*
================
CypherCommon Build ID

Build identity declarations used by logs, crash reports, tools, diagnostics
and editor about dialogs.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct build_id_t {
    const char *pProductName;
    const char *pInternalName;
    const char *pBranchName;
    const char *pCommitHash;
    const char *pBuildDate;
    const char *pBuildTime;
    version_t version;
};

const build_id_t *Cy_BuildId_GetEngine();
const build_id_t *Cy_BuildId_GetGame();
void Cy_BuildId_Format( const build_id_t &buildId, char *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BUILDID_H
