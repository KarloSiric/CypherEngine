//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_EntityOutputs_Tests.cpp
//  Purpose: Contract tests for reading and writing entity outputs, their
//           persistence, and the wireframe's resolution of them.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_EntityOutputs.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMap_Wireframe.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Outputs fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 Entity( const char *className, const char *name, math::vec3d_t at )
    {
        u64 id = 0;
        REQUIRE( MapCreate_Entity( &map, SV( className ), at, {}, &id ) == map_status_t::OK );
        bool changed = false;
        REQUIRE( MapEntityEdit_SetIdentityField( &map, { &id, 1 }, SV( "name" ), SV( name ), &changed ) == map_status_t::OK );
        return id;
    }
};
struct saved_t {
    std::string root;
    map_status_t Save( map_document_t *pMap )
    {
        text_buffer_t text{};
        if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) { return map_status_t::OUT_OF_MEMORY; }
        const map_status_t status = MapDocument_Save( pMap, &text );
        if ( status == map_status_t::OK ) { root.assign( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); }
        return status;
    }
};
} // namespace

TEST_CASE( "Entity outputs survive save and reload with all six members", "[map][entity-outputs]" )
{
    fixture_t f;
    const u64 start = f.Entity( "logic_wave_start", "wave_start", { 0, 0, 0 } );
    ( void )f.Entity( "logic_spawner", "wave1", { 64, 0, 0 } );
    bool changed = false;
    REQUIRE( MapEntityOutputs_Write( &f.map, start, { { "on_started", "wave1", "start", "fast", 0.5, 3 } }, &changed ) == map_status_t::OK );
    saved_t saved;
    REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { saved.root.data(), saved.root.size() } ) == map_status_t::OK );
    std::vector<map_output_t> read;
    REQUIRE( MapEntityOutputs_Read( &loaded, start, read ) == map_status_t::OK );
    REQUIRE( read.size() == 1 );
    CHECK( read[0].output == "on_started" ); CHECK( read[0].target == "wave1" ); CHECK( read[0].input == "start" );
    CHECK( read[0].parameter == "fast" ); CHECK( read[0].delay == 0.5 ); CHECK( read[0].times == 3 );
}

TEST_CASE( "Entity outputs write in firing order, round-trip, and resolve in the wireframe", "[map][entity-outputs]" )
{
    fixture_t f;
    const u64 trigger = f.Entity( "trigger_once", "door_trigger", { 0, 0, 0 } );
    const u64 door = f.Entity( "func_door", "door1", { 128, 0, 0 } );
    std::vector<map_output_t> outputs;
    REQUIRE( MapEntityOutputs_Read( &f.map, trigger, outputs ) == map_status_t::OK );
    CHECK( outputs.empty() );
    outputs.push_back( { "on_trigger", "door1", "open", "", 0.0, -1 } );
    outputs.push_back( { "on_trigger", "door*", "lock", "", 2.5, 1 } );
    outputs.push_back( { "on_trigger", "nobody", "open", "", 0.0, -1 } );
    bool changed = false;
    REQUIRE( MapEntityOutputs_Write( &f.map, trigger, outputs, &changed ) == map_status_t::OK );
    CHECK( changed );
    REQUIRE( MapEntityOutputs_Write( &f.map, trigger, outputs, &changed ) == map_status_t::OK );
    CHECK_FALSE( changed ); // Same list: no change.

    std::vector<map_output_t> read;
    REQUIRE( MapEntityOutputs_Read( &f.map, trigger, read ) == map_status_t::OK );
    REQUIRE( read.size() == 3 );
    CHECK( read[1].target == "door*" ); CHECK( read[1].delay == 2.5 ); CHECK( read[1].times == 1 );

    // The wireframe resolves them: two reach the door, one is missing.
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, f.map ) == map_status_t::OK );
    usize toDoor = 0, missing = 0;
    for ( usize i = 0; i < wire.connections.nCount; ++i ) {
        const auto &c = wire.connections.pData[i];
        if ( c.sourceId != trigger ) { continue; }
        toDoor += c.targetId == door; missing += c.status == map_wire_connection_status_t::MISSING_TARGET;
    }
    CHECK( toDoor == 2 ); CHECK( missing == 1 );
    MapWireframe_Shutdown( &wire );

    // Reorder and remove are one write each.
    std::swap( read[0], read[2] );
    read.pop_back();
    REQUIRE( MapEntityOutputs_Write( &f.map, trigger, read, &changed ) == map_status_t::OK );
    REQUIRE( MapEntityOutputs_Read( &f.map, trigger, outputs ) == map_status_t::OK );
    REQUIRE( outputs.size() == 2 ); CHECK( outputs[0].target == "nobody" );
    REQUIRE( MapEntityOutputs_Write( &f.map, trigger, {}, &changed ) == map_status_t::OK );
    CHECK( MapEntityEdit_FindEntity( &f.map, trigger ) != nullptr );
    REQUIRE( MapEntityOutputs_Read( &f.map, trigger, outputs ) == map_status_t::OK );
    CHECK( outputs.empty() );

    // Rejections leave the record alone.
    CHECK( MapEntityOutputs_Write( &f.map, trigger, { { "on_trigger", "door1", "open", "", -1.0, -1 } }, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapEntityOutputs_Write( &f.map, trigger, { { "on_trigger", "door1", "open", "", 0.0, 0 } }, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapEntityOutputs_Write( &f.map, 999999, { { "on_trigger", "door1", "open", "", 0.0, -1 } }, &changed ) == map_status_t::UNKNOWN_OBJECT );
    std::vector<map_output_t> many( MAP_ENTITY_OUTPUTS_MAX + 1, map_output_t{ "a", "b", "c", "", 0.0, -1 } );
    CHECK( MapEntityOutputs_Write( &f.map, trigger, many, &changed ) == map_status_t::LIMIT_EXCEEDED );
}
