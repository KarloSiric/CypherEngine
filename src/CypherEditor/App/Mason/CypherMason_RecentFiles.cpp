//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_RecentFiles.cpp
//  Purpose: Implements the recent maps list.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMason_RecentFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace cypher::mason
{

namespace
{

// The same map reached two ways (relative, symlinked) is one entry.
QString Canonical( const QString &file )
{
    const QFileInfo info( file );
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath( canonical.isEmpty() ? info.absoluteFilePath() : canonical );
}

bool Save( const mason_recent_t &recent )
{
    if ( recent.path.isEmpty() ) { return true; }
    if ( !QDir().mkpath( QFileInfo( recent.path ).absolutePath() ) ) { return false; }
    QSaveFile file( recent.path ); // Atomic: a failed write keeps the old list.
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) { return false; }
    const QByteArray text = recent.files.join( QLatin1Char( '\n' ) ).toUtf8() + '\n';
    return file.write( text ) == text.size() && file.commit();
}

void Trim( mason_recent_t *pRecent )
{
    while ( pRecent->files.size() > std::max( 0, pRecent->nMax ) ) { pRecent->files.removeLast(); }
}

} // namespace

void MasonRecent_Load( mason_recent_t *pRecent, const QString &path, int nMax )
{
    pRecent->path = path;
    pRecent->nMax = nMax;
    pRecent->files.clear();
    QFile file( path );
    if ( path.isEmpty() || !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) { return; }
    for ( const QString &line : QString::fromUtf8( file.readAll() ).split( QLatin1Char( '\n' ) ) ) {
        const QString entry = line.trimmed();
        // A hand-edited or damaged list never yields relative or duplicate entries.
        if ( entry.isEmpty() || QDir::isRelativePath( entry ) || pRecent->files.contains( entry ) ) { continue; }
        pRecent->files.append( entry );
    }
    Trim( pRecent );
}

bool MasonRecent_Add( mason_recent_t *pRecent, const QString &file )
{
    if ( file.isEmpty() ) { return true; }
    const QString entry = Canonical( file );
    pRecent->files.removeAll( entry );
    pRecent->files.prepend( entry );
    Trim( pRecent );
    return Save( *pRecent );
}

bool MasonRecent_Remove( mason_recent_t *pRecent, const QString &file )
{
    pRecent->files.removeAll( file );
    pRecent->files.removeAll( Canonical( file ) );
    return Save( *pRecent );
}

bool MasonRecent_Clear( mason_recent_t *pRecent )
{
    pRecent->files.clear();
    return Save( *pRecent );
}

void MasonRecent_SetMax( mason_recent_t *pRecent, int nMax )
{
    pRecent->nMax = nMax;
    if ( pRecent->files.size() > std::max( 0, nMax ) ) {
        Trim( pRecent );
        ( void )Save( *pRecent );
    }
}

} // namespace cypher::mason
