//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Instances.cpp
//  Purpose: Implements prefab instances in the editor.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Instances.h"

#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Groups.h"
#include "CypherMap_Prefab.h"

#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <vector>

namespace cypher::editor::map
{
using namespace common;

namespace
{
using vec3 = math::vec3d_t;

string_view_t View( const QByteArray &utf8 ) noexcept { return { utf8.constData(), static_cast<usize>( utf8.size() ) }; }
const key_value_t *Member( const key_value_t *pRecord, const char *pName ) noexcept { return KeyValue_Find( pRecord, StringView_FromCString( pName ) ); }

bool Number( const key_value_t *pValue, f64 *pOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, pOut ) ) { return true; }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { *pOut = static_cast<f64>( nSigned ); return true; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { *pOut = static_cast<f64>( nUnsigned ); return true; }
    return false;
}

bool Vector( const key_value_t *pValue, vec3 *pOut ) noexcept
{
    f64 c[3]{};
    if ( KeyValue_ChildCount( pValue ) != 3u ) { return false; }
    for ( usize i = 0; i < 3u; ++i ) { if ( !Number( KeyValue_ChildAt( pValue, i ), &c[i] ) ) { return false; } }
    *pOut = { c[0], c[1], c[2] };
    return true;
}

QString Text( const key_value_t *pValue )
{
    string_view_t text{};
    return KeyValue_GetString( pValue, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

// A prefab file read into a document of its own, its contents around the
// prefab's origin, and the wireframe of that document.
struct prefab_file_t {
    QString file{};
    qint64 modified{ -1 };
    QString problem{};
    std::unique_ptr<map_document_t> document{};
    std::unique_ptr<map_wireframe_t> wire{};
};

struct instance_t {
    u64 id{ 0 };
    u32 iLayer{ 0 };
    map_instance_info_t info{};
    u32 iFirstFace{ 0 }, nFaces{ 0 }; // Its faces in the live wireframe.
};

struct state_t {
    QHash<QString, std::shared_ptr<prefab_file_t>> files; // By virtual path.
    QHash<u64, instance_t> instances;
};
QHash<const map_workspace_t *, state_t> &States()
{
    static QHash<const map_workspace_t *, state_t> states;
    return states;
}

std::shared_ptr<prefab_file_t> Load( map_workspace_t *ws, state_t &state, const QString &source )
{
    const QString file = MapAssets_File( ws, source );
    auto cached = state.files.value( source );
    const qint64 modified = file.isEmpty() ? -1 : QFileInfo( file ).lastModified().toMSecsSinceEpoch();
    if ( cached != nullptr && cached->file == file && cached->modified == modified ) { return cached; }
    auto prefab = std::make_shared<prefab_file_t>();
    prefab->file = file;
    prefab->modified = modified;
    state.files.insert( source, prefab );
    if ( file.isEmpty() ) { prefab->problem = QStringLiteral( "%1 is not in the content folders" ).arg( source ); return prefab; }
    QFile in( file );
    if ( !in.open( QIODevice::ReadOnly ) ) { prefab->problem = QStringLiteral( "%1 cannot be read" ).arg( file ); return prefab; }
    const QByteArray text = in.readAll();
    auto document = std::make_unique<map_document_t>();
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "prefab" );
    desc.game = StringView_FromCString( "reap" );
    vector_t<u64> ids{};
    map_status_t status = MapDocument_Create( document.get(), ws->pGui->pAllocator, desc );
    if ( status == map_status_t::OK ) { status = Vector_Init( &ids, ws->pGui->pAllocator ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY; }
    if ( status == map_status_t::OK ) { status = MapPrefab_Insert( document.get(), View( text ), { 0, 0, 0 }, &ids ); }
    Vector_Shutdown( &ids );
    auto wire = std::make_unique<map_wireframe_t>();
    if ( status == map_status_t::OK ) {
        status = MapWireframe_Init( wire.get(), ws->pGui->pAllocator ) ? MapWireframe_Build( wire.get(), *document ) : map_status_t::OUT_OF_MEMORY;
    }
    if ( status != map_status_t::OK ) {
        prefab->problem = QStringLiteral( "%1 is not a prefab file Mason can read (%2)" ).arg( source, QString::fromUtf8( MapDocument_StatusName( status ) ) );
        return prefab;
    }
    prefab->document = std::move( document );
    prefab->wire = std::move( wire );
    return prefab;
}

u32 LayerIndexOf( const map_document_t &map, const char *pLayer ) noexcept
{
    for ( usize i = 0; i < map.layers.nCount; ++i ) { if ( std::strcmp( map.layers.pData[i].id, pLayer ) == 0 ) { return static_cast<u32>( i ); } }
    return 0;
}

vec3 Linear( const math::affine3d_t &a, vec3 v ) noexcept
{
    return { a.m[0] * v.x + a.m[3] * v.y + a.m[6] * v.z, a.m[1] * v.x + a.m[4] * v.y + a.m[7] * v.z, a.m[2] * v.x + a.m[5] * v.y + a.m[8] * v.z };
}

vec3 Unit( vec3 v ) noexcept
{
    const f64 length = std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
    return length > 1e-12 ? vec3{ v.x / length, v.y / length, v.z / length } : vec3{ 0, 0, 1 };
}

// Adds the instance to the live wireframe: its prefab's points (placed),
// lines and faces under the instance's ID, or a marker box when the prefab
// cannot be read. Materials are looked up by path in the map's table.
void Append( map_workspace_t *ws, instance_t &instance, const prefab_file_t *pPrefab )
{
    map_wireframe_t &wire = ws->wire;
    const auto transform = MapInstances_Transform( instance.info.origin, instance.info.angles, instance.info.scale );
    map_wire_object_t object{};
    object.id = instance.id;
    object.kind = map_wire_kind_t::PREFAB;
    object.iLayer = instance.iLayer;
    object.iFirstLine = static_cast<u32>( wire.lines.nCount );
    object.iFirstPoint = static_cast<u32>( wire.points.nCount );
    instance.iFirstFace = static_cast<u32>( wire.faces.nCount );
    bool ok = true;
    const auto push = [&]( vec3 p ) {
        ok = ok && Vector_PushBack( &wire.points, p ) && Vector_PushBack( &wire.pointSourceIds, u64{ 0 } );
        if ( ok ) { MapBounds_AddPoint( object.bounds, p ); }
    };
    const auto box = [&]( vec3 lo, vec3 hi ) {
        const u32 base = static_cast<u32>( wire.points.nCount );
        for ( u32 c = 0; c < 8u; ++c ) {
            push( math::Affine3d_TransformPoint( transform, { ( c & 1u ) ? hi.x : lo.x, ( c & 2u ) ? hi.y : lo.y, ( c & 4u ) ? hi.z : lo.z } ) );
        }
        constexpr u32 kEdges[12][2]{ { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for ( const auto &edge : kEdges ) { ok = ok && Vector_PushBack( &wire.lines, map_wire_line_t{ base + edge[0], base + edge[1] } ); }
    };
    if ( pPrefab == nullptr || pPrefab->wire == nullptr ) {
        box( { -16, -16, 0 }, { 16, 16, 32 } ); // Where the missing prefab stands.
    } else {
        const map_wireframe_t &source = *pPrefab->wire;
        const u32 base = static_cast<u32>( wire.points.nCount );
        for ( usize i = 0; i < source.points.nCount; ++i ) { push( math::Affine3d_TransformPoint( transform, source.points.pData[i] ) ); }
        for ( usize i = 0; i < source.lines.nCount; ++i ) {
            ok = ok && Vector_PushBack( &wire.lines, map_wire_line_t{ base + source.lines.pData[i].iA, base + source.lines.pData[i].iB } );
        }
        for ( usize i = 0; i < source.objects.nCount; ++i ) {
            const auto &part = source.objects.pData[i];
            if ( part.kind == map_wire_kind_t::ENTITY && part.bounds.bHas ) { box( part.bounds.box.minimum, part.bounds.box.maximum ); }
        }
        // Normals turn with the instance; a non-uniform scale bends them
        // by its inverse.
        const vec3 s = instance.info.scale;
        for ( usize i = 0; i < source.faces.nCount && ok; ++i ) {
            map_wire_face_t face = source.faces.pData[i];
            face.id = instance.id; face.sideId = 0; face.faceId = 0;
            const vec3 n = face.normal;
            face.normal = Unit( Linear( transform, { s.x != 0 ? n.x / ( s.x * s.x ) : n.x, s.y != 0 ? n.y / ( s.y * s.y ) : n.y, s.z != 0 ? n.z / ( s.z * s.z ) : n.z } ) );
            const string_view_t path = MapMaterials_Path( &pPrefab->document->materials, face.material );
            u64 material = 0;
            if ( path.cchLength != 0 ) { ( void )MapMaterials_Intern( &ws->pDocument->materials, path, &material ); }
            face.material = material;
            const u32 first = static_cast<u32>( wire.faceIndices.nCount );
            for ( u32 k = 0; k < face.nIndices && ok; ++k ) { ok = Vector_PushBack( &wire.faceIndices, base + source.faceIndices.pData[face.iFirstIndex + k] ); }
            face.iFirstIndex = first;
            ok = ok && Vector_PushBack( &wire.faces, face );
        }
    }
    instance.nFaces = static_cast<u32>( wire.faces.nCount ) - instance.iFirstFace;
    object.nLines = static_cast<u32>( wire.lines.nCount ) - object.iFirstLine;
    object.nPoints = static_cast<u32>( wire.points.nCount ) - object.iFirstPoint;
    if ( !ok || !Vector_PushBack( &wire.objects, object ) ) {
        CY_LOG_WRITE( Warning, Editor, "A prefab instance could not be added to the map views" );
        return;
    }
    // Objects stay sorted by ID: FindObject searches them.
    std::rotate( std::upper_bound( wire.objects.pData, wire.objects.pData + wire.objects.nCount - 1, object.id,
                     []( u64 id, const map_wire_object_t &o ) { return id < o.id; } ),
                 wire.objects.pData + wire.objects.nCount - 1, wire.objects.pData + wire.objects.nCount );
    MapBounds_AddBounds( wire.bounds, object.bounds );
}

map_workspace_t *Ws( void *pContext ) noexcept { return static_cast<map_workspace_t *>( pContext ); }
command_result_t Result( bool done ) noexcept { return done ? command_result_t::OK : command_result_t::FAILED; }

bool Ready( const map_workspace_t *ws ) noexcept
{
    return ws != nullptr && ws->pDocument != nullptr && !ws->pDocument->bReadOnly && !ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &ws->history );
}

std::vector<u64> SelectedInstances( const map_workspace_t *ws )
{
    std::vector<u64> ids;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        if ( MapInstances_IsInstance( ws, ws->selection.ids.pData[i] ) ) { ids.push_back( ws->selection.ids.pData[i] ); }
    }
    return ids;
}

/*
================
Create Instance from Selection
================
*/

// What can go into a prefab: world geometry and entities, no instances.
std::vector<u64> PrefabContents( const map_workspace_t *ws )
{
    std::vector<u64> ids;
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const u64 id = ws->selection.ids.pData[i];
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        if ( object == nullptr || object->kind == map_wire_kind_t::PREFAB || object->kind == map_wire_kind_t::TERRAIN ) { return {}; }
        if ( object->owner != 0 && EditorSelection_Contains( &ws->selection, object->owner ) ) { continue; } // Comes with its entity.
        ids.push_back( id );
    }
    return ids;
}

u32 CreateState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return Ready( ws ) && !PrefabContents( ws ).empty() ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

// Adds an instance record to a layer's `prefabs` section of the copy.
map_status_t AddInstance( map_document_t *copy, string_view_t layer, const QString &source, vec3 origin, vec3 angles, vec3 scale, u64 id, const key_value_t *pOverrides )
{
    map_layer_store_t *store = MapDocument_LayerStore( copy, layer );
    if ( store == nullptr ) { return map_status_t::UNKNOWN_LAYER; }
    key_value_document_t *d = store->store.pDocument;
    key_value_t *root = KeyValue_Root( d );
    key_value_t *prefabs = KeyValue_Find( root, StringView_FromCString( "prefabs" ) );
    if ( prefabs == nullptr ) { prefabs = KeyValue_ObjectInsert( d, root, StringView_FromCString( "prefabs" ), key_value_type_t::ARRAY ); }
    key_value_t *record = prefabs != nullptr ? KeyValue_ArrayAppend( d, prefabs, key_value_type_t::OBJECT ) : nullptr;
    if ( record == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    const QByteArray utf8 = source.toUtf8();
    key_value_t *idNode = KeyValue_ObjectInsert( d, record, StringView_FromCString( "id" ), key_value_type_t::U64 );
    key_value_t *sourceNode = KeyValue_ObjectInsert( d, record, StringView_FromCString( "source" ), key_value_type_t::STRING );
    if ( idNode == nullptr || sourceNode == nullptr || !KeyValue_SetU64( d, idNode, id ) || !KeyValue_SetString( d, sourceNode, View( utf8 ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto vector = [&]( const char *name, vec3 v ) {
        key_value_t *node = KeyValue_ObjectInsert( d, record, StringView_FromCString( name ), key_value_type_t::ARRAY );
        if ( node == nullptr ) { return false; }
        for ( f64 c : { v.x, v.y, v.z } ) {
            key_value_t *item = KeyValue_ArrayAppend( d, node, key_value_type_t::F64 );
            if ( item == nullptr || !KeyValue_SetF64( d, item, c ) ) { return false; }
        }
        return true;
    };
    if ( !vector( "origin", origin ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( ( angles.x != 0 || angles.y != 0 || angles.z != 0 ) && !vector( "angles", angles ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( scale.x != 1 || scale.y != 1 || scale.z != 1 ) {
        if ( scale.x == scale.y && scale.y == scale.z ) {
            key_value_t *node = KeyValue_ObjectInsert( d, record, StringView_FromCString( "scale" ), key_value_type_t::F64 );
            if ( node == nullptr || !KeyValue_SetF64( d, node, scale.x ) ) { return map_status_t::OUT_OF_MEMORY; }
        } else if ( !vector( "scale", scale ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( pOverrides != nullptr && KeyValue_CloneInto( d, record, pOverrides ) == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    return map_status_t::OK;
}

command_result_t CreateInstance( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( CreateState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        const QStringList roots = MapAssets_Roots( ws );
        QString file;
        if ( args.nArgs >= 1 ) { file = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ); }
        else if ( QApplication::instance() != nullptr && !roots.isEmpty() ) {
            const QString folder = QDir( roots.front() ).filePath( QStringLiteral( "prefabs" ) );
            ( void )QDir().mkpath( folder );
            file = QFileDialog::getSaveFileName( QApplication::activeWindow(), QStringLiteral( "Create Instance from Selection" ), folder, QStringLiteral( "Prefabs (*.cyprefab)" ) );
            if ( file.isEmpty() ) { return command_result_t::OK; } // Cancelled.
        }
        if ( file.isEmpty() ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Create Instance: there are no content folders to save the prefab in" ) );
            return command_result_t::FAILED;
        }
        if ( QFileInfo( file ).isRelative() && !roots.isEmpty() ) { file = QDir( roots.front() ).filePath( file ); }
        if ( !file.endsWith( QStringLiteral( ".cyprefab" ) ) ) { file += QStringLiteral( ".cyprefab" ); }
        const QString source = MapAssets_VirtualPath( ws, file );
        if ( source.isEmpty() ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Create Instance: save the prefab inside a content folder, so the map can name it (%1)" ).arg( roots.join( QStringLiteral( ", " ) ) ) );
            return command_result_t::FAILED;
        }
        const std::vector<u64> contents = PrefabContents( ws );
        const QByteArray name = QFileInfo( file ).completeBaseName().left( 128 ).toUtf8();
        text_buffer_t text{};
        if ( !TextBuffer_Init( &text, ws->pGui->pAllocator, 0 ) ) { return command_result_t::FAILED; }
        vec3 origin{};
        const map_status_t written = MapPrefab_Write( ws->pDocument, { contents.data(), contents.size() }, View( name ), &text, &origin );
        bool saved = false;
        if ( written == map_status_t::OK ) {
            ( void )QDir().mkpath( QFileInfo( file ).absolutePath() );
            QSaveFile out( file );
            const string_view_t view = TextBuffer_View( &text );
            saved = out.open( QIODevice::WriteOnly ) && out.write( view.pData, static_cast<qint64>( view.cchLength ) ) == static_cast<qint64>( view.cchLength ) && out.commit();
        }
        TextBuffer_Shutdown( &text );
        if ( !saved ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Create Instance: the prefab could not be written to %1" ).arg( file ) );
            return command_result_t::FAILED;
        }
        // The selection leaves the map and an instance of the new prefab
        // takes its place, in the layer of its first object.
        const auto *first = MapWireframe_FindObject( ws->wire, contents.front() );
        const QByteArray layer( first != nullptr && first->iLayer < ws->pDocument->layers.nCount ? ws->pDocument->layers.pData[first->iLayer].id : "" );
        struct job_t { std::vector<u64> contents; QString source; vec3 origin; QByteArray layer; u64 id; } job{ contents, source, origin, layer, 0 };
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Create Instance from Selection", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            map_status_t status = MapEdit_Delete( copy, { j.contents.data(), j.contents.size() } );
            j.id = MapDocument_AllocateId( copy );
            if ( status == map_status_t::OK ) { status = AddInstance( copy, View( j.layer ), j.source, j.origin, {}, { 1, 1, 1 }, j.id, nullptr ); }
            *changed = status == map_status_t::OK;
            return status == map_status_t::OK && !Vector_PushBack( out, j.id ) ? map_status_t::OUT_OF_MEMORY : status;
        }, &job );
        if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Saved %1 and placed an instance of it (Edit Prefab changes every instance)" ).arg( source ) ); }
        return Result( done );
    } catch ( ... ) { return command_result_t::FAILED; }
}

/*
================
Collapse and Edit
================
*/

u32 InstanceState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return Ready( ws ) && !SelectedInstances( ws ).empty() ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

// Sets object member `name` to a copy of `value`.
bool SetMember( key_value_document_t *d, key_value_t *object, string_view_t name, const key_value_t *value ) noexcept
{
    const key_value_type_t type = KeyValue_Type( value );
    key_value_t *node = KeyValue_ObjectInsert( d, object, name, type );
    if ( node == nullptr ) { return false; }
    bool_t b = CY_FALSE; i64 i = 0; u64 u = 0; f64 f = 0.0; string_view_t text{};
    switch ( type ) {
        case key_value_type_t::BOOL: return KeyValue_GetBool( value, &b ) && KeyValue_SetBool( d, node, b );
        case key_value_type_t::I64: return KeyValue_GetI64( value, &i ) && KeyValue_SetI64( d, node, i );
        case key_value_type_t::U64: return KeyValue_GetU64( value, &u ) && KeyValue_SetU64( d, node, u );
        case key_value_type_t::F64: return KeyValue_GetF64( value, &f ) && KeyValue_SetF64( d, node, f );
        case key_value_type_t::STRING: return KeyValue_GetString( value, &text ) && KeyValue_SetString( d, node, text );
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY:
            for ( const key_value_t *child = KeyValue_FirstChild( value ); child != nullptr; child = KeyValue_NextSibling( child ) ) {
                if ( KeyValue_CloneInto( d, node, child ) == nullptr ) { return false; }
            }
            return true;
        default: return true;
    }
}

// The instance's overrides on its entities, by entity name: "<name>.<member
// path>" sets that member (`name.properties.speed`, `name.name`).
void ApplyOverrides( map_document_t *copy, const std::vector<u64> &inserted, const key_value_t *pOverrides )
{
    for ( const key_value_t *pOverride = KeyValue_FirstChild( pOverrides ); pOverride != nullptr; pOverride = KeyValue_NextSibling( pOverride ) ) {
        const string_view_t key = KeyValue_Name( pOverride );
        const QString path = QString::fromUtf8( key.pData, static_cast<qsizetype>( key.cchLength ) );
        const qsizetype dot = path.indexOf( QLatin1Char( '.' ) );
        if ( dot <= 0 ) { continue; }
        const QByteArray object = path.left( dot ).toUtf8();
        const QStringList members = path.mid( dot + 1 ).split( QLatin1Char( '.' ) );
        for ( u64 id : inserted ) {
            map_layer_store_t *store = nullptr;
            key_value_t *record = MapDocument_FindObject( copy, id, &store );
            string_view_t name{};
            const bool named = record != nullptr && KeyValue_GetString( KeyValue_Find( record, StringView_FromCString( "name" ) ), &name ) &&
                StringView_Equals( name, View( object ) );
            if ( !named ) { continue; }
            key_value_t *at = record;
            for ( qsizetype m = 0; at != nullptr && m < members.size(); ++m ) {
                const QByteArray member = members[m].toUtf8();
                key_value_t *next = KeyValue_Find( at, View( member ) );
                if ( m + 1 == members.size() ) {
                    if ( next != nullptr ) { ( void )KeyValue_Remove( store->store.pDocument, at, next ); }
                    ( void )SetMember( store->store.pDocument, at, View( member ), pOverride );
                    break;
                }
                if ( next == nullptr ) { next = KeyValue_ObjectInsert( store->store.pDocument, at, View( member ), key_value_type_t::OBJECT ); }
                at = next;
            }
        }
    }
}

// The instance's prefab, copied into the map where the instance stands.
map_status_t CollapseOne( map_document_t *copy, const QByteArray &text, const map_instance_info_t &info, const key_value_t *pOverrides, std::vector<u64> &inserted )
{
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, copy->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    map_status_t status = MapPrefab_Insert( copy, View( text ), { 0, 0, 0 }, &ids );
    const span_t<const u64> span{ ids.pData, ids.nCount };
    if ( status == map_status_t::OK && ( info.scale.x != 1 || info.scale.y != 1 || info.scale.z != 1 ) ) { status = MapEdit_Scale( copy, span, info.scale, { 0, 0, 0 }, true ); }
    // Euler X, Y, Z = roll, pitch, yaw: Rz(yaw) Ry(pitch) Rx(roll), as the angles mean.
    if ( status == map_status_t::OK && ( info.angles.x != 0 || info.angles.y != 0 || info.angles.z != 0 ) ) {
        status = MapEdit_Rotate( copy, span, { info.angles.z, info.angles.x, info.angles.y }, { 0, 0, 0 }, true );
    }
    if ( status == map_status_t::OK ) { status = MapEdit_Translate( copy, span, info.origin, true ); }
    if ( status == map_status_t::OK ) {
        inserted.assign( ids.pData, ids.pData + ids.nCount );
        ApplyOverrides( copy, inserted, pOverrides );
    }
    Vector_Shutdown( &ids );
    return status;
}

QByteArray PrefabText( map_workspace_t *ws, const QString &source, QString *pProblem )
{
    const QString file = MapAssets_File( ws, source );
    QFile in( file );
    if ( file.isEmpty() || !in.open( QIODevice::ReadOnly ) ) {
        if ( pProblem != nullptr ) { *pProblem = QStringLiteral( "%1 is not in the content folders" ).arg( source ); }
        return {};
    }
    return in.readAll();
}

command_result_t Collapse( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( InstanceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        struct item_t { u64 id; map_instance_info_t info; QByteArray text; };
        struct job_t { std::vector<item_t> items; std::vector<u64> selected; } job;
        for ( u64 id : SelectedInstances( ws ) ) {
            item_t item{ id, {}, {} };
            ( void )MapInstances_Info( ws, id, &item.info );
            QString problem;
            item.text = PrefabText( ws, item.info.source, &problem );
            if ( item.text.isEmpty() ) { MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Collapse Instance: %1" ).arg( problem ) ); return command_result_t::FAILED; }
            job.items.push_back( std::move( item ) );
        }
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Collapse Instance", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            for ( const auto &item : j.items ) {
                const key_value_t *overrides = KeyValue_Find( MapDocument_FindObject( copy, item.id, nullptr ), StringView_FromCString( "overrides" ) );
                std::vector<u64> inserted;
                map_status_t status = CollapseOne( copy, item.text, item.info, overrides, inserted );
                if ( status == map_status_t::OK ) { status = MapDocument_RemoveObject( copy, item.id ); }
                if ( status != map_status_t::OK ) { return status; }
                for ( u64 id : inserted ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
            }
            ++copy->geometry.revision;
            *changed = true;
            return map_status_t::OK;
        }, &job );
        if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Collapsed %1 into ordinary objects" ).arg( job.items.size() == 1 ? QStringLiteral( "the instance" ) : QStringLiteral( "%1 instances" ).arg( job.items.size() ) ) ); }
        return Result( done );
    } catch ( ... ) { return command_result_t::FAILED; }
}

// Edit Prefab in place: the instance becomes a group of ordinary objects;
// Finish writes the group back to the prefab file.
struct edit_session_t {
    u64 group{ 0 };
    map_instance_info_t info{};
    u64 instance{ 0 };
    QByteArray layer{};
    std::shared_ptr<key_value_document_t> overrides{};
    bool bPending{ false };     // Admission allocated, map transition not yet published.
    bool bSavedToFile{ false }; // A successful file save is independent of map undo.
};
QHash<const map_workspace_t *, edit_session_t> &Sessions()
{
    static QHash<const map_workspace_t *, edit_session_t> sessions;
    return sessions;
}

u32 EditState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return InstanceState( pContext ) == COMMAND_STATE_ENABLED && SelectedInstances( ws ).size() == 1 && !Sessions().contains( ws ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t EditPrefab( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( EditState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        const u64 id = SelectedInstances( ws ).front();
        edit_session_t session{};
        session.instance = id;
        ( void )MapInstances_Info( ws, id, &session.info );
        QString problem;
        const QByteArray text = PrefabText( ws, session.info.source, &problem );
        if ( text.isEmpty() ) { MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Edit Prefab: %1" ).arg( problem ) ); return command_result_t::FAILED; }
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        session.layer = QByteArray( object != nullptr && object->iLayer < ws->pDocument->layers.nCount ? ws->pDocument->layers.pData[object->iLayer].id : "" );
        // Keep the overrides to put them back on the instance afterwards.
        // Refuse before changing the map if that capture cannot be retained.
        if ( const key_value_t *overrides = KeyValue_Find( MapDocument_FindObject( ws->pDocument, id, nullptr ), StringView_FromCString( "overrides" ) ) ) {
            key_value_document_desc_t desc{}; desc.pAllocator = ws->pGui->pAllocator;
            key_value_document_t *pDocument = KeyValue_CreateDocument( desc );
            if ( pDocument != nullptr && KeyValue_SetRootType( pDocument, key_value_type_t::OBJECT ) && KeyValue_CloneInto( pDocument, KeyValue_Root( pDocument ), overrides ) != nullptr ) {
                session.overrides = std::shared_ptr<key_value_document_t>( pDocument, []( key_value_document_t *p ) { KeyValue_DestroyDocument( p ); } );
            } else {
                KeyValue_DestroyDocument( pDocument );
                MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "Edit Prefab: not enough memory to preserve the instance overrides; the map has not changed" ) );
                return command_result_t::FAILED;
            }
        }
        struct job_t { u64 id; QByteArray text; map_instance_info_t info; QString name; u64 group; } job{ id, text, session.info, QStringLiteral( "Editing %1" ).arg( session.info.source ), 0 };
        const QString notice = QStringLiteral( "Editing %1 in place: change the group's objects (Add to Group takes new ones in), then Finish Editing Prefab saves it and every instance follows" ).arg( session.info.source );
        // Allocate the session entry before the map/history can publish. Keep
        // it inactive until publication succeeds; observers cannot finish a
        // half-prepared session during the edit's synchronous notifications.
        session.bPending = true;
        Sessions().insert( ws, session );
        struct admission_t {
            const map_workspace_t *workspace;
            bool committed{ false };
            ~admission_t() { if ( !committed ) { Sessions().remove( workspace ); } }
        } admission{ ws };
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Edit Prefab", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            try {
            auto &j = *static_cast<job_t *>( context );
            std::vector<u64> inserted;
            // Overrides stay with the instance: the prefab is edited as it is.
            map_status_t status = CollapseOne( copy, j.text, j.info, nullptr, inserted );
            if ( status == map_status_t::OK ) { status = MapDocument_RemoveObject( copy, j.id ); }
            const QByteArray name = j.name.left( 120 ).toUtf8();
            if ( status == map_status_t::OK ) { status = MapDocument_AddGroup( copy, View( name ), { inserted.data(), inserted.size() }, &j.group ); }
            if ( status != map_status_t::OK ) { return status; }
            for ( u64 member : inserted ) { if ( !Vector_PushBack( out, member ) ) { return map_status_t::OUT_OF_MEMORY; } }
            ++copy->geometry.revision;
            *changed = true;
            return map_status_t::OK;
            } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
        }, &job );
        if ( !done ) { return command_result_t::FAILED; }
        auto current = Sessions().find( ws );
        if ( current == Sessions().end() ) { return command_result_t::FAILED; }
        current->group = job.group;
        current->bPending = false;
        admission.committed = true;
        MapWorkspace_Notify( ws, MAP_CHANGE_VIEW ); // Enable Finish/Cancel after admission.
        MapNotice_Post( ws, map_notice_t::INFO, notice );
        return command_result_t::OK;
    } catch ( ... ) { return command_result_t::FAILED; }
}

u32 FinishState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    const auto session = Sessions().constFind( ws );
    return Ready( ws ) && session != Sessions().cend() && !session->bPending ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

void FinishFailed( map_workspace_t *ws )
{
    const auto session = Sessions().constFind( ws );
    if ( session != Sessions().cend() && session->bSavedToFile ) {
        MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "The prefab was saved on disk, but its editing group could not be replaced in the map. The group is still here. Retry Finish Editing Prefab, or Cancel to restore the instance using the saved file; Cancel does not undo the file save." ) );
    } else {
        MapNotice_Post( ws, map_notice_t::ERROR, QStringLiteral( "The prefab edit could not be completed; the editing group and prefab file have not changed" ) );
    }
}

template <bool kSave>
command_result_t FinishEdit( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( FinishState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        const edit_session_t session = Sessions().value( ws );
        const QString completed = kSave ? QStringLiteral( "Saved %1; every instance of it now shows the change" ).arg( session.info.source )
            : ( session.bSavedToFile ? QStringLiteral( "Stopped editing %1; previously saved prefab changes remain on disk" )
                                    : QStringLiteral( "Stopped editing %1; the prefab is unchanged" ) ).arg( session.info.source );
        struct members_t { vector_t<u64> value{}; ~members_t() { Vector_Shutdown( &value ); } } members;
        if ( !Vector_Init( &members.value, ws->pGui->pAllocator ) ) { FinishFailed( ws ); return command_result_t::FAILED; }
        // Deleting every member dissolves the group: then there is nothing to keep.
        const auto listed = MapDocument_GroupMembers( ws->pDocument, session.group, &members.value );
        if ( listed != map_status_t::OK && listed != map_status_t::UNKNOWN_OBJECT ) { FinishFailed( ws ); return command_result_t::FAILED; }
        std::vector<u64> ids;
        if ( members.value.nCount != 0u ) { ids.assign( members.value.pData, members.value.pData + members.value.nCount ); }
        if ( kSave ) {
            if ( ids.empty() ) { MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Finish Editing Prefab: the group is empty; a prefab needs something in it" ) ); return command_result_t::FAILED; }
            // Back into the prefab's own space: undo the placement on a copy.
            map_document_t *raw = nullptr;
            map_status_t status = MapEdit_Clone( ws->pDocument, &raw );
            std::unique_ptr<map_document_t> copy( raw );
            const span_t<const u64> span{ ids.data(), ids.size() };
            if ( status == map_status_t::OK ) { status = MapEdit_Translate( copy.get(), span, { -session.info.origin.x, -session.info.origin.y, -session.info.origin.z }, true ); }
            const vec3 a = session.info.angles;
            // The inverse of Rz(yaw) Ry(pitch) Rx(roll), one axis at a time.
            if ( status == map_status_t::OK && a.y != 0 ) { status = MapEdit_Rotate( copy.get(), span, { 0, 0, -a.y }, { 0, 0, 0 }, true ); }
            if ( status == map_status_t::OK && a.x != 0 ) { status = MapEdit_Rotate( copy.get(), span, { 0, -a.x, 0 }, { 0, 0, 0 }, true ); }
            if ( status == map_status_t::OK && a.z != 0 ) { status = MapEdit_Rotate( copy.get(), span, { -a.z, 0, 0 }, { 0, 0, 0 }, true ); }
            const vec3 s = session.info.scale;
            if ( status == map_status_t::OK && ( s.x != 1 || s.y != 1 || s.z != 1 ) ) { status = MapEdit_Scale( copy.get(), span, { 1.0 / s.x, 1.0 / s.y, 1.0 / s.z }, { 0, 0, 0 }, true ); }
            struct text_t { text_buffer_t value{}; ~text_t() { TextBuffer_Shutdown( &value ); } } text;
            const QByteArray name = QFileInfo( session.info.source ).completeBaseName().left( 128 ).toUtf8();
            if ( status == map_status_t::OK ) { status = TextBuffer_Init( &text.value, ws->pGui->pAllocator, 0 ) ? MapPrefab_WriteAt( copy.get(), span, View( name ), { 0, 0, 0 }, &text.value ) : map_status_t::OUT_OF_MEMORY; }
            bool saved = false;
            const QString file = MapAssets_File( ws, session.info.source );
            if ( status == map_status_t::OK && !file.isEmpty() ) {
                QSaveFile out( file );
                const string_view_t view = TextBuffer_View( &text.value );
                saved = out.open( QIODevice::WriteOnly ) && out.write( view.pData, static_cast<qint64>( view.cchLength ) ) == static_cast<qint64>( view.cchLength ) && out.commit();
                // File replacement and the map/history transition are separate
                // publications. Retain this fact before any later allocation,
                // so a failed transition or Cancel never claims to undo it.
                if ( saved ) { Sessions().find( ws )->bSavedToFile = true; }
            }
            if ( !saved ) {
                MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Finish Editing Prefab: %1 could not be written; the group is still here" ).arg( file.isEmpty() ? session.info.source : file ) );
                return command_result_t::FAILED;
            }
        }
        // The group leaves and the instance comes back where it was.
        struct job_t { const std::vector<u64> &ids; const edit_session_t &session; } job{ ids, session };
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, kSave ? "Finish Editing Prefab" : "Cancel Editing Prefab", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            try {
            auto &j = *static_cast<job_t *>( context );
            // Deleting the members dissolves their group (groups are pruned at publication).
            map_status_t status = j.ids.empty() ? map_status_t::OK : MapEdit_Delete( copy, { j.ids.data(), j.ids.size() } );
            const key_value_t *overrides = j.session.overrides != nullptr ? KeyValue_FirstChild( KeyValue_Root( j.session.overrides.get() ) ) : nullptr;
            if ( status == map_status_t::OK ) {
                status = AddInstance( copy, View( j.session.layer ), j.session.info.source, j.session.info.origin, j.session.info.angles, j.session.info.scale, j.session.instance, overrides );
            }
            ++copy->geometry.revision;
            *changed = status == map_status_t::OK;
            return status == map_status_t::OK && !Vector_PushBack( out, j.session.instance ) ? map_status_t::OUT_OF_MEMORY : status;
            } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
        }, &job );
        if ( !done ) { FinishFailed( ws ); return command_result_t::FAILED; }
        Sessions().remove( ws );
        MapInstances_ReloadFiles( ws );
        MapNotice_Post( ws, map_notice_t::SUCCESS, completed );
        return command_result_t::OK;
    } catch ( ... ) { FinishFailed( ws ); return command_result_t::FAILED; }
}

// Place Instance: an instance of an existing prefab file at the map cursor
// (where the pointer last was in a 2D view), in the current layer.
u32 PlaceState( void *pContext ) noexcept
{
    const auto *ws = Ws( pContext );
    return Ready( ws ) ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

command_result_t PlaceInstance( void *pContext, const command_args_t &args ) noexcept
{
    auto *ws = Ws( pContext );
    if ( PlaceState( pContext ) == COMMAND_STATE_NONE ) { return command_result_t::DISABLED; }
    try {
        QString source;
        if ( args.nArgs >= 1 ) { source = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ); }
        else if ( QApplication::instance() != nullptr && !MapAssets_Roots( ws ).isEmpty() ) {
            const QString file = QFileDialog::getOpenFileName( QApplication::activeWindow(), QStringLiteral( "Place Instance" ),
                QDir( MapAssets_Roots( ws ).front() ).filePath( QStringLiteral( "prefabs" ) ), QStringLiteral( "Prefabs (*.cyprefab)" ) );
            if ( file.isEmpty() ) { return command_result_t::OK; } // Cancelled.
            source = MapAssets_VirtualPath( ws, file );
            if ( source.isEmpty() ) {
                MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Place Instance: the prefab must be inside a content folder" ) );
                return command_result_t::FAILED;
            }
        }
        if ( source.isEmpty() || MapAssets_File( ws, source ).isEmpty() ) {
            MapNotice_Post( ws, map_notice_t::WARNING, QStringLiteral( "Place Instance: %1 is not in the content folders" ).arg( source.isEmpty() ? QStringLiteral( "no prefab" ) : source ) );
            return command_result_t::FAILED;
        }
        vec3 at = ws->cursor;
        const f64 grid = ws->bSnapToGrid ? static_cast<f64>( ws->gridSize ) : 0.0;
        if ( grid > 0.0 ) { at = { std::round( at.x / grid ) * grid, std::round( at.y / grid ) * grid, std::round( at.z / grid ) * grid }; }
        struct job_t { QString source; vec3 at; u64 id; } job{ source, at, 0 };
        const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Place Instance", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            j.id = MapDocument_AllocateId( copy );
            const map_status_t status = AddInstance( copy, MapDocument_CreationLayer( copy ), j.source, j.at, {}, { 1, 1, 1 }, j.id, nullptr );
            *changed = status == map_status_t::OK;
            return status == map_status_t::OK && !Vector_PushBack( out, j.id ) ? map_status_t::OUT_OF_MEMORY : status;
        }, &job );
        if ( done ) { MapNotice_Post( ws, map_notice_t::SUCCESS, QStringLiteral( "Placed an instance of %1" ).arg( source ) ); }
        return Result( done );
    } catch ( ... ) { return command_result_t::FAILED; }
}

command_result_t Reload( void *pContext, const command_args_t & ) noexcept
{
    auto *ws = Ws( pContext );
    if ( ws == nullptr || ws->pDocument == nullptr ) { return command_result_t::DISABLED; }
    MapInstances_ReloadFiles( ws );
    MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "Prefab files read again" ) );
    return command_result_t::OK;
}
} // namespace

