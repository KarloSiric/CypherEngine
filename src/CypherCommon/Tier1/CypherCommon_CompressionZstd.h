//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionZstd.h
//  Purpose: Declares CypherCommon Tier1 CompressionZstd support.
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

#ifndef CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
#define CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
#pragma once

/*
================
CypherCommon Compression Zstd

Zstandard compression integration declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

usize CompressionZstd_CompressBound( usize cbInput );
bool_t CompressionZstd_Compress( const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );
bool_t CompressionZstd_Decompress( const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
