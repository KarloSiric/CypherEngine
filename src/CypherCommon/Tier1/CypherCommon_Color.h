//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Color.h
//  Purpose: Declares CypherCommon Tier1 Color support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COLOR_H
#define CYPHER_COMMON_TIER1_COLOR_H
#pragma once

/*
================
CypherCommon Color

Color value declarations shared by tools, UI and renderer-facing data.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct color32_t {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

struct colorf_t {
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

color32_t Color32_FromRGBA( u8 r, u8 g, u8 b, u8 a );
colorf_t ColorF_FromRGBA( f32 r, f32 g, f32 b, f32 a );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COLOR_H