math::affine3d_t MapInstances_Transform( vec3 origin, vec3 angles, vec3 scale ) noexcept
{
    const auto sinCos = []( f64 degrees, f64 &sine, f64 &cosine ) noexcept {
        // Match Core prefab-scale admission: exact authored quarters only.
        // Near-quarter values remain real rotations, however small the angle.
        const f64 turn = std::remainder( degrees, 360.0 );
        if ( turn == 0 ) { sine = 0; cosine = 1; }
        else if ( turn == 90 ) { sine = 1; cosine = 0; }
        else if ( turn == -90 ) { sine = -1; cosine = 0; }
        else if ( turn == 180 || turn == -180 ) { sine = 0; cosine = -1; }
        else {
            constexpr f64 rad = 3.14159265358979323846 / 180.0;
            sine = std::sin( degrees * rad ); cosine = std::cos( degrees * rad );
        }
    };
    f64 cp{}, sp{}, cy{}, sy{}, cr{}, sr{};
    sinCos( angles.x, sp, cp ); sinCos( angles.y, sy, cy ); sinCos( angles.z, sr, cr );
    // Rz(yaw) Ry(pitch) Rx(roll), then the scale on each column.
    const vec3 c0{ cy * cp * scale.x, sy * cp * scale.x, -sp * scale.x };
    const vec3 c1{ ( cy * sp * sr - sy * cr ) * scale.y, ( sy * sp * sr + cy * cr ) * scale.y, cp * sr * scale.y };
    const vec3 c2{ ( cy * sp * cr + sy * sr ) * scale.z, ( sy * sp * cr - cy * sr ) * scale.z, cp * cr * scale.z };
    return math::Affine3d_FromColumns( c0, c1, c2, origin );
}

