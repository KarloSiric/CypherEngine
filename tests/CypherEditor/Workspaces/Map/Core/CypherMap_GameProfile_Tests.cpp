//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_GameProfile_Tests.cpp
//  Purpose: Tests decoding game profiles: classes with their bases folded
//           in, typed defaults, includes, map settings, migrations, value
//           checks, and the tolerant handling of broken definitions.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_GameProfile.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }

struct files_t {
    std::map<std::string, std::string> files;
    map_game_read_fn Reader() const
    {
        return [this]( const std::string &path, std::string &text ) {
            const auto it = files.find( path );
            if ( it == files.end() ) { return false; }
            text = it->second; return true;
        };
    }
};

bool HasProblem( const map_game_profile_t &profile, const char *text )
{
    for ( const auto &problem : profile.problems ) { if ( problem.find( text ) != std::string::npos ) { return true; } }
    return false;
}

std::string Text( const key_value_t *value )
{
    string_view_t text{};
    return KeyValue_GetString( value, &text ) ? std::string( text.pData, text.cchLength ) : std::string();
}

constexpr const char *kReap = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    id = "reap"
    name = "REAP"
    version = 3u
    units_per_meter = 39.37
    includes = [ "game/entities/logic.cygame" ]
    editor = { default_point_class = "info_player_start" default_solid_class = "func_door" categories = [ "Info", "Waves", "Lights", "Doors", "Logic" ] }
    map_settings = [
        { key = "wave_count" type = "integer" default = 10 min = 1 max = 100 label = "Waves" group = "Waves" },
        { key = "difficulty" type = "choice" default = "normal" choices = [ "easy", "normal", "hard", "nightmare" ] }
    ]
    migrations = [
        { version = 3u rename_class = { from = "info_wave" to = "info_wave_start" } },
        { version = 3u rename_property = { class = "info_enemy_spawn" from = "type" to = "enemy" } }
    ]
    classes = [
        {
            name = "info_player_start"
            kind = "point"
            label = "Player Start"
            category = "Info"
            description = "Where a player spawns at the start of the match."
            editor = { size = [ -16, -16, 0, 16, 16, 72 ] color = "#40c040" helpers = [ { type = "arrow" } ] }
            properties = [
                { key = "team" type = "choice" default = "any" choices = [ "any", "red", "blue" ] label = "Team" },
                { key = "priority" type = "integer" default = 0 min = 0 max = 100 label = "Priority" }
            ]
            outputs = [ { name = "on_spawn" description = "A player spawned here." } ]
        },
        {
            name = "info_wave_start"
            kind = "point"
            base = [ "targetable" ]
            category = "Waves"
            properties = [
                { key = "wave" type = "integer" default = 1 min = 1 max = 100 },
                { key = "start_disabled" type = "bool" default = true label = "Starts Off" },
                { key = "announce" type = "asset" default = "" extensions = [ ".cysnd" ] }
            ]
            inputs = [ { name = "start" }, { name = "abort" } ]
            outputs = [ { name = "on_started" }, { name = "on_cleared" } ]
        },
        {
            name = "light"
            kind = "point"
            base = [ "targetable" ]
            category = "Lights"
            editor = { size = [ -8, -8, -8, 8, 8, 8 ] color_from = "color" helpers = [ { type = "radius" property = "range" } ] }
            properties = [
                { key = "color" type = "color" default = "#ffffff" },
                { key = "intensity" type = "real" default = 300.0 min = 0.0 max = 100000.0 unit = "candela" },
                { key = "range" type = "real" default = 512 }
            ]
            inputs = [ { name = "turn_on" }, { name = "turn_off" }, { name = "set_color" type = "string" } ]
        },
        {
            name = "func_door"
            kind = "solid"
            base = [ "targetable" ]
            category = "Doors"
            properties = [
                { key = "move_dir" type = "angles" default = [ 0.0, 90.0, 0.0 ] },
                { key = "flags" type = "flags" default = [ "silent" ] choices = [ "silent", "locked" ] }
            ]
            outputs = [ { name = "on_open" } ]
        }
    ]
}
)cykv";

constexpr const char *kLogic = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    classes = [
        {
            name = "targetable"
            kind = "abstract"
            properties = [ { key = "start_disabled" type = "bool" default = false label = "Start Disabled" group = "Logic" } ]
            inputs = [ { name = "enable" }, { name = "disable" }, { name = "toggle" } ]
        },
        { name = "logic_relay" kind = "point" base = [ "targetable" ] category = "Logic" inputs = [ { name = "trigger" } ] outputs = [ { name = "on_trigger" } ] }
    ]
}
)cykv";
} // namespace

