//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Autosave.cpp
//  Purpose: Implements map autosave copies and their rotation.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Autosave.h"

#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <vector>

namespace cypher::editor::map
{
using namespace common;

namespace
{
QString BaseName( const map_workspace_t *ws )
{
    const QString name = ws->path.isEmpty() ? QString() : QFileInfo( ws->path ).completeBaseName();
    return name.isEmpty() ? QStringLiteral( "untitled" ) : name;
}

// The copy numbers of this map already in the folder, oldest first.
std::vector<u64> Numbers( const QDir &dir, const QString &base )
{
    const QRegularExpression pattern( QStringLiteral( "^%1\\.(\\d+)\\.cymap$" ).arg( QRegularExpression::escape( base ) ) );
    std::vector<u64> numbers;
    for ( const QString &file : dir.entryList( QDir::Files ) ) {
        const auto match = pattern.match( file );
        bool ok = false;
        const u64 n = match.hasMatch() ? match.captured( 1 ).toULongLong( &ok ) : 0u;
        if ( ok ) { numbers.push_back( n ); }
    }
    std::sort( numbers.begin(), numbers.end() );
    return numbers;
}

// One copy: its map file (CYMAP.md 1), nothing else.
void RemoveCopy( const QDir &dir, const QString &base, u64 n )
{
    ( void )QFile::remove( dir.filePath( QStringLiteral( "%1.%2.cymap" ).arg( base ).arg( n ) ) );
}
} // namespace

QString MapAutosave_Folder( const map_workspace_t *ws )
{
    if ( ws != nullptr && !ws->path.isEmpty() ) { return QFileInfo( ws->path ).absoluteDir().filePath( QStringLiteral( "autosave" ) ); }
    return QDir( QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) ).filePath( QStringLiteral( "autosave" ) );
}

map_autosave_status_t MapAutosave_Run( const map_workspace_t *ws, map_autosave_t *state, const QString &folder, u32 keep, QString *pPathOut )
{
    if ( pPathOut != nullptr ) { pPathOut->clear(); }
    if ( ws == nullptr || state == nullptr || ws->pDocument == nullptr || folder.isEmpty() ) { return map_autosave_status_t::FAILED; }
    if ( !MapWorkspace_IsModified( ws ) || ws->pDocument == state->pLastCopied ) { return map_autosave_status_t::UNCHANGED; }
    if ( EditorHistory_IsTransactionOpen( &ws->history ) ) { return map_autosave_status_t::BUSY; }
    QDir dir( folder );
    if ( !dir.mkpath( QStringLiteral( "." ) ) ) { return map_autosave_status_t::FAILED; }
    const QString base = BaseName( ws );
    auto numbers = Numbers( dir, base );
    const u64 next = numbers.empty() ? 1u : numbers.back() + 1u;
    // A file left by a copy cut short is replaced, never appended to.
    RemoveCopy( dir, base, next );
    const QString path = dir.filePath( QStringLiteral( "%1.%2.cymap" ).arg( base ).arg( next ) );
    map_document_t *copy = nullptr;
    bool saved = MapEdit_Clone( ws->pDocument, &copy ) == map_status_t::OK;
    if ( saved ) { saved = MapFiles_Save( copy, path.toUtf8().constData() ).status == map_files_status_t::OK; }
    delete copy;
    if ( !saved ) {
        RemoveCopy( dir, base, next );
        Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Editor, "Autosave could not write its copy", CY_SOURCE_LOCATION );
        return map_autosave_status_t::FAILED;
    }
    state->pLastCopied = ws->pDocument;
    numbers.push_back( next );
    const usize kept = std::max<u32>( keep, 1u );
    for ( usize i = 0; i + kept < numbers.size(); ++i ) { RemoveCopy( dir, base, numbers[i] ); }
    if ( pPathOut != nullptr ) { *pPathOut = path; }
    return map_autosave_status_t::SAVED;
}

} // namespace cypher::editor::map
