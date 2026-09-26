//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Workspace.h
//  Purpose: Declares editor workspaces (`.cyworkspace`, cypher.workspace V1)
//           and the settings scope stack a workspace, its project, and the
//           user's global settings form.
//  Details: A workspace is where one developer works on one machine
//           (ADR 0009): which project, an optional workspace content root
//           that overlays the project's content, personal settings
//           overrides, and remembered state. It is never committed with the
//           project.
//
//             @cykv 1
//             @schema "cypher.workspace" 1
//             {
//                 id = "7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77"
//                 name = "main"
//                 owner = "Karlo"
//                 project = "../reap/reap.cyproject"
//                 content = "wip"
//                 settings = { editor = { autosave_minutes = 5 } }
//                 state = { open_maps = [ "maps/facility.cymap" ] layout = "four_view" }
//             }
//
//           The id, owner, and name let a peer or an MCP agent address the
//           workspace later; `sharing` and `automation` are reserved member
//           names for those features and are preserved untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_WORKSPACE_H
#define CYPHER_EDITOR_CORE_WORKSPACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_UniqueId.h"
#include "CypherCommon/Tier2/CypherCommon_ProjectManifest.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

inline constexpr common::u32 EDITOR_WORKSPACE_SCHEMA_VERSION = 1u;
inline constexpr common::usize EDITOR_WORKSPACE_NAME_MAX_LENGTH = 128u;
inline constexpr common::usize EDITOR_WORKSPACE_PATH_MAX_LENGTH = 1024u;

// Zero-copy: strings and blocks borrow the parsed document.
struct workspace_view_t {
    common::unique_id_t id{};
    common::string_view_t name{};
    common::string_view_t owner{};    // Empty when absent or invalid.
    common::string_view_t project{};  // Native path to the .cyproject, relative to the workspace file.
    common::string_view_t content{};  // Relative workspace content root; empty = edit project content.
    const common::key_value_t *pSettings{ nullptr }; // Personal settings overrides.
    const common::key_value_t *pState{ nullptr };    // Remembered state (open maps, layout, ...).
};

enum class workspace_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_HEADER,   // Not cypher.workspace V1, or the root is not an object.
    INVALID_ID,       // id missing or not a nonzero UUID.
    INVALID_NAME,     // name missing, empty, or too long.
    INVALID_PROJECT,  // project missing or not a usable native path.
    OUT_OF_MEMORY,
    STORE_FAILED
};

// Problems in optional members; the workspace still opens.
enum workspace_problem_flags_t : common::u32 {
    WORKSPACE_PROBLEM_NONE = 0u,
    WORKSPACE_PROBLEM_OWNER = 1u << 0u,    // owner ignored.
    WORKSPACE_PROBLEM_CONTENT = 1u << 1u,  // content ignored: absolute or escapes the workspace.
    WORKSPACE_PROBLEM_SETTINGS = 1u << 2u, // settings is not an object.
    WORKSPACE_PROBLEM_STATE = 1u << 3u     // state is not an object.
};

CYPHER_NODISCARD common::settings_document_identity_t EditorWorkspace_Identity() noexcept;

// pWorkspaceOut is written only on OK. pProblemFlagsOut may be null.
CYPHER_NODISCARD workspace_status_t EditorWorkspace_Decode(
    const common::key_value_document_t *pDocument,
    workspace_view_t *pWorkspaceOut,
    common::u32 *pProblemFlagsOut ) noexcept;

// Fills an initialized, empty store as a new workspace with a fresh random
// id. owner and content may be empty.
CYPHER_NODISCARD workspace_status_t EditorWorkspace_Create(
    common::settings_document_t *pStore,
    common::string_view_t name,
    common::string_view_t owner,
    common::string_view_t projectPath,
    common::string_view_t content ) noexcept;

// A path the workspace may use for its content root: relative, no "..",
// no empty segments, and no drive or root prefix.
CYPHER_NODISCARD common::bool_t EditorWorkspace_IsContentPath( common::string_view_t path ) noexcept;

/*
================
Settings Scopes

The order every editor setting resolves in (ADR 0009): workspace, project,
user, then the built-in default. Use with common::Setting_Resolve.
================
*/
enum editor_scope_t : common::usize {
    EDITOR_SCOPE_WORKSPACE = 0u,
    EDITOR_SCOPE_PROJECT,
    EDITOR_SCOPE_USER,
    EDITOR_SCOPE_COUNT
};

struct editor_scopes_t {
    const common::key_value_t *roots[EDITOR_SCOPE_COUNT]{}; // Null entries are skipped.
};

// Any argument may be null when that scope is not open.
CYPHER_NODISCARD editor_scopes_t EditorScopes_Make(
    const workspace_view_t *pWorkspace,
    const common::project_manifest_view_t *pProject,
    const common::settings_document_t *pUser ) noexcept;

CYPHER_NODISCARD const char *EditorScope_Name( common::usize iScope ) noexcept;

CYPHER_NODISCARD const char *EditorWorkspace_StatusName( workspace_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_WORKSPACE_H
