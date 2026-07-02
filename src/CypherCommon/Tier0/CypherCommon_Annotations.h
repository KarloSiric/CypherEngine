//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Annotations.h
//  Purpose: Declares CypherCommon Tier0 Annotations support.
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

#ifndef CYPHER_COMMON_TIER0_ANNOTATIONS_H
#define CYPHER_COMMON_TIER0_ANNOTATIONS_H
#pragma once

/*
================
CypherCommon Annotations

Portable annotation macro surface for future static analysis.
================
*/

#ifndef CY_IN
    #define CY_IN
#endif

#ifndef CY_OUT
    #define CY_OUT
#endif

#ifndef CY_INOUT
    #define CY_INOUT
#endif

#ifndef CY_OPTIONAL
    #define CY_OPTIONAL
#endif

#ifndef CY_CAP
    #define CY_CAP( count )
#endif

#ifndef CY_Z
    #define CY_Z
#endif

#endif // CYPHER_COMMON_TIER0_ANNOTATIONS_H
