//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Document.cpp
//  Purpose: Implements the map document: loading and saving `.cymap`
//           files (cypher.map V11), identity (missing, duplicate, and raised
//           IDs), geometry transfer through the readable codec, placement of
//           every object kind, and object operations.
//  Details: Loading moves each layer's sections out of the file's tree into
//           that layer's store and keeps the rest (identity, settings, layer
//           names) as the root tree. Saving never edits stores in place. It
//           decides every object's layer first, builds fresh output trees in
//           the canonical member order, nests them back into their layers,
//           and checks every limit before producing text - so a refused save
//           produces nothing. The file is written with the compact CYKV
//           layout (bare keys, shortest reals, one-line short containers)
//           because people read and diff it (CYMAP.md section 3).
//
//           Loading processes layers in file order so that IDs assigned to
//           hand-added objects are the same on every machine.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Reworked on 2026-09-27 for map-owned readable geometry, nested brush
//    entities, shapes, foliage, notes, selection sets, info annotations,
//    and hand-editing identity rules
//  - Reworked on 2026-10-12 for V11: one file, sections nested in layers
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
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
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

// The small shared tables come first and the level itself, layer by layer,
// last, so the top of the file reads as what the map is. The root has no
// `info`: a summary of the whole map would change with every edit and make
// any two people's changes conflict (ADR 0011); each layer has its own.
constexpr const char *kRootOrder[]{ "map_id", "name", "game", "game_version", "next_id", "description", "authors", "tags",
                                    "thumbnail", "units_per_meter", "visgroups", "selection_sets", "cordons", "settings",
                                    "layers" };
constexpr const char *kLayerOrder[]{ "id", "name", "color", "editor_only" };
// Everything a layer object can hold that the writer produces itself; other
// members are kept and written after these.
constexpr const char *kLayerMembers[]{ "id", "name", "color", "editor_only", "info", "entities", "brushes", "meshes",
                                       "patches", "terrains", "shapes", "foliage", "notes", "groups", "prefabs" };
constexpr const char *kVisgroupOrder[]{ "id", "name", "color", "parent" };
constexpr const char *kSelectionSetOrder[]{ "id", "name", "members" };
constexpr const char *kCordonOrder[]{ "name", "bounds" };
constexpr const char *kStoreOrder[]{ "info", "entities", "brushes", "meshes", "patches", "terrains", "shapes", "foliage",
                                     "notes", "groups", "prefabs" };
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
            for ( const key_value_t *pChildA = KeyValue_FirstChild( pA ), *pChildB = KeyValue_FirstChild( pB ); pChildA != nullptr;
                  pChildA = KeyValue_NextSibling( pChildA ), pChildB = KeyValue_NextSibling( pChildB ) ) {
                if ( !KeyValuesEqual( pChildA, pChildB ) ) { return CY_FALSE; }
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

// Children one step at a time: constant time per step, where
// KeyValue_ChildAt walks from the first child on every call - quadratic over
// a layer's sections, which hold every object of the layer.
CYPHER_NODISCARD key_value_t *FirstOf( key_value_t *pContainer ) noexcept { return KeyValue_FirstChildMutable( pContainer ); }
CYPHER_NODISCARD const key_value_t *FirstOf( const key_value_t *pContainer ) noexcept { return KeyValue_FirstChild( pContainer ); }
CYPHER_NODISCARD key_value_t *NextOf( key_value_t *pNode ) noexcept { return KeyValue_NextSiblingMutable( pNode ); }
CYPHER_NODISCARD const key_value_t *NextOf( const key_value_t *pNode ) noexcept { return KeyValue_NextSibling( pNode ); }

// Comments a person wrote around a value travel with it through a save,
// also where the writer rebuilds the value in canonical form.
CYPHER_NODISCARD bool_t CarryComments( key_value_document_t *pDocument, key_value_t *pTo, const key_value_t *pFrom ) noexcept
{
    if ( pTo == nullptr || pFrom == nullptr ) { return CY_TRUE; }
    const key_value_comments_t comments = KeyValue_Comments( pFrom );
    if ( comments.leading.cchLength == 0u && comments.trailing.cchLength == 0u && comments.inner.cchLength == 0u ) { return CY_TRUE; }
    return KeyValue_SetComments( pDocument, pTo, comments );
}

CYPHER_NODISCARD settings_document_identity_t RootIdentity() noexcept
{
    return { SV( MAP_SCHEMA_ID ), MAP_SCHEMA_VERSION, MAP_SCHEMA_VERSION };
}

// Layer stores never reach a file; the identity only satisfies the settings
// document, which every map tree uses for its header and ownership.
CYPHER_NODISCARD settings_document_identity_t StoreIdentity() noexcept
{
    return { SV( "cypher.map.layer" ), MAP_SCHEMA_VERSION, MAP_SCHEMA_VERSION };
}

void AddProblem( map_document_t *pMap, map_problem_code_t code, string_view_t layer, const char *pMember, u64 id ) noexcept
{
    map_problem_t problem{};
    problem.code = code;
    const usize cchLayer = layer.cchLength < MAP_LAYER_ID_CAPACITY - 1u ? layer.cchLength : MAP_LAYER_ID_CAPACITY - 1u;
    if ( cchLayer != 0u ) { std::memcpy( problem.layer, layer.pData, cchLayer ); }
    if ( pMember != nullptr ) { std::snprintf( problem.member, sizeof( problem.member ), "%s", pMember ); }
    problem.id = id;
    // Diagnostics are best effort: running out of memory while recording one
    // must not turn a readable map into an unreadable one.
    ( void )Vector_PushBack( &pMap->problems, problem );
}

// ---------------------------------------------------------------------------
// Layers and layer stores
// ---------------------------------------------------------------------------

CYPHER_NODISCARD usize LayerIndex( const map_document_t *pMap, string_view_t layer ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &pMap->layers ); ++i ) {
        if ( StringView_Equals( SV( pMap->layers.pData[i].id ), layer ) ) { return i; }
    }
    return CY_INVALID_SIZE;
}

CYPHER_NODISCARD map_layer_store_t *NewStore( map_document_t *pMap, string_view_t layer ) noexcept
{
    void *pMemory = Allocator_Allocate( pMap->pAllocator, sizeof( map_layer_store_t ), alignof( map_layer_store_t ) );
    if ( pMemory == nullptr ) { return nullptr; }
    map_layer_store_t *pStore = new ( pMemory ) map_layer_store_t{};
    const bool_t bOk = CopyText( pStore->layer, layer ) &&
                       SettingsDocument_Init( &pStore->store, pMap->pAllocator, StoreIdentity() ) == settings_document_status_t::OK;
    if ( !bOk ) {
        pStore->~map_layer_store_t();
        Allocator_Free( pMap->pAllocator, pMemory, sizeof( map_layer_store_t ), alignof( map_layer_store_t ) );
        return nullptr;
    }
    return pStore;
}

void DestroyStore( const allocator_t *pAllocator, map_layer_store_t *pStore ) noexcept
{
    if ( pStore == nullptr ) { return; }
    pStore->~map_layer_store_t();
    Allocator_Free( pAllocator, pStore, sizeof( map_layer_store_t ), alignof( map_layer_store_t ) );
}

CYPHER_NODISCARD map_layer_store_t *FindStore( map_document_t *pMap, string_view_t layer ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &pMap->layerStores ); ++i ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[i];
        if ( StringView_Equals( SV( pStore->layer ), layer ) ) { return pStore; }
    }
    return nullptr;
}

CYPHER_NODISCARD map_layer_store_t *GetOrAddStore( map_document_t *pMap, string_view_t layer ) noexcept
{
    map_layer_store_t *pStore = FindStore( pMap, layer );
    if ( pStore != nullptr ) { return pStore; }
    pStore = NewStore( pMap, layer );
    if ( pStore == nullptr ) { return nullptr; }
    if ( !Vector_PushBack( &pMap->layerStores, pStore ) ) {
        DestroyStore( pMap->pAllocator, pStore );
        return nullptr;
    }
    return pStore;
}

