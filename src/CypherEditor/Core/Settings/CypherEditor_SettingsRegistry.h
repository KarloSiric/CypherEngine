//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsRegistry.h
//  Purpose: Declares the editor's settings registry and scope stack: every
//           setting is registered by the module that owns it, and every
//           value resolves through workspace, project, and user scopes down
//           to its registered default (CYSETTINGS.md).
//  Details: ADR 0008: "Settings are registered by their owners." A module
//           hands over a static table of descriptors (path, type, default,
//           limits, label, description, page), so the settings dialog is
//           generated from the registry and lists every setting a running
//           editor has, including those of plugins. Nothing keeps a
//           central preferences struct.
//
//           Scopes are borrowed settings stores ordered most specific
//           first; writing a value goes into one chosen scope and stays
//           sparse (a value equal to what wider scopes give is removed from
//           that scope), so improved defaults still reach users.
//
//           Listeners hear about every change made through the registry, so
//           a grid-size edit in the dialog reaches the views at once.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_SETTINGS_REGISTRY_H
#define CYPHER_EDITOR_CORE_SETTINGS_REGISTRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

inline constexpr common::usize EDITOR_SETTINGS_MAX_SCOPES = 4u;
// Mason's cached projections, Tool Properties and persistent Settings page
// can own 41 subscriptions together. Keep registration bounded with headroom.
inline constexpr common::usize EDITOR_SETTINGS_MAX_LISTENERS = 64u;

// Scopes, most specific first (CYSETTINGS.md 1).
enum class settings_scope_t : common::u8 {
    WORKSPACE = 0u,
    PROJECT,
    USER,
    COUNT,
    DEFAULT = COUNT // The registered default; not a store.
};

enum class settings_registry_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT, // Bad descriptor, value, or scope.
    DUPLICATE,        // A path is already registered; nothing was added.
    OUT_OF_MEMORY,
    NO_SCOPE,         // The target scope has no store attached.
    STORE_FAILED
};

// path is the changed setting's path; empty when many changed at once (a
// scope was attached or replaced).
using settings_listener_fn = void ( * )( void *pContext, common::string_view_t path ) noexcept;

struct settings_listener_t {
    settings_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

struct settings_registry_t {
    settings_registry_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( settings_registry_t );
    ~settings_registry_t() noexcept;

    common::vector_t<const common::setting_descriptor_t *> descriptors{}; // Registration order: the dialog's order.
    common::settings_document_t *scopes[static_cast<common::usize>( settings_scope_t::COUNT )]{}; // Borrowed.
    settings_listener_t listeners[EDITOR_SETTINGS_MAX_LISTENERS]{};
    common::usize nListeners{ 0u };
};

CYPHER_NODISCARD settings_registry_status_t EditorSettings_Init( settings_registry_t *pRegistry, const common::allocator_t *pAllocator ) noexcept;
void EditorSettings_Shutdown( settings_registry_t *pRegistry ) noexcept;

// Registers a static table, all or nothing. Every descriptor needs a path,
// a label, a page, and a default that satisfies its own limits.
CYPHER_NODISCARD settings_registry_status_t EditorSettings_Register(
    settings_registry_t *pRegistry,
    const common::setting_descriptor_t *pDescriptors,
    common::usize nDescriptors ) noexcept;

CYPHER_NODISCARD const common::setting_descriptor_t *EditorSettings_Find( const settings_registry_t *pRegistry, common::string_view_t path ) noexcept;
CYPHER_NODISCARD common::usize EditorSettings_Count( const settings_registry_t *pRegistry ) noexcept;
CYPHER_NODISCARD const common::setting_descriptor_t *EditorSettings_At( const settings_registry_t *pRegistry, common::usize iIndex ) noexcept;

// Attaches (or with null detaches) the store holding one scope. Listeners
// are told with an empty path.
void EditorSettings_SetScope( settings_registry_t *pRegistry, settings_scope_t scope, common::settings_document_t *pStore ) noexcept;

struct settings_value_source_t {
    common::setting_value_t value{};
    settings_scope_t source{ settings_scope_t::DEFAULT }; // Where it came from.
    common::usize nInvalid{ 0u };                          // Unusable values passed over in narrower scopes.
};

// The effective value of a registered setting.
CYPHER_NODISCARD settings_value_source_t EditorSettings_Resolve( const settings_registry_t *pRegistry, const common::setting_descriptor_t &descriptor ) noexcept;

// What a scope would inherit if it held nothing: the resolution through
// only the scopes wider than it.
CYPHER_NODISCARD settings_value_source_t EditorSettings_ResolveInherited(
    const settings_registry_t *pRegistry,
    const common::setting_descriptor_t &descriptor,
    settings_scope_t scope ) noexcept;

// Typed shortcuts by path for code that reads a setting; fallback when the
// path is not registered.
CYPHER_NODISCARD common::bool_t EditorSettings_Bool( const settings_registry_t *pRegistry, const char *pPath, common::bool_t fallback ) noexcept;
CYPHER_NODISCARD common::i64 EditorSettings_Integer( const settings_registry_t *pRegistry, const char *pPath, common::i64 fallback ) noexcept;
CYPHER_NODISCARD common::f64 EditorSettings_Real( const settings_registry_t *pRegistry, const char *pPath, common::f64 fallback ) noexcept;
CYPHER_NODISCARD common::string_view_t EditorSettings_Text( const settings_registry_t *pRegistry, const char *pPath, common::string_view_t fallback ) noexcept;

// Writes a value into one scope, sparsely, and tells listeners.
CYPHER_NODISCARD settings_registry_status_t EditorSettings_Write(
    settings_registry_t *pRegistry,
    settings_scope_t scope,
    const common::setting_descriptor_t &descriptor,
    const common::setting_value_t &value ) noexcept;

// Removes a scope's own value so it inherits again, and tells listeners.
CYPHER_NODISCARD settings_registry_status_t EditorSettings_Reset(
    settings_registry_t *pRegistry,
    settings_scope_t scope,
    const common::setting_descriptor_t &descriptor ) noexcept;

CYPHER_NODISCARD common::bool_t EditorSettings_AddListener( settings_registry_t *pRegistry, settings_listener_fn pfnChanged, void *pContext ) noexcept;
void EditorSettings_RemoveListener( settings_registry_t *pRegistry, settings_listener_fn pfnChanged, void *pContext ) noexcept;

CYPHER_NODISCARD const char *EditorSettings_ScopeName( settings_scope_t scope ) noexcept;

// The identity of settings files (`cypher.settings`, V1 read, V2 written).
CYPHER_NODISCARD common::settings_document_identity_t EditorSettings_FileIdentity() noexcept;

// The framework's own settings (CYSETTINGS.md 4.1-4.5, 4.7-4.10); workspaces
// register theirs, for example the map workspace's editor.map. Static
// lifetime.
CYPHER_NODISCARD const common::setting_descriptor_t *EditorSettings_FrameworkCatalogue( common::usize *pnDescriptorsOut ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_SETTINGS_REGISTRY_H
