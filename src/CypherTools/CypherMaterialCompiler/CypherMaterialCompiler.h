//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.h
//  Purpose: Declares the reusable compiler module for Cypher material recipes.
//  Details: The module validates typed shader and texture resource references
//           through the source VFS and emits deterministic `.cymat_c` resources.
//           It has no terminal, Qt, Mason, renderer, or graphics-API dependency.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_MATERIALCOMPILER_H
#define CYPHER_TOOLS_MATERIALCOMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCompiler.h"

namespace cypher::tools
{

inline constexpr cypher::common::u32 CY_MATERIAL_COMPILER_API_VERSION = 1u;
inline constexpr cypher::common::u32 CY_MATERIAL_COMPILER_VERSION = 1u;

enum material_compiler_diagnostic_code_t :
    cypher::common::tool_diagnostic_code_t {
    CY_MATERIAL_DIAGNOSTIC_INVALID_PATH = 0x434D0001u,
    CY_MATERIAL_DIAGNOSTIC_READ_FAILED,
    CY_MATERIAL_DIAGNOSTIC_INVALID_TEXT,
    CY_MATERIAL_DIAGNOSTIC_CYKV_PARSE_FAILED,
    CY_MATERIAL_DIAGNOSTIC_SCHEMA_FAILED,
    CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_READ_FAILED,
    CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_PARSE_FAILED,
    CY_MATERIAL_DIAGNOSTIC_DEPENDENCY_SCHEMA_FAILED,
    CY_MATERIAL_DIAGNOSTIC_COOK_FAILED,
    CY_MATERIAL_DIAGNOSTIC_WRITE_FAILED,
    CY_MATERIAL_DIAGNOSTIC_TOOLCHAIN_FAILED
};

// Returns the process-lifetime descriptor registered by CypherResourceCompiler.
CYPHER_NODISCARD
const cypher::common::tool_compiler_desc_t *
CypherMaterialCompiler_Descriptor() noexcept;

} // namespace cypher::tools

#endif // CYPHER_TOOLS_MATERIALCOMPILER_H
