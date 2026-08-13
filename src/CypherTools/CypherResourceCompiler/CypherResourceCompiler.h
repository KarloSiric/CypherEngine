//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherResourceCompiler/CypherResourceCompiler.h
//  Purpose: Declares the command-line host for registered resource compilers.
//  Details: The application resolves shared CLI policy and dispatches canonical
//           virtual inputs through ToolFramework. Format-specific compilation
//           remains inside independently testable compiler modules.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_RESOURCECOMPILER_H
#define CYPHER_TOOLS_RESOURCECOMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCompilerRegistry.h"
#include "CypherCommon_ToolCliRunner.h"

namespace cypher::tools
{

inline constexpr cypher::common::u32 CY_RESOURCE_COMPILER_API_VERSION = 1u;

// Runs CypherResourceCompiler with the process argument contract from main().
CYPHER_NODISCARD
cypher::common::tool_exit_code_t CypherResourceCompiler_Run(
    cypher::common::i32 argc,
    const char *const *pArgv ) noexcept;

} // namespace cypher::tools

#endif // CYPHER_TOOLS_RESOURCECOMPILER_H