// Writes a store with the compact layout people read and diff (CYMAP.md 3).
CYPHER_NODISCARD map_status_t WriteCompactText( const settings_document_t *pStore, const allocator_t *pAllocator, text_buffer_t *pText, usize cbMax ) noexcept
{
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE | KEY_VALUE_WRITE_FLAG_BARE_KEYS |
                    KEY_VALUE_WRITE_FLAG_SHORTEST_REALS | KEY_VALUE_WRITE_FLAG_COMMENTS;
    options.nIndentSpaces = 4u;
    options.nLineWidth = MAP_LINE_WIDTH;
    const key_value_t *pRoot = SettingsDocument_Root( pStore );
    // One pass into a growing buffer: measuring first would lay out and
    // format every number of the level twice.
    struct sink_t {
        text_buffer_t *pText;
        usize cbMax;
        bool_t bTooLarge;
    } sink{ pText, cbMax, CY_FALSE };
    if ( !TextBuffer_Init( pText, pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    const key_value_write_result_t written = KeyValue_WriteTextToSink(
        pRoot, options,
        []( string_view_t text, void *pUserData ) noexcept -> bool_t {
            auto &out = *static_cast<sink_t *>( pUserData );
            if ( text.cchLength > out.cbMax - TextBuffer_Length( out.pText ) ) {
                out.bTooLarge = CY_TRUE;
                return CY_FALSE;
            }
            return TextBuffer_Append( out.pText, text );
        },
        &sink );
    if ( sink.bTooLarge ) { return map_status_t::LIMIT_EXCEEDED; }
    return written.status == key_value_write_status_t::OK ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
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
        auto *pAt1 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt1 = NextOf( pAt1 ) ) {
            key_value_t *pObject = pAt1;
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            if ( !strip( pObject ) ) { return CY_FALSE; }
            if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                auto *pAt44 = FirstOf( pNested );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt44 = NextOf( pAt44 ) ) {
                    key_value_t *pRecord = pAt44;
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

// Every present, valid ID in a store, in section order then document order:
// objects, entity-owned geometry, and geometry parts. Foliage has no IDs.
template <typename visit_t>
CYPHER_NODISCARD bool_t VisitStoreIds( key_value_t *pRoot, visit_t &visit ) noexcept
{
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section == SEC_FOLIAGE ) { continue; }
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        auto *pAt2 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt2 = NextOf( pAt2 ) ) {
            key_value_t *pObject = pAt2;
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            const bool_t bOk = IsGeometrySection( section )
                ? MapGeometry_ForEachId( KindOfSection( section ), pObject, Trampoline<visit_t>, &visit )
                : VisitObjectId( pObject, visit );
            if ( !bOk ) { return CY_FALSE; }
            if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                auto *pAt45 = FirstOf( pNested );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt45 = NextOf( pAt45 ) ) {
                    key_value_t *pRecord = pAt45;
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
        auto *pAt3 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt3 = NextOf( pAt3 ) ) {
            key_value_t *pObject = pAt3;
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
CYPHER_NODISCARD bool_t AssignStoreIds( map_document_t *pMap, map_layer_store_t *pStore, u64 &nextId ) noexcept
{
    key_value_document_t *pDocument = pStore->store.pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );
    const string_view_t path = SV( pStore->layer );
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section == SEC_FOLIAGE ) { continue; }
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        auto *pAt4 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt4 = NextOf( pAt4 ) ) {
            key_value_t *pObject = pAt4;
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
                auto *pAt46 = FirstOf( pNested );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt46 = NextOf( pAt46 ) ) {
                    key_value_t *pRecord = pAt46;
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
    const key_value_t *pUnits = KeyValue_Find( pRoot, SV( "units_per_meter" ) );
    f64 units = 0.0;
    if ( pUnits != nullptr && !( ReadNumber( pUnits, units ) && units > 0.0 ) ) {
        AddProblem( pMap, map_problem_code_t::ROOT_MEMBER_IGNORED, {}, "units_per_meter", 0u );
    }
    return map_status_t::OK;
}

// Appends layers the root does not list yet so the root's layer array
// matches map_document_t::layers index for index.
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
// Loading
// ---------------------------------------------------------------------------

// Parses the file into the root tree. The settings document's own loader
// stops at 16 MiB and 2^20 values - right for a settings file, too small for
// a whole level - so the map parses with its own bounds and checks the
// header itself. Both CYKV languages are read: a hand-written map may use
// CYKV 2 definitions. Why a file cannot be read is recorded with its line,
// since the file is meant to be edited by hand.
CYPHER_NODISCARD map_status_t ParseMap( map_document_t *pMap, string_view_t text ) noexcept
{
    char detail[48]{};
    if ( text.cchLength > MAP_TEXT_MAX ) {
        AddProblem( pMap, map_problem_code_t::TEXT_UNREADABLE, {}, "larger than MAP_TEXT_MAX", 0u );
        return map_status_t::ROOT_UNREADABLE;
    }
    key_value_document_desc_t desc{};
    desc.pAllocator = pMap->pAllocator;
    key_value_document_t *pParsed = KeyValue_CreateDocument( desc );
    if ( pParsed == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_parse_options_t options{};
    options.flags |= KEY_VALUE_PARSE_FLAG_KEEP_COMMENTS; // A hand-edited map keeps its comments through saves.
    options.cbMaxInput = MAP_TEXT_MAX;
    options.cbMaxStringData = MAP_TEXT_MAX;
    options.nMaxNodes = MAP_VALUES_MAX;
    const key_value_parse_result_t parsed = KeyValue_ParseText( text, options, pParsed );
    const key_value_document_header_t header = KeyValue_DocumentHeader( pParsed );
    map_status_t status = map_status_t::OK;
    if ( parsed.status == key_value_parse_status_t::OUT_OF_MEMORY ) {
        status = map_status_t::OUT_OF_MEMORY;
    } else if ( parsed.status != key_value_parse_status_t::OK ) {
        std::snprintf( detail, sizeof( detail ), "line %u: %s", static_cast<unsigned>( parsed.errorLocation.nLine ),
                       KeyValue_ParseStatusName( parsed.status ) );
        AddProblem( pMap, map_problem_code_t::TEXT_UNREADABLE, {}, detail, 0u );
        status = map_status_t::ROOT_UNREADABLE;
    } else if ( ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION_1 && header.nLanguageVersion != CYKV_LANGUAGE_VERSION_2 ) ||
                !StringView_Equals( header.schemaId, SV( MAP_SCHEMA_ID ) ) ) {
        AddProblem( pMap, map_problem_code_t::TEXT_UNREADABLE, {}, "not a cypher.map file", 0u );
        status = map_status_t::ROOT_UNREADABLE;
    } else if ( header.nSchemaVersion != MAP_SCHEMA_VERSION ) {
        std::snprintf( detail, sizeof( detail ), "cypher.map %u", static_cast<unsigned>( header.nSchemaVersion ) );
        AddProblem( pMap, map_problem_code_t::VERSION_UNSUPPORTED, {}, detail, 0u );
        status = map_status_t::ROOT_UNREADABLE;
    } else if ( KeyValue_Type( KeyValue_Root( pParsed ) ) != key_value_type_t::OBJECT ) {
        AddProblem( pMap, map_problem_code_t::TEXT_UNREADABLE, {}, "root is not an object", 0u );
        status = map_status_t::ROOT_UNREADABLE;
    }
    if ( status != map_status_t::OK ) {
        KeyValue_DestroyDocument( pParsed );
        return status;
    }
    KeyValue_DestroyDocument( pMap->root.pDocument );
    pMap->root.pDocument = pParsed;
    pMap->root.nLoadedVersion = header.nSchemaVersion;
    return map_status_t::OK;
}

// Moves every layer's sections out of the root tree into that layer's
// store, then rebuilds the root without them so its arena does not keep a
// second copy of the level. A section that is not a list cannot be read, or
// written back under its name, without guessing: it stays in the layer as
// written and is reported. `info` blocks are summaries for readers and are
// dropped (CYMAP.md 8).
CYPHER_NODISCARD map_status_t SplitLayers( map_document_t *pMap ) noexcept
{
    key_value_t *pRoot = KeyValue_Root( pMap->root.pDocument );
    const key_value_t *pLayers = KeyValue_Find( pRoot, SV( "layers" ) );
    CY_ASSERT_MSG( KeyValue_ChildCount( pLayers ) == Vector_Count( &pMap->layers ), "ReadRoot lists the root's layers in order" );
    auto *pAt5 = FirstOf( pLayers );
    for ( usize i = 0u; i < KeyValue_ChildCount( pLayers ); ++i, pAt5 = NextOf( pAt5 ) ) {
        const key_value_t *pLayer = pAt5;
        map_layer_store_t *pStore = GetOrAddStore( pMap, SV( pMap->layers.pData[i].id ) );
        if ( pStore == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        key_value_document_t *pDocument = pStore->store.pDocument;
        for ( const char *pSection : kSections ) {
            const key_value_t *pMember = KeyValue_Find( pLayer, SV( pSection ) );
            if ( pMember == nullptr ) { continue; }
            if ( KeyValue_Type( pMember ) != key_value_type_t::ARRAY ) {
                AddProblem( pMap, map_problem_code_t::SECTION_UNREADABLE, SV( pStore->layer ), pSection, 0u );
                continue;
            }
            if ( !Clone( pDocument, KeyValue_Root( pDocument ), pMember ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        if ( !StripInfo( pDocument, KeyValue_Root( pDocument ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    }

    key_value_document_desc_t desc{};
    desc.pAllocator = pMap->pAllocator;
    key_value_document_t *pCompact = KeyValue_CreateDocument( desc );
    if ( pCompact == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    bool_t bOk = KeyValue_SetDocumentHeader( pCompact, KeyValue_DocumentHeader( pMap->root.pDocument ) ) &&
                 KeyValue_SetRootType( pCompact, key_value_type_t::OBJECT );
    key_value_t *pCompactRoot = KeyValue_Root( pCompact );
    bOk = bOk && CarryComments( pCompact, pCompactRoot, pRoot );
    auto *pAt6 = FirstOf( pRoot );
    for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pRoot ); ++i, pAt6 = NextOf( pAt6 ) ) {
        const key_value_t *pMember = pAt6;
        const string_view_t name = KeyValue_Name( pMember );
        if ( StringView_Equals( name, SV( "info" ) ) ) { continue; }
        if ( !StringView_Equals( name, SV( "layers" ) ) ) {
            bOk = Clone( pCompact, pCompactRoot, pMember );
            continue;
        }
        key_value_t *pOut = KeyValue_ObjectInsert( pCompact, pCompactRoot, name, key_value_type_t::ARRAY );
        bOk = pOut != nullptr && CarryComments( pCompact, pOut, pMember );
        auto *pAt47 = FirstOf( pMember );
        for ( usize j = 0u; bOk && j < KeyValue_ChildCount( pMember ); ++j, pAt47 = NextOf( pAt47 ) ) {
            const key_value_t *pLayer = pAt47;
            key_value_t *pLayerOut = KeyValue_ArrayAppend( pCompact, pOut, key_value_type_t::OBJECT );
            bOk = pLayerOut != nullptr && CarryComments( pCompact, pLayerOut, pLayer );
            auto *pAt48 = FirstOf( pLayer );
            for ( usize k = 0u; bOk && k < KeyValue_ChildCount( pLayer ); ++k, pAt48 = NextOf( pAt48 ) ) {
                const key_value_t *pLayerMember = pAt48;
                const string_view_t memberName = KeyValue_Name( pLayerMember );
                const bool_t bMoved = IsListed( memberName, Order( kSections ) ) && KeyValue_Type( pLayerMember ) == key_value_type_t::ARRAY;
                if ( bMoved || StringView_Equals( memberName, SV( "info" ) ) ) { continue; }
                bOk = Clone( pCompact, pLayerOut, pLayerMember );
            }
        }
    }
    if ( !bOk ) {
        KeyValue_DestroyDocument( pCompact );
        return map_status_t::OUT_OF_MEMORY;
    }
    KeyValue_DestroyDocument( pMap->root.pDocument );
    pMap->root.pDocument = pCompact;
    return map_status_t::OK;
}

// Lifts one record's geometry into the geometry document, or keeps the
// record verbatim when it cannot be read.
CYPHER_NODISCARD map_status_t ExtractRecord(
    map_document_t *pMap,
    map_layer_store_t *pStore,
    map_geometry_kind_t kind,
    key_value_t *pRecord,
    u32 iLayer,
    u64 owner ) noexcept
{
    const string_view_t path = SV( pStore->layer );
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
                 !MapGeometry_StripRecord( kind, pStore->store.pDocument, pRecord ) ) {
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

CYPHER_NODISCARD map_status_t ExtractStoreGeometry( map_document_t *pMap, map_layer_store_t *pStore ) noexcept
{
    key_value_t *pRoot = KeyValue_Root( pStore->store.pDocument );
    const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pStore->layer ) ) );
    for ( u32 section = SEC_BRUSHES; section <= SEC_TERRAINS; ++section ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        auto *pAt7 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt7 = NextOf( pAt7 ) ) {
            const map_status_t status = ExtractRecord( pMap, pStore, KindOfSection( section ), pAt7, iLayer, 0u );
            if ( status != map_status_t::OK ) { return status; }
        }
    }
    key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
    auto *pAt8 = FirstOf( pEntities );
    for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i, pAt8 = NextOf( pAt8 ) ) {
        key_value_t *pEntity = pAt8;
        u64 entityId = 0u;
        if ( !EntityIsReadable( pEntity, entityId ) ) {
            AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, SV( pStore->layer ), "entities", 0u );
            continue;
        }
        for ( map_geometry_kind_t kind : kOwnedKinds ) {
            key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
            auto *pAt49 = FirstOf( pNested );
            for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt49 = NextOf( pAt49 ) ) {
                const map_status_t status = ExtractRecord( pMap, pStore, kind, pAt49, iLayer, entityId );
                if ( status != map_status_t::OK ) { return status; }
            }
        }
    }
    // Other objects without a usable shape are reported here; they are
    // written back verbatim.
    for ( u32 section : { static_cast<u32>( SEC_SHAPES ), static_cast<u32>( SEC_NOTES ), static_cast<u32>( SEC_GROUPS ),
                          static_cast<u32>( SEC_PREFABS ) } ) {
        key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
        auto *pAt9 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt9 = NextOf( pAt9 ) ) {
            u64 id = 0u;
            const key_value_t *pObject = pAt9;
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) ) {
                AddProblem( pMap, map_problem_code_t::OBJECT_UNREADABLE, SV( pStore->layer ), kSections[section], 0u );
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

// One layer's output: what goes into its sections, built into pOutput.
struct target_t {
    u32 iLayer{ 0u };
    vector_t<placed_t> sections[SEC_COUNT]{};
    vector_t<foliage_item_t> foliage{};
    vector_t<const key_value_t *> extras{};  // Unknown store members.
    map_bounds_t bounds{};                    // Everything positioned, for the layer's `info`.
    settings_document_t *pOutput{ nullptr };  // Section lists; becomes the layer's store after the save.
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
            pTarget->~target_t();
            Allocator_Free( pAllocator, pTarget, sizeof( target_t ), alignof( target_t ) );
        }
        geo::GeometryFragment_Shutdown( &fragment );
    }
};

