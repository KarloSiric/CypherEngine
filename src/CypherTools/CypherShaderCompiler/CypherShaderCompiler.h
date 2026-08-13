//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.h
//  Purpose: Declares the reusable compiler module for Cypher shader recipes.
//  Details: The module converts one validated `.cyshader` CYKV document into a
//           deterministic `.cyshader_c` resource. It has no terminal, Qt, Mason,
//           or OpenGL-context dependency and is hosted through ToolFramework.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_SHADERCOMPILER_H
#define CYPHER_TOOLS_SHADERCOMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCompiler.h"

namespace cypher::tools
{

inline constexpr cypher::common::u32 CY_SHADER_COMPILER_API_VERSION = 1u;
inline constexpr cypher::common::u32 CY_SHADER_COMPILER_VERSION = 1u;

enum shader_compiler_diagnostic_code_t :
    cypher::common::tool_diagnostic_code_t {
    CY_SHADER_DIAGNOSTIC_INVALID_PATH = 0x43590001u,
    CY_SHADER_DIAGNOSTIC_READ_FAILED,
    CY_SHADER_DIAGNOSTIC_INVALID_TEXT,
    CY_SHADER_DIAGNOSTIC_CYKV_PARSE_FAILED,
    CY_SHADER_DIAGNOSTIC_SCHEMA_FAILED,
    CY_SHADER_DIAGNOSTIC_UNSUPPORTED_INCLUDE,
    CY_SHADER_DIAGNOSTIC_PREPROCESS_FAILED,
    CY_SHADER_DIAGNOSTIC_PARSE_FAILED,
    CY_SHADER_DIAGNOSTIC_LINK_FAILED,
    CY_SHADER_DIAGNOSTIC_COOK_FAILED,
    CY_SHADER_DIAGNOSTIC_WRITE_FAILED,
    CY_SHADER_DIAGNOSTIC_TOOLCHAIN_FAILED
};

// Returns the process-lifetime descriptor registered by CypherResourceCompiler.
CYPHER_NODISCARD
const cypher::common::tool_compiler_desc_t *
CypherShaderCompiler_Descriptor() noexcept;

} // namespace cypher::tools

#endif // CYPHER_TOOLS_SHADERCOMPILER_H
