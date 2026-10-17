//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Hotspots.cpp
//  Purpose: Implements reading and writing hotspot rectangle files.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Hotspots.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"

#include <cmath>
#include <memory>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;

namespace
{
string_view_t SV( const char *text ) noexcept { return StringView_FromCString( text ); }
struct document_delete_t { void operator()( key_value_document_t *d ) const noexcept { KeyValue_DestroyDocument( d ); } };
using document_ptr_t = std::unique_ptr<key_value_document_t, document_delete_t>;

document_ptr_t Document() noexcept
{
    key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
    return document_ptr_t( KeyValue_CreateDocument( desc ) );
}

// A number of any CYKV numeric type.
bool Number( const key_value_t *value, f64 &out ) noexcept
{
    i64 i = 0; u64 u = 0;
    if ( KeyValue_GetF64( value, &out ) ) { return std::isfinite( out ); }
    if ( KeyValue_GetI64( value, &i ) ) { out = static_cast<f64>( i ); return true; }
    if ( KeyValue_GetU64( value, &u ) ) { out = static_cast<f64>( u ); return true; }
    return false;
}
bool Numbers( const key_value_t *array, f64 *out, usize count ) noexcept
{
    if ( KeyValue_Type( array ) != key_value_type_t::ARRAY || KeyValue_ChildCount( array ) != count ) { return false; }
    for ( usize i = 0; i < count; ++i ) { if ( !Number( KeyValue_ChildAt( array, i ), out[i] ) ) { return false; } }
    return true;
}
bool Flag( const key_value_t *record, const char *name, bool fallback ) noexcept
{
    bool_t value = fallback ? CY_TRUE : CY_FALSE;
    const key_value_t *node = KeyValue_Find( record, SV( name ) );
    return node != nullptr && KeyValue_GetBool( node, &value ) ? value != CY_FALSE : fallback;
}
bool AppendNumbers( key_value_document_t *d, key_value_t *record, const char *name, std::initializer_list<f64> values ) noexcept
{
    key_value_t *array = KeyValue_ObjectInsert( d, record, SV( name ), key_value_type_t::ARRAY );
    if ( array == nullptr ) { return false; }
    for ( f64 v : values ) {
        key_value_t *item = KeyValue_ArrayAppend( d, array, key_value_type_t::F64 );
        if ( item == nullptr || !KeyValue_SetF64( d, item, v ) ) { return false; }
    }
    return true;
}
bool AppendFlag( key_value_document_t *d, key_value_t *record, const char *name, bool value ) noexcept
{
    key_value_t *node = KeyValue_ObjectInsert( d, record, SV( name ), key_value_type_t::BOOL );
    return node != nullptr && KeyValue_SetBool( d, node, value ? CY_TRUE : CY_FALSE );
}
} // namespace

std::string MapHotspots_SidecarPath( string_view_t materialPath )
{
    std::string path( materialPath.pData != nullptr ? materialPath.pData : "", materialPath.cchLength );
    const usize slash = path.find_last_of( "/\\" ), dot = path.find_last_of( '.' );
    if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) ) { path.resize( dot ); }
    return path + ".cyrect";
}