CYPHER_NODISCARD target_t *GetTarget( save_plan_t &plan, u32 iLayer ) noexcept
{
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        target_t *pTarget = plan.targets.pData[i];
        if ( pTarget->iLayer == iLayer ) { return pTarget; }
    }
    void *pMemory = Allocator_Allocate( plan.pAllocator, sizeof( target_t ), alignof( target_t ) );
    if ( pMemory == nullptr ) { return nullptr; }
    target_t *pTarget = new ( pMemory ) target_t{};
    pTarget->iLayer = iLayer;
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

CYPHER_NODISCARD bool_t Place( target_t *pTarget, u32 section, const placed_t &placed ) noexcept
{
    return pTarget != nullptr && Vector_PushBack( &pTarget->sections[section], placed );
}

CYPHER_NODISCARD map_bounds_t ShapeBounds( const key_value_t *pShape ) noexcept
{
    map_bounds_t bounds{};
    const key_value_t *pPoints = KeyValue_Find( pShape, SV( "points" ) );
    auto *pAt10 = FirstOf( pPoints );
    for ( usize i = 0u; i < KeyValue_ChildCount( pPoints ); ++i, pAt10 = NextOf( pAt10 ) ) {
        math::vec3d_t point{};
        if ( ReadVec3( pAt10, point ) ) { MapBounds_AddPoint( bounds, point ); }
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
    const auto add = [&]( u64 id, map_geometry_kind_t kind, usize iObject, map_bounds_t bounds ) noexcept {
        live_t live{};
        live.id = id;
        live.kind = kind;
        live.iObject = iObject;
        live.bounds = bounds;
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
        ( void )add( brush.solid.sourceId.value, map_geometry_kind_t::BRUSH, i, bounds );
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.meshes ); ++i ) {
        usize nVertices = 0u, nFaces = 0u;
        geo::geometry_status_t boundsStatus{};
        const map_bounds_t bounds = MapGeometry_MeshBounds( *fragment.meshes.pData[i], pMap->pAllocator, &nVertices, &nFaces, &boundsStatus );
        if ( boundsStatus == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
        if ( add( fragment.meshes.pData[i]->sourceId.value, map_geometry_kind_t::MESH, i, bounds ) ) {
            plan.lives.pData[Vector_Count( &plan.lives ) - 1u].nVertices = nVertices;
            plan.lives.pData[Vector_Count( &plan.lives ) - 1u].nFaces = nFaces;
        }
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.patches ); ++i ) {
        const map_bounds_t bounds = MapGeometry_PatchBounds( *fragment.patches.pData[i] );
        ( void )add( fragment.patches.pData[i]->sourceId.value, map_geometry_kind_t::PATCH, i, bounds );
    }
    for ( usize i = 0u; bOk && i < Vector_Count( &fragment.heightFields ); ++i ) {
        const geo::heightfield_t &terrain = *fragment.heightFields.pData[i];
        ( void )add( terrain.sourceId.value, map_geometry_kind_t::TERRAIN, i, MapGeometry_TerrainBounds( terrain ) );
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
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const key_value_t *pRoot = SettingsDocument_Root( &pStore->store );
        for ( u32 section = SEC_BRUSHES; section <= SEC_TERRAINS; ++section ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            auto *pAt11 = FirstOf( pArray );
            for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt11 = NextOf( pAt11 ) ) { addRef( pAt11 ); }
        }
        const key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
        auto *pAt12 = FirstOf( pEntities );
        for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i, pAt12 = NextOf( pAt12 ) ) {
            const key_value_t *pEntity = pAt12;
            if ( !EntityShapeOk( pEntity ) ) { continue; }
            for ( map_geometry_kind_t kind : kOwnedKinds ) {
                const key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
                auto *pAt50 = FirstOf( pNested );
                for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt50 = NextOf( pAt50 ) ) { addRef( pAt50 ); }
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

// Decides every object's layer (CYMAP.md section 9).
CYPHER_NODISCARD map_status_t PlanSave( map_document_t *pMap, save_plan_t &plan ) noexcept
{
    map_status_t status = PlanLives( pMap, plan );
    if ( status != map_status_t::OK ) { return status; }
    bool_t bOk = CY_TRUE;
    constexpr u64 kUnreadable = ~0ull;

    // 1. Entities stay in their layer.
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pStore->layer ) ) );
        CY_ASSERT_MSG( iLayer != static_cast<u32>( CY_INVALID_SIZE ), "Stores only exist for declared layers" );
        const key_value_t *pEntities = KeyValue_Find( SettingsDocument_Root( &pStore->store ), SV( kSections[SEC_ENTITIES] ) );
        auto *pAt13 = FirstOf( pEntities );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pEntities ); ++i, pAt13 = NextOf( pAt13 ) ) {
            const key_value_t *pEntity = pAt13;
            u64 id = 0u;
            if ( !EntityIsReadable( pEntity, id ) ) {
                bOk = Place( GetTarget( plan, iLayer ), SEC_ENTITIES, placed_t{ kUnreadable, i, pEntity, nullptr } );
                continue;
            }
            math::vec3d_t origin{};
            const bool_t bOrigin = ReadVec3( KeyValue_Find( pEntity, SV( "origin" ) ), origin );
            target_t *pTarget = GetTarget( plan, iLayer );
            bOk = Place( pTarget, SEC_ENTITIES, placed_t{ id, i, pEntity, nullptr } ) &&
                  Vector_PushBack( &plan.placements, placement_t{ id, iLayer } );
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
        target_t *pTarget = GetTarget( plan, owner.iLayer );
        bOk = pTarget != nullptr && Vector_PushBack( &plan.placements, placement_t{ owned.pLive->id, owner.iLayer } );
        if ( bOk ) {
            MapBounds_AddBounds( pTarget->bounds, owned.pLive->bounds );
            CY_ASSERT( Vector_Count( &plan.placements ) > nEntityPlacements );
        }
    }

    // 3. World geometry by its layer.
    for ( usize i = 0u; bOk && i < Vector_Count( &plan.lives ); ++i ) {
        const live_t &live = plan.lives.pData[i];
        if ( live.owner != 0u ) { continue; }
        target_t *pTarget = GetTarget( plan, live.iLayer );
        bOk = Place( pTarget, SectionOfKind( live.kind ), placed_t{ live.id, 0u, nullptr, &live } ) &&
              Vector_PushBack( &plan.placements, placement_t{ live.id, live.iLayer } );
        if ( bOk ) { MapBounds_AddBounds( pTarget->bounds, live.bounds ); }
    }

    // 4. Everything else read from the stores: unreadable geometry, shapes,
    //    notes, prefabs, foliage, unknown members. Groups wait for step 5.
    usize nFoliageOrder = 0u;
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pStore->layer ) ) );
        const key_value_t *pRoot = SettingsDocument_Root( &pStore->store );
        for ( u32 section = SEC_BRUSHES; bOk && section <= SEC_TERRAINS; ++section ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            auto *pAt14 = FirstOf( pArray );
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pArray ); ++i, pAt14 = NextOf( pAt14 ) ) {
                const key_value_t *pRecord = pAt14;
                u64 id = 0u;
                // A record of a live object is regenerated in step 3; any
                // other record could not be read and stays as written.
                if ( KeyValue_Type( pRecord ) == key_value_type_t::OBJECT && ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) &&
                     FindLive( plan, id ) != nullptr ) {
                    continue;
                }
                bOk = Place( GetTarget( plan, iLayer ), section, placed_t{ kUnreadable, i, pRecord, nullptr } );
            }
        }
        for ( u32 section : { static_cast<u32>( SEC_SHAPES ), static_cast<u32>( SEC_NOTES ), static_cast<u32>( SEC_PREFABS ) } ) {
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            auto *pAt15 = FirstOf( pArray );
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pArray ); ++i, pAt15 = NextOf( pAt15 ) ) {
                const key_value_t *pObject = pAt15;
                u64 id = 0u;
                if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) ) {
                    bOk = Place( GetTarget( plan, iLayer ), section, placed_t{ kUnreadable, i, pObject, nullptr } );
                    continue;
                }
                map_bounds_t bounds{};
                math::vec3d_t point{};
                if ( section == SEC_SHAPES ) {
                    bounds = ShapeBounds( pObject );
                } else if ( ReadVec3( KeyValue_Find( pObject, SV( section == SEC_NOTES ? "position" : "origin" ) ), point ) ) {
                    MapBounds_AddPoint( bounds, point );
                }
                target_t *pTarget = GetTarget( plan, iLayer );
                bOk = Place( pTarget, section, placed_t{ id, i, pObject, nullptr } ) &&
                      Vector_PushBack( &plan.placements, placement_t{ id, iLayer } );
                if ( bOk ) { MapBounds_AddBounds( pTarget->bounds, bounds ); }
            }
        }
        // Foliage: instances of one model in one layer are written as one
        // record, sorted by position.
        const key_value_t *pFoliage = KeyValue_Find( pRoot, SV( kSections[SEC_FOLIAGE] ) );
        auto *pAt16 = FirstOf( pFoliage );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pFoliage ); ++i, pAt16 = NextOf( pAt16 ) ) {
            const key_value_t *pRecord = pAt16;
            string_view_t model{};
            const key_value_t *pInstances = KeyValue_Find( pRecord, SV( "instances" ) );
            if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT || !KeyValue_GetString( KeyValue_Find( pRecord, SV( "model" ) ), &model ) ||
                 model.cchLength == 0u || KeyValue_Type( pInstances ) != key_value_type_t::ARRAY ) {
                bOk = Place( GetTarget( plan, iLayer ), SEC_FOLIAGE, placed_t{ kUnreadable, i, pRecord, nullptr } );
                continue;
            }
            bool_t bKnown = CY_FALSE;
            for ( usize k = 0u; k < Vector_Count( &plan.foliageProps ) && !bKnown; ++k ) {
                bKnown = plan.foliageProps.pData[k].iLayer == iLayer && StringView_Equals( plan.foliageProps.pData[k].model, model );
            }
            if ( !bKnown ) {
                bOk = Vector_PushBack( &plan.foliageProps, foliage_props_t{ iLayer, model, KeyValue_Find( pRecord, SV( "properties" ) ) } );
            }
            auto *pAt51 = FirstOf( pInstances );
            for ( usize k = 0u; bOk && k < KeyValue_ChildCount( pInstances ); ++k, pAt51 = NextOf( pAt51 ) ) {
                foliage_item_t item{};
                item.model = model;
                item.pInstance = pAt51;
                item.order = nFoliageOrder++;
                item.bReadable = KeyValue_Type( item.pInstance ) == key_value_type_t::ARRAY && KeyValue_ChildCount( item.pInstance ) == 7u;
                for ( usize c = 0u; item.bReadable && c < 7u; ++c ) {
                    f64 value = 0.0;
                    item.bReadable = ReadNumber( KeyValue_ChildAt( item.pInstance, c ), value );
                    if ( c < 3u ) { item.position[c] = value; }
                }
                const math::vec3d_t position = math::Vec3d_Make( item.position[0], item.position[1], item.position[2] );
                target_t *pTarget = GetTarget( plan, iLayer );
                bOk = pTarget != nullptr && Vector_PushBack( &pTarget->foliage, item );
                if ( bOk && item.bReadable ) { MapBounds_AddPoint( pTarget->bounds, position ); }
            }
        }
        // Unknown store members stay with this layer.
        auto *pAt17 = FirstOf( pRoot );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pRoot ); ++i, pAt17 = NextOf( pAt17 ) ) {
            const key_value_t *pMember = pAt17;
            if ( IsListed( KeyValue_Name( pMember ), Order( kStoreOrder ) ) ) { continue; }
            target_t *pTarget = GetTarget( plan, iLayer );
            bOk = pTarget != nullptr && Vector_PushBack( &pTarget->extras, pMember );
        }
    }
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    SortPlacements( plan );

    // 5. Groups: the layer of their first member.
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pStore->layer ) ) );
        const key_value_t *pGroups = KeyValue_Find( SettingsDocument_Root( &pStore->store ), SV( kSections[SEC_GROUPS] ) );
        auto *pAt18 = FirstOf( pGroups );
        for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pGroups ); ++i, pAt18 = NextOf( pAt18 ) ) {
            const key_value_t *pGroup = pAt18;
            u64 id = 0u;
            if ( KeyValue_Type( pGroup ) != key_value_type_t::OBJECT || !ReadId( KeyValue_Find( pGroup, SV( "id" ) ), id ) ) {
                bOk = Place( GetTarget( plan, iLayer ), SEC_GROUPS, placed_t{ kUnreadable, i, pGroup, nullptr } );
                continue;
            }
            u32 iTargetLayer = iLayer;
            const key_value_t *pMembers = KeyValue_Find( pGroup, SV( "members" ) );
            u64 first = 0u;
            if ( KeyValue_ChildCount( pMembers ) != 0u && ReadId( KeyValue_ChildAt( pMembers, 0u ), first ) ) {
                if ( const placement_t *pFirst = FindPlacement( plan, first ) ) { iTargetLayer = pFirst->iLayer; }
            }
            bOk = Place( GetTarget( plan, iTargetLayer ), SEC_GROUPS, placed_t{ id, i, pGroup, nullptr } );
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
    if ( KeyValue_Type( pSource ) != key_value_type_t::ARRAY ) { return Clone( ctx.pDocument, pParent, pSource ); }
    struct entry_t { u64 id; usize order; const key_value_t *source; };
    vector_t<entry_t> ids{};
    // Root-only ordered arrays have no map in their writer context. Every
    // output shares the initialized save plan and its allocator.
    if ( !Vector_Init( &ids, ctx.pPlan->pAllocator, KeyValue_ChildCount( pSource ) ) ) { return CY_FALSE; }
    auto *at = FirstOf( pSource );
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i, at = NextOf( at ) ) {
        u64 id = 0u;
        if ( !ReadId( at, id ) ) { return Clone( ctx.pDocument, pParent, pSource ); }
        if ( !Vector_PushBack( &ids, entry_t{ id, i, at } ) ) { return CY_FALSE; }
    }
    if ( ids.nCount > 1 ) {
        std::sort( ids.pData, ids.pData + ids.nCount, []( const entry_t &a, const entry_t &b ) noexcept {
            return a.id != b.id ? a.id < b.id : a.order < b.order;
        } );
    }
    key_value_t *pArray = KeyValue_ObjectInsert( ctx.pDocument, pParent, SV( pName ), key_value_type_t::ARRAY );
    for ( usize i = 0u; pArray != nullptr && i < Vector_Count( &ids ); ++i ) {
        // Sorting changes entry order, not ownership of their authored notes.
        key_value_t *pId = KeyValue_CloneInto( ctx.pDocument, pArray, ids.pData[i].source );
        if ( pId == nullptr || !KeyValue_SetU64( ctx.pDocument, pId, ids.pData[i].id ) ) { return CY_FALSE; }
    }
    return pArray != nullptr && CarryComments( ctx.pDocument, pArray, pSource );
}

