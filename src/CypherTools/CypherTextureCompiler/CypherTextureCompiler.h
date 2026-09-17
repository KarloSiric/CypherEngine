//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.h
//  Purpose: Declares the reusable compiler module for Cypher texture recipes.
//  Details: The module imports PNG, JPEG, TGA, or EXR source images and emits one
//           deterministic `.cytex_c` resource. It has no terminal, Qt, Mason,
//           renderer, or native graphics API dependency.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TEXTURECOMPILER_H
#define CYPHER_TOOLS_TEXTURECOMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCompiler.h"

namespace cypher::tools
{

inline constexpr cypher::common::u32 CY_TEXTURE_COMPILER_API_VERSION = 1u;
inline constexpr cypher::common::u32 CY_TEXTURE_COMPILER_VERSION = 4u;

enum texture_compiler_diagnostic_code_t :
    cypher::common::tool_diagnostic_code_t {
    CY_TEXTURE_DIAGNOSTIC_INVALID_PATH = 0x435A0001u,
    CY_TEXTURE_DIAGNOSTIC_READ_FAILED,
    CY_TEXTURE_DIAGNOSTIC_INVALID_TEXT,
    CY_TEXTURE_DIAGNOSTIC_CYKV_PARSE_FAILED,
    CY_TEXTURE_DIAGNOSTIC_SCHEMA_FAILED,
    CY_TEXTURE_DIAGNOSTIC_IMAGE_READ_FAILED,
    CY_TEXTURE_DIAGNOSTIC_IMAGE_DECODE_FAILED,
    CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_IMAGE,
    CY_TEXTURE_DIAGNOSTIC_INVALID_DIMENSIONS,
    CY_TEXTURE_DIAGNOSTIC_MIP_GENERATION_FAILED,
    CY_TEXTURE_DIAGNOSTIC_COOK_FAILED,
    CY_TEXTURE_DIAGNOSTIC_WRITE_FAILED,
    CY_TEXTURE_DIAGNOSTIC_TOOLCHAIN_FAILED,
    CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_POLICY,
    CY_TEXTURE_DIAGNOSTIC_UNSUPPORTED_PRESERVED_IMAGE,
    CY_TEXTURE_DIAGNOSTIC_INVALID_STREAMING_POLICY,
    CY_TEXTURE_DIAGNOSTIC_CAPACITY_EXCEEDED
};

// Computes the exact decoded byte footprint and level count for a tight 2D mip
// chain. Outputs are transactional. The caller applies the cooked-format byte
// ceiling so this query can also explain an over-capacity source before any
// derived mip storage is allocated.
CYPHER_NODISCARD
cypher::common::bool_t CypherTextureCompiler_CalculateMipDataSize(
    cypher::common::u32 nWidth,
    cypher::common::u32 nHeight,
    cypher::common::u32 cbPixel,
    cypher::common::bool_t bGenerateMips,
    cypher::common::u32 *pMipLevelsOut,
    cypher::common::u64 *pDataSizeOut ) noexcept;

// Returns the process-lifetime descriptor registered by CypherResourceCompiler.
CYPHER_NODISCARD
const cypher::common::tool_compiler_desc_t *
CypherTextureCompiler_Descriptor() noexcept;

} // namespace cypher::tools

#endif // CYPHER_TOOLS_TEXTURECOMPILER_H
