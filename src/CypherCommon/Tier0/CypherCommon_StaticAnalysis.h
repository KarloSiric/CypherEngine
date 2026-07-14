//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_StaticAnalysis.h
//  Purpose: Declares CypherCommon Tier0 StaticAnalysis support.
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

#ifndef CYPHER_COMMON_TIER0_STATICANALYSIS_H
#define CYPHER_COMMON_TIER0_STATICANALYSIS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Static Analysis

Static-analysis and code-audit macro surface.
================
*/

#include "CypherCommon_Debug.h"
#include "CypherCommon_Defines.h"

#define CY_ANALYSIS_ASSUME( expression )    do { CYPHER_UNUSED( expression ); } while ( 0 )
#define CY_ANALYSIS_SUPPRESS( id )
#define CY_ANALYSIS_UNREACHABLE()          CYPHER_TRAP()

#endif // CYPHER_COMMON_TIER0_STATICANALYSIS_H