TEST_CASE( "A game profile loads its classes with their bases folded in, typed defaults and includes", "[map][game]" )
{
    files_t f;
    f.files["game/reap.cygame"] = kReap;
    f.files["game/entities/logic.cygame"] = kLogic;
    map_game_profile_t profile;
    REQUIRE( MapGame_Load( "game/reap.cygame", f.Reader(), profile ) == map_status_t::OK );
    INFO( ( profile.problems.empty() ? std::string() : profile.problems.front() ) );
    CHECK( profile.problems.empty() );
    CHECK( profile.id == "reap" ); CHECK( profile.name == "REAP" ); CHECK( profile.version == 3u );
    CHECK( profile.files == std::vector<std::string>{ "game/reap.cygame", "game/entities/logic.cygame" } );
    CHECK( profile.defaultPointClass == "info_player_start" ); CHECK( profile.defaultSolidClass == "func_door" );
    CHECK( profile.categories.size() == 5u );
    REQUIRE( profile.classes.size() == 6u ); // Four here, then the two in the include.

    const auto *start = MapGame_FindClass( profile, SV( "info_player_start" ) );
    REQUIRE( start != nullptr );
    CHECK( start->kind == map_game_kind_t::POINT ); CHECK( start->label == "Player Start" ); CHECK( start->category == "Info" );
    CHECK( start->bSize ); CHECK( start->size[5] == 72.0 ); CHECK( start->color == "#40c040" );
    REQUIRE( start->helpers.size() == 1u ); CHECK( start->helpers[0].type == "arrow" );
    const auto *team = MapGame_FindProperty( *start, SV( "team" ) );
    REQUIRE( team != nullptr ); CHECK( team->type == map_game_type_t::CHOICE ); CHECK( Text( team->pDefault ) == "any" );
    REQUIRE( start->outputs.size() == 1u ); CHECK( start->outputs[0].description == "A player spawned here." );

    // A base defined in an include, after the class: its members come first,
    // and the class's own definition of start_disabled replaces the base's in place.
    const auto *wave = MapGame_FindClass( profile, SV( "info_wave_start" ) );
    REQUIRE( wave != nullptr );
    REQUIRE( wave->properties.size() == 3u );
    CHECK( wave->properties[0].key == "start_disabled" ); CHECK( wave->properties[0].label == "Starts Off" );
    bool_t on = CY_FALSE; CHECK( KeyValue_GetBool( wave->properties[0].pDefault, &on ) ); CHECK( on != CY_FALSE );
    CHECK( wave->properties[1].key == "wave" );
    REQUIRE( wave->inputs.size() == 5u ); CHECK( wave->inputs[0].name == "enable" ); CHECK( wave->inputs[4].name == "abort" );

    // Defaults keep their CYKV types; an integer default is accepted for a real.
    const auto *light = MapGame_FindClass( profile, SV( "light" ) );
    REQUIRE( light != nullptr );
    f64 intensity = 0.0; CHECK( KeyValue_GetF64( MapGame_FindProperty( *light, SV( "intensity" ) )->pDefault, &intensity ) ); CHECK( intensity == 300.0 );
    CHECK( MapGame_FindProperty( *light, SV( "range" ) )->pDefault != nullptr );
    CHECK( light->colorFrom == "color" );
    CHECK( light->inputs.back().type == "string" );
    CHECK( MapGame_FindClass( profile, SV( "targetable" ) )->kind == map_game_kind_t::ABSTRACT );

    REQUIRE( profile.mapSettings.size() == 2u ); CHECK( profile.mapSettings[1].choices.size() == 4u );
    REQUIRE( profile.migrations.size() == 2u );
    CHECK( profile.migrations[0].kind == map_game_migration_kind_t::RENAME_CLASS ); CHECK( profile.migrations[0].to == "info_wave_start" );
    CHECK( profile.migrations[1].className == "info_enemy_spawn" );
}

