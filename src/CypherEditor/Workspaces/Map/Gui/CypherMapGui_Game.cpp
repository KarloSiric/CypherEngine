//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Game.cpp
//  Purpose: Implements finding, caching and using the workspace's game
//           profile.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Game.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMap_EntityEdit.h"

#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QFile>
#include <QHash>

#include <algorithm>
#include <memory>

namespace cypher::editor::map
{
using namespace common;

namespace
{
struct entry_t {
    std::string game{};
    u64 generation{ 0u };
    std::shared_ptr<map_game_profile_t> pProfile{};
    QString status{};
};
QHash<const map_workspace_t *, entry_t> &Entries()
{
    static QHash<const map_workspace_t *, entry_t> entries;
    return entries;
}

std::string GameId( const map_workspace_t *ws )
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return {}; }
    string_view_t id{};
    return KeyValue_GetString( KeyValue_Find( SettingsDocument_Root( &ws->pDocument->root ), StringView_FromCString( "game" ) ), &id )
        ? std::string( id.pData, id.cchLength ) : std::string();
}

entry_t &Load( const map_workspace_t *ws, bool force )
{
    entry_t &entry = Entries()[ws];
    const std::string game = GameId( ws );
    const u64 generation = MapAssets_Generation( ws );
    if ( !force && entry.game == game && entry.generation == generation && ( entry.pProfile != nullptr || !entry.status.isEmpty() ) ) { return entry; }
    entry = entry_t{ game, generation, nullptr, {} };
    if ( game.empty() ) { entry.status = QStringLiteral( "The map names no game" ); return entry; }
    if ( generation == 0u ) { entry.status = QStringLiteral( "No content folders to find the game profile in" ); return entry; }
    const std::string path = "game/" + game + ".cygame";
    auto profile = std::make_shared<map_game_profile_t>();
    const map_game_read_fn read = [ws]( const std::string &virtualPath, std::string &text ) {
        const QString file = MapAssets_File( ws, QString::fromStdString( virtualPath ) );
        QFile in( file );
        if ( file.isEmpty() || !in.open( QIODevice::ReadOnly ) ) { return false; }
        text = in.readAll().toStdString();
        return true;
    };
    const map_status_t status = MapGame_Load( path, read, *profile );
    if ( status != map_status_t::OK ) {
        entry.status = QStringLiteral( "Game profile %1: %2" ).arg( QString::fromStdString( path ),
            profile->problems.empty() ? QStringLiteral( "not found" ) : QString::fromStdString( profile->problems.front() ) );
        CY_LOG_WRITE( Warning, Editor, entry.status.toUtf8().constData() );
        return entry;
    }
    entry.status = QStringLiteral( "Game profile %1: %2 classes" ).arg( QString::fromStdString( profile->name ) ).arg( profile->classes.size() );
    if ( !profile->problems.empty() ) { entry.status += QStringLiteral( ", %1 problems (first: %2)" ).arg( profile->problems.size() ).arg( QString::fromStdString( profile->problems.front() ) ); }
    CY_LOG_WRITE( Info, Editor, entry.status.toUtf8().constData() );
    for ( const auto &problem : profile->problems ) { CY_LOG_WRITE( Warning, Editor, ( "Game profile: " + problem ).c_str() ); }
    entry.pProfile = std::move( profile );
    return entry;
}

QStringList Names( const std::vector<map_game_io_t> &list )
{
    QStringList names;
    for ( const auto &io : list ) { names.append( QString::fromStdString( io.name ) ); }
    return names;
}
} // namespace

const map_game_profile_t *MapGame_Profile( const map_workspace_t *ws ) { return ws != nullptr ? Load( ws, false ).pProfile.get() : nullptr; }

map_status_t MapGame_Reload( const map_workspace_t *ws, QString *pSummaryOut )
{
    if ( ws == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    const entry_t &entry = Load( ws, true );
    if ( pSummaryOut != nullptr ) { *pSummaryOut = entry.status; }
    return entry.pProfile != nullptr ? map_status_t::OK : map_status_t::ROOT_UNREADABLE;
}

QString MapGame_Status( const map_workspace_t *ws ) { return ws != nullptr ? Load( ws, false ).status : QString(); }

void MapGame_Forget( const map_workspace_t *ws ) { Entries().remove( ws ); }

const map_game_class_t *MapGame_Class( const map_workspace_t *ws, string_view_t className )
{
    const map_game_profile_t *profile = MapGame_Profile( ws );
    return profile != nullptr ? MapGame_FindClass( *profile, className ) : nullptr;
}

map_status_t MapGame_WriteDefaults( const map_workspace_t *ws, map_document_t *map, u64 entity, string_view_t className ) noexcept
{
    try {
        const map_game_class_t *cls = MapGame_Class( ws, className );
        if ( cls == nullptr ) { return map_status_t::OK; } // Unknown classes keep only what they were made with.
        const key_value_t *record = MapEntityEdit_FindEntity( map, entity );
        if ( record == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        std::vector<const map_game_property_t *> missing;
        const key_value_t *properties = KeyValue_Find( record, StringView_FromCString( "properties" ) );
        for ( const auto &property : cls->properties ) {
            if ( property.pDefault == nullptr ) { continue; }
            if ( KeyValue_Find( properties, { property.key.data(), property.key.size() } ) == nullptr ) { missing.push_back( &property ); }
        }
        // Looked up first: the record moves while it is edited.
        for ( const auto *property : missing ) {
            bool changed = false;
            const auto status = MapEntityEdit_SetProperty( map, { &entity, 1 }, { property->key.data(), property->key.size() }, property->pDefault, &changed );
            if ( status != map_status_t::OK ) { return status; }
        }
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
    return map_status_t::OK;
}

std::vector<const map_game_class_t *> MapGame_Placeable( const map_workspace_t *ws, map_game_kind_t kind )
{
    std::vector<const map_game_class_t *> classes;
    const map_game_profile_t *profile = MapGame_Profile( ws );
    if ( profile == nullptr ) { return classes; }
    for ( const auto &cls : profile->classes ) { if ( cls.kind == kind ) { classes.push_back( &cls ); } }
    const auto rank = [&]( const std::string &category ) {
        const auto it = std::find( profile->categories.begin(), profile->categories.end(), category );
        return it != profile->categories.end() ? static_cast<usize>( it - profile->categories.begin() ) : profile->categories.size();
    };
    std::stable_sort( classes.begin(), classes.end(), [&]( const map_game_class_t *a, const map_game_class_t *b ) {
        const usize ra = rank( a->category ), rb = rank( b->category );
        if ( ra != rb ) { return ra < rb; }
        if ( a->category != b->category ) { return a->category < b->category; }
        return a->name < b->name;
    } );
    return classes;
}

QStringList MapGame_Outputs( const map_workspace_t *ws, string_view_t className )
{
    const map_game_class_t *cls = MapGame_Class( ws, className );
    return cls != nullptr ? Names( cls->outputs ) : QStringList();
}

QStringList MapGame_Inputs( const map_workspace_t *ws, string_view_t className )
{
    const map_game_class_t *cls = MapGame_Class( ws, className );
    return cls != nullptr ? Names( cls->inputs ) : QStringList();
}

} // namespace cypher::editor::map
