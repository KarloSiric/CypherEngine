//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsTransfer.h
//  Purpose: Declares portable settings export and reviewed, atomic import.
//  Details: The application owns scope persistence and write permissions.
//           A complete candidate is prepared before that policy is called;
//           live settings change only after persistence succeeds.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_SETTINGS_TRANSFER_H
#define CYPHER_EDITOR_GUI_SETTINGS_TRANSFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_SettingsRegistry.h"

#include <QByteArray>
#include <QList>
#include <QString>

class QDialog;
class QWidget;

namespace cypher::editor::gui
{

enum class settings_transfer_status_t : common::u8 {
    OK = 0u, INVALID_DOCUMENT, NO_SCOPE, OUT_OF_MEMORY, IO_ERROR, READ_ONLY, CHANGED
};

struct settings_transfer_hooks_t {
    void *pContext{ nullptr };
    // Optional policy check, called both before review and before Apply.
    bool ( *pfnCanWrite )( void *, settings_scope_t, QString *pErrorOut ){ nullptr };
    // Required for import. Persist this complete candidate atomically without
    // mutating the attached scope or notifying its listeners. False preserves
    // the live scope; the application must preserve its previous file too.
    bool ( *pfnSave )( void *, settings_scope_t, const common::settings_document_t &, QString *pErrorOut ){ nullptr };
};

struct settings_import_row_t {
    QString path, current, incoming, note;
};

struct settings_import_t {
    settings_import_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( settings_import_t );
    common::settings_document_t candidate{};
    settings_registry_t *pRegistry{ nullptr }; // Borrowed; must outlive review.
    common::settings_document_t *pTarget{ nullptr }; // Borrowed attached scope.
    settings_scope_t scope{ settings_scope_t::USER };
    QByteArray baseline;
    common::settings_document_t *scopeStores[static_cast<common::usize>( settings_scope_t::COUNT )]{};
    QByteArray scopeBaselines[static_cast<common::usize>( settings_scope_t::COUNT )];
    common::usize nDescriptors{ 0u };
    QList<settings_import_row_t> rows;
    common::usize nUnknown{ 0u };
    bool bPrepared{ false };
};

// Export contains every registered effective value and merged unknown scope
// members. Theme/keymap/layout IDs are references, not embedded assets.
CYPHER_NODISCARD settings_transfer_status_t EditorSettingsTransfer_Export(
    const settings_registry_t *pRegistry, const QString &path, QString *pErrorOut = nullptr );

// Preview has no live side effects. Invalid known values reject the import;
// unknown members are retained. Imported values merge into the selected scope
// and are sparse against that scope's inherited values. Unmentioned values stay.
CYPHER_NODISCARD settings_transfer_status_t EditorSettingsTransfer_Prepare(
    settings_registry_t *pRegistry, const QByteArray &text, settings_scope_t scope,
    settings_import_t *pImport, QString *pErrorOut = nullptr );

// Refuses a stale review or a missing persistence hook. Publishes one change
// notification only after the candidate was saved successfully.
CYPHER_NODISCARD settings_transfer_status_t EditorSettingsTransfer_Apply(
    settings_import_t *pImport, const settings_transfer_hooks_t &hooks, QString *pErrorOut = nullptr );

// Owned modal review dialog. Opening, cancelling, or rejecting it never changes
// settings. The caller shows it with exec(); accepted means the import applied.
CYPHER_NODISCARD QDialog *EditorSettingsTransfer_CreateImport(
    QWidget *pParent, settings_registry_t *pRegistry, const QString &path,
    settings_scope_t scope = settings_scope_t::USER, const settings_transfer_hooks_t &hooks = {} );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_SETTINGS_TRANSFER_H