void MapInstances_Refresh( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return; }
    try {
        state_t &state = States()[ws];
        state.instances.clear();
        const map_document_t &map = *ws->pDocument;
        for ( usize iStore = 0; iStore < map.layerStores.nCount; ++iStore ) {
            const map_layer_store_t *store = map.layerStores.pData[iStore];
            const key_value_t *prefabs = Member( KeyValue_Root( static_cast<const key_value_document_t *>( store->store.pDocument ) ), "prefabs" );
            for ( const key_value_t *record = KeyValue_FirstChild( prefabs ); record != nullptr; record = KeyValue_NextSibling( record ) ) {
                instance_t instance{};
                if ( !KeyValue_GetU64( Member( record, "id" ), &instance.id ) || instance.id == 0 ||
                     !Vector( Member( record, "origin" ), &instance.info.origin ) ) { continue; }
                instance.iLayer = LayerIndexOf( map, store->layer );
                instance.info.source = Text( Member( record, "source" ) );
                instance.info.name = Text( Member( record, "name" ) );
                ( void )Vector( Member( record, "angles" ), &instance.info.angles );
                f64 uniform = 1.0;
                if ( Number( Member( record, "scale" ), &uniform ) ) { instance.info.scale = { uniform, uniform, uniform }; }
                else { ( void )Vector( Member( record, "scale" ), &instance.info.scale ); }
                const auto prefab = instance.info.source.isEmpty() ? nullptr : Load( ws, state, instance.info.source );
                instance.info.bLoaded = prefab != nullptr && prefab->wire != nullptr;
                instance.info.problem = prefab == nullptr ? QStringLiteral( "The instance names no prefab file" ) : prefab->problem;
                Append( ws, instance, instance.info.bLoaded ? prefab.get() : nullptr );
                state.instances.insert( instance.id, instance );
            }
        }
    } catch ( ... ) {
        CY_LOG_WRITE( Warning, Editor, "Prefab instances could not be shown" );
    }
}

