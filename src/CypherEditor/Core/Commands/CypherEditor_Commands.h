//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Commands.h
//  Purpose: Declares the editor command registry: every user action is
//           registered once as `module.command`, and menus, toolbars, context
//           menus, the tool sidebar, shortcuts, the console, Python plugins,
//           and MCP automation all reach it through this table.
//  Details: ADR 0008's "commands first" rule. A command is plain data plus a
//           function pointer and an owner context (function_pointer_policy:
//           command callbacks are a stable C-style boundary), so the GUI
//           builds its actions from the registry without the registry knowing
//           about Qt. The optional state callback lets menus grey out or
//           check an entry; execution refuses a disabled command so a
//           shortcut or script cannot bypass what the menu shows.
//
//           Console lines are "id arg arg ..." with double quotes for
//           arguments containing spaces and \" or \\ inside quotes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_COMMANDS_H
#define CYPHER_EDITOR_CORE_COMMANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

namespace cypher::editor
{

inline constexpr common::usize EDITOR_COMMAND_ID_MAX_LENGTH = 96u;
inline constexpr common::usize EDITOR_COMMAND_MAX_ARGS = 16u;
inline constexpr common::usize EDITOR_COMMAND_LINE_MAX_LENGTH = 4096u;

// Command IDs are "module.command": lower-case stable identifiers joined by
// dots, at least two parts. The first part names the owning module.
CYPHER_NODISCARD common::bool_t EditorCommand_IsValidId( common::string_view_t id ) noexcept;

enum command_flags_t : common::u32 {
    COMMAND_FLAG_NONE = 0u,
    COMMAND_FLAG_CHECKABLE = 1u << 0u,     // Menus show a check mark from the state callback.
    COMMAND_FLAG_CONSOLE_ONLY = 1u << 1u,  // Never placed in menus or toolbars.
    COMMAND_FLAG_EDITS_DOCUMENT = 1u << 2u // Changes a document: undoable, gated for automation.
};

enum command_state_flags_t : common::u32 {
    COMMAND_STATE_NONE = 0u,
    COMMAND_STATE_ENABLED = 1u << 0u,
    COMMAND_STATE_CHECKED = 1u << 1u
};

enum class command_result_t : common::u8 {
    OK = 0u,
    FAILED,            // The command ran and reported failure.
    INVALID_ARGUMENTS, // Wrong argument count or values.
    DISABLED,          // The state callback reports the command unavailable.
    UNKNOWN_COMMAND,   // No command with that ID.
    BAD_LINE           // Unterminated quote, too many arguments, or too long.
};

struct command_args_t {
    const common::string_view_t *pArgs{ nullptr }; // Valid only during the call.
    common::usize nArgs{ 0u };
};

using command_execute_fn = command_result_t ( * )( void *pContext, const command_args_t &args );
using command_state_fn = common::u32 ( * )( void *pContext ); // command_state_flags_t bits.

struct command_desc_t {
    const char *pId{ nullptr };          // "file.save"; static lifetime.
    const char *pLabel{ nullptr };       // "Save"
    const char *pDescription{ nullptr }; // Tooltip and console help.
    const char *pIcon{ nullptr };        // Icon resource name; may be null.
    const char *pUsage{ nullptr };       // Console usage, e.g. "map.grid <size>".
    common::u32 flags{ COMMAND_FLAG_NONE };
    command_execute_fn pfnExecute{ nullptr };
    command_state_fn pfnState{ nullptr }; // Null: always enabled, never checked.
    void *pContext{ nullptr };            // Passed back to both callbacks.
};

// Called after every execution attempt that found a command: console echo,
// logging, the recovery journal, and automation audit trails hang here.
using command_observer_fn = void ( * )(
    void *pObserverContext,
    const command_desc_t &command,
    const command_args_t &args,
    command_result_t result );

struct command_registry_t {
    common::vector_t<command_desc_t> commands{}; // Sorted by ID.
    command_observer_fn pfnObserver{ nullptr };
    void *pObserverContext{ nullptr };
};

enum class command_registry_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,   // Bad ID, missing label or execute callback.
    DUPLICATE_COMMAND,  // Nothing from the table was registered.
    OUT_OF_MEMORY
};

CYPHER_NODISCARD command_registry_status_t EditorCommands_Init(
    command_registry_t *pRegistry,
    const common::allocator_t *pAllocator ) noexcept;

void EditorCommands_Shutdown( command_registry_t *pRegistry ) noexcept;

// Registers a table of commands, all or nothing.
CYPHER_NODISCARD command_registry_status_t EditorCommands_Register(
    command_registry_t *pRegistry,
    const command_desc_t *pCommands,
    common::usize nCommands ) noexcept;

// Removes every command of one module (the part before the first dot), as a
// plugin or workspace unloads. Returns the number removed.
common::usize EditorCommands_UnregisterModule(
    command_registry_t *pRegistry,
    common::string_view_t module ) noexcept;

void EditorCommands_SetObserver(
    command_registry_t *pRegistry,
    command_observer_fn pfnObserver,
    void *pObserverContext ) noexcept;

CYPHER_NODISCARD const command_desc_t *EditorCommands_Find(
    const command_registry_t *pRegistry,
    common::string_view_t id ) noexcept;

CYPHER_NODISCARD common::usize EditorCommands_Count( const command_registry_t *pRegistry ) noexcept;

// Commands in ID order, for menus, the command palette, and help.
CYPHER_NODISCARD const command_desc_t *EditorCommands_At(
    const command_registry_t *pRegistry,
    common::usize iCommand ) noexcept;

// COMMAND_STATE_NONE for an unknown command.
CYPHER_NODISCARD common::u32 EditorCommands_State(
    const command_registry_t *pRegistry,
    common::string_view_t id ) noexcept;

CYPHER_NODISCARD command_result_t EditorCommands_Execute(
    const command_registry_t *pRegistry,
    common::string_view_t id,
    const command_args_t &args ) noexcept;

// Tokenizes and executes one console line.
CYPHER_NODISCARD command_result_t EditorCommands_ExecuteLine(
    const command_registry_t *pRegistry,
    common::string_view_t line ) noexcept;

// Writes up to nCapacity commands whose IDs start with prefix, in ID order,
// skipping none: console-only commands complete too. Returns the total.
CYPHER_NODISCARD common::usize EditorCommands_Complete(
    const command_registry_t *pRegistry,
    common::string_view_t prefix,
    const command_desc_t **ppMatches,
    common::usize nCapacity ) noexcept;

CYPHER_NODISCARD const char *EditorCommands_ResultName( command_result_t result ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_COMMANDS_H
