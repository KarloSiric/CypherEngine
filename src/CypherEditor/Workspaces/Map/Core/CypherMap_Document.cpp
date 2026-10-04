//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Document.cpp
//  Purpose: Implements the map document: loading and saving `.cymap` roots
//           and `.cymapchunk` chunks, identity (missing, duplicate, and
//           raised IDs), geometry transfer through the readable codec,
//           placement of every object kind, and object operations.
//  Details: Saving never edits chunk trees in place. It decides every
//           object's target chunk first, builds fresh output trees in the
//           canonical member order, checks every limit and every damaged
//           path, and only then hands texts to the sink - so a refused save
//           writes nothing. Chunks are written with the compact CYKV layout
//           (bare keys, shortest reals, one-line short containers) because
//           people read and diff these files (CYMAP.md section 3).
//
//           Loading processes chunks in path order so that IDs assigned to
//           hand-added objects are the same on every machine.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Reworked on 2026-09-27 for map-owned readable geometry, nested brush
//    entities, shapes, foliage, notes, selection sets, info annotations,
//    and hand-editing identity rules
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Document.h"

#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Fragment.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_SourceIdRegistry.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"
#include "CypherCommon/Tier1/CypherCommon_Sort.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace cypher::editor::map
{

using namespace cypher::common;
namespace geo = cypher::editor::geometry;

namespace
{

// ---------------------------------------------------------------------------
// Sections and member orders (CYMAP.md sections 4-6)
// ---------------------------------------------------------------------------

enum section_t : u32 {
    SEC_ENTITIES = 0u,
    SEC_BRUSHES,
    SEC_MESHES,
    SEC_PATCHES,
    SEC_TERRAINS,
    SEC_SHAPES,
    SEC_FOLIAGE,
    SEC_NOTES,
    SEC_GROUPS,
    SEC_PREFABS,
    SEC_COUNT
};
constexpr const char *kSections[SEC_COUNT]{ "entities", "brushes", "meshes", "patches", "terrains",
                                            "shapes", "foliage", "notes", "groups", "prefabs" };

constexpr bool IsGeometrySection( u32 section ) noexcept { return section >= SEC_BRUSHES && section <= SEC_TERRAINS; }
constexpr map_geometry_kind_t KindOfSection( u32 section ) noexcept { return static_cast<map_geometry_kind_t>( section - SEC_BRUSHES ); }
constexpr u32 SectionOfKind( map_geometry_kind_t kind ) noexcept { return SEC_BRUSHES + static_cast<u32>( kind ); }

// Kinds an entity can own; terrains are always world geometry.
constexpr map_geometry_kind_t kOwnedKinds[]{ map_geometry_kind_t::BRUSH, map_geometry_kind_t::MESH, map_geometry_kind_t::PATCH };

constexpr const char *kRootOrder[]{ "map_id", "name", "game", "game_version", "next_id", "description", "authors", "tags",
                                    "thumbnail", "units_per_meter", "cell_size", "layers", "visgroups", "selection_sets",
                                    "cordons", "settings" };
constexpr const char *kLayerOrder[]{ "id", "name", "color", "editor_only" };
constexpr const char *kVisgroupOrder[]{ "id", "name", "color", "parent" };
constexpr const char *kSelectionSetOrder[]{ "id", "name", "members" };
constexpr const char *kCordonOrder[]{ "name", "bounds" };
constexpr const char *kChunkOrder[]{ "map_id", "layer", "cell", "info", "entities", "brushes", "meshes", "patches",
                                     "terrains", "shapes", "foliage", "notes", "groups", "prefabs" };
constexpr const char *kEntityOrder[]{ "id", "class", "name", "origin", "angles", "scale", "parent", "comment", "visgroups",
                                      "info", "properties", "outputs", "brushes", "meshes", "patches", "editor" };
constexpr const char *kOutputOrder[]{ "output", "target", "input", "parameter", "delay", "times" };
constexpr const char *kGeometryLead[]{ "id", "name", "comment", "visgroups", "info" };
constexpr const char *kGeometryTail[]{ "modifiers", "paint" };
constexpr const char *kShapeOrder[]{ "id", "name", "comment", "visgroups", "info", "type", "closed", "points", "handles", "widths" };
constexpr const char *kFoliageOrder[]{ "model", "instances", "properties" };
constexpr const char *kNoteOrder[]{ "id", "position", "text", "color", "size", "visgroups" };
constexpr const char *kGroupOrder[]{ "id", "name", "comment", "visgroups", "members" };
constexpr const char *kPrefabOrder[]{ "id", "name", "comment", "visgroups", "info", "source", "origin", "angles", "scale", "overrides" };

struct order_t {
    const char *const *pNames;
    usize nNames;
};

template <usize nNames>
constexpr order_t Order( const char *const ( &names )[nNames] ) noexcept
{
    return { names, nNames };
}

CYPHER_NODISCARD order_t SectionOrder( u32 section ) noexcept
{
    switch ( section ) {
        case SEC_SHAPES: return Order( kShapeOrder );
        case SEC_FOLIAGE: return Order( kFoliageOrder );
        case SEC_NOTES: return Order( kNoteOrder );
        case SEC_GROUPS: return Order( kGroupOrder );
        case SEC_PREFABS: return Order( kPrefabOrder );
        default: return Order( kEntityOrder );
    }
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

CYPHER_NODISCARD string_view_t SV( const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

template <usize nCapacity>
CYPHER_NODISCARD bool_t CopyText( char ( &destination )[nCapacity], string_view_t text ) noexcept
{
    if ( text.cchLength >= nCapacity ) { return CY_FALSE; }
    if ( text.cchLength != 0u ) { std::memcpy( destination, text.pData, text.cchLength ); }
    destination[text.cchLength] = '\0';
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsStableId( string_view_t text, usize cchMax ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckStableIdentifier( text, cchMax ) );
}

// IDs are u64; a hand-typed "12" (signed) is accepted when positive.
CYPHER_NODISCARD bool_t ReadId( const key_value_t *pValue, u64 &idOut ) noexcept
{
    i64 nSigned = 0;
    if ( KeyValue_GetU64( pValue, &idOut ) ) { return idOut != 0u; }
    if ( KeyValue_GetI64( pValue, &nSigned ) && nSigned > 0 ) {
        idOut = static_cast<u64>( nSigned );
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadNumber( const key_value_t *pValue, f64 &valueOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, &valueOut ) ) { return std::isfinite( valueOut ); }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { valueOut = static_cast<f64>( nSigned ); return CY_TRUE; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { valueOut = static_cast<f64>( nUnsigned ); return CY_TRUE; }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadVec3( const key_value_t *pValue, math::vec3d_t &valueOut ) noexcept
{
    if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pValue ) != 3u ) { return CY_FALSE; }
    f64 components[3]{};
    for ( usize i = 0u; i < 3u; ++i ) {
        if ( !ReadNumber( KeyValue_ChildAt( pValue, i ), components[i] ) ) { return CY_FALSE; }
    }
    valueOut = math::Vec3d_Make( components[0], components[1], components[2] );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsListed( string_view_t name, order_t order ) noexcept
{
    for ( usize i = 0u; i < order.nNames; ++i ) {
        if ( StringView_Equals( name, SV( order.pNames[i] ) ) ) { return CY_TRUE; }
    }
    return CY_FALSE;
}

// Deep value equality (names, types, scalars, children in order).
CYPHER_NODISCARD bool_t KeyValuesEqual( const key_value_t *pA, const key_value_t *pB ) noexcept
{
    const key_value_type_t type = KeyValue_Type( pA );
    if ( type != KeyValue_Type( pB ) || !StringView_Equals( KeyValue_Name( pA ), KeyValue_Name( pB ) ) ) { return CY_FALSE; }
    switch ( type ) {
        case key_value_type_t::NULL_VALUE: return CY_TRUE;
        case key_value_type_t::BOOL: { bool_t a{}, b{}; return KeyValue_GetBool( pA, &a ) && KeyValue_GetBool( pB, &b ) && a == b; }
        case key_value_type_t::I64: { i64 a{}, b{}; return KeyValue_GetI64( pA, &a ) && KeyValue_GetI64( pB, &b ) && a == b; }
        case key_value_type_t::U64: { u64 a{}, b{}; return KeyValue_GetU64( pA, &a ) && KeyValue_GetU64( pB, &b ) && a == b; }
        case key_value_type_t::F64: {
            f64 a{}, b{};
            return KeyValue_GetF64( pA, &a ) && KeyValue_GetF64( pB, &b ) && std::memcmp( &a, &b, sizeof( f64 ) ) == 0;
        }
        case key_value_type_t::STRING: {
            string_view_t a{}, b{};
            return KeyValue_GetString( pA, &a ) && KeyValue_GetString( pB, &b ) && StringView_Equals( a, b );
        }
        case key_value_type_t::BINARY: {
            binary_block_t a{}, b{};
            return KeyValue_GetBinary( pA, &a ) && KeyValue_GetBinary( pB, &b ) && a.cbSize == b.cbSize &&
                   ( a.cbSize == 0u || std::memcmp( a.pData, b.pData, a.cbSize ) == 0 );
        }
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY: {
            const usize nChildren = KeyValue_ChildCount( pA );
            if ( nChildren != KeyValue_ChildCount( pB ) ) { return CY_FALSE; }
            for ( usize i = 0u; i < nChildren; ++i ) {
                if ( !KeyValuesEqual( KeyValue_ChildAt( pA, i ), KeyValue_ChildAt( pB, i ) ) ) { return CY_FALSE; }
            }
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD key_value_t *EnsureMember( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, key_value_type_t type ) noexcept
{
    key_value_t *pNode = KeyValue_Find( pObject, SV( pName ) );
    return pNode != nullptr ? pNode : KeyValue_ObjectInsert( pDocument, pObject, SV( pName ), type );
}

CYPHER_NODISCARD bool_t SetStringMember( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, string_view_t value ) noexcept
{
    key_value_t *pNode = EnsureMember( pDocument, pObject, pName, key_value_type_t::NULL_VALUE );
    return pNode != nullptr && KeyValue_SetString( pDocument, pNode, value );
}

CYPHER_NODISCARD bool_t SetU64Member( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, u64 value ) noexcept
{
    key_value_t *pNode = EnsureMember( pDocument, pObject, pName, key_value_type_t::NULL_VALUE );
    return pNode != nullptr && KeyValue_SetU64( pDocument, pNode, value );
}

CYPHER_NODISCARD bool_t SetI64Member( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, i64 value ) noexcept
{
    key_value_t *pNode = EnsureMember( pDocument, pObject, pName, key_value_type_t::NULL_VALUE );
    return pNode != nullptr && KeyValue_SetI64( pDocument, pNode, value );
}

CYPHER_NODISCARD bool_t SetRealMember( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, f64 value ) noexcept
{
    key_value_t *pNode = EnsureMember( pDocument, pObject, pName, key_value_type_t::NULL_VALUE );
    return pNode != nullptr && KeyValue_SetF64( pDocument, pNode, MapReal( value ) );
}

CYPHER_NODISCARD bool_t SetRealsMember( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, const f64 *pValues, usize nValues ) noexcept
{
    key_value_t *pNode = EnsureMember( pDocument, pObject, pName, key_value_type_t::ARRAY );
    if ( pNode == nullptr || !KeyValue_SetContainerType( pDocument, pNode, key_value_type_t::ARRAY ) ) { return CY_FALSE; }
    for ( usize i = 0u; i < nValues; ++i ) {
        key_value_t *pValue = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::NULL_VALUE );
        if ( pValue == nullptr || !KeyValue_SetF64( pDocument, pValue, MapReal( pValues[i] ) ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t SetVec3Member( key_value_document_t *pDocument, key_value_t *pObject, const char *pName, math::vec3d_t value ) noexcept
{
    const f64 values[3]{ value.x, value.y, value.z };
    return SetRealsMember( pDocument, pObject, pName, values, 3u );
}

CYPHER_NODISCARD bool_t Clone( key_value_document_t *pDocument, key_value_t *pParent, const key_value_t *pSource ) noexcept
{
    return KeyValue_CloneInto( pDocument, pParent, pSource ) != nullptr;
}

CYPHER_NODISCARD settings_document_identity_t RootIdentity() noexcept
{
    return { SV( MAP_SCHEMA_ID ), MAP_SCHEMA_VERSION, MAP_SCHEMA_VERSION };
}

CYPHER_NODISCARD settings_document_identity_t ChunkIdentity() noexcept
{
    return { SV( MAP_CHUNK_SCHEMA_ID ), MAP_SCHEMA_VERSION, MAP_SCHEMA_VERSION };
}

void AddProblem( map_document_t *pMap, map_problem_code_t code, string_view_t path, const char *pMember, u64 id ) noexcept
{
    map_problem_t problem{};
    problem.code = code;
    const usize cchPath = path.cchLength < MAP_CHUNK_PATH_CAPACITY - 1u ? path.cchLength : MAP_CHUNK_PATH_CAPACITY - 1u;
    if ( cchPath != 0u ) { std::memcpy( problem.path, path.pData, cchPath ); }
    if ( pMember != nullptr ) { std::snprintf( problem.member, sizeof( problem.member ), "%s", pMember ); }
    problem.id = id;
    // Diagnostics are best effort: running out of memory while recording one
    // must not turn a readable map into an unreadable one.
    ( void )Vector_PushBack( &pMap->problems, problem );
}

// ---------------------------------------------------------------------------
// Layers and chunks
// ---------------------------------------------------------------------------

CYPHER_NODISCARD usize LayerIndex( const map_document_t *pMap, string_view_t layer ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &pMap->layers ); ++i ) {
        if ( StringView_Equals( SV( pMap->layers.pData[i].id ), layer ) ) { return i; }
    }
    return CY_INVALID_SIZE;
}

CYPHER_NODISCARD map_chunk_t *NewChunk( map_document_t *pMap, string_view_t layer, const map_cell_t &cell ) noexcept
{
    void *pMemory = Allocator_Allocate( pMap->pAllocator, sizeof( map_chunk_t ), alignof( map_chunk_t ) );
    if ( pMemory == nullptr ) { return nullptr; }
    map_chunk_t *pChunk = new ( pMemory ) map_chunk_t{};
    const bool_t bOk = CopyText( pChunk->layer, layer ) &&
                       SettingsDocument_Init( &pChunk->store, pMap->pAllocator, ChunkIdentity() ) == settings_document_status_t::OK;
    pChunk->cell = cell;
    if ( !bOk ) {
        pChunk->~map_chunk_t();
        Allocator_Free( pMap->pAllocator, pMemory, sizeof( map_chunk_t ), alignof( map_chunk_t ) );
        return nullptr;
    }
    return pChunk;
}

void DestroyChunk( const allocator_t *pAllocator, map_chunk_t *pChunk ) noexcept
{
    if ( pChunk == nullptr ) { return; }
    pChunk->~map_chunk_t();
    Allocator_Free( pAllocator, pChunk, sizeof( map_chunk_t ), alignof( map_chunk_t ) );
}

CYPHER_NODISCARD map_chunk_t *FindWorkingChunk( map_document_t *pMap, string_view_t layer, const map_cell_t &cell ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &pMap->chunks ); ++i ) {
        map_chunk_t *pChunk = pMap->chunks.pData[i];
        if ( !pChunk->bDamaged && StringView_Equals( SV( pChunk->layer ), layer ) && MapCell_Equals( pChunk->cell, cell ) ) { return pChunk; }
    }
    return nullptr;
}

CYPHER_NODISCARD map_chunk_t *GetOrAddWorkingChunk( map_document_t *pMap, string_view_t layer, const map_cell_t &cell ) noexcept
{
    map_chunk_t *pChunk = FindWorkingChunk( pMap, layer, cell );
    if ( pChunk != nullptr ) { return pChunk; }
    pChunk = NewChunk( pMap, layer, cell );
    if ( pChunk == nullptr ) { return nullptr; }
    if ( !Vector_PushBack( &pMap->chunks, pChunk ) ) {
        DestroyChunk( pMap->pAllocator, pChunk );
        return nullptr;
    }
    return pChunk;
}

usize FormatChunkPath( string_view_t layer, const map_cell_t &cell, char ( &buffer )[MAP_CHUNK_PATH_CAPACITY] ) noexcept
{
    char cellName[MAP_CELL_NAME_CAPACITY]{};
    MapCell_FormatName( cell, cellName );
    const int written = std::snprintf( buffer, MAP_CHUNK_PATH_CAPACITY, "%.*s/%s%s", static_cast<int>( layer.cchLength ),
                                       layer.pData, cellName, MAP_CHUNK_FILE_EXTENSION );
    return written > 0 ? static_cast<usize>( written ) : 0u;
}

// Writes a store with the compact layout people read and diff (CYMAP.md 3).
CYPHER_NODISCARD map_status_t WriteCompactText( const settings_document_t *pStore, const allocator_t *pAllocator, text_buffer_t *pText, usize cbMax ) noexcept
{
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE | KEY_VALUE_WRITE_FLAG_BARE_KEYS |
                    KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
    options.nIndentSpaces = 4u;
    options.nLineWidth = MAP_LINE_WIDTH;
    const key_value_t *pRoot = SettingsDocument_Root( pStore );
    const key_value_write_result_t measured = KeyValue_WriteText( pRoot, options, nullptr, 0u );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED && measured.status != key_value_write_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    if ( measured.cchRequired > cbMax ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( !TextBuffer_Init( pText, pAllocator, measured.cchRequired ) || !TextBuffer_Resize( pText, measured.cchRequired ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    const key_value_write_result_t written = KeyValue_WriteText( pRoot, options, TextBuffer_Data( pText ), TextBuffer_Capacity( pText ) + 1u );
    return written.status == key_value_write_status_t::OK && written.cchWritten == measured.cchRequired ? map_status_t::OK
                                                                                                       : map_status_t::OUT_OF_MEMORY;
}

// ---------------------------------------------------------------------------
// Geometry records
// ---------------------------------------------------------------------------

CYPHER_NODISCARD usize RecordIndex( const map_document_t *pMap, u64 id ) noexcept
{
    usize iLow = 0u;
    usize iHigh = Vector_Count( &pMap->geometryRecords );
    while ( iLow < iHigh ) {
        const usize iMid = iLow + ( iHigh - iLow ) / 2u;
        if ( pMap->geometryRecords.pData[iMid].id < id ) { iLow = iMid + 1u; } else { iHigh = iMid; }
    }
    return iLow < Vector_Count( &pMap->geometryRecords ) && pMap->geometryRecords.pData[iLow].id == id ? iLow : CY_INVALID_SIZE;
}

void SortRecords( map_document_t *pMap ) noexcept
{
    std::sort( pMap->geometryRecords.pData, pMap->geometryRecords.pData + Vector_Count( &pMap->geometryRecords ),
               []( const map_geometry_record_t &a, const map_geometry_record_t &b ) { return a.id < b.id; } );
}

CYPHER_NODISCARD bool_t UpsertRecord( map_document_t *pMap, const map_geometry_record_t &record ) noexcept
{
    const usize iRecord = RecordIndex( pMap, record.id );
    if ( iRecord != CY_INVALID_SIZE ) {
        pMap->geometryRecords.pData[iRecord] = record;
        return CY_TRUE;
    }
    if ( !Vector_PushBack( &pMap->geometryRecords, record ) ) { return CY_FALSE; }
    SortRecords( pMap );
    return CY_TRUE;
}

// Keeps geometry allocation above every map ID and the map's next ID above
// every geometry ID.
void SyncIds( map_document_t *pMap ) noexcept
{
    geo::geometry_source_id_allocator_t &allocator = pMap->geometry.sourceIds.allocator;
    if ( allocator.next.value > pMap->nextId ) { pMap->nextId = allocator.next.value; }
    if ( pMap->nextId > 1u ) {
        ( void )geo::GeometrySourceIdAllocator_AdvancePast( &allocator, geo::geometry_source_id_t{ pMap->nextId - 1u } );
    }
}

CYPHER_NODISCARD bool_t IsLiveGeometry( const map_document_t *pMap, u64 id, map_geometry_kind_t *pKindOut ) noexcept
{
    const geo::geometry_source_id_t sourceId{ id };
    map_geometry_kind_t kind = map_geometry_kind_t::COUNT;
    if ( geo::GeometryDocument_FindBrush( &pMap->geometry, sourceId ) != nullptr ) {
        kind = map_geometry_kind_t::BRUSH;
    } else if ( geo::GeometryDocument_FindMesh( &pMap->geometry, sourceId ) != nullptr ) {
        kind = map_geometry_kind_t::MESH;
    } else if ( geo::GeometryDocument_FindPatch( &pMap->geometry, sourceId ) != nullptr ) {
        kind = map_geometry_kind_t::PATCH;
    } else if ( geo::GeometryDocument_FindHeightField( &pMap->geometry, sourceId ) != nullptr ) {
        kind = map_geometry_kind_t::TERRAIN;
    }
    if ( pKindOut != nullptr ) { *pKindOut = kind; }
    return kind != map_geometry_kind_t::COUNT;
}

// ---------------------------------------------------------------------------
// Object shape checks shared by load and save
// ---------------------------------------------------------------------------

// A readable entity: an object whose owned-geometry members, when present,
// are lists. Anything else is kept verbatim, owned geometry and all.
CYPHER_NODISCARD bool_t EntityShapeOk( const key_value_t *pEntity ) noexcept
{
    if ( KeyValue_Type( pEntity ) != key_value_type_t::OBJECT ) { return CY_FALSE; }
    for ( map_geometry_kind_t kind : kOwnedKinds ) {
        const key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
        if ( pNested != nullptr && KeyValue_Type( pNested ) != key_value_type_t::ARRAY ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EntityIsReadable( const key_value_t *pEntity, u64 &idOut ) noexcept
{
    return EntityShapeOk( pEntity ) && ReadId( KeyValue_Find( pEntity, SV( "id" ) ), idOut );
}

// `info` is written for readers and never read back (CYMAP.md 8).
CYPHER_NODISCARD bool_t StripInfo( key_value_document_t *pDocument, key_value_t *pRoot ) noexcept
{
    const auto strip = [&]( key_value_t *pObject ) noexcept {
        key_value_t *pInfo = KeyValue_Find( pObject, SV( "info" ) );
        return pInfo == nullptr || KeyValue_Remove( pDocument, pObject, pInfo );
    };
    if ( !strip( pRoot ) ) { return CY_FALSE; }
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            if ( !strip( pObject ) ) { return CY_FALSE; }
            if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                    key_value_t *pRecord = KeyValue_ChildAt( pNested, j );
                    if ( KeyValue_Type( pRecord ) == key_value_type_t::OBJECT && !strip( pRecord ) ) { return CY_FALSE; }
                }
            }
        }
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// ID walking
// ---------------------------------------------------------------------------

template <typename visit_t>
CYPHER_NODISCARD bool_t Trampoline( void *pContext, key_value_t *pSlot, u64 id ) noexcept
{
    return ( *static_cast<visit_t *>( pContext ) )( pSlot, id );
}

template <typename visit_t>
CYPHER_NODISCARD bool_t VisitObjectId( key_value_t *pObject, visit_t &visit ) noexcept
{
    key_value_t *pSlot = KeyValue_Find( pObject, SV( "id" ) );
    u64 id = 0u;
    return pSlot == nullptr || !ReadId( pSlot, id ) || visit( pSlot, id );
}

// Every present, valid ID in a chunk, in section order then document order:
// objects, entity-owned geometry, and geometry parts. Foliage has no IDs.
template <typename visit_t>
CYPHER_NODISCARD bool_t VisitChunkIds( key_value_t *pRoot, visit_t &visit ) noexcept
{
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section == SEC_FOLIAGE ) { continue; }
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            const bool_t bOk = IsGeometrySection( section )
                ? MapGeometry_ForEachId( KindOfSection( section ), pObject, Trampoline<visit_t>, &visit )
                : VisitObjectId( pObject, visit );
            if ( !bOk ) { return CY_FALSE; }
            if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                    key_value_t *pRecord = KeyValue_ChildAt( pNested, j );
                    if ( KeyValue_Type( pRecord ) == key_value_type_t::OBJECT &&
                         !MapGeometry_ForEachId( kind, pRecord, Trampoline<visit_t>, &visit ) ) {
                        return CY_FALSE;
                    }
                }
            }
        }
    }
    return CY_TRUE;
}

template <typename visit_t>
CYPHER_NODISCARD bool_t VisitRootIds( key_value_t *pRoot, visit_t &visit ) noexcept
{
    for ( const char *pArrayName : { "visgroups", "selection_sets" } ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( pArrayName ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            if ( KeyValue_Type( pObject ) == key_value_type_t::OBJECT && !VisitObjectId( pObject, visit ) ) { return CY_FALSE; }
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t AssignObjectId( key_value_document_t *pDocument, key_value_t *pObject, u64 &nextId, bool_t &bAssigned ) noexcept
{
    u64 id = 0u;
    bAssigned = CY_FALSE;
    if ( ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) ) { return CY_TRUE; }
    bAssigned = CY_TRUE;
    return SetU64Member( pDocument, pObject, "id", nextId++ );
}

void NoteAssigned( map_document_t *pMap, string_view_t path, const char *pSection, const key_value_t *pObject ) noexcept
{
    u64 id = 0u;
    ( void )ReadId( KeyValue_Find( pObject, SV( "id" ) ), id );
    AddProblem( pMap, map_problem_code_t::ID_ASSIGNED, path, pSection, id );
    pMap->bIdsAssigned = CY_TRUE;
}

// Gives hand-added objects and parts without IDs fresh ones (CYMAP.md 10).
CYPHER_NODISCARD bool_t AssignChunkIds( map_document_t *pMap, map_chunk_t *pChunk, u64 &nextId ) noexcept
{
    key_value_document_t *pDocument = pChunk->store.pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );
    const string_view_t path = SV( pChunk->sourcePath );
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section == SEC_FOLIAGE ) { continue; }
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            if ( IsGeometrySection( section ) ) {
                const usize nAssigned = MapGeometry_AssignIds( KindOfSection( section ), pDocument, pObject, &nextId );
                if ( nAssigned == CY_INVALID_SIZE ) { return CY_FALSE; }
                if ( nAssigned != 0u ) { NoteAssigned( pMap, path, kSections[section], pObject ); }
                continue;
            }
            if ( section == SEC_ENTITIES && !EntityShapeOk( pObject ) ) { continue; }
            bool_t bAssigned = CY_FALSE;
            if ( !AssignObjectId( pDocument, pObject, nextId, bAssigned ) ) { return CY_FALSE; }
            if ( bAssigned ) { NoteAssigned( pMap, path, kSections[section], pObject ); }
            if ( section != SEC_ENTITIES ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                    key_value_t *pRecord = KeyValue_ChildAt( pNested, j );
                    if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT ) { continue; }
                    const usize nAssigned = MapGeometry_AssignIds( kind, pDocument, pRecord, &nextId );
                    if ( nAssigned == CY_INVALID_SIZE ) { return CY_FALSE; }
                    if ( nAssigned != 0u ) { NoteAssigned( pMap, path, MapGeometry_SectionName( kind ), pRecord ); }
                }
            }
        }
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// Root reading
// ---------------------------------------------------------------------------

CYPHER_NODISCARD map_status_t ReadRoot( map_document_t *pMap ) noexcept
{
    const key_value_t *pRoot = SettingsDocument_Root( &pMap->root );
    string_view_t text{};
    const auto invalid = [&]( const char *pMember ) noexcept {
        AddProblem( pMap, map_problem_code_t::ROOT_MEMBER_INVALID, {}, pMember, 0u );
        return map_status_t::ROOT_INVALID;
    };
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, SV( "map_id" ) ), &text ) || !UniqueId_FromString( text, &pMap->mapId ) ||
         !UniqueId_IsValid( pMap->mapId ) ) {
        return invalid( "map_id" );
    }
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, SV( "name" ) ), &text ) || text.cchLength == 0u || text.cchLength > MAP_NAME_MAX_LENGTH ) {
        return invalid( "name" );
    }
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, SV( "game" ) ), &text ) || !IsStableId( text, 64u ) ) { return invalid( "game" ); }
    if ( !ReadId( KeyValue_Find( pRoot, SV( "next_id" ) ), pMap->nextId ) ) { return invalid( "next_id" ); }
    const key_value_t *pLayers = KeyValue_Find( pRoot, SV( "layers" ) );
    const usize nLayers = KeyValue_Type( pLayers ) == key_value_type_t::ARRAY ? KeyValue_ChildCount( pLayers ) : 0u;
    if ( nLayers == 0u || nLayers > MAP_LAYERS_MAX ) { return invalid( "layers" ); }
    for ( usize i = 0u; i < nLayers; ++i ) {
        map_layer_t layer{};
        if ( !KeyValue_GetString( KeyValue_Find( KeyValue_ChildAt( pLayers, i ), SV( "id" ) ), &text ) || !IsStableId( text, 64u ) ||
             !CopyText( layer.id, text ) || LayerIndex( pMap, text ) != CY_INVALID_SIZE ) {
            return invalid( "layers" );
        }
        if ( !Vector_PushBack( &pMap->layers, layer ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    const struct {
        const char *pName;
        usize nMax;
    } limited[]{ { "visgroups", MAP_VISGROUPS_MAX }, { "selection_sets", MAP_SELECTION_SETS_MAX }, { "cordons", MAP_CORDONS_MAX } };
    for ( const auto &entry : limited ) {
        if ( KeyValue_ChildCount( KeyValue_Find( pRoot, SV( entry.pName ) ) ) > entry.nMax ) { return invalid( entry.pName ); }
    }

    // Optional members fall back to defaults with a report.
    const key_value_t *pCellSize = KeyValue_Find( pRoot, SV( "cell_size" ) );
    if ( pCellSize != nullptr ) {
        math::vec3d_t size{};
        const bool_t bValid = ReadVec3( pCellSize, size ) && size.x >= 0.0 && size.y >= 0.0 && size.z >= 0.0 &&
                              ( size.x > 0.0 ) == ( size.y > 0.0 ) && !( size.x == 0.0 && size.z > 0.0 );
        if ( bValid ) {
            pMap->grid.size[0] = size.x;
            pMap->grid.size[1] = size.y;
            pMap->grid.size[2] = size.z;
        } else {
            AddProblem( pMap, map_problem_code_t::ROOT_MEMBER_IGNORED, {}, "cell_size", 0u );
        }
    }
    const key_value_t *pUnits = KeyValue_Find( pRoot, SV( "units_per_meter" ) );
    f64 units = 0.0;
    if ( pUnits != nullptr && !( ReadNumber( pUnits, units ) && units > 0.0 ) ) {
        AddProblem( pMap, map_problem_code_t::ROOT_MEMBER_IGNORED, {}, "units_per_meter", 0u );
    }
    return map_status_t::OK;
}

// Appends layers the root does not list yet (adopted from chunks) so the
// root's layer array matches map_document_t::layers index for index.
CYPHER_NODISCARD bool_t SyncRootLayers( map_document_t *pMap ) noexcept
{
    key_value_document_t *pDocument = pMap->root.pDocument;
    key_value_t *pLayers = KeyValue_Find( KeyValue_Root( pDocument ), SV( "layers" ) );
    CY_ASSERT_MSG( KeyValue_Type( pLayers ) == key_value_type_t::ARRAY, "Loaded and created roots always have a layers array" );
    for ( usize i = KeyValue_ChildCount( pLayers ); i < Vector_Count( &pMap->layers ); ++i ) {
        key_value_t *pLayer = KeyValue_ArrayAppend( pDocument, pLayers, key_value_type_t::OBJECT );
        if ( pLayer == nullptr || !SetStringMember( pDocument, pLayer, "id", SV( pMap->layers.pData[i].id ) ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// Loading chunks
// ---------------------------------------------------------------------------

// Opens one chunk file: identity, layer, cell, section shapes. Damage is
// recorded and the file is kept untouched.
CYPHER_NODISCARD map_status_t OpenChunk( map_document_t *pMap, const map_chunk_input_t &input, string_view_t mapIdText ) noexcept
{
    map_chunk_t *pChunk = NewChunk( pMap, SV( "" ), MapCell_Global() );
    if ( pChunk == nullptr || !Vector_PushBack( &pMap->chunks, pChunk ) ) {
        DestroyChunk( pMap->pAllocator, pChunk );
        return map_status_t::OUT_OF_MEMORY;
    }
    const auto damaged = [&]( map_problem_code_t code, const char *pMember ) noexcept {
        pChunk->bDamaged = CY_TRUE;
        AddProblem( pMap, code, input.path, pMember, 0u );
        return map_status_t::OK;
    };
    if ( !CopyText( pChunk->sourcePath, input.path ) ) { return damaged( map_problem_code_t::CHUNK_UNREADABLE, "path" ); }
    const settings_document_load_result_t loaded = input.text.cchLength <= MAP_CHUNK_TEXT_MAX
        ? SettingsDocument_Load( &pChunk->store, input.text )
        : settings_document_load_result_t{ settings_document_status_t::TEXT_TOO_LARGE };
    if ( loaded.status != settings_document_status_t::OK ) { return damaged( map_problem_code_t::CHUNK_UNREADABLE, nullptr ); }
    key_value_t *pRoot = KeyValue_Root( pChunk->store.pDocument );
    string_view_t text{};
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, SV( "map_id" ) ), &text ) || !StringView_Equals( text, mapIdText ) ) {
        return damaged( map_problem_code_t::CHUNK_FOREIGN_MAP, "map_id" );
    }
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, SV( "layer" ) ), &text ) || !IsStableId( text, 64u ) || !CopyText( pChunk->layer, text ) ) {
        return damaged( map_problem_code_t::CHUNK_UNREADABLE, "layer" );
    }
    // A section that is not a list cannot be re-placed or written back
    // under its name without guessing; the file is left alone.
    for ( const char *pSection : kSections ) {
        const key_value_t *pMember = KeyValue_Find( pRoot, SV( pSection ) );
        if ( pMember != nullptr && KeyValue_Type( pMember ) != key_value_type_t::ARRAY ) {
            return damaged( map_problem_code_t::CHUNK_UNREADABLE, pSection );
        }
    }
    if ( LayerIndex( pMap, text ) == CY_INVALID_SIZE ) {
        map_layer_t adopted{};
        ( void )CopyText( adopted.id, text );
        if ( Vector_Count( &pMap->layers ) >= MAP_LAYERS_MAX || !Vector_PushBack( &pMap->layers, adopted ) ) {
            return damaged( map_problem_code_t::CHUNK_UNREADABLE, "layer" );
        }
        AddProblem( pMap, map_problem_code_t::CHUNK_LAYER_ADOPTED, input.path, "layer", 0u );
    }
    if ( !MapCell_Read( KeyValue_Find( pRoot, SV( "cell" ) ), &pChunk->cell ) ) {
        pChunk->cell = MapCell_Global();
        AddProblem( pMap, map_problem_code_t::CHUNK_CELL_INVALID, input.path, "cell", 0u );
    }
    char canonical[MAP_CHUNK_PATH_CAPACITY]{};
    FormatChunkPath( SV( pChunk->layer ), pChunk->cell, canonical );
    if ( !StringView_Equals( SV( canonical ), input.path ) ) { AddProblem( pMap, map_problem_code_t::CHUNK_PATH_MISMATCH, input.path, nullptr, 0u ); }
    return StripInfo( pChunk->store.pDocument, pRoot ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

// Lifts one record's geometry into the geometry document, or keeps the
// record verbatim when it cannot be read.
CYPHER_NODISCARD map_status_t ExtractRecord(
    map_document_t *pMap,
    map_chunk_t *pChunk,
    map_geometry_kind_t kind,
    key_value_t *pRecord,
    u32 iLayer,
    u64 owner ) noexcept
{
    const string_view_t path = SV( pChunk->sourcePath );
    if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT ) {
        AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, path, MapGeometry_SectionName( kind ), 0u );
        return map_status_t::OK;
    }
    const map_geometry_read_result_t result = MapGeometry_Read( kind, pRecord, &pMap->materials, &pMap->geometry, pMap->geometryPolicy );
    char reason[48]{};
    std::snprintf( reason, sizeof( reason ), "%s %s", MapGeometry_SectionName( kind ), result.pReason );
    switch ( result.status ) {
        case map_geometry_read_status_t::OK:
            if ( !Vector_PushBack( &pMap->geometryRecords, map_geometry_record_t{ result.id, iLayer, owner } ) ||
                 !MapGeometry_StripRecord( kind, pChunk->store.pDocument, pRecord ) ) {
                return map_status_t::OUT_OF_MEMORY;
            }
            return map_status_t::OK;
        case map_geometry_read_status_t::OUT_OF_MEMORY: return map_status_t::OUT_OF_MEMORY;
        case map_geometry_read_status_t::INVALID:
            AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, path, reason, result.id );
            return map_status_t::OK;
        case map_geometry_read_status_t::IDENTITY_CONFLICT:
            pMap->bReadOnly = CY_TRUE;
            AddProblem( pMap, map_problem_code_t::DUPLICATE_ID, path, reason, result.id );
            return map_status_t::OK;
    }
    return map_status_t::OK;
}