void MapInstances_Forget( const map_workspace_t *ws ) noexcept { States().remove( ws ); Sessions().remove( ws ); }

map_status_t MapInstances_CollapseAll( map_workspace_t *ws, map_document_t *pCopy, QStringList *pProblems )
{
    if ( ws == nullptr || pCopy == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    try {
        const auto state = States().constFind( ws );
        if ( state == States().constEnd() ) { return map_status_t::OK; }
        for ( auto it = state.value().instances.cbegin(); it != state.value().instances.cend(); ++it ) {
            QString problem;
            const QByteArray text = PrefabText( ws, it.value().info.source, &problem );
            if ( text.isEmpty() ) {
                if ( pProblems != nullptr ) { pProblems->append( QStringLiteral( "instance #%1: %2" ).arg( it.key() ).arg( problem ) ); }
                ( void )MapDocument_RemoveObject( pCopy, it.key() );
                continue;
            }
            const key_value_t *overrides = KeyValue_Find( MapDocument_FindObject( pCopy, it.key(), nullptr ), StringView_FromCString( "overrides" ) );
            std::vector<u64> inserted;
            map_status_t status = CollapseOne( pCopy, text, it.value().info, overrides, inserted );
            if ( status == map_status_t::OK ) { status = MapDocument_RemoveObject( pCopy, it.key() ); }
            if ( status != map_status_t::OK ) { return status; }
        }
        return map_status_t::OK;
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

void MapInstances_ReloadFiles( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return; }
    States()[ws].files.clear();
    MapWorkspace_DocumentChanged( ws ); // Rebuilds the views with the files read again.
}

bool MapInstances_Info( const map_workspace_t *ws, u64 instance, map_instance_info_t *pInfoOut )
{
    const auto state = States().constFind( ws );
    if ( state == States().constEnd() ) { return false; }
    const auto it = state.value().instances.constFind( instance );
    if ( it == state.value().instances.constEnd() ) { return false; }
    if ( pInfoOut != nullptr ) { *pInfoOut = it.value().info; }
    return true;
}

bool MapInstances_IsInstance( const map_workspace_t *ws, u64 id ) noexcept
{
    const auto state = States().constFind( ws );
    return state != States().constEnd() && state.value().instances.contains( id );
}

f64 MapInstances_Raycast( const map_workspace_t *ws, u64 instance, vec3 origin, vec3 direction ) noexcept
{
    const auto state = States().constFind( ws );
    if ( state == States().constEnd() ) { return -1.0; }
    const auto it = state.value().instances.constFind( instance );
    if ( it == state.value().instances.constEnd() ) { return -1.0; }
    const map_wireframe_t &wire = ws->wire;
    f64 nearest = -1.0;
    for ( u32 f = it.value().iFirstFace; f < it.value().iFirstFace + it.value().nFaces && f < wire.faces.nCount; ++f ) {
        const map_wire_face_t &face = wire.faces.pData[f];
        if ( face.nIndices < 3 ) { continue; }
        const vec3 p0 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
        const f64 denominator = face.normal.x * direction.x + face.normal.y * direction.y + face.normal.z * direction.z;
        if ( std::fabs( denominator ) < 1e-12 ) { continue; }
        const f64 t = ( face.normal.x * ( p0.x - origin.x ) + face.normal.y * ( p0.y - origin.y ) + face.normal.z * ( p0.z - origin.z ) ) / denominator;
        if ( t <= 0.0 || ( nearest >= 0.0 && t >= nearest ) ) { continue; }
        const vec3 hit{ origin.x + direction.x * t, origin.y + direction.y * t, origin.z + direction.z * t };
        const f64 ax = std::fabs( face.normal.x ), ay = std::fabs( face.normal.y ), az = std::fabs( face.normal.z );
        const int drop = az >= ax && az >= ay ? 2 : ax >= ay ? 0 : 1;
        const auto comp = []( vec3 p, int axis ) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
        const int i0 = drop == 0 ? 1 : 0, i1 = drop == 2 ? 1 : 2;
        bool inside = false;
        for ( u32 k = 0, j = face.nIndices - 1; k < face.nIndices; j = k++ ) {
            const vec3 a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
            const vec3 b = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + j]];
            if ( ( comp( a, i1 ) > comp( hit, i1 ) ) != ( comp( b, i1 ) > comp( hit, i1 ) ) &&
                 comp( hit, i0 ) < ( comp( b, i0 ) - comp( a, i0 ) ) * ( comp( hit, i1 ) - comp( a, i1 ) ) / ( comp( b, i1 ) - comp( a, i1 ) ) + comp( a, i0 ) ) { inside = !inside; }
        }
        if ( inside ) { nearest = t; }
    }
    return nearest;
}

