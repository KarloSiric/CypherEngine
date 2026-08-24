//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderFormats.h
//  Purpose: Provides the public umbrella include for render-asset formats.
//  Details: Consumers that need the complete render-asset contract may include
//           this header. Internal code should continue including the narrowest
//           format header that declares the API it uses.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Render Formats Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_RENDERFORMATS_H
#define CYPHER_COMMON_FORMATS_RENDERFORMATS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedMaterial.h"
#include "CypherCommon_CookedResource.h"
#include "CypherCommon_CookedShader.h"
#include "CypherCommon_CookedTexture.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_RenderAssetSchema.h"
#include "CypherCommon_RenderFormat.h"

#endif // CYPHER_COMMON_FORMATS_RENDERFORMATS_H