// A value equal to `value` everywhere: one number, or a list of numbers.
CYPHER_NODISCARD bool_t IsUniform( const key_value_t *pNode, f64 value ) noexcept
{
    f64 number = 0.0;
    if ( ReadNumber( pNode, number ) ) { return number == value; }
    if ( KeyValue_Type( pNode ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pNode ) == 0u ) { return CY_FALSE; }
    auto *pAt20 = FirstOf( pNode );
    for ( usize i = 0u; i < KeyValue_ChildCount( pNode ); ++i, pAt20 = NextOf( pAt20 ) ) {
        if ( !ReadNumber( pAt20, number ) || number != value ) { return CY_FALSE; }
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
    auto *pAt21 = FirstOf( pArray );
    for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt21 = NextOf( pAt21 ) ) {
        if ( !ReadNumber( pAt21, value ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t AppendReals( key_value_document_t *pDocument, key_value_t *pArray, const key_value_t *pSource ) noexcept
{
    auto *pAt22 = FirstOf( pSource );
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i, pAt22 = NextOf( pAt22 ) ) {
        f64 value = 0.0;
        ( void )ReadNumber( pAt22, value );
        key_value_t *pValue = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::NULL_VALUE );
        if ( pValue == nullptr || !KeyValue_SetF64( pDocument, pValue, MapReal( value ) ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Copies pSource into pParent (an object, under its own name, or an array)
// in its canonical spelling.
CYPHER_NODISCARD bool_t CloneCanonicalValue( key_value_document_t *pDocument, key_value_t *pParent, const key_value_t *pSource ) noexcept
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
            auto *pAt23 = FirstOf( pSource );
            for ( usize i = 0u; bRows && i < KeyValue_ChildCount( pSource ); ++i, pAt23 = NextOf( pAt23 ) ) { bRows = AllNumbers( pAt23 ); }
            if ( !bRows ) { break; }
            key_value_t *pNode = insert( key_value_type_t::ARRAY );
            auto *pAt24 = FirstOf( pSource );
            for ( usize i = 0u; pNode != nullptr && i < KeyValue_ChildCount( pSource ); ++i, pAt24 = NextOf( pAt24 ) ) {
                key_value_t *pRow = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::ARRAY );
                if ( pRow == nullptr || !AppendReals( pDocument, pRow, pAt24 ) ) { return CY_FALSE; }
            }
            return pNode != nullptr;
        }
        case spelling_t::AS_WRITTEN: break;
    }
    return Clone( pDocument, pParent, pSource );
}

CYPHER_NODISCARD bool_t CloneCanonical( key_value_document_t *pDocument, key_value_t *pParent, const key_value_t *pSource ) noexcept
{
    // The rewritten value is the parent's newest child, object or array.
    return CloneCanonicalValue( pDocument, pParent, pSource ) && CarryComments( pDocument, KeyValue_LastChild( pParent ), pSource );
}

CYPHER_NODISCARD bool_t CloneUnknown( key_value_document_t *pDocument, key_value_t *pOut, const key_value_t *pSource, order_t known ) noexcept
{
    auto *pAt25 = FirstOf( pSource );
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i, pAt25 = NextOf( pAt25 ) ) {
        const key_value_t *pMember = pAt25;
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
    if ( pOut == nullptr || !SetU64Member( pDocument, pOut, "id", live.id ) || !CarryComments( pDocument, pOut, live.pRecord ) ) { return CY_FALSE; }
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
    auto *pAt26 = FirstOf( pRecord );
    for ( usize i = 0u; i < KeyValue_ChildCount( pRecord ); ++i, pAt26 = NextOf( pAt26 ) ) {
        const key_value_t *pMember = pAt26;
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
    if ( pOut == nullptr || !CarryComments( pDocument, pOut, pSource ) ) { return CY_FALSE; }
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
    if ( pOut == nullptr || !CarryComments( pDocument, pOut, pEntity ) ) { return CY_FALSE; }
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
            auto *pAt27 = FirstOf( pNested );
            for ( usize i = 0u; i < KeyValue_ChildCount( pNested ); ++i, pAt27 = NextOf( pAt27 ) ) {
                u64 recordId = 0u;
                const key_value_t *pRecord = pAt27;
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
            auto *pAt28 = FirstOf( pNested );
            for ( usize i = 0u; i < KeyValue_ChildCount( pNested ); ++i, pAt28 = NextOf( pAt28 ) ) {
                u64 recordId = 0u;
                const key_value_t *pRecord = pAt28;
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
            bOk = pOutputs != nullptr && CarryComments( pDocument, pOutputs, pMember );
            // Outputs keep their authored order: it is the order they fire in.
            auto *pAt29 = FirstOf( pMember );
            for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pMember ); ++i, pAt29 = NextOf( pAt29 ) ) {
                const key_value_t *pOutput = pAt29;
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
    if ( pOut == nullptr || !CarryComments( pDocument, pOut, pObject ) ) { return CY_FALSE; }
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
    for ( const key_value_t *pChild = KeyValue_FirstChild( pNode ); pChild != nullptr && nCount <= nLimit; pChild = KeyValue_NextSibling( pChild ) ) {
        nCount += CountValues( pChild, nLimit - nCount );
    }
    return nCount;
}

// Builds one layer's summary and sections in canonical order. Nothing is
// produced here; the caller checks every layer before making any text.
CYPHER_NODISCARD map_status_t BuildTargetOutput( writer_ctx_t &ctx, target_t &target ) noexcept
{
    map_document_t *pMap = ctx.pMap;
    usize nObjects = 0u;
    for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
        if ( section != SEC_FOLIAGE ) { nObjects += Vector_Count( &target.sections[section] ); }
    }
    if ( nObjects > MAP_LAYER_OBJECTS_MAX || Vector_Count( &target.foliage ) > MAP_FOLIAGE_INSTANCES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }

    void *pMemory = Allocator_Allocate( pMap->pAllocator, sizeof( settings_document_t ), alignof( settings_document_t ) );
    if ( pMemory == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    target.pOutput = new ( pMemory ) settings_document_t{};
    if ( SettingsDocument_Init( target.pOutput, pMap->pAllocator, StoreIdentity() ) != settings_document_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    key_value_document_t *pDocument = target.pOutput->pDocument;
    ctx.pDocument = pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );

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
        const map_layer_store_t *pSourceStore = FindStore( pMap, SV( pMap->layers.pData[target.iLayer].id ) );
        const key_value_t *pSourceSection = pSourceStore != nullptr
            ? KeyValue_Find( SettingsDocument_Root( &pSourceStore->store ), SV( kSections[section] ) )
            : nullptr;
        if ( pArray == nullptr || !CarryComments( pDocument, pArray, pSourceSection ) ) { return map_status_t::OUT_OF_MEMORY; }
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
        // An unknown member cannot share a name with a section; two values
        // under one name cannot both be kept, so the save is refused.
        if ( const key_value_t *pExisting = KeyValue_Find( pRoot, KeyValue_Name( pExtra ) ) ) {
            if ( KeyValuesEqual( pExisting, pExtra ) ) { continue; }
            return map_status_t::SAVE_BLOCKED;
        }
        if ( !Clone( pDocument, pRoot, pExtra ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}

// The level, layer by layer: each layer's own members, then the summary and
// sections BuildTargetOutput made for it, then members this build does not
// know. A section kept as written because it is not a list
// (SECTION_UNREADABLE) is written back unchanged unless the layer now has
// objects of that kind: two values under one name cannot both be kept, and
// dropping either would lose data, so the save is refused.
CYPHER_NODISCARD map_status_t WriteLayers( save_plan_t &plan, key_value_document_t *pDocument, key_value_t *pRoot, const key_value_t *pSource ) noexcept
{
    key_value_t *pArray = KeyValue_ObjectInsert( pDocument, pRoot, SV( "layers" ), key_value_type_t::ARRAY );
    if ( pArray == nullptr || !CarryComments( pDocument, pArray, pSource ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *pAt30 = FirstOf( pSource );
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i, pAt30 = NextOf( pAt30 ) ) {
        const key_value_t *pLayer = pAt30;
        key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
        if ( pOut == nullptr || !CarryComments( pDocument, pOut, pLayer ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( const char *pName : kLayerOrder ) {
            const key_value_t *pMember = KeyValue_Find( pLayer, SV( pName ) );
            if ( pMember != nullptr && !CloneCanonical( pDocument, pOut, pMember ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        for ( usize t = 0u; t < Vector_Count( &plan.targets ); ++t ) {
            const target_t &target = *plan.targets.pData[t];
            if ( target.iLayer != i ) { continue; }
            const key_value_t *pContent = SettingsDocument_Root( target.pOutput );
            auto *pAt52 = FirstOf( pContent );
            for ( usize k = 0u; k < KeyValue_ChildCount( pContent ); ++k, pAt52 = NextOf( pAt52 ) ) {
                if ( !Clone( pDocument, pOut, pAt52 ) ) { return map_status_t::OUT_OF_MEMORY; }
            }
        }
        auto *pAt53 = FirstOf( pLayer );
        for ( usize k = 0u; k < KeyValue_ChildCount( pLayer ); ++k, pAt53 = NextOf( pAt53 ) ) {
            const key_value_t *pMember = pAt53;
            const string_view_t name = KeyValue_Name( pMember );
            if ( IsListed( name, Order( kLayerOrder ) ) || StringView_Equals( name, SV( "info" ) ) ) { continue; }
            if ( const key_value_t *pExisting = KeyValue_Find( pOut, name ) ) {
                if ( !IsListed( name, Order( kSections ) ) && KeyValuesEqual( pExisting, pMember ) ) { continue; }
                return map_status_t::SAVE_BLOCKED;
            }
            if ( !Clone( pDocument, pOut, pMember ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    return map_status_t::OK;
}

CYPHER_NODISCARD bool_t WriteOrderedArray( key_value_document_t *pDocument, key_value_t *pRoot, const key_value_t *pSource, order_t order, save_plan_t &plan ) noexcept
{
    key_value_t *pArray = KeyValue_ObjectInsert( pDocument, pRoot, KeyValue_Name( pSource ), key_value_type_t::ARRAY );
    if ( pArray == nullptr || !CarryComments( pDocument, pArray, pSource ) ) { return CY_FALSE; }
    writer_ctx_t ctx{ nullptr, &plan, pDocument };
    auto *pAt31 = FirstOf( pSource );
    for ( usize i = 0u; i < KeyValue_ChildCount( pSource ); ++i, pAt31 = NextOf( pAt31 ) ) {
        const key_value_t *pEntry = pAt31;
        if ( KeyValue_Type( pEntry ) != key_value_type_t::OBJECT ) {
            if ( !Clone( pDocument, pArray, pEntry ) ) { return CY_FALSE; }
            continue;
        }
        key_value_t *pOut = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
        if ( pOut == nullptr || !CarryComments( pDocument, pOut, pEntry ) ) { return CY_FALSE; }
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

// The whole map's tree in canonical order: root members, the summary, and
// every layer with the sections BuildTargetOutput made for it.
CYPHER_NODISCARD map_status_t BuildRootOutput( map_document_t *pMap, save_plan_t &plan, settings_document_t *pOutput ) noexcept
{
    if ( !SyncRootLayers( pMap ) || !SetU64Member( pMap->root.pDocument, KeyValue_Root( pMap->root.pDocument ), "next_id", pMap->nextId ) ||
         SettingsDocument_Init( pOutput, pMap->pAllocator, RootIdentity() ) != settings_document_status_t::OK ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    key_value_document_t *pDocument = pOutput->pDocument;
    key_value_t *pRoot = KeyValue_Root( pDocument );
    const key_value_t *pWorking = SettingsDocument_Root( &pMap->root );
    if ( !CarryComments( pDocument, pRoot, pWorking ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( const char *pName : kRootOrder ) {
        const string_view_t name = SV( pName );
        const key_value_t *pMember = KeyValue_Find( pWorking, name );
        if ( pMember == nullptr ) { continue; }
        const bool_t bArray = KeyValue_Type( pMember ) == key_value_type_t::ARRAY;
        bool_t bOk = CY_TRUE;
        if ( bArray && StringView_Equals( name, SV( "layers" ) ) ) {
            const map_status_t status = WriteLayers( plan, pDocument, pRoot, pMember );
            if ( status != map_status_t::OK ) { return status; }
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
            auto *pAt32 = FirstOf( pMember );
            for ( usize i = 0u; bStrings && i < KeyValue_ChildCount( pMember ); ++i, pAt32 = NextOf( pAt32 ) ) {
                bStrings = KeyValue_GetString( pAt32, &tags[nTags++] );
            }
            if ( !bStrings ) {
                bOk = Clone( pDocument, pRoot, pMember );
            } else {
                std::sort( tags, tags + nTags, []( string_view_t a, string_view_t b ) { return StringView_Compare( a, b ) < 0; } );
                key_value_t *pTags = KeyValue_ObjectInsert( pDocument, pRoot, name, key_value_type_t::ARRAY );
                bOk = pTags != nullptr && CarryComments( pDocument, pTags, pMember );
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
    return CloneUnknown( pDocument, pRoot, pWorking, Order( kRootOrder ) ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

// Plans the save and builds every output tree. The document changes only
// in bookkeeping the save needs anyway: ID allocation kept in step, the
// root's next_id, and its layer list.
CYPHER_NODISCARD map_status_t BuildOutputs( map_document_t *pMap, save_plan_t &plan, settings_document_t *pOutput ) noexcept
{
    SyncIds( pMap );
    plan.pAllocator = pMap->pAllocator;
    if ( !Vector_Init( &plan.lives, pMap->pAllocator ) || !Vector_Init( &plan.owned, pMap->pAllocator ) ||
         !Vector_Init( &plan.placements, pMap->pAllocator ) || !Vector_Init( &plan.foliageProps, pMap->pAllocator ) ||
         !Vector_Init( &plan.targets, pMap->pAllocator ) || !Vector_Init( &plan.scratch, pMap->pAllocator ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    map_status_t status = PlanSave( pMap, plan );
    if ( status != map_status_t::OK ) { return status; }
    std::sort( plan.targets.pData, plan.targets.pData + Vector_Count( &plan.targets ),
               []( const target_t *a, const target_t *b ) { return a->iLayer < b->iLayer; } );
    writer_ctx_t ctx{ pMap, &plan, nullptr };
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        status = BuildTargetOutput( ctx, *plan.targets.pData[i] );
        if ( status != map_status_t::OK ) { return status; }
    }
    return BuildRootOutput( pMap, plan, pOutput );
}

// After a save the layer stores are the written sections, minus `info` and
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
        auto *pAt33 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt33 = NextOf( pAt33 ) ) {
            if ( !strip( KindOfSection( section ), pAt33 ) ) { return CY_FALSE; }
        }
    }
    key_value_t *pEntities = KeyValue_Find( pRoot, SV( kSections[SEC_ENTITIES] ) );
    auto *pAt34 = FirstOf( pEntities );
    for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ); ++i, pAt34 = NextOf( pAt34 ) ) {
        key_value_t *pEntity = pAt34;
        if ( !EntityShapeOk( pEntity ) ) { continue; }
        for ( map_geometry_kind_t kind : kOwnedKinds ) {
            key_value_t *pNested = KeyValue_Find( pEntity, SV( MapGeometry_SectionName( kind ) ) );
            auto *pAt54 = FirstOf( pNested );
            for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt54 = NextOf( pAt54 ) ) {
                if ( !strip( kind, pAt54 ) ) { return CY_FALSE; }
            }
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD key_value_t *FindObjectInternal( map_document_t *pMap, u64 id, map_layer_store_t **ppStoreOut, key_value_t **ppParentOut ) noexcept
{
    if ( pMap == nullptr || id == 0u ) { return nullptr; }
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        key_value_t *pRoot = KeyValue_Root( pStore->store.pDocument );
        for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
            if ( section == SEC_FOLIAGE ) { continue; }
            key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            auto *pAt35 = FirstOf( pArray );
            for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt35 = NextOf( pAt35 ) ) {
                key_value_t *pObject = pAt35;
                u64 objectId = 0u;
                if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
                if ( ReadId( KeyValue_Find( pObject, SV( "id" ) ), objectId ) && objectId == id ) {
                    if ( ppStoreOut != nullptr ) { *ppStoreOut = pStore; }
                    if ( ppParentOut != nullptr ) { *ppParentOut = pArray; }
                    return pObject;
                }
                if ( section != SEC_ENTITIES || !EntityShapeOk( pObject ) ) { continue; }
                for ( map_geometry_kind_t kind : kOwnedKinds ) {
                    key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                    auto *pAt55 = FirstOf( pNested );
                    for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt55 = NextOf( pAt55 ) ) {
                        key_value_t *pRecord = pAt55;
                        if ( ReadId( KeyValue_Find( pRecord, SV( "id" ) ), objectId ) && objectId == id ) {
                            if ( ppStoreOut != nullptr ) { *ppStoreOut = pStore; }
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
    for ( usize i = 0u; i < Vector_Count( &pMap->layerStores ); ++i ) { DestroyStore( pMap->pAllocator, pMap->layerStores.pData[i] ); }
    Vector_Shutdown( &pMap->layerStores );
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
    const bool_t bOk = Vector_Init( &pMap->layerStores, pAllocator ) && Vector_Init( &pMap->layers, pAllocator ) &&
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
        return bRoot ? VisitRootIds( pRoot, visit ) : VisitStoreIds( pRoot, visit );
    };
    bOk = walk( pMap->root.pDocument, KeyValue_Root( pMap->root.pDocument ), {}, CY_TRUE );
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        bOk = walk( pStore->store.pDocument, KeyValue_Root( pStore->store.pDocument ), SV( pStore->layer ), CY_FALSE );
    }
    if ( !bReassign ) { pMap->bReadOnly = CY_TRUE; }
    return bOk;
}

// Records of one foliage model in one layer should agree on properties; the
// first (in file order) is what saving keeps.
void CheckFoliage( map_document_t *pMap ) noexcept
{
    struct seen_t {
        u32 iLayer;
        string_view_t model;
        const key_value_t *pProperties;
    };
    vector_t<seen_t> seen{};
    if ( !Vector_Init( &seen, pMap->pAllocator ) ) { return; }
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const u32 iLayer = static_cast<u32>( LayerIndex( pMap, SV( pStore->layer ) ) );
        const key_value_t *pFoliage = KeyValue_Find( SettingsDocument_Root( &pStore->store ), SV( kSections[SEC_FOLIAGE] ) );
        auto *pAt36 = FirstOf( pFoliage );
        for ( usize i = 0u; i < KeyValue_ChildCount( pFoliage ); ++i, pAt36 = NextOf( pAt36 ) ) {
            const key_value_t *pRecord = pAt36;
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
            if ( !bSame ) { AddProblem( pMap, map_problem_code_t::FOLIAGE_PROPERTIES_DIFFER, SV( pStore->layer ), "foliage", 0u ); }
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
    string_view_t text,
    u32 flags ) noexcept
{
    if ( pMap == nullptr || pAllocator == nullptr || pMap->pAllocator != nullptr || text.pData == nullptr ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    map_status_t status = InitContainers( pMap, pAllocator );
    const auto fail = [&]( map_status_t failure ) noexcept {
        MapDocument_Shutdown( pMap );
        return failure;
    };
    // Keeps the diagnostics for the caller and drops everything else.
    const auto refuse = [&]( map_status_t failure ) noexcept {
        vector_t<map_problem_t> problems{};
        Vector_Move( &problems, &pMap->problems );
        MapDocument_Shutdown( pMap );
        Vector_Move( &pMap->problems, &problems );
        return failure;
    };
    if ( status != map_status_t::OK ) { return fail( status ); }
    if ( SettingsDocument_Init( &pMap->root, pAllocator, RootIdentity() ) != settings_document_status_t::OK ) {
        return fail( map_status_t::OUT_OF_MEMORY );
    }
    status = ParseMap( pMap, text );
    if ( status == map_status_t::OK ) { status = ReadRoot( pMap ); }
    if ( status == map_status_t::OUT_OF_MEMORY ) { return fail( status ); }
    if ( status != map_status_t::OK ) { return refuse( status ); }
    status = SplitLayers( pMap );
    if ( status != map_status_t::OK ) { return fail( status ); }

    // Identity: every explicit ID across the root and the layers.
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, pAllocator ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    bool_t bOk = CY_TRUE;
    auto collect = [&]( key_value_t *, u64 id ) noexcept {
        bOk = Vector_PushBack( &ids, id );
        return bOk;
    };
    ( void )VisitRootIds( KeyValue_Root( pMap->root.pDocument ), collect );
    for ( usize iStore = 0u; bOk && iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        ( void )VisitStoreIds( KeyValue_Root( pStore->store.pDocument ), collect );
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
        auto *pAt37 = FirstOf( pArray );
        for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt37 = NextOf( pAt37 ) ) {
            key_value_t *pObject = pAt37;
            bool_t bAssigned = CY_FALSE;
            if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { continue; }
            if ( !AssignObjectId( pMap->root.pDocument, pObject, nextId, bAssigned ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
            if ( bAssigned ) { NoteAssigned( pMap, {}, pArrayName, pObject ); }
        }
    }
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        if ( !AssignStoreIds( pMap, pStore, nextId ) ) { return fail( map_status_t::OUT_OF_MEMORY ); }
    }

    // Geometry into the geometry document.
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        status = ExtractStoreGeometry( pMap, pStore );
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

map_status_t MapDocument_Save( map_document_t *pMap, text_buffer_t *pTextOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || !TextBuffer_IsValid( pTextOut ) || !Allocator_IsValid( pTextOut->pAllocator ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }

    // Build everything and check every limit before any text exists.
    save_plan_t plan{};
    settings_document_t output{};
    map_status_t status = BuildOutputs( pMap, plan, &output );
    if ( status != map_status_t::OK ) { return status; }
    // The reader's value budget is a limit like the text size: a map it
    // would reject is refused here instead of written.
    if ( CountValues( SettingsDocument_Root( &output ), MAP_VALUES_MAX ) > MAP_VALUES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    text_buffer_t text{};
    status = WriteCompactText( &output, pMap->pAllocator, &text, MAP_TEXT_MAX );
    if ( status != map_status_t::OK ) { return status; }

    // The stores the document will hold after the save: each layer's store
    // is the sections built for it; layers that came out empty keep none.
    // Everything that can fail happens before the document or the caller's
    // text changes.
    vector_t<map_layer_store_t *> storesAfter{};
    const auto discard = [&]() noexcept {
        for ( usize j = 0u; j < Vector_Count( &storesAfter ); ++j ) { DestroyStore( pMap->pAllocator, storesAfter.pData[j] ); }
        return map_status_t::OUT_OF_MEMORY;
    };
    if ( !Vector_Init( &storesAfter, pMap->pAllocator ) || !Vector_Reserve( &storesAfter, Vector_Count( &plan.targets ) ) ) { return discard(); }
    for ( usize i = 0u; i < Vector_Count( &plan.targets ); ++i ) {
        target_t &target = *plan.targets.pData[i];
        map_layer_store_t *pStore = NewStore( pMap, SV( pMap->layers.pData[target.iLayer].id ) );
        if ( pStore == nullptr ) { return discard(); }
        const bool_t bPushed = Vector_PushBack( &storesAfter, pStore ); // Reserved above; cannot fail.
        CY_ASSERT( bPushed );
        ( void )bPushed;
        std::swap( pStore->store.pDocument, target.pOutput->pDocument );
        if ( !StripAfterSave( pStore->store.pDocument, plan ) ) { return discard(); }
    }
    if ( !TextBuffer_Assign( pTextOut, TextBuffer_View( &text ) ) ) { return discard(); }

    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) { DestroyStore( pMap->pAllocator, pMap->layerStores.pData[iStore] ); }
    Vector_Shutdown( &pMap->layerStores );
    Vector_Move( &pMap->layerStores, &storesAfter );
    pMap->bIdsAssigned = CY_FALSE;
    // What was produced is CYKV 1 whatever the file was read as.
    if ( KeyValue_DocumentHeader( pMap->root.pDocument ).nLanguageVersion != CYKV_LANGUAGE_VERSION_1 ) {
        ( void )KeyValue_SetDocumentHeader( pMap->root.pDocument, { CYKV_LANGUAGE_VERSION_1, SV( MAP_SCHEMA_ID ), MAP_SCHEMA_VERSION } );
    }
    return map_status_t::OK;
}

map_status_t MapDocument_BuildTree( map_document_t *pMap, key_value_document_t **ppOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || ppOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *ppOut = nullptr;
    save_plan_t plan{};
    settings_document_t output{};
    const map_status_t status = BuildOutputs( pMap, plan, &output );
    if ( status != map_status_t::OK ) { return status; }
    *ppOut = output.pDocument;
    output.pDocument = nullptr;
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

string_view_t MapDocument_CreationLayer( const map_document_t *pMap ) noexcept
{
    if ( pMap == nullptr || pMap->layers.nCount == 0u ) { return {}; }
    const string_view_t current = StringView_FromCString( pMap->creationLayer );
    return current.cchLength != 0u && MapDocument_HasLayer( pMap, current ) ? current : StringView_FromCString( pMap->layers.pData[0].id );
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
    // The root and map_document_t::layers stay index for index, so the new
    // layer's entry lands at the same index in both. Reserving first means
    // the push below cannot fail after the root changed.
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
        map_layer_store_t *pStore = nullptr;
        key_value_t *pParent = nullptr;
        const key_value_t *pOwner = FindObjectInternal( pMap, ownerEntityId, &pStore, &pParent );
        if ( pOwner == nullptr || pParent != KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
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
    map_layer_store_t *pStore = nullptr; key_value_t *pParent = nullptr;
    if ( placement.owner != 0u ) {
        if ( kind == map_geometry_kind_t::TERRAIN ) { return map_status_t::INVALID_ARGUMENT; }
        key_value_t *pContainer = nullptr;
        pParent = FindObjectInternal( pMap, placement.owner, &pStore, &pContainer );
        if ( pParent == nullptr || pStore == nullptr ||
             pContainer != KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
            return map_status_t::UNKNOWN_OBJECT;
        }
    } else {
        pStore = GetOrAddStore( pMap, SV( pMap->layers.pData[placement.iLayer].id ) );
        if ( pStore == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        pParent = KeyValue_Root( pStore->store.pDocument );
    }
    auto *pArray = EnsureMember( pStore->store.pDocument, pParent, MapGeometry_SectionName( kind ), key_value_type_t::ARRAY );
    if ( pArray == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    if ( KeyValue_Type( pArray ) != key_value_type_t::ARRAY ) { return map_status_t::INVALID_ARGUMENT; }
    return KeyValue_CloneInto( pStore->store.pDocument, pArray, pRecord ) != nullptr ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_AddEntity( map_document_t *pMap, string_view_t layer, string_view_t className, math::vec3d_t origin, u64 *pIdOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || className.cchLength == 0u || pIdOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( LayerIndex( pMap, layer ) == CY_INVALID_SIZE ) { return map_status_t::UNKNOWN_LAYER; }
    map_layer_store_t *pStore = GetOrAddStore( pMap, layer );
    if ( pStore == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_document_t *pDocument = pStore->store.pDocument;
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

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

map_status_t MapDocument_AddGroup( map_document_t *pMap, string_view_t name, span_t<const u64> members, u64 *pIdOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pIdOut == nullptr || !Span_IsValid( members ) || members.nCount == 0u ||
         !StringView_IsValid( name ) || name.cchLength > MAP_NAME_MAX_LENGTH ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    vector_t<u64> sorted{};
    if ( !Vector_Init( &sorted, pMap->pAllocator, members.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < members.nCount; ++i ) {
        if ( members.pData[i] == 0u || !Vector_PushBack( &sorted, members.pData[i] ) ) { Vector_Shutdown( &sorted ); return map_status_t::INVALID_ARGUMENT; }
    }
    std::sort( sorted.pData, sorted.pData + sorted.nCount );
    const string_view_t layer = Vector_Count( &pMap->layers ) != 0u ? SV( pMap->layers.pData[0].id ) : SV( "default" );
    map_layer_store_t *pStore = GetOrAddStore( pMap, layer );
    key_value_document_t *pDocument = pStore != nullptr ? pStore->store.pDocument : nullptr;
    key_value_t *pGroups = pDocument != nullptr ? EnsureMember( pDocument, KeyValue_Root( pDocument ), kSections[SEC_GROUPS], key_value_type_t::ARRAY ) : nullptr;
    key_value_t *pGroup = pGroups != nullptr ? KeyValue_ArrayAppend( pDocument, pGroups, key_value_type_t::OBJECT ) : nullptr;
    const u64 id = MapDocument_AllocateId( pMap );
    key_value_t *pMembers = nullptr;
    bool_t bOk = pGroup != nullptr && id != 0u && SetU64Member( pDocument, pGroup, "id", id ) &&
        ( name.cchLength == 0u || SetStringMember( pDocument, pGroup, "name", name ) );
    if ( bOk ) { pMembers = KeyValue_ObjectInsert( pDocument, pGroup, SV( "members" ), key_value_type_t::ARRAY ); bOk = pMembers != nullptr; }
    for ( usize i = 0u; bOk && i < sorted.nCount; ++i ) {
        if ( i != 0u && sorted.pData[i] == sorted.pData[i - 1u] ) { continue; }
        key_value_t *pItem = KeyValue_ArrayAppend( pDocument, pMembers, key_value_type_t::U64 );
        bOk = pItem != nullptr && KeyValue_SetU64( pDocument, pItem, sorted.pData[i] );
    }
    Vector_Shutdown( &sorted );
    if ( !bOk ) { return map_status_t::OUT_OF_MEMORY; }
    *pIdOut = id;
    return map_status_t::OK;
}

namespace
{
// The group whose members list id directly; 0 when none does.
u64 DirectGroupOf( map_document_t *pMap, u64 id ) noexcept
{
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const key_value_t *pGroups = KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_GROUPS] ) );
        auto *pAt38 = FirstOf( pGroups );
        for ( usize i = 0u; i < KeyValue_ChildCount( pGroups ); ++i, pAt38 = NextOf( pAt38 ) ) {
            const key_value_t *pGroup = pAt38;
            const key_value_t *pMembers = KeyValue_Find( pGroup, SV( "members" ) );
            u64 groupId = 0u;
            if ( !ReadId( KeyValue_Find( pGroup, SV( "id" ) ), groupId ) ) { continue; }
            auto *pAt56 = FirstOf( pMembers );
            for ( usize k = 0u; k < KeyValue_ChildCount( pMembers ); ++k, pAt56 = NextOf( pAt56 ) ) {
                u64 member = 0u;
                if ( ReadId( pAt56, member ) && member == id ) { return groupId; }
            }
        }
    }
    return 0u;
}
} // namespace

u64 MapDocument_GroupOf( map_document_t *pMap, u64 id ) noexcept
{
    if ( pMap == nullptr || id == 0u ) { return 0u; }
    u64 outer = 0u;
    // Climb nested groups; a bounded walk also survives a cyclic file.
    for ( u64 current = DirectGroupOf( pMap, id ), steps = 0u; current != 0u && steps < 64u; current = DirectGroupOf( pMap, current ), ++steps ) { outer = current; }
    return outer;
}

map_status_t MapDocument_GroupMembers( map_document_t *pMap, u64 groupId, vector_t<u64> *pOut ) noexcept
{
    if ( pMap == nullptr || pOut == nullptr || !Vector_IsValid( pOut ) ) { return map_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pOut );
    vector_t<u64> pending{};
    if ( !Vector_Init( &pending, pMap->pAllocator ) || !Vector_PushBack( &pending, groupId ) ) { Vector_Shutdown( &pending ); return map_status_t::OUT_OF_MEMORY; }
    map_status_t result = map_status_t::UNKNOWN_OBJECT;
    for ( usize visited = 0u; Vector_Count( &pending ) != 0u && visited < 4096u; ++visited ) {
        const u64 current = pending.pData[Vector_Count( &pending ) - 1u];
        Vector_PopBack( &pending );
        map_layer_store_t *pStore = nullptr; key_value_t *pParent = nullptr;
        const key_value_t *pRecord = FindObjectInternal( pMap, current, &pStore, &pParent );
        const bool isGroup = pRecord != nullptr && pParent == KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_GROUPS] ) );
        if ( current == groupId && !isGroup ) { break; }
        if ( !isGroup ) {
            if ( !Vector_PushBack( pOut, current ) ) { result = map_status_t::OUT_OF_MEMORY; break; }
            continue;
        }
        result = map_status_t::OK;
        const key_value_t *pMembers = KeyValue_Find( pRecord, SV( "members" ) );
        auto *pAt39 = FirstOf( pMembers );
        for ( usize k = 0u; k < KeyValue_ChildCount( pMembers ); ++k, pAt39 = NextOf( pAt39 ) ) {
            u64 member = 0u;
            if ( ReadId( pAt39, member ) && member != 0u ) {
                // Geometry a tool created has no record yet; it is still a member.
                if ( !Vector_PushBack( &pending, member ) ) { result = map_status_t::OUT_OF_MEMORY; break; }
            }
        }
        if ( result != map_status_t::OK ) { break; }
    }
    Vector_Shutdown( &pending );
    return result;
}

// ---------------------------------------------------------------------------
// Visgroups
// ---------------------------------------------------------------------------

map_status_t MapDocument_AddVisgroup( map_document_t *pMap, string_view_t name, u64 *pIdOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pIdOut == nullptr || !StringView_IsValid( name ) || name.cchLength == 0u ||
         name.cchLength > MAP_NAME_MAX_LENGTH ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    key_value_document_t *pDocument = pMap->root.pDocument;
    key_value_t *pRoot = pDocument != nullptr ? KeyValue_Root( pDocument ) : nullptr;
    key_value_t *pArray = pRoot != nullptr ? EnsureMember( pDocument, pRoot, "visgroups", key_value_type_t::ARRAY ) : nullptr;
    if ( pArray == nullptr || KeyValue_Type( pArray ) != key_value_type_t::ARRAY ) { return map_status_t::OUT_OF_MEMORY; }
    if ( KeyValue_ChildCount( pArray ) >= MAP_VISGROUPS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    key_value_t *pVisgroup = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::OBJECT );
    const u64 id = MapDocument_AllocateId( pMap );
    if ( pVisgroup == nullptr || id == 0u || !SetU64Member( pDocument, pVisgroup, "id", id ) || !SetStringMember( pDocument, pVisgroup, "name", name ) ) {
        return map_status_t::OUT_OF_MEMORY;
    }
    *pIdOut = id;
    return map_status_t::OK;
}

map_status_t MapDocument_AddToVisgroup( map_document_t *pMap, u64 visgroupId, u64 objectId ) noexcept
{
    if ( pMap == nullptr || visgroupId == 0u || objectId == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    map_layer_store_t *pStore = nullptr;
    key_value_t *pRecord = FindObjectInternal( pMap, objectId, &pStore, nullptr );
    if ( pRecord == nullptr ) {
        // Unsaved geometry: retain a record holding only the membership.
        map_geometry_kind_t kind{};
        if ( !IsLiveGeometry( pMap, objectId, &kind ) ) { return map_status_t::UNKNOWN_OBJECT; }
        key_value_document_desc_t desc{}; desc.pAllocator = pMap->pAllocator;
        key_value_document_t *pScratch = KeyValue_CreateDocument( desc );
        key_value_t *pList = nullptr, *pItem = nullptr;
        const bool_t bBuilt = pScratch != nullptr && KeyValue_SetRootType( pScratch, key_value_type_t::OBJECT ) &&
            SetU64Member( pScratch, KeyValue_Root( pScratch ), "id", objectId ) &&
            ( pList = KeyValue_ObjectInsert( pScratch, KeyValue_Root( pScratch ), SV( "visgroups" ), key_value_type_t::ARRAY ) ) != nullptr &&
            ( pItem = KeyValue_ArrayAppend( pScratch, pList, key_value_type_t::U64 ) ) != nullptr && KeyValue_SetU64( pScratch, pItem, visgroupId );
        const map_status_t result = bBuilt ? MapDocument_InsertGeometryRecord( pMap, kind, KeyValue_Root( pScratch ) ) : map_status_t::OUT_OF_MEMORY;
        KeyValue_DestroyDocument( pScratch );
        return result;
    }
    key_value_document_t *pDocument = pStore->store.pDocument;
    key_value_t *pList = EnsureMember( pDocument, pRecord, "visgroups", key_value_type_t::ARRAY );
    if ( pList == nullptr || KeyValue_Type( pList ) != key_value_type_t::ARRAY ) { return map_status_t::OUT_OF_MEMORY; }
    // Kept sorted, as the format asks.
    usize insertAt = KeyValue_ChildCount( pList );
    auto *pAt40 = FirstOf( pList );
    for ( usize i = 0u; i < KeyValue_ChildCount( pList ); ++i, pAt40 = NextOf( pAt40 ) ) {
        u64 existing = 0u;
        if ( !ReadId( pAt40, existing ) ) { continue; }
        if ( existing == visgroupId ) { return map_status_t::OK; }
        if ( existing > visgroupId && insertAt == KeyValue_ChildCount( pList ) ) { insertAt = i; }
    }
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, pMap->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool_t bOk = CY_TRUE;
    auto *pAt41 = FirstOf( pList );
    for ( usize i = 0u; bOk && i < KeyValue_ChildCount( pList ); ++i, pAt41 = NextOf( pAt41 ) ) {
        u64 existing = 0u;
        if ( i == insertAt ) { bOk = Vector_PushBack( &ids, visgroupId ); }
        if ( bOk && ReadId( pAt41, existing ) ) { bOk = Vector_PushBack( &ids, existing ); }
    }
    if ( bOk && insertAt == KeyValue_ChildCount( pList ) ) { bOk = Vector_PushBack( &ids, visgroupId ); }
    // Rewrite the list in order.
    while ( bOk && KeyValue_ChildCount( pList ) != 0u ) { bOk = KeyValue_Remove( pDocument, pList, KeyValue_ChildAt( pList, 0u ) ); }
    for ( usize i = 0u; bOk && i < ids.nCount; ++i ) {
        key_value_t *pItem = KeyValue_ArrayAppend( pDocument, pList, key_value_type_t::U64 );
        bOk = pItem != nullptr && KeyValue_SetU64( pDocument, pItem, ids.pData[i] );
    }
    Vector_Shutdown( &ids );
    return bOk ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

namespace
{
bool_t ListsVisgroup( const key_value_t *pRecord, u64 visgroupId ) noexcept
{
    const key_value_t *pList = KeyValue_Find( pRecord, SV( "visgroups" ) );
    auto *pAt42 = FirstOf( pList );
    for ( usize i = 0u; i < KeyValue_ChildCount( pList ); ++i, pAt42 = NextOf( pAt42 ) ) {
        u64 id = 0u;
        if ( ReadId( pAt42, id ) && id == visgroupId ) { return CY_TRUE; }
    }
    return CY_FALSE;
}
} // namespace

map_status_t MapDocument_VisgroupMembers( map_document_t *pMap, u64 visgroupId, vector_t<u64> *pOut ) noexcept
{
    if ( pMap == nullptr || pOut == nullptr || !Vector_IsValid( pOut ) ) { return map_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pOut );
    for ( usize iStore = 0u; iStore < Vector_Count( &pMap->layerStores ); ++iStore ) {
        const map_layer_store_t *pStore = pMap->layerStores.pData[iStore];
        const key_value_t *pRoot = KeyValue_Root( pStore->store.pDocument );
        for ( u32 section = 0u; section < SEC_COUNT; ++section ) {
            if ( section == SEC_FOLIAGE ) { continue; }
            const key_value_t *pArray = KeyValue_Find( pRoot, SV( kSections[section] ) );
            auto *pAt43 = FirstOf( pArray );
            for ( usize i = 0u; i < KeyValue_ChildCount( pArray ); ++i, pAt43 = NextOf( pAt43 ) ) {
                const key_value_t *pObject = pAt43;
                u64 id = 0u;
                if ( ListsVisgroup( pObject, visgroupId ) && ReadId( KeyValue_Find( pObject, SV( "id" ) ), id ) && !Vector_PushBack( pOut, id ) ) {
                    return map_status_t::OUT_OF_MEMORY;
                }
                if ( section != SEC_ENTITIES ) { continue; }
                for ( map_geometry_kind_t kind : kOwnedKinds ) {
                    const key_value_t *pNested = KeyValue_Find( pObject, SV( MapGeometry_SectionName( kind ) ) );
                    auto *pAt57 = FirstOf( pNested );
                    for ( usize j = 0u; j < KeyValue_ChildCount( pNested ); ++j, pAt57 = NextOf( pAt57 ) ) {
                        const key_value_t *pRecord = pAt57;
                        if ( ListsVisgroup( pRecord, visgroupId ) && ReadId( KeyValue_Find( pRecord, SV( "id" ) ), id ) && !Vector_PushBack( pOut, id ) ) {
                            return map_status_t::OUT_OF_MEMORY;
                        }
                    }
                }
            }
        }
    }
    return map_status_t::OK;
}

key_value_t *MapDocument_FindObject( map_document_t *pMap, u64 id, map_layer_store_t **ppStoreOut ) noexcept
{
    return FindObjectInternal( pMap, id, ppStoreOut, nullptr );
}

map_status_t MapDocument_RemoveObject( map_document_t *pMap, u64 id ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || id == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    map_layer_store_t *pStore = nullptr;
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
        key_value_t *pRecord = FindObjectInternal( pMap, id, &pStore, &pParent );
        if ( pRecord != nullptr && !KeyValue_Remove( pStore->store.pDocument, pParent, pRecord ) ) { return map_status_t::OUT_OF_MEMORY; }
        return map_status_t::OK;
    }
    key_value_t *pNode = FindObjectInternal( pMap, id, &pStore, &pParent );
    if ( pNode == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( pParent == KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
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
    return KeyValue_Remove( pStore->store.pDocument, pParent, pNode ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_SetEntityOrigin( map_document_t *pMap, u64 id, math::vec3d_t origin ) noexcept
{
    map_layer_store_t *pStore = nullptr;
    key_value_t *pParent = nullptr;
    key_value_t *pNode = FindObjectInternal( pMap, id, &pStore, &pParent );
    if ( pNode == nullptr || pParent != KeyValue_Find( KeyValue_Root( pStore->store.pDocument ), SV( kSections[SEC_ENTITIES] ) ) ) {
        return map_status_t::UNKNOWN_OBJECT;
    }
    return SetVec3Member( pStore->store.pDocument, pNode, "origin", origin ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_status_t MapDocument_MaterialRef( map_document_t *pMap, string_view_t path, u64 *pRefOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pRefOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( path.cchLength > MAP_MATERIAL_PATH_MAX ) { return map_status_t::INVALID_ARGUMENT; }
    return MapMaterials_Intern( &pMap->materials, path, pRefOut ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}

map_layer_store_t *MapDocument_LayerStore( map_document_t *pMap, string_view_t layer ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || LayerIndex( pMap, layer ) == CY_INVALID_SIZE ) { return nullptr; }
    return GetOrAddStore( pMap, layer );
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
        case map_status_t::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
        case map_status_t::GEOMETRY_FAILED: return "GEOMETRY_FAILED";
        case map_status_t::UNKNOWN_OBJECT: return "UNKNOWN_OBJECT";
        case map_status_t::UNKNOWN_LAYER: return "UNKNOWN_LAYER";
        case map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED: return "MODIFIER_TRANSFORM_UNSUPPORTED";
        case map_status_t::PREFAB_TRANSFORM_UNSUPPORTED: return "PREFAB_TRANSFORM_UNSUPPORTED";
    }
    return "UNKNOWN";
}

const char *MapDocument_ProblemName( map_problem_code_t code ) noexcept
{
    switch ( code ) {
        case map_problem_code_t::ROOT_MEMBER_INVALID: return "ROOT_MEMBER_INVALID";
        case map_problem_code_t::ROOT_MEMBER_IGNORED: return "ROOT_MEMBER_IGNORED";
        case map_problem_code_t::TEXT_UNREADABLE: return "TEXT_UNREADABLE";
        case map_problem_code_t::VERSION_UNSUPPORTED: return "VERSION_UNSUPPORTED";
        case map_problem_code_t::SECTION_UNREADABLE: return "SECTION_UNREADABLE";
        case map_problem_code_t::OBJECT_UNREADABLE: return "OBJECT_UNREADABLE";
        case map_problem_code_t::ID_ASSIGNED: return "ID_ASSIGNED";
        case map_problem_code_t::DUPLICATE_ID: return "DUPLICATE_ID";
        case map_problem_code_t::ID_ABOVE_NEXT_ID: return "ID_ABOVE_NEXT_ID";
        case map_problem_code_t::FOLIAGE_PROPERTIES_DIFFER: return "FOLIAGE_PROPERTIES_DIFFER";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::map
