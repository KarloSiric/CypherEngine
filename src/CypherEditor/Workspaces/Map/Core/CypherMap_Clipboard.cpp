//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Authored selections transfer across maps with fresh identities and materials.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_Clipboard.h"
#include "CypherMap_Edit.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"
#include "CypherCommon/Tier1/CypherCommon_Unicode.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
string_view_t SV( const char *s ) noexcept { return StringView_FromCString( s ); }
bool Finite( math::vec3d_t v ) noexcept { return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ); }
bool ReadVec( const key_value_t *n, math::vec3d_t &v ) noexcept
{
    if ( KeyValue_Type( n ) != key_value_type_t::ARRAY || KeyValue_ChildCount( n ) != 3 ) { return false; }
    f64 c[3]{};
    for ( usize i = 0; i < 3; ++i ) {
        const auto *slot = KeyValue_ChildAt( n, i ); i64 signedValue = 0; u64 unsignedValue = 0;
        if ( KeyValue_GetF64( slot, &c[i] ) ) { continue; }
        if ( KeyValue_GetI64( slot, &signedValue ) ) { c[i] = static_cast<f64>( signedValue ); }
        else if ( KeyValue_GetU64( slot, &unsignedValue ) ) { c[i] = static_cast<f64>( unsignedValue ); }
        else { return false; }
    }
    v = { c[0], c[1], c[2] }; return Finite( v );
}
bool WriteVec( key_value_document_t *d, key_value_t *array, math::vec3d_t v ) noexcept
{
    auto *n = KeyValue_ArrayAppend( d, array, key_value_type_t::ARRAY );
    if ( !n ) { return false; }
    for ( const f64 c : { v.x, v.y, v.z } ) {
        auto *s = KeyValue_ArrayAppend( d, n, key_value_type_t::F64 );
        if ( !s || !KeyValue_SetF64( d, s, c ) ) { return false; }
    }
    return true;
}
bool WriteId( key_value_document_t *d, key_value_t *n, const char *name, u64 id ) noexcept
{
    auto *slot = KeyValue_ObjectInsert( d, n, SV( name ), key_value_type_t::U64 );
    return slot && KeyValue_SetU64( d, slot, id );
}
bool WriteString( key_value_document_t *d, key_value_t *n, const char *name, string_view_t value ) noexcept
{
    auto *slot = KeyValue_ObjectInsert( d, n, SV( name ), key_value_type_t::STRING );
    return slot && KeyValue_SetString( d, slot, value );
}
struct document_delete_t { void operator()( key_value_document_t *d ) const noexcept { KeyValue_DestroyDocument( d ); } };
using document_ptr_t = std::unique_ptr<key_value_document_t, document_delete_t>;
document_ptr_t Document( const allocator_t *a ) noexcept
{
    key_value_document_desc_t desc{}; desc.pAllocator = a; return document_ptr_t( KeyValue_CreateDocument( desc ) );
}
map_geometry_kind_t Kind( const map_document_t &m, u64 id ) noexcept
{
    if ( geo::GeometryDocument_FindBrush( &m.geometry, { id } ) ) { return map_geometry_kind_t::BRUSH; }
    if ( geo::GeometryDocument_FindMesh( &m.geometry, { id } ) ) { return map_geometry_kind_t::MESH; }
    if ( geo::GeometryDocument_FindPatch( &m.geometry, { id } ) ) { return map_geometry_kind_t::PATCH; }
    if ( geo::GeometryDocument_FindHeightField( &m.geometry, { id } ) ) { return map_geometry_kind_t::TERRAIN; }
    return map_geometry_kind_t::COUNT;
}
const map_geometry_record_t *Placement( const map_document_t &m, u64 id ) noexcept
{
    for ( usize i = 0; i < m.geometryRecords.nCount; ++i ) { if ( m.geometryRecords.pData[i].id == id ) { return &m.geometryRecords.pData[i]; } }
    return nullptr;
}
bool Contains( const vector_t<u64> &ids, u64 id ) noexcept
{
    for ( usize i = 0; i < ids.nCount; ++i ) { if ( ids.pData[i] == id ) { return true; } } return false;
}
bool AddUnique( vector_t<u64> &ids, u64 id ) noexcept { return Contains( ids, id ) || Vector_PushBack( &ids, id ); }
bool ReadAuthoredId( const key_value_t *slot, u64 &id ) noexcept
{
    if ( KeyValue_GetU64( slot, &id ) ) { return id != 0; }
    i64 signedId = 0;
    if ( KeyValue_GetI64( slot, &signedId ) && signedId > 0 ) { id = static_cast<u64>( signedId ); return true; }
    return false;
}
bool IsEntity( const map_document_t &m, u64 id ) noexcept
{
    map_chunk_t *chunk = nullptr;
    const auto *record = MapDocument_FindObject( const_cast<map_document_t *>( &m ), id, &chunk );
    if ( !record || !chunk ) { return false; }
    const auto *entities = KeyValue_Find( SettingsDocument_Root( &chunk->store ), SV( "entities" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( entities ); ++i ) { if ( KeyValue_ChildAt( entities, i ) == record ) { return true; } }
    return false;
}
bool EntityChildrenKnown( const map_document_t &m, const key_value_t *record, u64 owner ) noexcept
{
    // Never discard future/unreadable nested objects merely to copy their owner.
    if ( KeyValue_Find( record, SV( "terrains" ) ) ) { return false; }
    for ( const auto kind : { map_geometry_kind_t::BRUSH, map_geometry_kind_t::MESH, map_geometry_kind_t::PATCH } ) {
        const auto *children = KeyValue_Find( record, SV( MapGeometry_SectionName( kind ) ) );
        if ( children && KeyValue_Type( children ) != key_value_type_t::ARRAY ) { return false; }
        for ( usize i = 0; i < KeyValue_ChildCount( children ); ++i ) {
            u64 id = 0; const auto *child = KeyValue_ChildAt( children, i );
            if ( !ReadAuthoredId( KeyValue_Find( child, SV( "id" ) ), id ) || Kind( m, id ) != kind ) { return false; }
            const auto *p = Placement( m, id ); if ( !p || p->owner != owner ) { return false; }
        }
    }
    return true;
}
map_status_t WriteGeometry( const map_document_t &m, u64 id, map_geometry_kind_t kind, key_value_document_t *d, key_value_t *record ) noexcept
{
    if ( !WriteId( d, record, "id", id ) ) { return map_status_t::OUT_OF_MEMORY; }
    bool ok = false;
    if ( kind == map_geometry_kind_t::BRUSH ) {
        geo::brush_source_t source{};
        const auto s = geo::GeometryDocument_TryCopyBrushSource( &m.geometry, { id }, m.pAllocator, &source );
        if ( s != geo::geometry_status_t::OK ) { return s == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
        ok = MapGeometry_WriteBrush( source, m.materials, d, record ); geo::BrushSource_Shutdown( &source );
    } else if ( kind == map_geometry_kind_t::MESH ) { ok = MapGeometry_WriteMesh( *geo::GeometryDocument_FindMesh( &m.geometry, { id } ), m.materials, m.pAllocator, d, record ); }
    else if ( kind == map_geometry_kind_t::PATCH ) { ok = MapGeometry_WritePatch( *geo::GeometryDocument_FindPatch( &m.geometry, { id } ), m.materials, d, record ); }
    else if ( kind == map_geometry_kind_t::TERRAIN ) { ok = MapGeometry_WriteTerrain( *geo::GeometryDocument_FindHeightField( &m.geometry, { id } ), d, record ); }
    if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
    if ( const auto *old = MapDocument_FindObject( const_cast<map_document_t *>( &m ), id, nullptr ) ) {
        for ( usize i = 0; i < KeyValue_ChildCount( old ); ++i ) {
            const auto *c = KeyValue_ChildAt( old, i );
            if ( !MapGeometry_IsGeometryMember( kind, KeyValue_Name( c ) ) && !StringView_Equals( KeyValue_Name( c ), SV( "id" ) ) &&
                !KeyValue_CloneInto( d, record, c ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        if ( !MapGeometry_MergeResidual( kind, d, record, old ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    return map_status_t::OK;
}
const char *KindText( map_geometry_kind_t kind ) noexcept { return kind == map_geometry_kind_t::COUNT ? "entity" : MapGeometry_SectionName( kind ); }
bool ParseKind( string_view_t s, map_geometry_kind_t &kind ) noexcept
{
    for ( const auto k : { map_geometry_kind_t::BRUSH, map_geometry_kind_t::MESH, map_geometry_kind_t::PATCH, map_geometry_kind_t::TERRAIN, map_geometry_kind_t::COUNT } ) {
        if ( StringView_Equals( s, SV( KindText( k ) ) ) ) { kind = k; return true; }
    }
    return false;
}
bool LayerValid( string_view_t s ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckStableIdentifier( s, MAP_LAYER_ID_CAPACITY - 1 ) );
}
struct entry_t { map_geometry_kind_t kind; key_value_t *record; string_view_t layer; u64 oldId, owner, newId{ 0 }; };
struct bundle_t { document_ptr_t document{}; vector_t<entry_t> entries{}; vector_t<u64> roots{}; map_bounds_t bounds{}; };
entry_t *Find( bundle_t &b, u64 id ) noexcept
{
    for ( usize i = 0; i < b.entries.nCount; ++i ) { if ( b.entries.pData[i].oldId == id ) { return &b.entries.pData[i]; } } return nullptr;
}
map_status_t Parse( const allocator_t *a, string_view_t text, bundle_t &b ) noexcept
{
    if ( !Allocator_IsValid( a ) || !text.pData || text.cchLength == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( text.cchLength > MAP_CLIPBOARD_TEXT_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    b.document = Document( a );
    if ( !b.document || !Vector_Init( &b.entries, a ) || !Vector_Init( &b.roots, a ) ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_parse_options_t options{}; options.cbMaxInput = options.cbMaxStringData = MAP_CLIPBOARD_TEXT_MAX; options.nMaxNodes = MAP_CHUNK_VALUES_MAX;
    const auto parsed = KeyValue_ParseText( text, options, b.document.get() );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        if ( parsed.status == key_value_parse_status_t::OUT_OF_MEMORY ) { return map_status_t::OUT_OF_MEMORY; }
        if ( parsed.status == key_value_parse_status_t::INPUT_LIMIT || parsed.status == key_value_parse_status_t::NODE_LIMIT || parsed.status == key_value_parse_status_t::STRING_LIMIT ) { return map_status_t::LIMIT_EXCEEDED; }
        return map_status_t::ROOT_UNREADABLE;
    }
    const auto header = KeyValue_DocumentHeader( b.document.get() );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION_1 || !StringView_Equals( header.schemaId, SV( MAP_CLIPBOARD_SCHEMA_ID ) ) || header.nSchemaVersion != MAP_CLIPBOARD_SCHEMA_VERSION ) { return map_status_t::ROOT_UNREADABLE; }
    auto *root = KeyValue_Root( b.document.get() );
    auto *records = KeyValue_Find( root, SV( "objects" ) ); auto *roots = KeyValue_Find( root, SV( "selection" ) ); auto *bounds = KeyValue_Find( root, SV( "bounds" ) );
    if ( KeyValue_Type( root ) != key_value_type_t::OBJECT || KeyValue_ChildCount( root ) != 3 ||
        KeyValue_Type( records ) != key_value_type_t::ARRAY || KeyValue_ChildCount( records ) == 0 || KeyValue_ChildCount( records ) > MAP_CHUNK_OBJECTS_MAX ||
        KeyValue_Type( roots ) != key_value_type_t::ARRAY || KeyValue_ChildCount( roots ) == 0 || KeyValue_ChildCount( roots ) > MAP_CHUNK_OBJECTS_MAX ||
        KeyValue_Type( bounds ) != key_value_type_t::ARRAY || KeyValue_ChildCount( bounds ) != 2 ||
        !ReadVec( KeyValue_ChildAt( bounds, 0 ), b.bounds.box.minimum ) || !ReadVec( KeyValue_ChildAt( bounds, 1 ), b.bounds.box.maximum ) ) { return map_status_t::ROOT_INVALID; }
    const auto &box = b.bounds.box;
    if ( box.minimum.x > box.maximum.x || box.minimum.y > box.maximum.y || box.minimum.z > box.maximum.z ) { return map_status_t::ROOT_INVALID; }
    b.bounds.bHas = CY_TRUE;
    vector_t<u64> allIds{}; if ( !Vector_Init( &allIds, a ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < KeyValue_ChildCount( records ); ++i ) {
        auto *item = KeyValue_ChildAt( records, i ); auto *record = KeyValue_Find( item, SV( "record" ) );
        string_view_t kindText{}, layer{}; u64 id = 0, owner = 0; map_geometry_kind_t kind{};
        if ( KeyValue_Type( item ) != key_value_type_t::OBJECT || KeyValue_ChildCount( item ) != 4 || KeyValue_Type( record ) != key_value_type_t::OBJECT ||
            !KeyValue_GetString( KeyValue_Find( item, SV( "kind" ) ), &kindText ) || !ParseKind( kindText, kind ) ||
            !KeyValue_GetString( KeyValue_Find( item, SV( "layer" ) ), &layer ) || !LayerValid( layer ) ||
            !KeyValue_GetU64( KeyValue_Find( item, SV( "owner" ) ), &owner ) || !KeyValue_GetU64( KeyValue_Find( record, SV( "id" ) ), &id ) || id == 0 ) { return map_status_t::ROOT_INVALID; }
        if ( kind == map_geometry_kind_t::COUNT ) {
            string_view_t cls{};
            if ( owner != 0 || !KeyValue_GetString( KeyValue_Find( record, SV( "class" ) ), &cls ) || cls.cchLength == 0 || cls.cchLength > MAP_NAME_MAX_LENGTH ) { return map_status_t::ROOT_INVALID; }
            if ( Unicode_ValidateUtf8( cls ).status != unicode_status_t::OK ) { return map_status_t::ROOT_INVALID; }
            for ( usize k = 0; k < cls.cchLength; ++k ) { if ( cls.pData[k] == '\0' ) { return map_status_t::ROOT_INVALID; } }
            for ( const char *s : { "brushes", "meshes", "patches", "terrains" } ) { if ( KeyValue_Find( record, SV( s ) ) ) { return map_status_t::ROOT_INVALID; } }
            if ( const auto *origin = KeyValue_Find( record, SV( "origin" ) ) ) { math::vec3d_t v{}; if ( !ReadVec( origin, v ) ) { return map_status_t::ROOT_INVALID; } }
            if ( !Vector_PushBack( &allIds, id ) ) { return map_status_t::OUT_OF_MEMORY; }
        } else {
            // Canonical clipboard geometry must carry every identity already.
            u64 scratchNext = 1; const usize assigned = MapGeometry_AssignIds( kind, b.document.get(), record, &scratchNext );
            if ( assigned == CY_INVALID_SIZE ) { return map_status_t::OUT_OF_MEMORY; }
            if ( assigned != 0 || ( kind == map_geometry_kind_t::TERRAIN && owner != 0 ) ) { return map_status_t::ROOT_INVALID; }
            if ( !MapGeometry_CollectIds( kind, record, allIds ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
        if ( !Vector_PushBack( &b.entries, entry_t{ kind, record, layer, id, owner } ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    std::sort( allIds.pData, allIds.pData + allIds.nCount );
    for ( usize i = 1; i < allIds.nCount; ++i ) { if ( allIds.pData[i] == allIds.pData[i - 1] ) { return map_status_t::ROOT_INVALID; } }
    for ( usize i = 0; i < KeyValue_ChildCount( roots ); ++i ) {
        u64 id = 0;
        if ( !KeyValue_GetU64( KeyValue_ChildAt( roots, i ), &id ) || !Find( b, id ) || Contains( b.roots, id ) ) { return map_status_t::ROOT_INVALID; }
        if ( !Vector_PushBack( &b.roots, id ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    for ( usize i = 0; i < b.entries.nCount; ++i ) {
        const auto &e = b.entries.pData[i]; const auto *owner = e.owner ? Find( b, e.owner ) : nullptr;
        if ( e.owner && ( !owner || owner->kind != map_geometry_kind_t::COUNT || !Contains( b.roots, owner->oldId ) ) ) { return map_status_t::ROOT_INVALID; }
        if ( !Contains( b.roots, e.oldId ) && !e.owner ) { return map_status_t::ROOT_INVALID; }
    }
    return map_status_t::OK;
}
map_status_t Save( key_value_document_t *d, text_buffer_t *out ) noexcept
{
    key_value_write_options_t options{}; options.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS; options.nLineWidth = MAP_LINE_WIDTH;
    const auto measured = KeyValue_WriteText( KeyValue_Root( d ), options, nullptr, 0 );
    if ( measured.status == key_value_write_status_t::OUT_OF_MEMORY ) { return map_status_t::OUT_OF_MEMORY; }
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED && measured.status != key_value_write_status_t::OK ) { return map_status_t::ROOT_INVALID; }
    if ( measured.cchRequired > MAP_CLIPBOARD_TEXT_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    text_buffer_t staged{};
    if ( !TextBuffer_Init( &staged, out->pAllocator, measured.cchRequired ) || !TextBuffer_Resize( &staged, measured.cchRequired ) ) { return map_status_t::OUT_OF_MEMORY; }
    // TextBuffer stores usable character capacity; its allocation also owns the
    // following terminator byte that the bounded writer includes in its size.
    const auto written = KeyValue_WriteText( KeyValue_Root( d ), options, TextBuffer_Data( &staged ), TextBuffer_Capacity( &staged ) + 1 );
    if ( written.status == key_value_write_status_t::OUT_OF_MEMORY ) { return map_status_t::OUT_OF_MEMORY; }
    if ( written.status != key_value_write_status_t::OK || written.cchWritten != measured.cchRequired ) { return map_status_t::ROOT_INVALID; }
    // Copy/Cut must never publish text that this build cannot paste back. In
    // particular, retained malformed entities must not become a successful cut.
    bundle_t validated{};
    const auto validation = Parse( out->pAllocator, TextBuffer_View( &staged ), validated );
    if ( validation != map_status_t::OK ) { return validation; }
    std::swap( staged.pData, out->pData ); std::swap( staged.cchLength, out->cchLength ); std::swap( staged.cchCapacity, out->cchCapacity );
    return map_status_t::OK;
}
map_status_t CheckFresh( const map_document_t &m ) noexcept
{
    return m.nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &m.geometry.sourceIds.allocator ) ||
        m.nextId == CY_U64_MAX || m.geometry.sourceIds.allocator.next.value == CY_U64_MAX ? map_status_t::LIMIT_EXCEEDED : map_status_t::OK;
}
string_view_t DestinationLayer( const map_document_t &m, string_view_t layer ) noexcept
{
    return MapDocument_HasLayer( &m, layer ) ? layer : SV( m.layers.pData[0].id );
}
bool ReplaceEntity( map_chunk_t &chunk, key_value_t *dst, const key_value_t *src, u64 id ) noexcept
{
    auto *d = chunk.store.pDocument;
    while ( KeyValue_ChildCount( dst ) ) { if ( !KeyValue_Remove( d, dst, KeyValue_ChildAt( dst, 0 ) ) ) { return false; } }
    for ( usize i = 0; i < KeyValue_ChildCount( src ); ++i ) {
        const auto *c = KeyValue_ChildAt( src, i );
        if ( !StringView_Equals( KeyValue_Name( c ), SV( "id" ) ) && !KeyValue_CloneInto( d, dst, c ) ) { return false; }
    }
    return WriteId( d, dst, "id", id );
}
}

map_status_t MapClipboard_WriteSelection( const map_document_t *source, span_t<const u64> ids, text_buffer_t *out ) noexcept
{
    if ( !source || !Allocator_IsValid( source->pAllocator ) || !Span_IsValid( ids ) || ids.nCount == 0 ||
        !TextBuffer_IsValid( out ) || !Allocator_IsValid( out->pAllocator ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( ids.nCount > MAP_CHUNK_OBJECTS_MAX || source->layers.nCount == 0 ) { return map_status_t::LIMIT_EXCEEDED; }
    vector_t<u64> roots{}, expanded{};
    if ( !Vector_Init( &roots, source->pAllocator, ids.nCount ) || !Vector_Init( &expanded, source->pAllocator, ids.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        if ( !id || ( Kind( *source, id ) == map_geometry_kind_t::COUNT && !IsEntity( *source, id ) ) ) { return map_status_t::UNKNOWN_OBJECT; }
        if ( !AddUnique( roots, id ) || !AddUnique( expanded, id ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize k = 0; k < source->geometryRecords.nCount; ++k ) {
            const auto &p = source->geometryRecords.pData[k];
            if ( p.owner == id && !AddUnique( expanded, p.id ) ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    if ( expanded.nCount > MAP_CHUNK_OBJECTS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    map_wireframe_t wire{};
    if ( !MapWireframe_Init( &wire, source->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto status = MapWireframe_Build( &wire, *source ); if ( status != map_status_t::OK ) { return status; }
    map_bounds_t bounds{};
    for ( usize i = 0; i < wire.objects.nCount; ++i ) { const auto &o = wire.objects.pData[i]; if ( Contains( expanded, o.id ) ) { MapBounds_AddBounds( bounds, o.bounds ); } }
    // Logic entities can legitimately have no spatial origin or owned geometry.
    // Their neutral placement envelope must not invent an authored origin.
    if ( !bounds.bHas ) { bounds = { {}, CY_TRUE }; }
    if ( !Finite( bounds.box.minimum ) || !Finite( bounds.box.maximum ) ) { return map_status_t::GEOMETRY_FAILED; }
    auto d = Document( source->pAllocator );
    if ( !d || !KeyValue_SetRootType( d.get(), key_value_type_t::OBJECT ) ||
        !KeyValue_SetDocumentHeader( d.get(), { CYKV_LANGUAGE_VERSION_1, SV( MAP_CLIPBOARD_SCHEMA_ID ), MAP_CLIPBOARD_SCHEMA_VERSION } ) ) { return map_status_t::OUT_OF_MEMORY; }
    auto *root = KeyValue_Root( d.get() ); auto *selection = KeyValue_ObjectInsert( d.get(), root, SV( "selection" ), key_value_type_t::ARRAY );
    auto *boundsArray = KeyValue_ObjectInsert( d.get(), root, SV( "bounds" ), key_value_type_t::ARRAY ); auto *objects = KeyValue_ObjectInsert( d.get(), root, SV( "objects" ), key_value_type_t::ARRAY );
    if ( !selection || !boundsArray || !objects || !WriteVec( d.get(), boundsArray, bounds.box.minimum ) || !WriteVec( d.get(), boundsArray, bounds.box.maximum ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < roots.nCount; ++i ) { auto *n = KeyValue_ArrayAppend( d.get(), selection, key_value_type_t::U64 ); if ( !n || !KeyValue_SetU64( d.get(), n, roots.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; } }
    for ( usize i = 0; i < expanded.nCount; ++i ) {
        const u64 id = expanded.pData[i]; const auto kind = Kind( *source, id ); map_chunk_t *chunk = nullptr;
        const auto *old = MapDocument_FindObject( const_cast<map_document_t *>( source ), id, &chunk ); const auto *placement = Placement( *source, id );
        if ( placement && placement->iLayer >= source->layers.nCount ) { return map_status_t::UNKNOWN_LAYER; }
        const auto layer = kind == map_geometry_kind_t::COUNT && chunk ? SV( chunk->layer ) : SV( source->layers.pData[placement ? placement->iLayer : 0].id );
        const u64 owner = placement && Contains( roots, placement->owner ) ? placement->owner : 0;
        auto *item = KeyValue_ArrayAppend( d.get(), objects, key_value_type_t::OBJECT );
        if ( !item || !WriteString( d.get(), item, "kind", SV( KindText( kind ) ) ) || !WriteString( d.get(), item, "layer", layer ) || !WriteId( d.get(), item, "owner", owner ) ) { return map_status_t::OUT_OF_MEMORY; }
        auto *record = KeyValue_ObjectInsert( d.get(), item, SV( "record" ), key_value_type_t::OBJECT ); if ( !record ) { return map_status_t::OUT_OF_MEMORY; }
        if ( kind == map_geometry_kind_t::COUNT ) {
            if ( !old || !EntityChildrenKnown( *source, old, id ) ) { return map_status_t::INVALID_ARGUMENT; }
            if ( const auto *origin = KeyValue_Find( old, SV( "origin" ) ) ) {
                math::vec3d_t v{}; const f64 limit = source->geometryPolicy.numerical.fCoordinateMagnitudeLimit;
                if ( !ReadVec( origin, v ) || std::abs( v.x ) > limit || std::abs( v.y ) > limit || std::abs( v.z ) > limit ) { return map_status_t::INVALID_ARGUMENT; }
            }
            for ( usize k = 0; k < KeyValue_ChildCount( old ); ++k ) {
                const auto *c = KeyValue_ChildAt( old, k ); const auto name = KeyValue_Name( c );
                if ( StringView_Equals( name, SV( "id" ) ) ) { continue; }
                if ( StringView_Equals( name, SV( "brushes" ) ) || StringView_Equals( name, SV( "meshes" ) ) || StringView_Equals( name, SV( "patches" ) ) ) { continue; }
                if ( !KeyValue_CloneInto( d.get(), record, c ) ) { return map_status_t::OUT_OF_MEMORY; }
            }
            if ( !WriteId( d.get(), record, "id", id ) ) { return map_status_t::OUT_OF_MEMORY; }
        } else { status = WriteGeometry( *source, id, kind, d.get(), record ); if ( status != map_status_t::OK ) { return status; } }
    }
    return Save( d.get(), out );
}

map_status_t MapClipboard_ReadBounds( const allocator_t *a, string_view_t text, map_bounds_t *out ) noexcept
{
    if ( !out ) { return map_status_t::INVALID_ARGUMENT; }
    bundle_t b{}; const auto s = Parse( a, text, b ); if ( s == map_status_t::OK ) { *out = b.bounds; } return s;
}

map_status_t MapClipboard_Paste( map_document_t *m, string_view_t text, math::vec3d_t offset, vector_t<u64> *out ) noexcept
{
    if ( !m || !Allocator_IsValid( m->pAllocator ) || !Vector_IsValid( out ) || !Allocator_IsValid( out->pAllocator ) || !Finite( offset ) || m->layers.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( m->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( m->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    bundle_t b{}; auto status = Parse( m->pAllocator, text, b ); if ( status != map_status_t::OK ) { return status; }
    vector_t<u64> staged{}, normalized{};
    if ( !Vector_Init( &staged, out->pAllocator, b.roots.nCount ) || !Vector_Init( &normalized, m->pAllocator, b.entries.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    const u64 revision = m->geometry.revision;
    // Owners precede children even when the selected root order is reversed.
    for ( usize i = 0; i < b.entries.nCount; ++i ) {
        auto &e = b.entries.pData[i]; if ( e.kind != map_geometry_kind_t::COUNT ) { continue; }
        status = CheckFresh( *m ); if ( status != map_status_t::OK ) { return status; }
        string_view_t cls{}; ( void )KeyValue_GetString( KeyValue_Find( e.record, SV( "class" ) ), &cls );
        math::vec3d_t origin{}; if ( auto *n = KeyValue_Find( e.record, SV( "origin" ) ) ) { ( void )ReadVec( n, origin ); }
        if ( KeyValue_Find( e.record, SV( "origin" ) ) ) {
            const f64 limit = m->geometryPolicy.numerical.fCoordinateMagnitudeLimit;
            const math::vec3d_t placed{ origin.x + offset.x, origin.y + offset.y, origin.z + offset.z };
            if ( !Finite( placed ) || std::abs( origin.x ) > limit || std::abs( origin.y ) > limit || std::abs( origin.z ) > limit ||
                 std::abs( placed.x ) > limit || std::abs( placed.y ) > limit || std::abs( placed.z ) > limit ) { return map_status_t::INVALID_ARGUMENT; }
        }
        status = MapDocument_AddEntity( m, DestinationLayer( *m, e.layer ), cls, origin, &e.newId ); if ( status != map_status_t::OK ) { return status; }
        map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( m, e.newId, &chunk );
        if ( !record || !chunk || !ReplaceEntity( *chunk, record, e.record, e.newId ) ) { return map_status_t::OUT_OF_MEMORY; }
        // Hand-authored integer origins are valid map input. Canonicalize the
        // known position field so the existing transform path sees real values.
        if ( auto *n = KeyValue_Find( record, SV( "origin" ) ) ) {
            const f64 components[]{ origin.x, origin.y, origin.z };
            for ( usize k = 0; k < 3; ++k ) { if ( !KeyValue_SetF64( chunk->store.pDocument, KeyValue_ChildAt( n, k ), components[k] ) ) { return map_status_t::OUT_OF_MEMORY; } }
        }
    }
    for ( usize i = 0; i < b.entries.nCount; ++i ) {
        auto &e = b.entries.pData[i]; if ( e.kind == map_geometry_kind_t::COUNT ) { continue; }
        status = CheckFresh( *m ); if ( status != map_status_t::OK ) { return status; }
        struct remap_t { key_value_document_t *document; geo::geometry_source_id_allocator_t allocator; u64 oldRoot, newRoot{ 0 }; bool exhausted{ false }; } remap{
            b.document.get(), { { std::max( m->nextId, m->geometry.sourceIds.allocator.next.value ) } }, e.oldId };
        if ( !MapGeometry_ForEachId( e.kind, e.record, []( void *context, key_value_t *slot, u64 old ) noexcept -> bool_t {
            auto &r = *static_cast<remap_t *>( context ); const auto id = geo::GeometrySourceIdAllocator_Allocate( &r.allocator );
            if ( id.status != geo::geometry_status_t::OK ) { r.exhausted = true; return CY_FALSE; }
            if ( old == r.oldRoot ) { r.newRoot = id.id.value; }
            return KeyValue_SetU64( r.document, slot, id.id.value );
        }, &remap ) ) { return remap.exhausted ? map_status_t::LIMIT_EXCEEDED : map_status_t::OUT_OF_MEMORY; }
        e.newId = remap.newRoot;
        const auto read = MapGeometry_Read( e.kind, e.record, &m->materials, &m->geometry, m->geometryPolicy );
        if ( read.status != map_geometry_read_status_t::OK ) { return read.status == map_geometry_read_status_t::OUT_OF_MEMORY ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED; }
        m->nextId = remap.allocator.next.value;
        status = MapDocument_SetGeometryLayer( m, e.newId, DestinationLayer( *m, e.layer ) );
        if ( status == map_status_t::OK && e.owner ) { status = MapDocument_SetGeometryOwner( m, e.newId, Find( b, e.owner )->newId ); }
        if ( status != map_status_t::OK ) { return status; }
        if ( !MapGeometry_StripRecord( e.kind, b.document.get(), e.record ) ) { return map_status_t::OUT_OF_MEMORY; }
        status = MapDocument_InsertGeometryRecord( m, e.kind, e.record ); if ( status != map_status_t::OK ) { return status; }
    }
    for ( usize i = 0; i < b.entries.nCount; ++i ) {
        const auto &e = b.entries.pData[i];
        // Origin-less brush entities are containers, not movable helper points.
        // Translate their children directly. Origin-bearing owners expand their
        // children through MapEdit_Translate, so those children are omitted here.
        const auto *owner = e.owner ? Find( b, e.owner ) : nullptr;
        const bool originOwner = owner && KeyValue_Find( owner->record, SV( "origin" ) );
        const bool originEntity = e.kind == map_geometry_kind_t::COUNT && KeyValue_Find( e.record, SV( "origin" ) );
        if ( ( originEntity || ( e.kind != map_geometry_kind_t::COUNT && !originOwner ) ) &&
             !Vector_PushBack( &normalized, e.newId ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( normalized.nCount != 0 && ( offset.x != 0 || offset.y != 0 || offset.z != 0 ) ) {
        status = MapEdit_Translate( m, { normalized.pData, normalized.nCount }, offset, true ); if ( status != map_status_t::OK ) { return status; }
    }
    for ( usize i = 0; i < b.roots.nCount; ++i ) { if ( !Vector_PushBack( &staged, Find( b, b.roots.pData[i] )->newId ) ) { return map_status_t::OUT_OF_MEMORY; } }
    m->geometry.revision = revision + 1;
    Vector_Shutdown( out ); Vector_Move( out, &staged );
    return map_status_t::OK;
}
}
