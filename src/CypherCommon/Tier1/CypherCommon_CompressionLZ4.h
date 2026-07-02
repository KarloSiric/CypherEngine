//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionLZ4.h
//  Purpose: Declares CypherCommon Tier1 CompressionLZ4 support.
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

#ifndef CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
#define CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
#pragma once

/*
================
CypherCommon Compression LZ4

LZ4 compression integration declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

usize CompressionLZ4_CompressBound( usize cbInput );
bool_t CompressionLZ4_Compress( const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );
bool_t CompressionLZ4_Decompress( const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