CYPHER_NODISCARD map_status_t ExtractChunkGeometry( map_document_t *pMap, map_chunk_t *pChunk ) noexcept
{
    key_value_t *pRoot = KeyValue_Root( pChunk->store.pDocument );
    const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pChunk->layer ) ) );
    for ( u32 section = SEC_BRUSHES; section <= SEC_TERRAINS; ++section ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            const map_status_t status = ExtractRecord( pMap, pChunk, KindOfSection( section ), KeyValue_ChildAt( pArray, i ), iLayer, 0u );
            if ( status != map_status_t::OK ) { return status; }
        }
    }
    key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i ) {
        key_value_t *pEntity = KeyValue_ChildAt( pEntities, i );
        u64 entityId = 0u;
        if ( !EntityIsReadable( pEntity, entityId ) ) {
            AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, SV( pChunk->sourcePath ), "entities", 0u );
            continue;
        }
        for ( map_geometry_kind_t kind : kOwnedKinds ) {
            key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
            for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                const map_status_t status = ExtractRecord( pMap, pChunk, kind, KeyValue_ChildAt( pNested, j ), iLayer, entityId );
                if ( status != map_status_t::OK ) { return status; }
            }
        }
    }
    // Other objects without a usable shape are reported here; they are
    // written back verbatim.
    for ( u32 section : { static_cast<u32>( SEC_SHAPES ), static_cast<u32>( SEC_NOTES ), static_cast<u32>( SEC_GROUPS ),
                          static_cast<u32>( SEC_PREFABS ) } ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            u64 id = 0u;
            const key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) ) {
                AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, SV( pChunk->sourcePath ), kSections[section], 0u );
            }
        }
    }
    return map_status_t::OK;
}

