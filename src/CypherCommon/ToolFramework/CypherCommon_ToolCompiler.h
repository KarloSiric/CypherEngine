//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCompiler.h
//  Purpose: Declares the reusable compiler-module contract for Cypher assets.
//  Details: A compiler processes one source root per request and emits diagnostics,
//           dependencies, artifacts, progress, and reports through the tool host.
//           It has no terminal, Qt, or process-lifecycle dependency.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILER_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolInvocation.h"

namespace cypher::common
{

enum tool_compiler_flags_t : flags32_t {
    TOOL_COMPILER_FLAG_NONE = 0u,
    TOOL_COMPILER_FLAG_DETERMINISTIC = CYPHER_BIT32( 0 ),
    TOOL_COMPILER_FLAG_THREAD_SAFE = CYPHER_BIT32( 1 ),
    TOOL_COMPILER_FLAG_INCREMENTAL = CYPHER_BIT32( 2 ),
    TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE = CYPHER_BIT32( 3 ),
    TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN = CYPHER_BIT32( 4 )
};

struct tool_compile_request_t {
    const tool_invocation_t *pInvocation{ nullptr };
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID };
    string_view_t input{};
    string_view_t output{};
    string_view_t resourceType{};
};

using tool_compiler_probe_fn_t = bool_t ( * )(
    string_view_t input,
    void *pUserData ) noexcept;

using tool_compiler_execute_fn_t = tool_status_t ( * )(
    const tool_compile_request_t &request,
    tool_report_t *pReport,
    void *pUserData ) noexcept;

struct tool_compiler_desc_t {
    string_view_t id{};
    string_view_t displayName{};
    string_view_t resourceType{};
    string_view_t cookedExtension{};
    const string_view_t *pSourceExtensions{ nullptr };
    usize nSourceExtensions{ 0u };
    u32 nApiVersion{ 0u };
    u32 nCompilerVersion{ 0u };
    flags32_t flags{ TOOL_COMPILER_FLAG_NONE };
    tool_compiler_probe_fn_t pfnProbe{ nullptr };
    tool_compiler_execute_fn_t pfnExecute{ nullptr };
    void *pUserData{ nullptr };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCompiler_CheckDescriptor(
    const tool_compiler_desc_t &compiler ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCompiler_SupportsInput(
    const tool_compiler_desc_t &compiler,
    string_view_t input ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCompiler_Execute(
    const tool_compiler_desc_t &compiler,
    const tool_compile_request_t &request,
    tool_report_t *pReport ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILER_H
