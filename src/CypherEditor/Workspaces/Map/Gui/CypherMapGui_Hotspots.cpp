//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Hotspots.cpp
//  Purpose: Implements hotspot rectangle files on disk.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Hotspots.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

namespace cypher::editor::map
{
using namespace common;

namespace
{
QHash<const map_workspace_t *, map_asset_file_fn> &Resolvers()
{
    static QHash<const map_workspace_t *, map_asset_file_fn> resolvers;
    return resolvers;
}
QHash<const map_workspace_t *, u64> &Generations()
{
    static QHash<const map_workspace_t *, u64> generations;
    return generations;
}
QHash<const map_workspace_t *, QStringList> &Roots()
{
    static QHash<const map_workspace_t *, QStringList> roots;
    return roots;
}
QHash<const map_workspace_t *, map_asset_list_fn> &Listers()
{
    static QHash<const map_workspace_t *, map_asset_list_fn> listers;
    return listers;
}
u64 g_nextGeneration = 1u;
QString Resolve( const map_workspace_t *ws, const QString &path )
{
    const auto it = Resolvers().constFind( ws );
    return it != Resolvers().constEnd() && it.value() ? it.value()( path ) : QString();
}
QString Sidecar( const QString &materialPath )
{
    const QByteArray utf8 = materialPath.toUtf8();
    return QString::fromStdString( MapHotspots_SidecarPath( { utf8.constData(), static_cast<usize>( utf8.size() ) } ) );
}
} // namespace

void MapHotspots_SetResolver( const map_workspace_t *pWorkspace, map_asset_file_fn resolver )
{
    if ( resolver ) { Resolvers().insert( pWorkspace, std::move( resolver ) ); Generations().insert( pWorkspace, g_nextGeneration++ ); }
    else { Resolvers().remove( pWorkspace ); Generations().remove( pWorkspace ); }
}

QString MapAssets_File( const map_workspace_t *pWorkspace, const QString &virtualPath ) { return Resolve( pWorkspace, virtualPath ); }

u64 MapAssets_Generation( const map_workspace_t *pWorkspace ) { return Generations().value( pWorkspace, 0u ); }

void MapAssets_SetLister( const map_workspace_t *pWorkspace, map_asset_list_fn lister )
{
    if ( lister ) { Listers().insert( pWorkspace, std::move( lister ) ); }
    else { Listers().remove( pWorkspace ); }
}

void MapAssets_SetRoots( const map_workspace_t *pWorkspace, const QStringList &roots )
{
    if ( roots.isEmpty() ) { Roots().remove( pWorkspace ); } else { Roots().insert( pWorkspace, roots ); }
}

QStringList MapAssets_Roots( const map_workspace_t *pWorkspace ) { return Roots().value( pWorkspace ); }

QString MapAssets_VirtualPath( const map_workspace_t *pWorkspace, const QString &file )
{
    const QString absolute = QFileInfo( file ).absoluteFilePath();
    for ( const QString &root : Roots().value( pWorkspace ) ) {
        const QString base = QDir( root ).absolutePath() + QLatin1Char( '/' );
        if ( absolute.startsWith( base ) ) { return absolute.mid( base.size() ); }
    }
    return QString();
}

QStringList MapAssets_List( const map_workspace_t *pWorkspace, const QStringList &extensions )
{
    const auto it = Listers().constFind( pWorkspace );
    return it != Listers().constEnd() && it.value() ? it.value()( extensions ) : QStringList();
}

QString MapHotspots_File( const map_workspace_t *pWorkspace, const QString &materialPath )
{
    if ( materialPath.isEmpty() ) { return QString(); }
    const QString existing = Resolve( pWorkspace, Sidecar( materialPath ) );
    if ( !existing.isEmpty() ) { return existing; }
    const QString material = Resolve( pWorkspace, materialPath );
    return material.isEmpty() ? QString() : Sidecar( material );
}

std::vector<geometry::mesh_hotspot_rect_t> MapHotspots_Load( const map_workspace_t *pWorkspace, const QString &materialPath, QString *pProblem )
{
    if ( pProblem != nullptr ) { pProblem->clear(); }
    std::vector<geometry::mesh_hotspot_rect_t> rects;
    const QString file = MapHotspots_File( pWorkspace, materialPath );
    QFile in( file );
    if ( file.isEmpty() || !in.exists() ) {
        if ( pProblem != nullptr ) { *pProblem = QStringLiteral( "%1 has no hotspot rectangles (%2)" ).arg( materialPath, Sidecar( materialPath ) ); }
        return rects;
    }
    if ( in.size() > static_cast<qint64>( MAP_HOTSPOTS_TEXT_MAX ) || !in.open( QIODevice::ReadOnly ) ) {
        if ( pProblem != nullptr ) { *pProblem = QStringLiteral( "%1 cannot be read" ).arg( file ); }
        return rects;
    }
    const QByteArray text = in.readAll();
    const auto status = MapHotspots_Parse( { text.constData(), static_cast<usize>( text.size() ) }, rects );
    if ( status != map_status_t::OK && pProblem != nullptr ) { *pProblem = QStringLiteral( "%1 is not a hotspot rectangle file (%2)" ).arg( file, QString::fromUtf8( MapDocument_StatusName( status ) ) ); }
    return rects;
}

bool MapHotspots_Save( const map_workspace_t *pWorkspace, const QString &materialPath, const std::vector<geometry::mesh_hotspot_rect_t> &rects, QString *pFileOut )
{
    const QString file = MapHotspots_File( pWorkspace, materialPath );
    if ( pFileOut != nullptr ) { *pFileOut = file; }
    if ( file.isEmpty() ) { return false; }
    if ( rects.empty() ) { return !QFileInfo::exists( file ) || QFile::remove( file ); }
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) { return false; }
    bool ok = MapHotspots_Write( rects, &text ) == map_status_t::OK;
    if ( ok ) {
        // Written whole or not at all.
        QSaveFile out( file );
        const string_view_t view = TextBuffer_View( &text );
        ok = out.open( QIODevice::WriteOnly ) && out.write( view.pData, static_cast<qint64>( view.cchLength ) ) == static_cast<qint64>( view.cchLength ) && out.commit();
    }
    TextBuffer_Shutdown( &text );
    return ok;
}

} // namespace cypher::editor::map