// ---------------------------------------------------------------------------
// Save planning
// ---------------------------------------------------------------------------

// A geometry object as it will be written: which fragment entry holds it,
// where it is, and the map's record for it.
struct live_t {
    u64 id{ 0u };
    map_geometry_kind_t kind{ map_geometry_kind_t::COUNT };
    usize iObject{ 0u };                   // Index into the fragment's list for its kind.
    map_bounds_t bounds{};                 // Full bounds, for `info`.
    map_bounds_t place{};                  // What placement uses (a terrain's footprint).
    usize nVertices{ 0u };                 // Meshes: counts for `info`.
    usize nFaces{ 0u };
    u32 iLayer{ 0u };
    u64 owner{ 0u };                       // Owning entity, or 0.
    const key_value_t *pRecord{ nullptr }; // The map's record, when one exists.
};

struct owned_t {
    u64 owner{ 0u };
    const live_t *pLive{ nullptr };
};

struct placement_t {
    u64 id{ 0u };
    u32 iLayer{ 0u };
    map_cell_t cell{};
};

struct record_ref_t {
    u64 id{ 0u };
    const key_value_t *pNode{ nullptr };
};

struct placed_t {
    u64 key{ 0u };                         // ID; ~0 keeps unreadable objects last, in order.
    usize order{ 0u };
    const key_value_t *pNode{ nullptr };   // Map record, or a node written verbatim.
    const live_t *pLive{ nullptr };        // Geometry regenerated from the geometry document.
};

struct foliage_item_t {
    string_view_t model{};
    const key_value_t *pInstance{ nullptr };
    f64 position[3]{};
    bool_t bReadable{ CY_FALSE };
    usize order{ 0u };
};

struct foliage_props_t {
    u32 iLayer{ 0u };
    string_view_t model{};
    const key_value_t *pProperties{ nullptr };
};

struct target_t {
    u32 iLayer{ 0u };
    map_cell_t cell{};
    vector_t<placed_t> sections[SEC_COUNT]{};
    vector_t<foliage_item_t> foliage{};
    vector_t<const key_value_t *> extras{};  // Unknown chunk members.
    map_bounds_t bounds{};                    // Everything positioned, for the chunk's `info`.
    settings_document_t *pOutput{ nullptr };
    text_buffer_t *pText{ nullptr };
    char path[MAP_CHUNK_PATH_CAPACITY]{};
};

struct save_plan_t {
    save_plan_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( save_plan_t );

    const allocator_t *pAllocator{ nullptr };
    geo::geometry_fragment_t fragment{};
    vector_t<live_t> lives{};                 // Sorted by ID.
    vector_t<owned_t> owned{};                // Sorted by owner, then ID.
    vector_t<placement_t> placements{};       // Every placed object with an ID.
    vector_t<foliage_props_t> foliageProps{}; // First record's properties per layer and model.
    vector_t<target_t *> targets{};
    vector_t<u64> scratch{};

    ~save_plan_t() noexcept
    {
        for ( usize i = 0u; i < Vector_Count( &targets ); ++i ) {
            target_t *pTarget = targets.pData[i];
            if ( pTarget->pOutput != nullptr ) {
                pTarget->pOutput->~settings_document_t();
                Allocator_Free( pAllocator, pTarget->pOutput, sizeof( settings_document_t ), alignof( settings_document_t ) );
            }
            if ( pTarget->pText != nullptr ) {
                pTarget->pText->~text_buffer_t();
                Allocator_Free( pAllocator, pTarget->pText, sizeof( text_buffer_t ), alignof( text_buffer_t ) );
            }
            pTarget->~target_t();
            Allocator_Free( pAllocator, pTarget, sizeof( target_t ), alignof( target_t ) );
        }
        geo::GeometryFragment_Shutdown( &fragment );
    }
};

CYPHER_NODISCARD target_t *GetTarget( save_plan_t &plan, u32 iLayer, const map_cell_t &cell ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        target_t *pTarget = plan.targets.pData[i];
        if ( pTarget->iLayer == iLayer && MapCell_Equals( pTarget->cell, cell ) ) { return pTarget; }
    }
    void *pMemory = Allocator_Allocate( plan.pAllocator, sizeof( target_t ), alignof( target_t ) );
    if ( pMemory == nullptr ) { return nullptr; }
    target_t *pTarget = new ( pMemory ) target_t{};
    pTarget->iLayer = iLayer;
    pTarget->cell = cell;
    bool_t bOk = Vector_Init( &pTarget->foliage, plan.pAllocator ) && Vector_Init( &pTarget->extras, plan.pAllocator );
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) { bOk = bOk && Vector_Init( &pTarget->sections[section], plan.pAllocator ); }
    if ( !bOk || !Vector_PushBack( &plan.targets, pTarget ) ) {
        pTarget->~target_t();
        Allocator_Free( plan.pAllocator, pMemory, sizeof( target_t ), alignof( target_t ) );
        return nullptr;
    }
    return pTarget;
}

CYPHER_NODISCARD const live_t *FindLive( const save_plan_t &plan, u64 id ) noexcept
{
    const live_t *pBegin = plan.lives.pData;
    const live_t *pEnd = pBegin + Vector_Count( &plan.lives );
    const live_t *pFound = std::lower_bound( pBegin, pEnd, id, []( const live_t &entry, u64 value ) { return entry.id < value; } );
    return pFound != pEnd && pFound->id == id ? pFound : nullptr;
}

CYPHER_NODISCARD const placement_t *FindPlacement( const save_plan_t &plan, u64 id, usize nSorted = CY_INVALID_SIZE ) noexcept
{
    const usize nCount = nSorted == CY_INVALID_SIZE ? Vector_Count( &plan.placements ) : nSorted;
    CY_ASSERT( nCount <= Vector_Count( &plan.placements ) );
    if ( nCount == 0u ) { return nullptr; }
    const placement_t *pBegin = plan.placements.pData;
    const placement_t *pEnd = pBegin + nCount;
    const placement_t *pFound = std::lower_bound( pBegin, pEnd, id, []( const placement_t &entry, u64 value ) { return entry.id < value; } );
    return pFound != pEnd && pFound->id == id ? pFound : nullptr;
}

void SortPlacements( save_plan_t &plan ) noexcept
{
    std::sort( plan.placements.pData, plan.placements.pData + Vector_Count( &plan.placements ),
               []( const placement_t &a, const placement_t &b ) { return a.id < b.id; } );
}

CYPHER_NODISCARD map_cell_t CellFor( const map_document_t *pMap, const map_bounds_t &bounds ) noexcept
{
    return bounds.bHas ? MapCell_ForPosition( pMap->grid, MapBounds_Center( bounds ) ) : MapCell_Global();
}

CYPHER_NODISCARD bool_t Place( target_t *pTarget, u32 section, const placed_t &placed ) noexcept
{
    return pTarget != nullptr && Vector_PushBack( &pTarget->sections[section], placed );
}

CYPHER_NODISCARD map_bounds_t ShapeBounds( const key_value_t *pShape ) noexcept
{
    map_bounds_t bounds{};
    const key_value_t *pPoints = KeyValue_Find( pShape, SV( "points" ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pPoints ); ++i ) {
        math::vec3d_t point{};
        if ( ReadVec3( KeyValue_ChildAt( pPoints, i ), point ) ) { MapBounds_AddPoint( bounds, point ); }
    }
    return bounds;
}

// Collects live geometry from the geometry document with bounds, layers,
// owners, and the map's records for it.
CYPHER_NODISCARD map_status_t PlanLives( map_document_t *pMap, save_plan_t &plan ) noexcept
{
    if ( geo::GeometryFragment_Init( &plan.fragment, pMap->pAllocator ) != geo::geometry_status_t::OK ||
         geo::GeometryFragment_TryExtractAll( &pMap->geometry, &plan.fragment ) != geo::geometry_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    const geo::geometry_fragment_t &fragment = plan.fragment;
    bool_t bOk = CY_TRUE;
    const auto add = [&]( u64 id, map_geometry_kind_t kind, usize iObject, map_bounds_t bounds, map_bounds_t place ) noexcept {
        live_t live{};
        live.id = id;
        live.kind = kind;
        live.iObject = iObject;
        live.bounds = bounds;
        live.place = place;
        const usize iRecord = RecordIndex( pMap, id );
        if ( iRecord != CY_INVALID_SIZE ) {
            live.iLayer = pMap->geometryRecords.pData[iRecord].iLayer < Vector_Count( &pMap->layers ) ? pMap->geometryRecords.pData[iRecord].iLayer : 0u;
            live.owner = pMap->geometryRecords.pData[iRecord].owner;
        }
        bOk = bOk && Vector_PushBack( &plan.lives, live );
        return bOk;
    };
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.brushes ); ++i ) {
        const geo::brush_source_t &brush = *fragment.brushes.pData[i];
        geo::geometry_status_t boundsStatus{};
        const map_bounds_t bounds = MapGeometry_BrushBounds( brush, pMap->geometryPolicy, pMap->pAllocator, &boundsStatus );
        if ( boundsStatus == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
        ( void )add( brush.solid.sourceId.value, map_geometry_kind_t::BRUSH, i, bounds, bounds );
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.meshes ); ++i ) {
        usize nVertices = 0u, nFaces = 0u;
        geo::geometry_status_t boundsStatus{};
        const map_bounds_t bounds = MapGeometry_MeshBounds( *fragment.meshes.pData[i], pMap->pAllocator, &nVertices, &nFaces, &boundsStatus );
        if ( boundsStatus == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
        if ( add( fragment.meshes.pData[i]->sourceId.value, map_geometry_kind_t::MESH, i, bounds, bounds ) ) {
            plan.lives.pData[Vector_Count( &plan.lives ) - 1u].nVertices = nVertices;
            plan.lives.pData[Vector_Count( &plan.lives ) - 1u].nFaces = nFaces;
        }
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.patches ); ++i ) {
        const map_bounds_t bounds = MapGeometry_PatchBounds( *fragment.patches.pData[i] );
        ( void )add( fragment.patches.pData[i]->sourceId.value, map_geometry_kind_t::PATCH, i, bounds, bounds );
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.heightFields ); ++i ) {
        const geo::heightfield_t &terrain = *fragment.heightFields.pData[i];
        ( void )add( terrain.sourceId.value, map_geometry_kind_t::TERRAIN, i, MapGeometry_TerrainBounds( terrain ),
                     MapGeometry_TerrainFootprint( terrain ) );
    }
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    std::sort( plan.lives.pData, plan.lives.pData + Vector_Count( &plan.lives ), []( const live_t &a, const live_t &b ) { return a.id < b.id; } );

    // The map's record for each live object, wherever it sits (world list
    // or inside an entity), carries its name, comment, and other members.
    vector_t<record_ref_t> refs{};
    if ( !Vector_Init( &refs, pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto addRef = [&]( const key_value_t *pRecord ) noexcept {
        u64 id = 0u;
        if ( KeyValue_Type( pRecord ) == key_value_type_t::OBJECT && ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) &&
             FindLive( plan, id ) != nullptr ) {
            bOk = bOk && Vector_PushBack( &refs, record_ref_t{ id, pRecord } );
        }
    };
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const key_value_t *pRoot = SettingsDocument_Root( &pChunk->store );
        for ( u32 section = SEC_BRUSHES; section <= SEC_TERRAINS; ++section ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) { addRef( KeyValue_ChildAt( pArray, i ) ); }
        }
        const key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i ) {
            const key_value_t *pEntity = KeyValue_ChildAt( pEntities, i );
            if ( !EntityShapeOk( pEntity ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                const key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) { addRef( KeyValue_ChildAt( pNested, j ) ); }
            }
        }
    }
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    std::stable_sort( refs.pData, refs.pData + Vector_Count( &refs ), []( const record_ref_t &a, const record_ref_t &b ) { return a.id < b.id; } );
    for ( usize i = 0u, iRef = 0u; i < Vector_Count( &plan.lives ); ++i ) {
        live_t &live = plan.lives.pData[i];
        while ( iRef < Vector_Count( &refs ) && refs.pData[iRef].id < live.id ) { ++iRef; }
        if ( iRef < Vector_Count( &refs ) && refs.pData[iRef].id == live.id ) { live.pRecord = refs.pData[iRef].pNode; }
    }
    return map_status_t::OK;
}