TEST_CASE( "A game profile skips and reports what it cannot use", "[map][game]" )
{
    files_t f;
    f.files["a.cygame"] = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    id = "a" name = "A" version = 1u
    includes = [ "b.cygame", "missing.cygame" ]
    editor = { default_point_class = "nothing" }
    classes = [
        { name = "Bad Name" kind = "point" },
        { name = "no_kind" },
        { name = "looped" kind = "point" base = [ "loop_b" ] },
        { name = "loop_b" kind = "abstract" base = [ "looped" ] },
        { name = "orphan" kind = "point" base = [ "nowhere" ] properties = [ { key = "x" type = "real" } ] },
        {
            name = "props"
            kind = "point"
            properties = [
                { key = "odd" type = "quaternion" },
                { key = "pick" type = "choice" },
                { key = "speed" type = "real" default = "fast" },
                { key = "level" type = "integer" default = 7 min = 1 max = 5 },
                { key = "tint" type = "color" default = "red" },
                { key = "speed" type = "real" }
            ]
        },
        { name = "twice" kind = "point" label = "First" }
    ]
}
)cykv";
    f.files["b.cygame"] = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    includes = [ "a.cygame" ]
    classes = [ { name = "twice" kind = "point" label = "Second" } ]
}
)cykv";
    map_game_profile_t profile;
    REQUIRE( MapGame_Load( "a.cygame", f.Reader(), profile ) == map_status_t::OK );
    CHECK( HasProblem( profile, "a class without a lower-case name" ) );
    CHECK( HasProblem( profile, "no_kind" ) );
    CHECK( HasProblem( profile, "in a cycle" ) );
    CHECK( HasProblem( profile, "base nowhere is not defined" ) );
    CHECK( HasProblem( profile, "missing.cygame: cannot be read" ) );
    CHECK( HasProblem( profile, "a.cygame in a cycle" ) );
    CHECK( HasProblem( profile, "unknown type \"quaternion\"" ) );
    CHECK( HasProblem( profile, "a choice needs choices" ) );
    CHECK( HasProblem( profile, "props.speed: default is not a number" ) );
    CHECK( HasProblem( profile, "props.level: default is above" ) );
    CHECK( HasProblem( profile, "props.tint: default is not" ) );
    CHECK( HasProblem( profile, "props.speed: defined twice" ) );
    CHECK( HasProblem( profile, "class twice is defined twice" ) );
    CHECK( HasProblem( profile, "editor.default_point_class: nothing is not defined" ) );
    // What resolves stays: the orphan keeps its own property, a bad default
    // leaves the property without one, and the first "twice" wins.
    const auto *orphan = MapGame_FindClass( profile, SV( "orphan" ) );
    REQUIRE( orphan != nullptr ); CHECK( orphan->properties.size() == 1u );
    const auto *props = MapGame_FindClass( profile, SV( "props" ) );
    REQUIRE( props != nullptr );
    REQUIRE( props->properties.size() == 3u ); // speed, level, tint.
    CHECK( props->properties[0].pDefault == nullptr );
    CHECK( MapGame_FindClass( profile, SV( "twice" ) )->label == "First" );
    CHECK( MapGame_FindClass( profile, SV( "no_kind" ) ) == nullptr );
}

TEST_CASE( "A game profile that is not one fails to load", "[map][game]" )
{
    files_t f;
    f.files["notes.cygame"] = "@cykv 1\n@schema \"cypher.map\" 10\n{ }\n";
    f.files["nameless.cygame"] = "@cykv 1\n@schema \"cypher.game\" 1\n{ id = \"x\" }\n";
    f.files["broken.cygame"] = "@cykv 1\n@schema \"cypher.game\" 1\n{ id = \n";
    map_game_profile_t profile;
    CHECK( MapGame_Load( "absent.cygame", f.Reader(), profile ) == map_status_t::ROOT_UNREADABLE );
    CHECK( MapGame_Load( "notes.cygame", f.Reader(), profile ) == map_status_t::ROOT_UNREADABLE );
    CHECK( HasProblem( profile, "not a cypher.game V1 document" ) );
    CHECK( MapGame_Load( "nameless.cygame", f.Reader(), profile ) == map_status_t::ROOT_INVALID );
    CHECK( MapGame_Load( "broken.cygame", f.Reader(), profile ) == map_status_t::ROOT_UNREADABLE );
    CHECK( HasProblem( profile, "broken.cygame:" ) );
}

