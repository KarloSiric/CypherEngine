//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolArtifactWriter.h
//  Purpose: Declares transactional native-file publication for tool artifacts.
//  Details: Compiler modules hand completed bytes to this shared boundary. It
//           creates parent directories and publishes through a same-directory
//           temporary file so failed writes do not corrupt prior artifacts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACTWRITER_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACTWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"
#include "CypherCommon_ToolStatus.h"

namespace cypher::common
{

// Writes bytes beside nativePath, flushes them, and atomically replaces the
// destination. Parent directories are created as needed.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolArtifactWriter_WriteNative(
    string_view_t nativePath,
    binary_block_t contents ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACTWRITER_H