// Decides every object's target chunk (CYMAP.md section 9).
CYPHER_NODISCARD map_status_t PlanSave( map_document_t *pMap, save_plan_t &plan ) noexcept
{
    map_status_t status = PlanLives( pMap, plan );
    if ( status != map_status_t::OK ) { return status; }
    bool_t bOk = CY_TRUE;
    constexpr u64 kUnreadable = ~0ull;

    // 1. Entities by origin; entities without one go to global.
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pChunk->layer ) ) );
        CY_ASSERT_MSG( iLayer != static_cast<u32>( CY_INVALID_SIZE ), "Loaded chunks always have a known or adopted layer" );
        const key_value_t *pEntities = KeyValue_Find( SettingsDocument_Root( &pChunk->store ), SV( kSections[SEC_ENTITIES] ) );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pEntities ); ++i ) {
            const key_value_t *pEntity = KeyValue_ChildAt( pEntities, i );
            u64 id = 0u;
            if ( !EntityIsReadable( pEntity, id ) ) {
                bOk = Place( GetTarget( plan, iLayer, pChunk->cell ), SEC_ENTITIES, placed_t{ kUnreadable, i, pEntity, nullptr } );
                continue;
            }
            const key_value_t *pOrigin = KeyValue_Find( pEntity, SV( "origin" ) );
            map_cell_t cell = pChunk->cell; // An origin that cannot be read keeps its chunk.
            math::vec3d_t origin{};
            const bool_t bOrigin = pOrigin != nullptr && ReadVec3( pOrigin, origin );
            if ( pOrigin == nullptr ) {
                cell = MapCell_Global();
            } else if ( bOrigin ) {
                cell = MapCell_ForPosition( pMap->grid, origin );
            }
            target_t *pTarget = GetTarget( plan, iLayer, cell );
            bOk = Place( pTarget, SEC_ENTITIES, placed_t{ id, i, pEntity, nullptr } ) &&
                  Vector_PushBack( &plan.placements, placement_t{ id, iLayer, cell } );
            if ( bOk && bOrigin ) { MapBounds_AddPoint( pTarget->bounds, origin ); }
        }
    }
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    SortPlacements( plan );

    // 2. Owned geometry follows its entity; geometry whose owner is gone is
    //    written as world geometry.
    for ( usize i = 0u; i < Vector_Count( &plan.lives ); ++i ) {
        live_t &live = plan.lives.pData[i];
        if ( live.owner != 0u && ( live.kind == map_geometry_kind_t::TERRAIN || FindPlacement( plan, live.owner ) == nullptr ) ) { live.owner = 0u; }
        if ( live.owner != 0u ) { bOk = bOk && Vector_PushBack( &plan.owned, owned_t{ live.owner, &live } ); }
    }
    std::stable_sort( plan.owned.pData, plan.owned.pData + Vector_Count( &plan.owned ),
                      []( const owned_t &a, const owned_t &b ) { return a.owner < b.owner; } );
    const usize nEntityPlacements = Vector_Count( &plan.placements );
    for ( usize i = 0u; bOk && i < Vector_Count( &plan.owned ); ++i ) {
        const owned_t &owned = plan.owned.pData[i];
        // Owned geometry is appended below and can have IDs smaller than
        // its owner. Only the original entity prefix remains sorted here.
        const placement_t *pOwner = FindPlacement( plan, owned.owner, nEntityPlacements );
        CY_ASSERT( pOwner != nullptr );
        const placement_t owner = *pOwner; // Pushing below may move the vector.
        target_t *pTarget = GetTarget( plan, owner.iLayer, owner.cell );
        bOk = pTarget != nullptr && Vector_PushBack( &plan.placements, placement_t{ owned.pLive->id, owner.iLayer, owner.cell } );
        if ( bOk ) {
            MapBounds_AddBounds( pTarget->bounds, owned.pLive->bounds );
            CY_ASSERT( Vector_Count( &plan.placements ) > nEntityPlacements );
        }
    }

    // 3. World geometry by its layer and the centre of its bounds.
    for ( usize i = 0u; bOk && i < Vector_Count( &plan.lives ); ++i ) {
        const live_t &live = plan.lives.pData[i];
        if ( live.owner != 0u ) { continue; }
        const map_cell_t cell = CellFor( pMap, live.place );
        target_t *pTarget = GetTarget( plan, live.iLayer, cell );
        bOk = Place( pTarget, SectionOfKind( live.kind ), placed_t{ live.id, 0u, nullptr, &live } ) &&
              Vector_PushBack( &plan.placements, placement_t{ live.id, live.iLayer, cell } );
        if ( bOk ) { MapBounds_AddBounds( pTarget->bounds, live.bounds ); }
    }

    // 4. Everything else read from the chunks: unreadable geometry, shapes,
    //    notes, prefabs, foliage, unknown members. Groups wait for step 5.
    usize nFoliageOrder = 0u;
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pChunk->layer ) ) );
        const key_value_t *pRoot = SettingsDocument_Root( &pChunk->store );
        for ( u32 section = SEC_BRUSHES; bOk && section <= SEC_TERRAINS; ++section ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pArray ); ++i ) {
                const key_value_t *pRecord = KeyValue_ChildAt( pArray, i );
                u64 id = 0u;
                // A record of a live object is regenerated in step 3; any
                // other record could not be read and stays as written.
                if ( KeyValue_Type( pRecord ) == key_value_type_t::OBJECT && ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) &&
                     FindLive( plan, id ) != nullptr ) {
                    continue;
                }
                bOk = Place( GetTarget( plan, iLayer, pChunk->cell ), section, placed_t{ kUnreadable, i, pRecord, nullptr } );
            }
        }
        for ( u32 section : { static_cast<u32>( SEC_SHAPES ), static_cast<u32>( SEC_NOTES ), static_cast<u32>( SEC_PREFABS ) } ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pArray ); ++i ) {
                const key_value_t *pObject = KeyValue_ChildAt( pArray, i );
                u64 id = 0u;
                if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) ) {
                    bOk = Place( GetTarget( plan, iLayer, pChunk->cell ), section, placed_t{ kUnreadable, i, pObject, nullptr } );
                    continue;
                }
                map_bounds_t bounds{};
                math::vec3d_t point{};
                if ( section == SEC_SHAPES ) {
                    bounds = ShapeBounds( pObject );
                } else if ( ReadVec3( KeyValue_Find( pObject, SV( section == SEC_NOTES ? "position" : "origin" ) ), point ) ) {
                    MapBounds_AddPoint( bounds, point );
                }
                const map_cell_t cell = bounds.bHas ? CellFor( pMap, bounds ) : pChunk->cell;
                target_t *pTarget = GetTarget( plan, iLayer, cell );
                bOk = Place( pTarget, section, placed_t{ id, i, pObject, nullptr } ) &&
                      Vector_PushBack( &plan.placements, placement_t{ id, iLayer, cell } );
                if ( bOk ) { MapBounds_AddBounds( pTarget->bounds, bounds ); }
            }
        }
        // Foliage: every instance goes to the chunk of its own position.
        const key_value_t *pFoliage = KeyValue_Find( pRoot, SV( kSections[SEC_FOLIAGE] ) );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pFoliage ); ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pFoliage, i );
            string_view_t model{};
            const key_value_t *pInstances = KeyValue_Find( pRecord, SV( "instances" ) );
            if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT || !KeyValue_GetString( KeyValue_Find( pRecord, SV( "model" ) ), &model ) ||
                 model.cchLength == 0u || KeyValue_Type( pInstances ) != key_value_type_t::ARRAY ) {
                bOk = Place( GetTarget( plan, iLayer, pChunk->cell ), SEC_FOLIAGE, placed_t{ kUnreadable, i, pRecord, nullptr } );
                continue;
            }
            bool_t bKnown = CY_FALSE;
            for ( usize k = 0u; k < Vector_Count( &plan.foliageProps ) && !bKnown; ++k ) {
                bKnown = plan.foliageProps.pData[k].iLayer == iLayer && StringView_Equals( plan.foliageProps.pData[k].model, model );
            }
            if ( !bKnown ) {
                bOk = Vector_PushBack( &plan.foliageProps, foliage_props_t{ iLayer, model, KeyValue_Find( pRecord, SV( "properties" ) ) } );
            }
            for ( usize k = 0u; bOk && k < KeyValue_ChildCount( pInstances ); ++k ) {
                foliage_item_t item{};
                item.model = model;
                item.pInstance = KeyValue_ChildAt( pInstances, k );
                item.order = nFoliageOrder++;
                item.bReadable = KeyValue_Type( item.pInstance ) == key_value_type_t::ARRAY && KeyValue_ChildCount( item.pInstance ) == 7u;
                for ( usize c = 0u; item.bReadable && c < 7u; ++c ) {
                    f64 value = 0.0;
                    item.bReadable = ReadNumber( KeyValue_ChildAt( item.pInstance, c ), value );
                    if ( c < 3u ) { item.position[c] = value; }
                }
                const math::vec3d_t position = math::Vec3d_Make( item.position[0], item.position[1], item.position[2] );
                const map_cell_t cell = item.bReadable ? MapCell_ForPosition( pMap->grid, position ) : pChunk->cell;
                target_t *pTarget = GetTarget( plan, iLayer, cell );
                bOk = pTarget != nullptr && Vector_PushBack( &pTarget->foliage, item );
                if ( bOk && item.bReadable ) { MapBounds_AddPoint( pTarget->bounds, position ); }
            }
        }
        // Unknown chunk members stay with this chunk's layer and cell.
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pRoot ); ++i ) {
            const key_value_t *pMember = KeyValue_ChildAt( pRoot, i );
            if ( IsListed( KeyValue_Name( pMember ), Order( kChunkOrder ) ) ) { continue; }
            target_t *pTarget = GetTarget( plan, iLayer, pChunk->cell );
            bOk = pTarget != nullptr && Vector_PushBack( &pTarget->extras, pMember );
        }
    }
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    SortPlacements( plan );

    // 5. Groups: the global chunk of their first member's layer.
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pChunk->layer ) ) );
        const key_value_t *pGroups = KeyValue_Find( SettingsDocument_Root( &pChunk->store ), SV( kSections[SEC_GROUPS] ) );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pGroups ); ++i ) {
            const key_value_t *pGroup = KeyValue_ChildAt( pGroups, i );
            u64 id = 0u;
            if ( KeyValue_Type( pGroup ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pGroup, SV( "id" ) ), id ) ) {
                bOk = Place( GetTarget( plan, iLayer, pChunk->cell ), SEC_GROUPS, placed_t{ kUnreadable, i, pGroup, nullptr } );
                continue;
            }
            u32 iTargetLayer = iLayer;
            const key_value_t *pMembers = KeyValue_Find( pGroup, SV( "members" ) );
            u64 first = 0u;
            if ( KeyValue_ChildCount( pMembers ) != 0u && ReadId( KeyValue_ChildAt( pMembers, 0u ), first ) ) {
                if ( const placement_t *pFirst = FindPlacement( plan, first ) ) { iTargetLayer = pFirst->iLayer; }
            }
            bOk = Place( GetTarget( plan, iTargetLayer, MapCell_Global() ), SEC_GROUPS, placed_t{ id, i, pGroup, nullptr } );
        }
    }
    return bOk ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

// ---------------------------------------------------------------------------
// Output builders
// ---------------------------------------------------------------------------

struct writer_ctx_t {
    map_document_t *pMap{ nullptr };
    save_plan_t *pPlan{ nullptr };
    key_value_document_t *pDocument{ nullptr };
};

// A list of IDs is written sorted so its order never causes a diff.
CYPHER_NODISCARD bool_t CloneSortedIds( writer_ctx_t &ctx, key_value_t *pParent, const char *pName, const key_value_t *pSource ) noexcept
{
    vector_t<u64> &ids = ctx.pPlan->scratch;
    Vector_Clear( &ids );
    bool_t bAllIds = KeyValue_Type( pSource ) == key_value_type_t::ARRAY;
    for ( usize i = 0u; bAllIds && i < KeyValue_ChildCount( pSource ); ++i ) {
        u64 id = 0u;
        bAllIds = ReadId( KeyValue_ChildAt( pSource, i ), id ) && Vector_PushBack( &ids, id );
    }
    if ( !bAllIds ) { return Clone( ctx.pDocument, pParent, pSource ); }
    Sort_Unstable( Vector_Span( &ids ) );
    key_value_t *pArray = KeyValue_ObjectInsert( ctx.pDocument, pParent, SV( pName ), key_value_type_t::ARRAY );
    for ( usize i = 0u; pArray != nullptr && i < Vector_Count( &ids ); ++i ) {
        key_value_t *pId = KeyValue_ArrayAppend( ctx.pDocument, pArray, key_value_type_t::NULL_VALUE );
        if ( pId == nullptr || !KeyValue_SetU64( ctx.pDocument, pId, ids.pData[i] ) ) { return CY_FALSE; }
    }
    return pArray != nullptr;
}

