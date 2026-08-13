//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolRegistry.h
//  Purpose: Declares a caller-owned registry of tool product descriptors.
//  Details: The registry allocates no memory and lets launchers, Mason, and tests
//           discover named tool capabilities through stable borrowed metadata.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLREGISTRY_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLREGISTRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"

namespace cypher::common
{

struct tool_registry_t {
    const tool_application_desc_t **ppEntries{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolRegistry_Init(
    tool_registry_t *pRegistry,
    const tool_application_desc_t **ppStorage,
    usize nCapacity ) noexcept;

CYPHER_COMMON_API void ToolRegistry_Clear( tool_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolRegistry_Register(
    tool_registry_t *pRegistry,
    const tool_application_desc_t *pApplication ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_application_desc_t *ToolRegistry_Find(
    const tool_registry_t *pRegistry,
    string_view_t id ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_application_desc_t *ToolRegistry_At(
    const tool_registry_t *pRegistry,
    usize iApplication ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLREGISTRY_H
