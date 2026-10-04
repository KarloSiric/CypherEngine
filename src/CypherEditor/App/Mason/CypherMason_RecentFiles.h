//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_RecentFiles.h
//  Purpose: Declares Mason's recent maps list: newest first, one absolute
//           path per line in a small text file beside the user settings.
//  Details: Kept out of the settings document on purpose: it changes on
//           every open and save, belongs to this machine, and is not a
//           preference anyone edits. Paths that no longer exist stay listed
//           (the drive may be unplugged) and are marked missing where shown.
//           The length follows editor.general.recent_files.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_MASON_RECENT_FILES_H
#define CYPHER_MASON_RECENT_FILES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QString>
#include <QStringList>

namespace cypher::mason
{

struct mason_recent_t {
    QString path{};        // The list file; empty keeps the list in memory only.
    QStringList files{};   // Newest first, absolute, unique.
    int nMax{ 16 };
};

// Reads the list (a missing file is an empty list).
void MasonRecent_Load( mason_recent_t *pRecent, const QString &path, int nMax );
// Moves the file to the front (adding it), trims to the maximum, saves.
// False only when the list file could not be written.
bool MasonRecent_Add( mason_recent_t *pRecent, const QString &file );
bool MasonRecent_Remove( mason_recent_t *pRecent, const QString &file );
bool MasonRecent_Clear( mason_recent_t *pRecent );
void MasonRecent_SetMax( mason_recent_t *pRecent, int nMax );

} // namespace cypher::mason

#endif // CYPHER_MASON_RECENT_FILES_H