// A value equal to `value` everywhere: one number, or a list of numbers.
CYPHER_NODISCARD bool_t IsUniform( const key_value_t *pNode, f64 value ) noexcept
{
    f64 number = 0.0;
    if ( ReadNumber( pNode, number ) ) { return number == value; }
    if ( KeyValue_Type( pNode ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pNode ) == 0u ) { return CY_FALSE; }
    for ( usize i = 0u; i < KeyValue_ChildCount( pNode ); ++i ) {
        if ( !ReadNumber( KeyValue_ChildAt( pNode, i ), number ) || number != value ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// One spelling per value (CYMAP.md 3): members whose type the format
// defines are rewritten in that type - IDs as u64, positions and angles as
// reals, `times` as an integer - so a hand-typed `id = 7` or
// `origin = [ 1, 2, 3 ]` saves as `id = 7u` and `origin = [ 1.0, 2.0, 3.0 ]`.
// A value of the wrong shape is copied as written; fixing it is not the
// writer's business. Entity properties are typed by the game profile and
// never pass through here.
enum class spelling_t : u8 { AS_WRITTEN = 0u, ID, REAL, REALS, REAL_ROWS, REAL_OR_REALS, INTEGER };

CYPHER_NODISCARD spelling_t SpellingOf( string_view_t name ) noexcept
{
    static constexpr struct {
        const char *pName;
        spelling_t spelling;
    } kSpellings[]{
        { "id", spelling_t::ID },          { "parent", spelling_t::ID },        { "origin", spelling_t::REALS },
        { "angles", spelling_t::REALS },   { "position", spelling_t::REALS },   { "bounds", spelling_t::REALS },
        { "points", spelling_t::REAL_ROWS }, { "handles", spelling_t::REAL_ROWS }, { "instances", spelling_t::REAL_ROWS },
        { "widths", spelling_t::REALS },   { "delay", spelling_t::REAL },       { "size", spelling_t::REAL },
        { "scale", spelling_t::REAL_OR_REALS }, { "times", spelling_t::INTEGER },
    };
    for ( const auto &entry : kSpellings ) {
        if ( StringView_Equals( name, SV( entry.pName ) ) ) { return entry.spelling; }
    }
    return spelling_t::AS_WRITTEN;
}

CYPHER_NODISCARD bool_t AllNumbers( const key_value_t *pArray ) noexcept
{
    if ( KeyValue_Type( pArray ) != key_value_type_t::ARRAY ) { return CY_FALSE; }
    f64 value = 0.0;
    for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
        if ( !ReadNumber( KeyValue_ChildAt( pArray, i ), value ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t AppendReals( key_value_document_t *pDocument, key_value_t *pArray, const key_value_t *pSource ) noexcept
{
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i ) {
        f64 value = 0.0;
        ( void )ReadNumber( KeyValue_ChildAt( pSource, i ), value );
        key_value_t *pValue = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::NULL_VALUE );
        if ( pValue == nullptr || !KeyValue_SetF64( pDocument, pValue, MapReal( value ) ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Copies pSource into pParent (an object, under its own name, or an array)
// in its canonical spelling.
CYPHER_NODISCARD bool_t CloneCanonical( key_value_document_t *pDocument, key_value_t *pParent, const key_value_t *pSource ) noexcept
{
    const string_view_t name = KeyValue_Name( pSource );
    const bool_t bIntoArray = KeyValue_Type( pParent ) == key_value_type_t::ARRAY;
    const auto insert = [&]( key_value_type_t type ) noexcept {
        return bIntoArray ? KeyValue_ArrayAppend( pDocument, pParent, type ) : KeyValue_ObjectInsert( pDocument, pParent, name, type );
    };
    f64 number = 0.0;
    u64 id = 0u;
    switch ( SpellingOf( name ) ) {
        case spelling_t::ID:
            if ( ReadId( pSource, id ) ) {
                key_value_t *pNode = insert( key_value_type_t::NULL_VALUE );
                return pNode != nullptr && KeyValue_SetU64( pDocument, pNode, id );
            }
            break;
        case spelling_t::REAL:
            if ( ReadNumber( pSource, number ) ) {
                key_value_t *pNode = insert( key_value_type_t::NULL_VALUE );
                return pNode != nullptr && KeyValue_SetF64( pDocument, pNode, MapReal( number ) );
            }
            break;
        case spelling_t::INTEGER:
            if ( ReadNumber( pSource, number ) && std::floor( number ) == number && std::abs( number ) < 9.0e15 ) {
                key_value_t *pNode = insert( key_value_type_t::NULL_VALUE );
                return pNode != nullptr && KeyValue_SetI64( pDocument, pNode, static_cast<i64>( number ) );
            }
            break;
        case spelling_t::REAL_OR_REALS:
            if ( ReadNumber( pSource, number ) ) {
                key_value_t *pNode = insert( key_value_type_t::NULL_VALUE );
                return pNode != nullptr && KeyValue_SetF64( pDocument, pNode, MapReal( number ) );
            }
            [[fallthrough]];
        case spelling_t::REALS:
            if ( AllNumbers( pSource ) ) {
                key_value_t *pNode = insert( key_value_type_t::ARRAY );
                return pNode != nullptr && AppendReals( pDocument, pNode, pSource );
            }
            break;
        case spelling_t::REAL_ROWS: {
            bool_t bRows = KeyValue_Type( pSource ) == key_value_type_t::ARRAY;
            for ( usize i = 0u; bRows && i < KeyValue_ChildCount( pSource ); ++i ) { bRows = AllNumbers( KeyValue_ChildAt( pSource, i ) ); }
            if ( !bRows ) { break; }
            key_value_t *pNode = insert( key_value_type_t::ARRAY );
            for ( usize i = 0u; pNode != nullptr && i < KeyValue_ChildCount( pSource ); ++i ) {
                key_value_t *pRow = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::ARRAY );
                if ( pRow == nullptr || !AppendReals( pDocument, pRow, KeyValue_ChildAt( pSource, i ) ) ) { return CY_FALSE; }
            }
            return pNode != nullptr;
        }
        case spelling_t::AS_WRITTEN: break;
    }
    return Clone( pDocument, pParent, pSource );
}

CYPHER_NODISCARD bool_t CloneUnknown( key_value_document_t *pDocument, key_value_t *pOut, const key_value_t *pSource, order_t known ) noexcept
{
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i ) {
        const key_value_t *pMember = KeyValue_ChildAt( pSource, i );
        if ( !IsListed( KeyValue_Name( pMember ), known ) && !Clone( pDocument, pOut, pMember ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t WriteInfoBounds( writer_ctx_t &ctx, key_value_t *pObject, const map_bounds_t &bounds ) noexcept
{
    if ( !bounds.bHas ) { return CY_TRUE; }
    key_value_t *pInfo = KeyValue_ObjectInsert( ctx.pDocument, pObject, SV( "info" ), key_value_type_t::OBJECT );
    return pInfo != nullptr && MapGeometry_WriteBounds( ctx.pDocument, pInfo, "bounds", bounds );
}

CYPHER_NODISCARD bool_t WriteLive( writer_ctx_t &ctx, key_value_t *pArray, const live_t &live ) noexcept
{
    key_value_document_t *pDocument = ctx.pDocument;
    const geo::geometry_fragment_t &fragment = ctx.pPlan->fragment;
    key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
    if ( pOut == nullptr || !SetU64Member( pDocument, pOut, "id", live.id ) ) { return CY_FALSE; }
    const key_value_t *pRecord = live.pRecord;
    for ( const char *pName : { "name", "comment" } ) {
        const key_value_t *pMember = KeyValue_Find( pRecord, SV( pName ) );
        if ( pMember != nullptr && !Clone( pDocument, pOut, pMember ) ) { return CY_FALSE; }
    }
    const key_value_t *pVisgroups = KeyValue_Find( pRecord, SV( "visgroups" ) );
    if ( pVisgroups != nullptr && !CloneSortedIds( ctx, pOut, "visgroups", pVisgroups ) ) { return CY_FALSE; }
    if ( live.bounds.bHas || live.kind == map_geometry_kind_t::MESH ) {
        key_value_t *pInfo = KeyValue_ObjectInsert( pDocument, pOut, SV( "info" ), key_value_type_t::OBJECT );
        if ( pInfo == nullptr || ( live.bounds.bHas && !MapGeometry_WriteBounds( pDocument, pInfo, "bounds", live.bounds ) ) ) { return CY_FALSE; }
        if ( live.kind == map_geometry_kind_t::MESH &&
             ( !SetI64Member( pDocument, pInfo, "vertices", static_cast<i64>( live.nVertices ) ) ||
               !SetI64Member( pDocument, pInfo, "faces", static_cast<i64>( live.nFaces ) ) ) ) {
            return CY_FALSE;
        }
    }
    bool_t bWritten = CY_FALSE;
    switch ( live.kind ) {
        case map_geometry_kind_t::BRUSH:
            bWritten = MapGeometry_WriteBrush( *fragment.brushes.pData[live.iObject], ctx.pMap->materials, pDocument, pOut );
            break;
        case map_geometry_kind_t::MESH:
            bWritten = MapGeometry_WriteMesh( *fragment.meshes.pData[live.iObject], ctx.pMap->materials, ctx.pMap->pAllocator, pDocument, pOut );
            break;
        case map_geometry_kind_t::PATCH:
            bWritten = MapGeometry_WritePatch( *fragment.patches.pData[live.iObject], ctx.pMap->materials, pDocument, pOut );
            break;
        case map_geometry_kind_t::TERRAIN:
            bWritten = MapGeometry_WriteTerrain( *fragment.heightFields.pData[live.iObject], pDocument, pOut );
            break;
        case map_geometry_kind_t::COUNT: break;
    }
    if ( !bWritten || ( pRecord != nullptr && !MapGeometry_MergeResidual( live.kind, pDocument, pOut, pRecord ) ) ) { return CY_FALSE; }
    for ( const char *pName : kGeometryTail ) {
        const key_value_t *pMember = KeyValue_Find( pRecord, SV( pName ) );
        if ( pMember != nullptr && !Clone( pDocument, pOut, pMember ) ) { return CY_FALSE; }
    }
    if ( pRecord == nullptr ) { return CY_TRUE; }
    for ( usize i = 0u; i < KeyValue_ChildCount( pRecord ); ++i ) {
        const key_value_t *pMember = KeyValue_ChildAt( pRecord, i );
        const string_view_t name = KeyValue_Name( pMember );
        if ( !IsListed( name, Order( kGeometryLead ) ) && !IsListed( name, Order( kGeometryTail ) ) &&
             !MapGeometry_IsGeometryMember( live.kind, name ) && !Clone( pDocument, pOut, pMember ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

// Every output member written, defaults included (CYMAP.md 6.2).
CYPHER_NODISCARD bool_t WriteOutput( key_value_document_t *pDocument, key_value_t *pArray, const key_value_t *pSource ) noexcept
{
    key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
    if ( pOut == nullptr ) { return CY_FALSE; }
    for ( const char *pName : kOutputOrder ) {
        const key_value_t *pMember = KeyValue_Find( pSource, SV( pName ) );
        if ( pMember != nullptr ) {
            if ( !CloneCanonical( pDocument, pOut, pMember ) ) { return CY_FALSE; }
            continue;
        }
        const string_view_t name = SV( pName );
        bool_t bOk = CY_TRUE;
        if ( StringView_Equals( name, SV( "parameter" ) ) ) {
            bOk = SetStringMember( pDocument, pOut, pName, SV( "" ) );
        } else if ( StringView_Equals( name, SV( "delay" ) ) ) {
            bOk = SetRealMember( pDocument, pOut, pName, 0.0 );
        } else if ( StringView_Equals( name, SV( "times" ) ) ) {
            bOk = SetI64Member( pDocument, pOut, pName, -1 );
        }
        if ( !bOk ) { return CY_FALSE; }
    }
    return CloneUnknown( pDocument, pOut, pSource, Order( kOutputOrder ) );
}

CYPHER_NODISCARD bool_t WriteEntity( writer_ctx_t &ctx, key_value_t *pArray, const key_value_t *pEntity, u64 id ) noexcept
{
    key_value_document_t *pDocument = ctx.pDocument;
    const save_plan_t &plan = *ctx.pPlan;
    key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
    if ( pOut == nullptr ) { return CY_FALSE; }
    const owned_t *pBegin = std::lower_bound( plan.owned.pData, plan.owned.pData + Vector_Count( &plan.owned ), id,
                                              []( const owned_t &entry, u64 value ) { return entry.owner < value; } );
    const owned_t *pEnd = pBegin;
    while ( pEnd < plan.owned.pData + Vector_Count( &plan.owned ) && pEnd->owner == id ) { ++pEnd; }
    for ( const char *pName : kEntityOrder ) {
        const string_view_t name = SV( pName );
        if ( StringView_Equals( name, SV( "info" ) ) ) {
            map_bounds_t bounds{};
            for ( const owned_t *pOwned = pBegin; pOwned < pEnd; ++pOwned ) { MapBounds_AddBounds( bounds, pOwned->pLive->bounds ); }
            if ( !WriteInfoBounds( ctx, pOut, bounds ) ) { return CY_FALSE; }
            continue;
        }
        bool_t bGeometry = CY_FALSE;
        for ( map_geometry_kind_t kind : kOwnedKinds ) {
            if ( !StringView_Equals( name, SV( MapGeometry_SectionName( kind ) ) ) ) { continue; }
            bGeometry = CY_TRUE;
            // Owned live geometry first (sorted by ID), then records that
            // could not be read, as they were.
            const key_value_t *pNested = KeyValue_Find( pEntity, name );
            usize nCount = 0u;
            for ( const owned_t *pOwned = pBegin; pOwned < pEnd; ++pOwned ) { nCount += pOwned->pLive->kind == kind ? 1u : 0u; }
            for ( usize i = 0u; i < KeyValue_ChildCount( pNested ); ++i ) {
                u64 recordId = 0u;
                const key_value_t *pRecord = KeyValue_ChildAt( pNested, i );
                const bool_t bLive = KeyValue_Type( pRecord ) == key_value_type_t::OBJECT &&
                                     ReadId( KeyValue_Find( pRecord, SV( "id" ) ), recordId ) && FindLive( plan, recordId ) != nullptr;
                nCount += bLive ? 0u : 1u;
            }
            if ( nCount == 0u ) { break; }
            key_value_t *pOwnedArray = KeyValue_ObjectInsert( pDocument, pOut, name, key_value_type_t::ARRAY );
            if ( pOwnedArray == nullptr ) { return CY_FALSE; }
            for ( const owned_t *pOwned = pBegin; pOwned < pEnd; ++pOwned ) {
                if ( pOwned->pLive->kind == kind && !WriteLive( ctx, pOwnedArray, *pOwned->pLive ) ) { return CY_FALSE; }
            }
            for ( usize i = 0u; i < KeyValue_ChildCount( pNested ); ++i ) {
                u64 recordId = 0u;
                const key_value_t *pRecord = KeyValue_ChildAt( pNested, i );
                const bool_t bLive = KeyValue_Type( pRecord ) == key_value_type_t::OBJECT &&
                                     ReadId( KeyValue_Find( pRecord, SV( "id" ) ), recordId ) && FindLive( plan, recordId ) != nullptr;
                if ( !bLive && !Clone( pDocument, pOwnedArray, pRecord ) ) { return CY_FALSE; }
            }
            break;
        }
        if ( bGeometry ) { continue; }
        const key_value_t *pMember = KeyValue_Find( pEntity, name );
        if ( pMember == nullptr ) { continue; }
        bool_t bOk = CY_TRUE;
        if ( StringView_Equals( name, SV( "angles" ) ) ) {
            bOk = IsUniform( pMember, 0.0 ) || CloneCanonical( pDocument, pOut, pMember );
        } else if ( StringView_Equals( name, SV( "scale" ) ) ) {
            bOk = IsUniform( pMember, 1.0 ) || CloneCanonical( pDocument, pOut, pMember );
        } else if ( StringView_Equals( name, SV( "visgroups" ) ) ) {
            bOk = CloneSortedIds( ctx, pOut, pName, pMember );
        } else if ( StringView_Equals( name, SV( "outputs" ) ) && KeyValue_Type( pMember ) == key_value_type_t::ARRAY ) {
            key_value_t *pOutputs = KeyValue_ObjectInsert( pDocument, pOut, name, key_value_type_t::ARRAY );
            bOk = pOutputs != nullptr;
            // Outputs keep their authored order: it is the order they fire in.
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pMember ); ++i ) {
                const key_value_t *pOutput = KeyValue_ChildAt( pMember, i );
                bOk = KeyValue_Type( pOutput ) == key_value_type_t::OBJECT ? WriteOutput( pDocument, pOutputs, pOutput )
                                                                            : Clone( pDocument, pOutputs, pOutput );
            }
        } else {
            bOk = CloneCanonical( pDocument, pOut, pMember );
        }
        if ( !bOk ) { return CY_FALSE; }
    }
    return CloneUnknown( pDocument, pOut, pEntity, Order( kEntityOrder ) );
}

CYPHER_NODISCARD bool_t WriteOrdered( writer_ctx_t &ctx, key_value_t *pArray, const key_value_t *pObject, u32 section ) noexcept
{
    key_value_document_t *pDocument = ctx.pDocument;
    const order_t order = SectionOrder( section );
    key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
    if ( pOut == nullptr ) { return CY_FALSE; }
    for ( usize i = 0u; i < order.nNames; ++i ) {
        const string_view_t name = SV( order.pNames[i] );
        if ( StringView_Equals( name, SV( "info" ) ) ) {
            if ( section == SEC_SHAPES && !WriteInfoBounds( ctx, pOut, ShapeBounds( pObject ) ) ) { return CY_FALSE; }
            continue;
        }
        const key_value_t *pMember = KeyValue_Find( pObject, name );
        if ( pMember == nullptr ) { continue; }
        const bool_t bIds = StringView_Equals( name, SV( "visgroups" ) ) || StringView_Equals( name, SV( "members" ) );
        if ( !( bIds ? CloneSortedIds( ctx, pOut, order.pNames[i], pMember ) : CloneCanonical( pDocument, pOut, pMember ) ) ) { return CY_FALSE; }
    }
    return CloneUnknown( pDocument, pOut, pObject, order );
}

CYPHER_NODISCARD bool_t WriteFoliage( writer_ctx_t &ctx, key_value_t *pRoot, target_t &target ) noexcept
{
    key_value_document_t *pDocument = ctx.pDocument;
    vector_t<placed_t> &verbatim = target.sections[SEC_FOLIAGE];
    if ( Vector_Count( &target.foliage ) == 0u && Vector_Count( &verbatim ) == 0u ) { return CY_TRUE; }
    std::stable_sort( target.foliage.pData, target.foliage.pData + Vector_Count( &target.foliage ),
                      []( const foliage_item_t &a, const foliage_item_t &b ) {
                          const i32 comparison = StringView_Compare( a.model, b.model );
                          if ( comparison != 0 ) { return comparison < 0; }
                          if ( a.bReadable != b.bReadable ) { return a.bReadable; }
                          if ( !a.bReadable ) { return a.order < b.order; }
                          for ( usize c = 0u; c < 3u; ++c ) {
                              if ( a.position[c] != b.position[c] ) { return a.position[c] < b.position[c]; }
                          }
                          return a.order < b.order;
                      } );
    key_value_t *pArray = KeyValue_ObjectInsert( pDocument, pRoot, SV( kSections[SEC_FOLIAGE] ), key_value_type_t::ARRAY );
    if ( pArray == nullptr ) { return CY_FALSE; }
    for ( usize i = 0u; i < Vector_Count( &target.foliage ); ) {
        const string_view_t model = target.foliage.pData[i].model;
        key_value_t *pRecord = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
        key_value_t *pInstances = nullptr;
        if ( pRecord == nullptr || !SetStringMember( pDocument, pRecord, "model", model ) ||
             ( pInstances = KeyValue_ObjectInsert( pDocument, pRecord, SV( "instances" ), key_value_type_t::ARRAY ) ) == nullptr ) {
            return CY_FALSE;
        }
        for ( ; i < Vector_Count( &target.foliage ) && StringView_Equals( target.foliage.pData[i].model, model ); ++i ) {
            const foliage_item_t &item = target.foliage.pData[i];
            if ( !item.bReadable ) {
                if ( !Clone( pDocument, pInstances, item.pInstance ) ) { return CY_FALSE; }
                continue;
            }
            key_value_t *pInstance = KeyValue_ArrayAppend( pDocument, pInstances, key_value_type_t::ARRAY );
            if ( pInstance == nullptr || !AppendReals( pDocument, pInstance, item.pInstance ) ) { return CY_FALSE; }
        }
        for ( usize k = 0u; k < Vector_Count( &ctx.pPlan->foliageProps ); ++k ) {
            const foliage_props_t &props = ctx.pPlan->foliageProps.pData[k];
            if ( props.iLayer == target.iLayer && StringView_Equals( props.model, model ) ) {
                if ( props.pProperties != nullptr && !Clone( pDocument, pRecord, props.pProperties ) ) { return CY_FALSE; }
                break;
            }
        }
    }
    for ( usize i = 0u; i < Vector_Count( &verbatim ); ++i ) {
        if ( !Clone( pDocument, pArray, verbatim.pData[i].pNode ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Values in a tree, stopping once past nLimit.
CYPHER_NODISCARD usize CountValues( const key_value_t *pNode, usize nLimit ) noexcept
{
    usize nCount = 1u;
    for ( usize i = 0u; i < KeyValue_ChildCount( pNode ) && nCount <= nLimit; ++i ) {
        nCount += CountValues( KeyValue_ChildAt( pNode, i ), nLimit - nCount );
    }
    return nCount;
}

// Builds one output chunk tree in canonical order. Nothing is written here;
// the caller checks every target before the sink sees any of them.
CYPHER_NODISCARD map_status_t BuildTargetOutput( writer_ctx_t &ctx, target_t &target ) noexcept
{
    map_document_t *pMap = ctx.pMap;
    usize nObjects = 0u;
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section != SEC_FOLIAGE ) { nObjects += Vector_Count( &target.sections[section] ); }
    }
    if ( nObjects > MAP_CHUNK_OBJECTS_MAX || Vector_Count( &target.foliage ) > MAP_FOLIAGE_INSTANCES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }

    void *pMemory = Allocator_Allocate( pMap->pAllocator, sizeof( settings_document_t ), alignof( settings_document_t ) );
    if ( pMemory == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    target.pOutput = new ( pMemory ) settings_document_t{};
    if ( SettingsDocument_Init( target.pOutput, pMap->pAllocator, ChunkIdentity() ) != settings_document_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    key_value_document_t *pDocument = target.pOutput->pDocument;
    ctx.pDocument = pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );

    char mapIdText[CY_UNIQUE_ID_STRING_CAPACITY]{};
    const usize cchMapId = UniqueId_ToString( pMap->mapId, mapIdText, sizeof( mapIdText ) );
    key_value_t *pCell = nullptr;
    if ( !SetStringMember( pDocument, pRoot, "map_id", { mapIdText, cchMapId } ) ||
         !SetStringMember( pDocument, pRoot, "layer", SV( pMap->layers.pData[target.iLayer].id ) ) ||
         ( pCell = KeyValue_ObjectInsert( pDocument, pRoot, SV( "cell" ), key_value_type_t::NULL_VALUE ) ) == nullptr ||
         !MapCell_Write( pDocument, pCell, target.cell ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }

    // Reader summary: counts per section and the bounds of everything placed.
    key_value_t *pInfo = KeyValue_ObjectInsert( pDocument, pRoot, SV( "info" ), key_value_type_t::OBJECT );
    if ( pInfo == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        const usize nCount = section == SEC_FOLIAGE ? Vector_Count( &target.foliage ) : Vector_Count( &target.sections[section] );
        if ( nCount != 0u && !SetI64Member( pDocument, pInfo, kSections[section], static_cast<i64>( nCount ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( target.bounds.bHas && !MapGeometry_WriteBounds( pDocument, pInfo, "bounds", target.bounds ) ) { return map_status_t::OUT_OF_MEMORY; }

    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section == SEC_FOLIAGE ) {
            if ( !WriteFoliage( ctx, pRoot, target ) ) { return map_status_t::OUT_OF_MEMORY; }
            continue;
        }
        vector_t<placed_t> &placed = target.sections[section];
        if ( Vector_Count( &placed ) == 0u ) { continue; }
        std::stable_sort( placed.pData, placed.pData + Vector_Count( &placed ), []( const placed_t &a, const placed_t &b ) { return a.key < b.key; } );
        key_value_t *pArray = KeyValue_ObjectInsert( pDocument, pRoot, SV( kSections[section] ), key_value_type_t::ARRAY );
        if ( pArray == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize i = 0u; i < Vector_Count( &placed ); ++i ) {
            const placed_t &item = placed.pData[i];
            bool_t bOk = CY_FALSE;
            if ( item.pLive != nullptr ) {
                bOk = WriteLive( ctx, pArray, *item.pLive );
            } else if ( item.key == ~0ull ) {
                bOk = Clone( pDocument, pArray, item.pNode );
            } else if ( section == SEC_ENTITIES ) {
                bOk = WriteEntity( ctx, pArray, item.pNode, item.key );
            } else {
                bOk = WriteOrdered( ctx, pArray, item.pNode, section );
            }
            if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    for ( usize i = 0u; i < Vector_Count( &target.extras ); ++i ) {
        const key_value_t *pExtra = target.extras.pData[i];
        // Two files for one chunk (a renamed copy beside the canonical one)
        // can carry the same unknown member. Identical copies collapse;
        // different ones cannot both be kept under one name, and dropping
        // either would lose data, so the save is refused.
        if ( const key_value_t *pExisting = KeyValue_Find( pRoot, KeyValue_Name( pExtra ) ) ) {
            if ( KeyValuesEqual( pExisting, pExtra ) ) { continue; }
            return map_status_t::SAVE_BLOCKED;
        }
        if ( !Clone( pDocument, pRoot, pExtra ) ) { return map_status_t::OUT_OF_MEMORY; }
    }

    // The reader's value budget is a limit like the text size: a chunk it
    // would reject is refused here instead of written.
    if ( CountValues( pRoot, MAP_CHUNK_VALUES_MAX ) > MAP_CHUNK_VALUES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    void *pTextMemory = Allocator_Allocate( pMap->pAllocator, sizeof( text_buffer_t ), alignof( text_buffer_t ) );
    if ( pTextMemory == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    target.pText = new ( pTextMemory ) text_buffer_t{};
    FormatChunkPath( SV( pMap->layers.pData[target.iLayer].id ), target.cell, target.path );
    return WriteCompactText( target.pOutput, pMap->pAllocator, target.pText, MAP_CHUNK_TEXT_MAX );
}

CYPHER_NODISCARD bool_t WriteOrderedArray( key_value_document_t *pDocument, key_value_t *pRoot, const key_value_t *pSource, order_t order, save_plan_t &plan ) noexcept
{
    key_value_t *pArray = KeyValue_ObjectInsert( pDocument, pRoot, KeyValue_Name( pSource ), key_value_type_t::ARRAY );
    if ( pArray == nullptr ) { return CY_FALSE; }
    writer_ctx_t ctx{ nullptr, &plan, pDocument };
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i ) {
        const key_value_t *pEntry = KeyValue_ChildAt( pSource, i );
        if ( KeyValue_Type( pEntry ) != key_value_type_t::OBJECT ) {
            if ( !Clone( pDocument, pArray, pEntry ) ) { return CY_FALSE; }
            continue;
        }
        key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
        if ( pOut == nullptr ) { return CY_FALSE; }
        for ( usize k = 0u; k < order.nNames; ++k ) {
            const key_value_t *pMember = KeyValue_Find( pEntry, SV( order.pNames[k] ) );
            if ( pMember == nullptr ) { continue; }
            const bool_t bIds = StringView_Equals( SV( order.pNames[k] ), SV( "members" ) );
            if ( !( bIds ? CloneSortedIds( ctx, pOut, order.pNames[k], pMember ) : CloneCanonical( pDocument, pOut, pMember ) ) ) { return CY_FALSE; }
        }
        if ( !CloneUnknown( pDocument, pOut, pEntry, order ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD map_status_t BuildRootOutput( map_document_t *pMap, save_plan_t &plan, settings_document_t *pOutput, text_buffer_t *pText ) noexcept
{
    if ( !SyncRootLayers( pMap ) || !SetU64Member( pMap->root.pDocument, KeyValue_Root( pMap->root.pDocument ), "next_id", pMap->nextId ) ||
         SettingsDocument_Init( pOutput, pMap->pAllocator, RootIdentity() ) != settings_document_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    key_value_document_t *pDocument = pOutput->pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );
    const key_value_t *pWorking = SettingsDocument_Root( &pMap->root );
    for ( const char *pName : kRootOrder ) {
        const key_value_t *pMember = KeyValue_Find( pWorking, SV( pName ) );
        if ( pMember == nullptr ) { continue; }
        const string_view_t name = SV( pName );
        const bool_t bArray = KeyValue_Type( pMember ) == key_value_type_t::ARRAY;
        bool_t bOk = CY_TRUE;
        if ( bArray && StringView_Equals( name, SV( "layers" ) ) ) {
            bOk = WriteOrderedArray( pDocument, pRoot, pMember, Order( kLayerOrder ), plan );
        } else if ( bArray && StringView_Equals( name, SV( "visgroups" ) ) ) {
            bOk = WriteOrderedArray( pDocument, pRoot, pMember, Order( kVisgroupOrder ), plan );
        } else if ( bArray && StringView_Equals( name, SV( "selection_sets" ) ) ) {
            bOk = WriteOrderedArray( pDocument, pRoot, pMember, Order( kSelectionSetOrder ), plan );
        } else if ( bArray && StringView_Equals( name, SV( "cordons" ) ) ) {
            bOk = WriteOrderedArray( pDocument, pRoot, pMember, Order( kCordonOrder ), plan );
        } else if ( bArray && StringView_Equals( name, SV( "tags" ) ) ) {
            // Tags are a set; sorted, they never cause a diff by order.
            string_view_t tags[64]{};
            usize nTags = 0u;
            bool_t bStrings = KeyValue_ChildCount( pMember ) <= std::size( tags );
            for ( usize i = 0u; bStrings && i < KeyValue_ChildCount( pMember ); ++i ) {
                bStrings = KeyValue_GetString( KeyValue_ChildAt( pMember, i ), &tags[nTags++] );
            }
            if ( !bStrings ) {
                bOk = Clone( pDocument, pRoot, pMember );
            } else {
                std::sort( tags, tags + nTags, []( string_view_t a, string_view_t b ) { return StringView_Compare( a, b ) < 0; } );
                key_value_t *pTags = KeyValue_ObjectInsert( pDocument, pRoot, name, key_value_type_t::ARRAY );
                bOk = pTags != nullptr;
                for ( usize i = 0u; bOk && i < nTags; ++i ) {
                    key_value_t *pTag = KeyValue_ArrayAppend( pDocument, pTags, key_value_type_t::NULL_VALUE );
                    bOk = pTag != nullptr && KeyValue_SetString( pDocument, pTag, tags[i] );
                }
            }
        } else {
            bOk = Clone( pDocument, pRoot, pMember );
        }
        if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( !CloneUnknown( pDocument, pRoot, pWorking, Order( kRootOrder ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    return WriteCompactText( pOutput, pMap->pAllocator, pText, MAP_ROOT_TEXT_MAX );
}

// After a save the working chunks are the written texts, minus `info` and
// minus the geometry members of live objects (their geometry stays in the
// geometry document). Records that could not be read stay whole.
CYPHER_NODISCARD bool_t StripAfterSave( key_value_document_t *pDocument, const save_plan_t &plan ) noexcept
{
    key_value_t *pRoot = KeyValue_Root( pDocument );
    if ( !StripInfo( pDocument, pRoot ) ) { return CY_FALSE; }
    const auto strip = [&]( map_geometry_kind_t kind, key_value_t *pRecord ) noexcept {
        u64 id = 0u;
        if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) || FindLive( plan, id ) == nullptr ) {
            return CY_TRUE;
        }
        return MapGeometry_StripRecord( kind, pDocument, pRecord );
    };
    for ( u32 section = SEC_BRUSHES; section <= SEC_TERRAINS; ++section ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            if ( !strip( KindOfSection( section ), KeyValue_ChildAt( pArray, i ) ) ) { return CY_FALSE; }
        }
    }
    key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
    for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i ) {
        key_value_t *pEntity = KeyValue_ChildAt( pEntities, i );
        if ( !EntityShapeOk( pEntity ) ) { continue; }
        for ( map_geometry_kind_t kind : kOwnedKinds ) {
            key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
            for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                if ( !strip( kind, KeyValue_ChildAt( pNested, j ) ) ) { return CY_FALSE; }
            }
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD key_value_t *FindObjectInternal( map_document_t *pMap, u64 id, map_chunk_t **ppChunkOut, key_value_t **ppParentOut ) noexcept
{
    if ( pMap == nullptr || id == 0u ) { return nullptr; }
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        key_value_t *pRoot = KeyValue_Root( pChunk->store.pDocument );
        for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
            if ( section == SEC_FOLIAGE ) { continue; }
            key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
                key_value_t *pObject = KeyValue_ChildAt( pArray, i );
                u64 objectId = 0u;
                if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
                if ( ReadId( KeyValue_Find( pObject, SV( "id" ) ), objectId ) && objectId == id ) {
                    if ( ppChunkOut != nullptr ) { *ppChunkOut = pChunk; }
                    if ( ppParentOut != nullptr ) { *ppParentOut = pArray; }
                    return pObject;
                }
                if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
                for ( map_geometry_kind_t kind : kOwnedKinds ) {
                    key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                    for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j ) {
                        key_value_t *pRecord = KeyValue_ChildAt( pNested, j );
                        if ( ReadId( KeyValue_Find( pRecord, SV( "id" ) ), objectId ) && objectId == id ) {
                            if ( ppChunkOut != nullptr ) { *ppChunkOut = pChunk; }
                            if ( ppParentOut != nullptr ) { *ppParentOut = pNested; }
                            return pRecord;
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

map_document_t::~map_document_t() noexcept
{
    MapDocument_Shutdown( this );
}

void MapDocument_Shutdown( map_document_t *pMap ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr ) { return; }
    for ( usize i = 0u; i < Vector_Count( &pMap->chunks ); ++i ) { DestroyChunk( pMap->pAllocator, pMap->chunks.pData[i] ); }
    Vector_Shutdown( &pMap->chunks );
    Vector_Shutdown( &pMap->layers );
    Vector_Shutdown( &pMap->geometryRecords );
    Vector_Shutdown( &pMap->problems );
    MapMaterials_Shutdown( &pMap->materials );
    geo::GeometryDocument_Shutdown( &pMap->geometry );
    SettingsDocument_Shutdown( &pMap->root );
    pMap->pAllocator = nullptr;
    pMap->nextId = 1u;
    pMap->bReadOnly = CY_FALSE;
    pMap->bIdsAssigned = CY_FALSE;
}

namespace
{

CYPHER_NODISCARD map_status_t InitContainers( map_document_t *pMap, const allocator_t *pAllocator ) noexcept
{
    pMap->pAllocator = pAllocator;
    const bool_t bOk = Vector_Init( &pMap->chunks, pAllocator ) && Vector_Init( &pMap->layers, pAllocator ) &&
                       Vector_Init( &pMap->geometryRecords, pAllocator ) && Vector_Init( &pMap->problems, pAllocator ) &&
                       MapMaterials_Init( &pMap->materials, pAllocator ) &&
                       geo::GeometryDocument_Init( &pMap->geometry, pAllocator, pMap->geometryPolicy ) == geo::geometry_status_t::OK;
    return bOk ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

// Duplicate IDs: reported (the map opens read-only), or with the reassign
// flag every occurrence after the first gets a fresh ID.
CYPHER_NODISCARD bool_t HandleDuplicates( map_document_t *pMap, const vector_t<u64> &duplicates, bool_t bReassign, u64 &nextId ) noexcept
{
    vector_t<u64> seen{};
    if ( !Vector_Init( &seen, pMap->pAllocator ) ) { return CY_FALSE; }
    const auto isDuplicate = [&]( u64 id ) noexcept {
        return std::binary_search( duplicates.pData, duplicates.pData + Vector_Count( &duplicates ), id );
    };
    bool_t bOk = CY_TRUE;
    const auto walk = [&]( key_value_document_t *pDocument, key_value_t *pRoot, string_view_t path, bool_t bRoot ) noexcept {
        auto visit = [&]( key_value_t *pSlot, u64 id ) noexcept {
            if ( !isDuplicate( id ) ) { return CY_TRUE; }
            if ( !bReassign ) {
                AddProblem( pMap, map_problem_code_t::DUPLICATE_ID, path, nullptr, id );
                return CY_TRUE;
            }
            u64 *pSeen = std::lower_bound( seen.pData, seen.pData + Vector_Count( &seen ), id );
            if ( pSeen == seen.pData + Vector_Count( &seen ) || *pSeen != id ) {
                bOk = Vector_Insert( &seen, static_cast<usize>( pSeen - seen.pData ), id );
                return bOk;
            }
            const u64 fresh = nextId++;
            AddProblem( pMap, map_problem_code_t::ID_ASSIGNED, path, "duplicate", fresh );
            pMap->bIdsAssigned = CY_TRUE;
            bOk = KeyValue_SetU64( pDocument, pSlot, fresh );
            return bOk;
        };
        return bRoot ? VisitRootIds( pRoot, visit ) : VisitChunkIds( pRoot, visit );
    };
    bOk = walk( pMap->root.pDocument, KeyValue_Root( pMap->root.pDocument ), {}, CY_TRUE );
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( !pChunk->bDamaged ) {
            bOk = walk( pChunk->store.pDocument, KeyValue_Root( pChunk->store.pDocument ), SV( pChunk->sourcePath ), CY_FALSE );
        }
    }
    if ( !bReassign ) { pMap->bReadOnly = CY_TRUE; }
    return bOk;
}

// Records of one foliage model in one layer should agree on properties; the
// first (in path order) is what saving keeps.
void CheckFoliage( map_document_t *pMap ) noexcept
{
    struct seen_t {
        u32 iLayer;
        string_view_t model;
        const key_value_t *pProperties;
    };
    vector_t<seen_t> seen{};
    if ( !Vector_Init( &seen, pMap->pAllocator ) ) { return; }
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pChunk->layer ) ) );
        const key_value_t *pFoliage = KeyValue_Find( SettingsDocument_Root( &pChunk->store ), SV( kSections[SEC_FOLIAGE] ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pFoliage ); ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pFoliage, i );
            string_view_t model{};
            if ( !KeyValue_GetString( KeyValue_Find( pRecord, SV( "model" ) ), &model ) ) { continue; }
            const key_value_t *pProperties = KeyValue_Find( pRecord, SV( "properties" ) );
            const seen_t *pFirst = nullptr;
            for ( usize k = 0u; k < Vector_Count( &seen ) && pFirst == nullptr; ++k ) {
                if ( seen.pData[k].iLayer == iLayer && StringView_Equals( seen.pData[k].model, model ) ) { pFirst = &seen.pData[k]; }
            }
            if ( pFirst == nullptr ) {
                ( void )Vector_PushBack( &seen, seen_t{ iLayer, model, pProperties } );
                continue;
            }
            const bool_t bSame = ( pFirst->pProperties == nullptr && pProperties == nullptr ) ||
                                 ( pFirst->pProperties != nullptr && pProperties != nullptr && KeyValuesEqual( pFirst->pProperties, pProperties ) );
            if ( !bSame ) { AddProblem( pMap, map_problem_code_t::FOLIAGE_PROPERTIES_DIFFER, SV( pChunk->sourcePath ), "foliage", 0u ); }
        }
    }
}

} // namespace

map_status_t MapDocument_Create( map_document_t *pMap, const allocator_t *pAllocator, const map_create_desc_t &desc ) noexcept
{
    const string_view_t layer = desc.layer.cchLength != 0u ? desc.layer : SV( "default" );
    if ( pMap == nullptr || pAllocator == nullptr || pMap->pAllocator != nullptr || desc.name.cchLength == 0u ||
         desc.name.cchLength > MAP_NAME_MAX_LENGTH || !IsStableId( desc.game, 64u ) || !IsStableId( layer, 64u ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    map_status_t status = InitContainers( pMap, pAllocator );
    if ( status != map_status_t::OK ) {
        MapDocument_Shutdown( pMap );
        return status;
    }
    char mapIdText[CY_UNIQUE_ID_STRING_CAPACITY]{};
    map_layer_t firstLayer{};
    key_value_t *pLayers = nullptr;
    key_value_t *pLayer = nullptr;
    const bool_t bOk = UniqueId_CreateRandom( &pMap->mapId ) &&
                       SettingsDocument_Init( &pMap->root, pAllocator, RootIdentity() ) == settings_document_status_t::OK &&
                       CopyText( firstLayer.id, layer ) && Vector_PushBack( &pMap->layers, firstLayer );
    key_value_document_t *pDocument = bOk ? pMap->root.pDocument : nullptr;
    key_value_t *pRoot = bOk ? KeyValue_Root( pDocument ) : nullptr;
    const usize cchMapId = bOk ? UniqueId_ToString( pMap->mapId, mapIdText, sizeof( mapIdText ) ) : 0u;
    const bool_t bWritten = bOk && cchMapId != 0u && SetStringMember( pDocument, pRoot, "map_id", { mapIdText, cchMapId } ) &&
                            SetStringMember( pDocument, pRoot, "name", desc.name ) && SetStringMember( pDocument, pRoot, "game", desc.game ) &&
                            SetU64Member( pDocument, pRoot, "next_id", 1u ) &&
                            ( pLayers = KeyValue_ObjectInsert( pDocument, pRoot, SV( "layers" ), key_value_type_t::ARRAY ) ) != nullptr &&
                            ( pLayer = KeyValue_ArrayAppend( pDocument, pLayers, key_value_type_t::OBJECT ) ) != nullptr &&
                            SetStringMember( pDocument, pLayer, "id", layer );
    if ( !bWritten ) {
        MapDocument_Shutdown( pMap );
        return map_status_t::OUT_OF_MEMORY;
    }
    return map_status_t::OK;
}

map_status_t MapDocument_Load(
    map_document_t *pMap,
    const allocator_t *pAllocator,
    string_view_t rootText,
    span_t<const map_chunk_input_t> chunks,
    u32 flags ) noexcept
{
    if ( pMap == nullptr || pAllocator == nullptr || pMap->pAllocator != nullptr || rootText.pData == nullptr ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    map_status_t status = InitContainers( pMap, pAllocator );
    const auto fail = [&]( map_status_t failure ) noexcept {
        MapDocument_Shutdown( pMap );
        return failure;
    };
    if ( status != map_status_t::OK ) { return fail( status ); }
    if ( SettingsDocument_Init( &pMap->root, pAllocator, RootIdentity() ) != settings_document_status_t::OK ) {
        return fail( map_status_t::OUT_OF_MEMORY );
    }
    if ( rootText.cchLength > MAP_ROOT_TEXT_MAX || SettingsDocument_Load( &pMap->root, rootText ).status != settings_document_status_t::OK ) {
        return fail( map_status_t::ROOT_UNREADABLE );
    }
    status = ReadRoot( pMap );
    if ( status != map_status_t::OK ) {
        // Keep the diagnostics for the caller; drop everything else.
        vector_t<map_problem_t> problems{};
        Vector_Move( &problems, &pMap->problems );
        MapDocument_Shutdown( pMap );
        Vector_Move( &pMap->problems, &problems );
        return status;
    }

    // Chunks in path order, whatever order the directory listing gave, so IDs
    // assigned on load are the same everywhere.
    vector_t<usize> order{};
    if ( !Vector_Init( &order, pAllocator ) || !Vector_Resize( &order, chunks.nCount ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    for ( usize i = 0u; i < chunks.nCount; ++i ) { order.pData[i] = i; }
    std::stable_sort( order.pData, order.pData + chunks.nCount, [&]( usize a, usize b ) {
        return StringView_Compare( chunks.pData[a].path, chunks.pData[b].path ) < 0;
    } );
    char mapIdText[CY_UNIQUE_ID_STRING_CAPACITY]{};
    const usize cchMapId = UniqueId_ToString( pMap->mapId, mapIdText, sizeof( mapIdText ) );
    for ( usize i = 0u; i < chunks.nCount; ++i ) {
        status = OpenChunk( pMap, chunks.pData[order.pData[i]], { mapIdText, cchMapId } );
        if ( status != map_status_t::OK ) { return fail( status ); }
    }

    // Identity: every explicit ID across the root and readable chunks.
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, pAllocator ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    bool_t bOk = CY_TRUE;
    auto collect = [&]( key_value_t *, u64 id ) noexcept {
        bOk = Vector_PushBack( &ids, id );
        return bOk;
    };
    ( void )VisitRootIds( KeyValue_Root( pMap->root.pDocument ), collect );
    for ( usize iChunk = 0u; bOk && iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( !pChunk->bDamaged ) { ( void )VisitChunkIds( KeyValue_Root( pChunk->store.pDocument ), collect ); }
    }
    if ( !bOk ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    Sort_Unstable( Vector_Span( &ids ) );
    vector_t<u64> duplicates{};
    if ( !Vector_Init( &duplicates, pAllocator ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    for ( usize i = 1u; i < Vector_Count( &ids ); ++i ) {
        const bool_t bRepeat = ids.pData[i] == ids.pData[i - 1u];
        const bool_t bFirstRepeat = bRepeat && ( Vector_Count( &duplicates ) == 0u || duplicates.pData[Vector_Count( &duplicates ) - 1u] != ids.pData[i] );
        if ( bFirstRepeat && !Vector_PushBack( &duplicates, ids.pData[i] ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    }
    const u64 rootNextId = pMap->nextId;
    const u64 maxId = Vector_Count( &ids ) != 0u ? ids.pData[Vector_Count( &ids ) - 1u] : 0u;
    u64 nextId = maxId >= rootNextId ? maxId + 1u : rootNextId;
    if ( Vector_Count( &duplicates ) != 0u &&
         !HandleDuplicates( pMap, duplicates, ( flags & MAP_LOAD_FLAG_REASSIGN_DUPLICATES ) != 0u, nextId ) ) {
        return fail( map_status_t::OUT_OF_MEMORY );
    }

    // Hand-added objects and parts get fresh IDs (CYMAP.md 10).
    key_value_t *pRootObject = KeyValue_Root( pMap->root.pDocument );
    for ( const char *pArrayName : { "visgroups", "selection_sets" } ) {
        key_value_t *pArray = KeyValue_Find( pRootObject, SV( pArrayName ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i ) {
            key_value_t *pObject = KeyValue_ChildAt( pArray, i );
            bool_t bAssigned = CY_FALSE;
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            if ( !AssignObjectId( pMap->root.pDocument, pObject, nextId, bAssigned ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
            if ( bAssigned ) { NoteAssigned( pMap, {}, pArrayName, pObject ); }
        }
    }
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( !pChunk->bDamaged && !AssignChunkIds( pMap, pChunk, nextId ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    }

    // Geometry into the geometry document.
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        status = ExtractChunkGeometry( pMap, pChunk );
        if ( status != map_status_t::OK ) { return fail( status ); }
    }
    SortRecords( pMap );

    if ( maxId >= rootNextId ) { AddProblem( pMap, map_problem_code_t::ID_ABOVE_NEXT_ID, {}, "next_id", maxId ); }
    pMap->nextId = nextId;
    SyncIds( pMap );
    ( void )geo::GeometrySourceIdRegistry_SealLoadedIds( &pMap->geometry.sourceIds );
    CheckFoliage( pMap );
    if ( pMap->bReadOnly ) { CY_LOG_WRITE( Warning, Editor, "Map opened read-only: duplicate IDs" ); }
    return map_status_t::OK;
}

map_status_t MapDocument_Save( map_document_t *pMap, const map_save_sink_t &sink ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || sink.pfnWriteChunk == nullptr || sink.pfnRemoveChunk == nullptr ||
         sink.pfnWriteRoot == nullptr ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    SyncIds( pMap );

    save_plan_t plan{};
    plan.pAllocator = pMap->pAllocator;
    if ( !Vector_Init( &plan.lives, pMap->pAllocator ) || !Vector_Init( &plan.owned, pMap->pAllocator ) ||
         !Vector_Init( &plan.placements, pMap->pAllocator ) || !Vector_Init( &plan.foliageProps, pMap->pAllocator ) ||
         !Vector_Init( &plan.targets, pMap->pAllocator ) || !Vector_Init( &plan.scratch, pMap->pAllocator ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    map_status_t status = PlanSave( pMap, plan );
    if ( status != map_status_t::OK ) { return status; }

    // Build every output and check every limit and damaged path before the
    // sink sees anything.
    std::sort( plan.targets.pData, plan.targets.pData + Vector_Count( &plan.targets ), []( const target_t *a, const target_t *b ) {
        return a->iLayer != b->iLayer ? a->iLayer < b->iLayer : MapCell_Compare( a->cell, b->cell ) < 0;
    } );
    writer_ctx_t ctx{ pMap, &plan, nullptr };
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        target_t &target = *plan.targets.pData[i];
        status = BuildTargetOutput( ctx, target );
        if ( status != map_status_t::OK ) { return status; }
        for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
            const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
            if ( pChunk->bDamaged && StringView_Equals( SV( pChunk->sourcePath ), SV( target.path ) ) ) { return map_status_t::SAVE_BLOCKED; }
        }
    }
    settings_document_t rootOutput{};
    text_buffer_t rootText{};
    status = BuildRootOutput( pMap, plan, &rootOutput, &rootText );
    if ( status != map_status_t::OK ) { return status; }

    // Write chunks, remove emptied files, write the root last.
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        const target_t &target = *plan.targets.pData[i];
        if ( !sink.pfnWriteChunk( sink.pContext, SV( target.path ), TextBuffer_View( target.pText ) ) ) { return map_status_t::SINK_FAILED; }
    }
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        const map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged || pChunk->sourcePath[0] == '\0' ) { continue; }
        bool_t bStillWritten = CY_FALSE;
        for ( usize i = 0u; i < Vector_Count( &plan.targets ) && !bStillWritten; ++i ) {
            bStillWritten = StringView_Equals( SV( pChunk->sourcePath ), SV( plan.targets.pData[i]->path ) );
        }
        // Several working chunks can share a path (a new chunk and the file
        // it will be written to); each path is removed at most once.
        bool_t bRemovedAlready = CY_FALSE;
        for ( usize j = 0u; j < iChunk && !bRemovedAlready; ++j ) {
            bRemovedAlready = !pMap->chunks.pData[j]->bDamaged && StringView_Equals( SV( pMap->chunks.pData[j]->sourcePath ), SV( pChunk->sourcePath ) );
        }
        if ( !bStillWritten && !bRemovedAlready && !sink.pfnRemoveChunk( sink.pContext, SV( pChunk->sourcePath ) ) ) {
            return map_status_t::SINK_FAILED;
        }
    }
    if ( !sink.pfnWriteRoot( sink.pContext, TextBuffer_View( &rootText ) ) ) { return map_status_t::SINK_FAILED; }

    // The document now mirrors what was written. Damaged chunks are kept as
    // they were.
    vector_t<map_chunk_t *> chunksAfter{};
    if ( !Vector_Init( &chunksAfter, pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        target_t &target = *plan.targets.pData[i];
        map_chunk_t *pChunk = NewChunk( pMap, SV( pMap->layers.pData[target.iLayer].id ), target.cell );
        if ( pChunk == nullptr || !CopyText( pChunk->sourcePath, SV( target.path ) ) ||
             SettingsDocument_Load( &pChunk->store, TextBuffer_View( target.pText ) ).status != settings_document_status_t::OK ||
             !StripAfterSave( pChunk->store.pDocument, plan ) || !Vector_PushBack( &chunksAfter, pChunk ) ) {
            DestroyChunk( pMap->pAllocator, pChunk );
            for ( usize j = 0u; j < Vector_Count( &chunksAfter ); ++j ) { DestroyChunk( pMap->pAllocator, chunksAfter.pData[j] ); }
            return map_status_t::OUT_OF_MEMORY; // Files are written; only the in-memory layout is stale.
        }
    }
    for ( usize iChunk = 0u; iChunk < Vector_Count( &pMap->chunks ); ++iChunk ) {
        map_chunk_t *pChunk = pMap->chunks.pData[iChunk];
        if ( pChunk->bDamaged && Vector_PushBack( &chunksAfter, pChunk ) ) { continue; }
        DestroyChunk( pMap->pAllocator, pChunk );
    }
    Vector_Shutdown( &pMap->chunks );
    Vector_Move( &pMap->chunks, &chunksAfter );
    pMap->bIdsAssigned = CY_FALSE;
    return map_status_t::OK;
}

u64 MapDocument_AllocateId( map_document_t *pMap ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr ) { return 0u; }
    SyncIds( pMap );
    const u64 id = pMap->nextId++;
    SyncIds( pMap );
    return id;
}

bool_t MapDocument_HasLayer( const map_document_t *pMap, string_view_t layer ) noexcept
{
    return pMap != nullptr && LayerIndex( pMap, layer ) != CY_INVALID_SIZE;
}

map_status_t MapDocument_AddLayer( map_document_t *pMap, string_view_t layer, string_view_t name ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || !IsStableId( layer, 64u ) || name.cchLength > MAP_NAME_MAX_LENGTH ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( LayerIndex( pMap, layer ) != CY_INVALID_SIZE ) { return map_status_t::OK; }
    if ( Vector_Count( &pMap->layers ) >= MAP_LAYERS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    map_layer_t entry{};
    ( void )CopyText( entry.id, layer );
    // Adopted layers reach the root first, so the new layer's entry lands at
    // the same index in the root as in map_document_t::layers. Reserving
    // first means the push below cannot fail after the root changed.
    if ( !SyncRootLayers( pMap ) || !Vector_Reserve( &pMap->layers, Vector_Count( &pMap->layers ) + 1u ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    key_value_document_t *pDocument = pMap->root.pDocument;
    key_value_t *pLayers = KeyValue_Find( KeyValue_Root( pDocument ), SV( "layers" ) );
    key_value_t *pLayer = KeyValue_ArrayAppend( pDocument, pLayers, key_value_type_t::OBJECT );
    if ( pLayer == nullptr || !SetStringMember( pDocument, pLayer, "id", layer ) ||
         ( name.cchLength != 0u && !SetStringMember( pDocument, pLayer, "name", name ) ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    const bool_t bPushed = Vector_PushBack( &pMap->layers, entry );
    CY_ASSERT( bPushed );
    return bPushed ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_SetGeometryLayer( map_document_t *pMap, u64 geometryId, string_view_t layer ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || geometryId == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    const usize iLayer = LayerIndex( pMap, layer );
    if ( iLayer == CY_INVALID_SIZE ) { return map_status_t::UNKNOWN_LAYER; }
    const usize iRecord = RecordIndex( pMap, geometryId );
    map_geometry_record_t record{ geometryId, static_cast<u32>( iLayer ), 0u };
    if ( iRecord != CY_INVALID_SIZE ) { record.owner = pMap->geometryRecords.pData[iRecord].owner; }
    if ( !UpsertRecord( pMap, record ) ) { return map_status_t::OUT_OF_MEMORY; }
    SyncIds( pMap );
    return map_status_t::OK;
}

map_status_t MapDocument_SetGeometryOwner( map_document_t *pMap, u64 geometryId, u64 ownerEntityId ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || geometryId == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    map_geometry_kind_t kind = map_geometry_kind_t::COUNT;
    if ( !IsLiveGeometry( pMap, geometryId, &kind ) ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( ownerEntityId != 0u ) {
        // Terrains are world geometry (CYMAP.md 6.2).
        if ( kind == map_geometry_kind_t::TERRAIN ) { return map_status_t::INVALID_ARGUMENT; }
        map_chunk_t *pChunk = nullptr;
        key_value_t *pParent = nullptr;
        const key_value_t *pOwner = FindObjectInternal( pMap, ownerEntityId, &pChunk, &pParent );
        if ( pOwner == nullptr || pParent != KeyValue_Find( KeyValue_Root( pChunk->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
            return map_status_t::UNKNOWN_OBJECT;
        }
    }
    const usize iRecord = RecordIndex( pMap, geometryId );
    map_geometry_record_t record{ geometryId, 0u, ownerEntityId };
    if ( iRecord != CY_INVALID_SIZE ) { record.iLayer = pMap->geometryRecords.pData[iRecord].iLayer; }
    return UpsertRecord( pMap, record ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_InsertGeometryRecord( map_document_t *pMap, map_geometry_kind_t kind, const key_value_t *pRecord ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || KeyValue_Type( pRecord ) != key_value_type_t::OBJECT ||
         kind == map_geometry_kind_t::COUNT ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    u64 id = 0u; map_geometry_kind_t actualKind{};
    if ( !ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) || !IsLiveGeometry( pMap, id, &actualKind ) || actualKind != kind ) {
        return map_status_t::UNKNOWN_OBJECT;
    }
    if ( FindObjectInternal( pMap, id, nullptr, nullptr ) != nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    const usize index = RecordIndex( pMap, id );
    if ( index == CY_INVALID_SIZE ) { return map_status_t::INVALID_ARGUMENT; }
    const auto &placement = pMap->geometryRecords.pData[index];
    if ( placement.iLayer >= pMap->layers.nCount ) { return map_status_t::UNKNOWN_LAYER; }
    map_chunk_t *pChunk = nullptr; key_value_t *pParent = nullptr;
    if ( placement.owner != 0u ) {
        if ( kind == map_geometry_kind_t::TERRAIN ) { return map_status_t::INVALID_ARGUMENT; }
        key_value_t *pContainer = nullptr;
        pParent = FindObjectInternal( pMap, placement.owner, &pChunk, &pContainer );
        if ( pParent == nullptr || pChunk == nullptr ||
             pContainer != KeyValue_Find( KeyValue_Root( pChunk->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
            return map_status_t::UNKNOWN_OBJECT;
        }
    } else {
        pChunk = GetOrAddWorkingChunk( pMap, SV( pMap->layers.pData[placement.iLayer].id ), {} );
        if ( pChunk == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        pParent = KeyValue_Root( pChunk->store.pDocument );
    }
    auto *pArray = EnsureMember( pChunk->store.pDocument, pParent, MapGeometry_SectionName( kind ), key_value_type_t::ARRAY );
    if ( pArray == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    if ( KeyValue_Type( pArray ) != key_value_type_t::ARRAY ) { return map_status_t::INVALID_ARGUMENT; }
    return KeyValue_CloneInto( pChunk->store.pDocument, pArray, pRecord ) != nullptr ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_AddEntity( map_document_t *pMap, string_view_t layer, string_view_t className, math::vec3d_t origin, u64 *pIdOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || className.cchLength == 0u || pIdOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( LayerIndex( pMap, layer ) == CY_INVALID_SIZE ) { return map_status_t::UNKNOWN_LAYER; }
    map_chunk_t *pChunk = GetOrAddWorkingChunk( pMap, layer, MapCell_ForPosition( pMap->grid, origin ) );
    if ( pChunk == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_document_t *pDocument = pChunk->store.pDocument;
    key_value_t *pEntities = EnsureMember( pDocument, KeyValue_Root( pDocument ), kSections[SEC_ENTITIES], key_value_type_t::ARRAY );
    key_value_t *pEntity = pEntities != nullptr ? KeyValue_ArrayAppend( pDocument, pEntities, key_value_type_t::OBJECT ) : nullptr;
    const u64 id = MapDocument_AllocateId( pMap );
    if ( pEntity == nullptr || !SetU64Member( pDocument, pEntity, "id", id ) || !SetStringMember( pDocument, pEntity, "class", className ) ||
         !SetVec3Member( pDocument, pEntity, "origin", origin ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    *pIdOut = id;
    return map_status_t::OK;
}

key_value_t *MapDocument_FindObject( map_document_t *pMap, u64 id, map_chunk_t **ppChunkOut ) noexcept
{
    return FindObjectInternal( pMap, id, ppChunkOut, nullptr );
}

map_status_t MapDocument_RemoveObject( map_document_t *pMap, u64 id ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || id == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    map_chunk_t *pChunk = nullptr;
    key_value_t *pParent = nullptr;
    map_geometry_kind_t kind = map_geometry_kind_t::COUNT;
    if ( IsLiveGeometry( pMap, id, &kind ) ) {
        const geo::geometry_source_id_t sourceId{ id };
        geo::geometry_status_t removed = geo::geometry_status_t::INVALID_ARGUMENT;
        switch ( kind ) {
            case map_geometry_kind_t::BRUSH: removed = geo::GeometryDocument_TryRemoveBrush( &pMap->geometry, sourceId ); break;
            case map_geometry_kind_t::MESH: removed = geo::GeometryDocument_TryRemoveMesh( &pMap->geometry, sourceId ); break;
            case map_geometry_kind_t::PATCH: removed = geo::GeometryDocument_TryRemovePatch( &pMap->geometry, sourceId ); break;
            case map_geometry_kind_t::TERRAIN: removed = geo::GeometryDocument_TryRemoveHeightField( &pMap->geometry, sourceId ); break;
            case map_geometry_kind_t::COUNT: break;
        }
        if ( removed != geo::geometry_status_t::OK ) { return map_status_t::GEOMETRY_FAILED; }
        const usize iRecord = RecordIndex( pMap, id );
        if ( iRecord != CY_INVALID_SIZE ) { Vector_Erase( &pMap->geometryRecords, iRecord ); }
        // The record goes too, so nothing of the object is written again.
        key_value_t *pRecord = FindObjectInternal( pMap, id, &pChunk, &pParent );
        if ( pRecord != nullptr && !KeyValue_Remove( pChunk->store.pDocument, pParent, pRecord ) ) { return map_status_t::OUT_OF_MEMORY; }
        return map_status_t::OK;
    }
    key_value_t *pNode = FindObjectInternal( pMap, id, &pChunk, &pParent );
    if ( pNode == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( pParent == KeyValue_Find( KeyValue_Root( pChunk->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
        // Removing a brush entity removes the brushes it owns.
        vector_t<u64> owned{};
        if ( !Vector_Init( &owned, pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize i = 0u; i < Vector_Count( &pMap->geometryRecords ); ++i ) {
            if ( pMap->geometryRecords.pData[i].owner == id && !Vector_PushBack( &owned, pMap->geometryRecords.pData[i].id ) ) {
                return map_status_t::OUT_OF_MEMORY;
            }
        }
        for ( usize i = 0u; i < Vector_Count( &owned ); ++i ) {
            const map_status_t status = MapDocument_RemoveObject( pMap, owned.pData[i] );
            if ( status != map_status_t::OK && status != map_status_t::UNKNOWN_OBJECT ) { return status; }
        }
    }
    return KeyValue_Remove( pChunk->store.pDocument, pParent, pNode ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_SetEntityOrigin( map_document_t *pMap, u64 id, math::vec3d_t origin ) noexcept
{
    map_chunk_t *pChunk = nullptr;
    key_value_t *pParent = nullptr;
    key_value_t *pNode = FindObjectInternal( pMap, id, &pChunk, &pParent );
    if ( pNode == nullptr || pParent != KeyValue_Find( KeyValue_Root( pChunk->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
        return map_status_t::UNKNOWN_OBJECT;
    }
    return SetVec3Member( pChunk->store.pDocument, pNode, "origin", origin ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_MaterialRef( map_document_t *pMap, string_view_t path, u64 *pRefOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pRefOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( path.cchLength > MAP_MATERIAL_PATH_MAX ) { return map_status_t::INVALID_ARGUMENT; }
    return MapMaterials_Intern( &pMap->materials, path, pRefOut ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

usize MapDocument_ChunkPath( const map_chunk_t &chunk, char ( &buffer )[MAP_CHUNK_PATH_CAPACITY] ) noexcept
{
    return FormatChunkPath( SV( chunk.layer ), chunk.cell, buffer );
}

const char *MapDocument_StatusName( map_status_t status ) noexcept
{
    switch ( status ) {
        case map_status_t::OK: return "OK";
        case map_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case map_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case map_status_t::ROOT_UNREADABLE: return "ROOT_UNREADABLE";
        case map_status_t::ROOT_INVALID: return "ROOT_INVALID";
        case map_status_t::READ_ONLY: return "READ_ONLY";
        case map_status_t::SAVE_BLOCKED: return "SAVE_BLOCKED";
        case map_status_t::SINK_FAILED: return "SINK_FAILED";
        case map_status_t::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
        case map_status_t::GEOMETRY_FAILED: return "GEOMETRY_FAILED";
        case map_status_t::UNKNOWN_OBJECT: return "UNKNOWN_OBJECT";
        case map_status_t::UNKNOWN_LAYER: return "UNKNOWN_LAYER";
    }
    return "UNKNOWN";
}

const char *MapDocument_ProblemName( map_problem_code_t code ) noexcept
{
    switch ( code ) {
        case map_problem_code_t::ROOT_MEMBER_INVALID: return "ROOT_MEMBER_INVALID";
        case map_problem_code_t::ROOT_MEMBER_IGNORED: return "ROOT_MEMBER_IGNORED";
        case map_problem_code_t::CHUNK_UNREADABLE: return "CHUNK_UNREADABLE";
        case map_problem_code_t::CHUNK_FOREIGN_MAP: return "CHUNK_FOREIGN_MAP";
        case map_problem_code_t::CHUNK_LAYER_ADOPTED: return "CHUNK_LAYER_ADOPTED";
        case map_problem_code_t::CHUNK_CELL_INVALID: return "CHUNK_CELL_INVALID";
        case map_problem_code_t::CHUNK_PATH_MISMATCH: return "CHUNK_PATH_MISMATCH";
        case map_problem_code_t::OBJECT_UNREADABLE: return "OBJECT_UNREADABLE";
        case map_problem_code_t::ID_ASSIGNED: return "ID_ASSIGNED";
        case map_problem_code_t::DUPLICATE_ID: return "DUPLICATE_ID";
        case map_problem_code_t::ID_ABOVE_NEXT_ID: return "ID_ABOVE_NEXT_ID";
        case map_problem_code_t::FOLIAGE_PROPERTIES_DIFFER: return "FOLIAGE_PROPERTIES_DIFFER";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::map