map_status_t MapHotspots_Parse( string_view_t text, std::vector<geo::mesh_hotspot_rect_t> &out )
{
    out.clear();
    auto document = Document();
    if ( document == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_parse_options_t options{}; options.cbMaxInput = options.cbMaxStringData = MAP_HOTSPOTS_TEXT_MAX; options.nMaxNodes = 65536u;
    const auto parsed = KeyValue_ParseText( text, options, document.get() );
    if ( parsed.status == key_value_parse_status_t::OUT_OF_MEMORY ) { return map_status_t::OUT_OF_MEMORY; }
    if ( parsed.status != key_value_parse_status_t::OK ) { return map_status_t::ROOT_UNREADABLE; }
    const auto header = KeyValue_DocumentHeader( document.get() );
    if ( !StringView_Equals( header.schemaId, SV( MAP_HOTSPOTS_SCHEMA_ID ) ) || header.nSchemaVersion != MAP_HOTSPOTS_SCHEMA_VERSION ) { return map_status_t::ROOT_UNREADABLE; }
    const key_value_t *rects = KeyValue_Find( KeyValue_Root( document.get() ), SV( "rects" ) );
    if ( KeyValue_Type( rects ) != key_value_type_t::ARRAY || KeyValue_ChildCount( rects ) > geo::kMeshHotspotRectsMax ) { return map_status_t::ROOT_INVALID; }
    for ( usize i = 0; i < KeyValue_ChildCount( rects ); ++i ) {
        const key_value_t *record = KeyValue_ChildAt( rects, i );
        f64 uv[4]{}, size[2]{};
        if ( !Numbers( KeyValue_Find( record, SV( "uv" ) ), uv, 4 ) || !Numbers( KeyValue_Find( record, SV( "size" ) ), size, 2 ) ) { out.clear(); return map_status_t::ROOT_INVALID; }
        if ( uv[0] < 0.0 || uv[1] < 0.0 || uv[2] > 1.0 || uv[3] > 1.0 || uv[2] <= uv[0] || uv[3] <= uv[1] || size[0] <= 0.0 || size[1] <= 0.0 ) { out.clear(); return map_status_t::ROOT_INVALID; }
        geo::mesh_hotspot_rect_t rect{};
        rect.uvMin = { uv[0], uv[1] }; rect.uvMax = { uv[2], uv[3] }; rect.worldSize = { size[0], size[1] };
        rect.bAllowRotation = Flag( record, "rotate", true ); rect.bTileU = Flag( record, "tile_u", false ); rect.bTileV = Flag( record, "tile_v", false );
        out.push_back( rect );
    }
    return map_status_t::OK;
}

map_status_t MapHotspots_Write( const std::vector<geo::mesh_hotspot_rect_t> &rects, text_buffer_t *out )
{
    if ( out == nullptr || !TextBuffer_IsValid( out ) || rects.size() > geo::kMeshHotspotRectsMax ) { return map_status_t::INVALID_ARGUMENT; }
    auto document = Document();
    if ( document == nullptr || !KeyValue_SetRootType( document.get(), key_value_type_t::OBJECT ) ||
         !KeyValue_SetDocumentHeader( document.get(), { CYKV_LANGUAGE_VERSION_1, SV( MAP_HOTSPOTS_SCHEMA_ID ), MAP_HOTSPOTS_SCHEMA_VERSION } ) ) { return map_status_t::OUT_OF_MEMORY; }
    key_value_t *array = KeyValue_ObjectInsert( document.get(), KeyValue_Root( document.get() ), SV( "rects" ), key_value_type_t::ARRAY );
    if ( array == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    for ( const auto &rect : rects ) {
        key_value_t *record = KeyValue_ArrayAppend( document.get(), array, key_value_type_t::OBJECT );
        if ( record == nullptr || !AppendNumbers( document.get(), record, "uv", { rect.uvMin.x, rect.uvMin.y, rect.uvMax.x, rect.uvMax.y } ) ||
             !AppendNumbers( document.get(), record, "size", { rect.worldSize.x, rect.worldSize.y } ) || !AppendFlag( document.get(), record, "rotate", rect.bAllowRotation ) ||
             !AppendFlag( document.get(), record, "tile_u", rect.bTileU ) || !AppendFlag( document.get(), record, "tile_v", rect.bTileV ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    key_value_write_options_t options{}; options.flags |= KEY_VALUE_WRITE_FLAG_BARE_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS; options.nLineWidth = 100;
    const auto measured = KeyValue_WriteText( KeyValue_Root( document.get() ), options, nullptr, 0 );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED && measured.status != key_value_write_status_t::OK ) { return map_status_t::ROOT_INVALID; }
    if ( !TextBuffer_Resize( out, measured.cchRequired ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto written = KeyValue_WriteText( KeyValue_Root( document.get() ), options, TextBuffer_Data( out ), TextBuffer_Capacity( out ) + 1 );
    return written.status == key_value_write_status_t::OK ? map_status_t::OK : map_status_t::ROOT_INVALID;
}

} // namespace cypher::editor::map
