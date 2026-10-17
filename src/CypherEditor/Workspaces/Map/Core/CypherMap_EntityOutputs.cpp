//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_EntityOutputs.cpp
//  Purpose: Implements reading and writing entity outputs on the entity's
//           record in its layer.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_EntityOutputs.h"
#include "CypherMap_EntityEdit.h"

#include <cmath>

namespace cypher::editor::map
{
using namespace common;

namespace
{
string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }

std::string Text( const key_value_t *record, const char *member )
{
    string_view_t text{};
    return KeyValue_GetString( KeyValue_Find( record, SV( member ) ), &text ) ? std::string( text.pData, text.cchLength ) : std::string();
}

bool Same( const map_output_t &a, const map_output_t &b ) noexcept
{
    return a.output == b.output && a.target == b.target && a.input == b.input && a.parameter == b.parameter && a.delay == b.delay && a.times == b.times;
}

bool Valid( const map_output_t &o ) noexcept
{
    for ( const std::string *text : { &o.output, &o.target, &o.input, &o.parameter } ) {
        if ( text->size() > MAP_ENTITY_OUTPUT_TEXT_MAX || text->find( '\0' ) != std::string::npos ) { return false; }
    }
    return std::isfinite( o.delay ) && o.delay >= 0.0 && ( o.times == -1 || o.times >= 1 );
}

bool SetText( key_value_document_t *document, key_value_t *record, const char *member, const std::string &value ) noexcept
{
    key_value_t *node = KeyValue_ObjectInsert( document, record, SV( member ), key_value_type_t::STRING );
    return node != nullptr && KeyValue_SetString( document, node, { value.data(), value.size() } );
}
} // namespace

map_status_t MapEntityOutputs_Read( const map_document_t *map, u64 entity, std::vector<map_output_t> &outputsOut )
{
    outputsOut.clear();
    const key_value_t *record = MapEntityEdit_FindEntity( map, entity );
    if ( record == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    const key_value_t *outputs = KeyValue_Find( record, SV( "outputs" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( outputs ); ++i ) {
        const key_value_t *connection = KeyValue_ChildAt( outputs, i );
        if ( KeyValue_Type( connection ) != key_value_type_t::OBJECT ) { continue; }
        map_output_t o{};
        o.output = Text( connection, "output" ); o.target = Text( connection, "target" );
        o.input = Text( connection, "input" ); o.parameter = Text( connection, "parameter" );
        f64 delay = 0.0; i64 times = -1;
        // A whole-second delay may be written as an integer.
        if ( i64 whole = 0; KeyValue_GetF64( KeyValue_Find( connection, SV( "delay" ) ), &delay ) ) { o.delay = delay; }
        else if ( KeyValue_GetI64( KeyValue_Find( connection, SV( "delay" ) ), &whole ) ) { o.delay = static_cast<f64>( whole ); }
        if ( KeyValue_GetI64( KeyValue_Find( connection, SV( "times" ) ), &times ) ) { o.times = times; }
        outputsOut.push_back( std::move( o ) );
    }
    return map_status_t::OK;
}

map_status_t MapEntityOutputs_Write( map_document_t *map, u64 entity, const std::vector<map_output_t> &outputs, bool *pChangedOut )
{
    if ( pChangedOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *pChangedOut = false;
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( outputs.size() > MAP_ENTITY_OUTPUTS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( const map_output_t &o : outputs ) { if ( !Valid( o ) ) { return map_status_t::INVALID_ARGUMENT; } }
    std::vector<map_output_t> current;
    auto result = MapEntityOutputs_Read( map, entity, current );
    if ( result != map_status_t::OK ) { return result; }
    if ( current.size() == outputs.size() ) {
        bool same = true;
        for ( usize i = 0; same && i < outputs.size(); ++i ) { same = Same( current[i], outputs[i] ); }
        if ( same ) { return map_status_t::OK; }
    }
    map_layer_store_t *layerStore = nullptr;
    auto *record = const_cast<key_value_t *>( MapEntityEdit_FindEntity( map, entity, &layerStore ) );
    if ( record == nullptr || layerStore == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    auto *document = layerStore->store.pDocument;
    if ( key_value_t *old = KeyValue_Find( record, SV( "outputs" ) ); old != nullptr && !KeyValue_Remove( document, record, old ) ) { return map_status_t::OUT_OF_MEMORY; }
    if ( !outputs.empty() ) {
        key_value_t *array = KeyValue_ObjectInsert( document, record, SV( "outputs" ), key_value_type_t::ARRAY );
        if ( array == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
        for ( const map_output_t &o : outputs ) {
            key_value_t *connection = KeyValue_ArrayAppend( document, array, key_value_type_t::OBJECT );
            key_value_t *delay = nullptr, *times = nullptr;
            const bool ok = connection != nullptr && SetText( document, connection, "output", o.output ) && SetText( document, connection, "target", o.target ) &&
                SetText( document, connection, "input", o.input ) && SetText( document, connection, "parameter", o.parameter ) &&
                ( delay = KeyValue_ObjectInsert( document, connection, SV( "delay" ), key_value_type_t::F64 ) ) != nullptr && KeyValue_SetF64( document, delay, o.delay ) &&
                ( times = KeyValue_ObjectInsert( document, connection, SV( "times" ), key_value_type_t::I64 ) ) != nullptr && KeyValue_SetI64( document, times, o.times );
            if ( !ok ) { return map_status_t::OUT_OF_MEMORY; }
        }
    }
    *pChangedOut = true;
    return map_status_t::OK;
}

} // namespace cypher::editor::map
