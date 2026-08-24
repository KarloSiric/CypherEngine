//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderFormat.h
//  Purpose: Declares stable identities for renderer-facing resource formats.
//  Details: Source schemas, cookers, resource loaders, and renderer clients share
//           these values without depending on a graphics backend or native GPU API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Render Format Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_RENDERFORMAT_H
#define CYPHER_COMMON_FORMATS_RENDERFORMAT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Endian.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_RENDER_SHADER_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'S', 'H' ); // Cooked shader program.
inline constexpr fourcc_t CY_RENDER_TEXTURE_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'T', 'X' ); // Cooked texture and mip chain.
inline constexpr fourcc_t CY_RENDER_MATERIAL_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'M', 'T' ); // Cooked material bindings and values.

inline constexpr format_version_t CY_RENDER_SHADER_RESOURCE_VERSION = 2u;
inline constexpr format_version_t CY_RENDER_TEXTURE_RESOURCE_VERSION = 1u;
inline constexpr format_version_t CY_RENDER_MATERIAL_RESOURCE_VERSION = 1u;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_RENDERFORMAT_H