TEST_CASE( "Property values are checked against their type, choices and limits", "[map][game]" )
{
    key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
    std::unique_ptr<key_value_document_t, map_game_document_delete_t> document( KeyValue_CreateDocument( desc ) );
    key_value_parse_options_t options{};
    REQUIRE( KeyValue_ParseText( SV( "@cykv 1\n@schema \"cypher.test\" 1\n{ word = \"red\" other = \"green\" whole = 7 big = 700 half = 0.5 hex = \"#a0b0c0\" "
                                     "flags = [ \"silent\" ] badflags = [ \"loud\" ] three = [ 1, 2.5, 3 ] two = [ 1, 2 ] id = 5u ids = [ 1u, 2u ] "
                                     "snd = \"sounds/a.cysnd\" png = \"textures/a.png\" }" ), options, document.get() ).status == key_value_parse_status_t::OK );
    const key_value_t *root = KeyValue_Root( document.get() );
    const auto value = [&]( const char *name ) { return KeyValue_Find( root, SV( name ) ); };
    map_game_property_t choice{}; choice.type = map_game_type_t::CHOICE; choice.choices = { "red", "blue" };
    CHECK( MapGame_CheckValue( choice, value( "word" ) ) );
    std::string why;
    CHECK_FALSE( MapGame_CheckValue( choice, value( "other" ), &why ) ); CHECK( why.find( "not one of the choices" ) != std::string::npos );
    map_game_property_t integer{}; integer.type = map_game_type_t::INTEGER; integer.bMax = true; integer.max = 100;
    CHECK( MapGame_CheckValue( integer, value( "whole" ) ) ); CHECK_FALSE( MapGame_CheckValue( integer, value( "big" ) ) );
    CHECK_FALSE( MapGame_CheckValue( integer, value( "half" ) ) );
    map_game_property_t real{}; real.type = map_game_type_t::REAL;
    CHECK( MapGame_CheckValue( real, value( "half" ) ) ); CHECK( MapGame_CheckValue( real, value( "whole" ) ) );
    map_game_property_t color{}; color.type = map_game_type_t::COLOR;
    CHECK( MapGame_CheckValue( color, value( "hex" ) ) ); CHECK_FALSE( MapGame_CheckValue( color, value( "word" ) ) );
    map_game_property_t flags{}; flags.type = map_game_type_t::FLAGS; flags.choices = { "silent", "boss" };
    CHECK( MapGame_CheckValue( flags, value( "flags" ) ) ); CHECK_FALSE( MapGame_CheckValue( flags, value( "badflags" ) ) );
    map_game_property_t vector{}; vector.type = map_game_type_t::VECTOR;
    CHECK( MapGame_CheckValue( vector, value( "three" ) ) ); CHECK_FALSE( MapGame_CheckValue( vector, value( "two" ) ) );
    map_game_property_t object{}; object.type = map_game_type_t::OBJECT;
    CHECK( MapGame_CheckValue( object, value( "id" ) ) ); CHECK_FALSE( MapGame_CheckValue( object, value( "word" ) ) );
    map_game_property_t objects{}; objects.type = map_game_type_t::OBJECTS;
    CHECK( MapGame_CheckValue( objects, value( "ids" ) ) );
    map_game_property_t asset{}; asset.type = map_game_type_t::ASSET; asset.extensions = { ".cysnd" };
    CHECK( MapGame_CheckValue( asset, value( "snd" ) ) ); CHECK_FALSE( MapGame_CheckValue( asset, value( "png" ) ) );
    CHECK_FALSE( MapGame_CheckValue( asset, nullptr, &why ) ); CHECK( why == "is missing" );
}

TEST_CASE( "The REAP game profile in the assets loads without problems", "[map][game]" )
{
    const map_game_read_fn read = []( const std::string &path, std::string &text ) {
        std::ifstream in( std::filesystem::path( CYPHER_ASSETS_DIR ) / path, std::ios::binary );
        if ( !in ) { return false; }
        std::ostringstream all; all << in.rdbuf(); text = all.str();
        return true;
    };
    map_game_profile_t profile;
    REQUIRE( MapGame_Load( "game/reap.cygame", read, profile ) == map_status_t::OK );
    INFO( ( profile.problems.empty() ? std::string() : profile.problems.front() ) );
    CHECK( profile.problems.empty() );
    CHECK( profile.id == "reap" );
    CHECK( profile.classes.size() == 10u );
    CHECK( MapGame_FindClass( profile, SV( "info_wave_start" ) )->inputs.size() == 5u ); // Its own two and the three of targetable.
}
