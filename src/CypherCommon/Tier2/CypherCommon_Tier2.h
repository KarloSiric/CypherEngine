//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Tier2.h
//  Purpose: Provides the aggregate public include for CypherCommon Tier2.
//  Details: Tier2 builds typed data contracts on Tier1 primitives. Consumers may
//           include individual headers when a narrower dependency is preferable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tier2 Contract

Tier2 exposes shared data contracts built from Tier0 and Tier1 utilities. It must remain
independent of renderer, game, editor, and platform UI implementations.
================
*/

#ifndef CYPHER_COMMON_TIER2_H
#define CYPHER_COMMON_TIER2_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_DataValidation.h"
#include "CypherCommon_ProjectManifest.h"
#include "CypherCommon_ProjectSchema.h"
#include "CypherCommon_Schema.h"
#include "CypherCommon_SchemaRegistry.h"
#include "CypherCommon_Settings.h"
#include "CypherCommon_SettingsSchema.h"

#endif // CYPHER_COMMON_TIER2_H
