//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsFile.h
//  Purpose: Declares the on-disk policy for settings-family files: backup
//           recovery on load, atomic saves with one backup, and never
//           overwriting a file that could not be read.
//  Details: ADR 0009's guarantees in file terms:
//             load    main file; if it cannot be read, the .bak; if that
//                     also fails, the scope runs on inherited values and
//                     saving is blocked until the user decides.
//             save    write <path>.tmp, copy the current good file to
//                     <path>.bak, then atomically replace <path>.
//             damage  a file that failed to load is copied to <path>.broken
//                     before anything is written over it, so the user's
//                     hand edits can always be recovered.
//           Everything here is Qt-free so the same policy serves the editor,
//           tools, and the runtime's own settings.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_SETTINGS_FILE_H
#define CYPHER_EDITOR_CORE_SETTINGS_FILE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

enum class settings_file_status_t : common::u8 {
    OK = 0u,            // Save completed, or the file loaded normally.
    MISSING,            // No file yet; the store is empty and saving creates it.
    RESTORED_FROM_BACKUP, // Main file was unreadable; the .bak loaded; main kept as .broken.
    UNREADABLE,         // Main file and backup unusable; saving is blocked.
    SAVE_BLOCKED,       // Save refused because an unreadable file is unresolved.
    IO_ERROR,           // A read, write, or replace failed; nothing was lost.
    OUT_OF_MEMORY,
    INVALID_ARGUMENT
};

struct settings_file_t {
    common::settings_document_t store{};  // The live values of this scope.
    common::text_buffer_t path{};         // Native path of the main file.
    common::bool_t bSaveBlocked{ common::CY_FALSE };
    // Why the main file failed to load, when it did.
    common::settings_document_load_result_t damage{};
};

CYPHER_NODISCARD settings_file_status_t EditorSettingsFile_Init(
    settings_file_t *pFile,
    const common::allocator_t *pAllocator,
    const common::settings_document_identity_t &identity,
    common::string_view_t nativePath ) noexcept;

void EditorSettingsFile_Shutdown( settings_file_t *pFile ) noexcept;

// Loads the scope following the policy above. On UNREADABLE the store holds
// an empty document, so every value resolves from wider scopes.
CYPHER_NODISCARD settings_file_status_t EditorSettingsFile_Load( settings_file_t *pFile ) noexcept;

// Saves the store. SAVE_BLOCKED while an unreadable file is unresolved.
CYPHER_NODISCARD settings_file_status_t EditorSettingsFile_Save( settings_file_t *pFile ) noexcept;

// The user chose to replace an unreadable file with the current values: the
// damaged file is kept as .broken and saving is unblocked.
CYPHER_NODISCARD settings_file_status_t EditorSettingsFile_AcceptOverwrite( settings_file_t *pFile ) noexcept;

CYPHER_NODISCARD const char *EditorSettingsFile_StatusName( settings_file_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_SETTINGS_FILE_H