command_registry_status_t MapInstances_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept
{
    CY_ASSERT( pWorkspace != nullptr && pRegistry != nullptr );
    constexpr u32 kEdit = COMMAND_FLAG_EDITS_DOCUMENT;
    const command_desc_t commands[]{
        { "map.prefab.create_instance", "Create Instance from Selection...", "Save the selection as a prefab file in a content folder and put an instance of it in its place (Hammer).", "asset-folder",
          "map.prefab.create_instance [file]", kEdit, CreateInstance, CreateState, pWorkspace },
        { "map.prefab.place_instance", "Place Instance...", "Place an instance of a prefab file at the map cursor, in the current layer (Hammer).", "asset-folder",
          "map.prefab.place_instance [file]", kEdit, PlaceInstance, PlaceState, pWorkspace },
        { "map.prefab.collapse", "Collapse Instance", "Copy the selected instances' prefabs into the map as ordinary objects (their overrides applied), and remove the instances.", "asset-folder", nullptr,
          kEdit, Collapse, InstanceState, pWorkspace },
        { "map.prefab.edit", "Edit Prefab", "Edit the selected instance's prefab in place: it becomes a group of ordinary objects until Finish Editing Prefab saves the prefab.", "asset-folder", nullptr,
          kEdit, EditPrefab, EditState, pWorkspace },
        { "map.prefab.finish_edit", "Finish Editing Prefab", "Save the prefab being edited from its group, and put the instance back: every instance of the prefab follows.", "file-save", nullptr,
          kEdit, FinishEdit<true>, FinishState, pWorkspace },
        { "map.prefab.cancel_edit", "Cancel Editing Prefab", "Throw away the changes to the prefab being edited and put the instance back.", "edit-undo", nullptr,
          kEdit, FinishEdit<false>, FinishState, pWorkspace },
        { "map.prefab.reload", "Reload Prefabs", "Read every prefab file again (after one changed outside Mason).", "assets-refresh", nullptr,
          COMMAND_FLAG_NONE, Reload, nullptr, pWorkspace },
    };
    return EditorCommands_Register( pRegistry, commands, std::size( commands ) );
}

} // namespace cypher::editor::map
