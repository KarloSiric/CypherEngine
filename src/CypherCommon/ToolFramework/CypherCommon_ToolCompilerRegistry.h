//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCompilerRegistry.h
//  Purpose: Declares deterministic discovery of reusable compiler modules.
//  Details: The caller owns registry storage and compiler descriptors. Extension
//           overlap is permitted, but input lookup reports ambiguity explicitly.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILERREGISTRY_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILERREGISTRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCompiler.h"

namespace cypher::common
{

struct tool_compiler_registry_t {
    const tool_compiler_desc_t **ppCompilers{ nullptr }; // Caller-owned pointer storage.
    usize nCount{ 0u };      // Registered descriptors in stable order.
    usize nCapacity{ 0u };   // Maximum pointers available in ppCompilers.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCompilerRegistry_Init(
    tool_compiler_registry_t *pRegistry,
    const tool_compiler_desc_t **ppStorage,
    usize nCapacity ) noexcept;

CYPHER_COMMON_API void ToolCompilerRegistry_Clear(
    tool_compiler_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCompilerRegistry_Register(
    tool_compiler_registry_t *pRegistry,
    const tool_compiler_desc_t *pCompiler ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_compiler_desc_t *ToolCompilerRegistry_FindById(
    const tool_compiler_registry_t *pRegistry,
    string_view_t id ) noexcept;

// Returns INVALID_CONFIGURATION when more than one compiler claims the input.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCompilerRegistry_FindForInput(
    const tool_compiler_registry_t *pRegistry,
    string_view_t input,
    const tool_compiler_desc_t **ppCompilerOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_compiler_desc_t *ToolCompilerRegistry_At(
    const tool_compiler_registry_t *pRegistry,
    usize iCompiler ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMPILERREGISTRY_H
