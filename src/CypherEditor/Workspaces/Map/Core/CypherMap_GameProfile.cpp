//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_GameProfile.cpp
//  Purpose: Implements decoding game profiles (CYGAME.md).
//  Details: Files are read first (the profile, then its includes depth
//           first), class records are decoded next, and bases are folded
//           in last, so a class may name a base defined later or in another
//           file. Defaults stay nodes of the parsed documents, which the
//           profile keeps, so an entity takes them with their CYKV types.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_GameProfile.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace cypher::editor::map
{
using namespace common;

void map_game_document_delete_t::operator()( key_value_document_t *pDocument ) const noexcept { KeyValue_DestroyDocument( pDocument ); }

namespace
{
string_view_t SV( const char *text ) noexcept { return StringView_FromCString( text ); }
string_view_t SV( const std::string &text ) noexcept { return { text.data(), text.size() }; }

std::string Str( const key_value_t *record, const char *name )
{
    string_view_t text{};
    return KeyValue_GetString( KeyValue_Find( record, SV( name ) ), &text ) ? std::string( text.pData, text.cchLength ) : std::string();
}
bool Flag( const key_value_t *record, const char *name ) noexcept
{
    bool_t value = CY_FALSE;
    return KeyValue_GetBool( KeyValue_Find( record, SV( name ) ), &value ) && value != CY_FALSE;
}
bool Number( const key_value_t *value, f64 &out ) noexcept
{
    i64 i = 0; u64 u = 0;
    if ( KeyValue_GetF64( value, &out ) ) { return std::isfinite( out ); }
    if ( KeyValue_GetI64( value, &i ) ) { out = static_cast<f64>( i ); return true; }
    if ( KeyValue_GetU64( value, &u ) ) { out = static_cast<f64>( u ); return true; }
    return false;
}
bool Id( const key_value_t *value ) noexcept
{
    i64 i = 0; u64 u = 0;
    return KeyValue_GetU64( value, &u ) || ( KeyValue_GetI64( value, &i ) && i >= 0 );
}
// Strings of an array; a non-string item makes the whole list unusable.
bool Strings( const key_value_t *array, std::vector<std::string> &out )
{
    out.clear();
    if ( array == nullptr ) { return true; }
    if ( KeyValue_Type( array ) != key_value_type_t::ARRAY ) { return false; }
    for ( usize i = 0; i < KeyValue_ChildCount( array ); ++i ) {
        string_view_t text{};
        if ( !KeyValue_GetString( KeyValue_ChildAt( array, i ), &text ) ) { out.clear(); return false; }
        out.emplace_back( text.pData, text.cchLength );
    }
    return true;
}
// Lower-case identifiers (CYGAME.md 4, 5).
bool Identifier( const std::string &name ) noexcept
{
    if ( name.empty() || name.size() > 64 || !( ( name[0] >= 'a' && name[0] <= 'z' ) || name[0] == '_' ) ) { return false; }
    return std::all_of( name.begin(), name.end(), []( char c ) { return ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_'; } );
}
bool Contains( const std::vector<std::string> &list, string_view_t text )
{
    return std::any_of( list.begin(), list.end(), [&]( const std::string &item ) { return StringView_Equals( SV( item ), text ); } );
}
bool HexColor( string_view_t text ) noexcept
{
    if ( ( text.cchLength != 7 && text.cchLength != 9 ) || text.pData[0] != '#' ) { return false; }
    for ( usize i = 1; i < text.cchLength; ++i ) {
        const char c = text.pData[i];
        if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ) ) ) { return false; }
    }
    return true;
}

struct type_name_t { const char *name; map_game_type_t type; };
constexpr type_name_t kTypes[]{
    { "bool", map_game_type_t::BOOL }, { "integer", map_game_type_t::INTEGER }, { "real", map_game_type_t::REAL },
    { "string", map_game_type_t::STRING }, { "choice", map_game_type_t::CHOICE }, { "flags", map_game_type_t::FLAGS },
    { "color", map_game_type_t::COLOR }, { "vector", map_game_type_t::VECTOR }, { "angles", map_game_type_t::ANGLES },
    { "asset", map_game_type_t::ASSET }, { "target", map_game_type_t::TARGET }, { "object", map_game_type_t::OBJECT },
    { "objects", map_game_type_t::OBJECTS }, { "shape", map_game_type_t::SHAPE }, { "faces", map_game_type_t::FACES } };
constexpr const char *kIoTypes[]{ "void", "bool", "integer", "real", "string", "target", "vector" };

struct loader_t {
    const map_game_read_fn &read;
    map_game_profile_t &out;
    std::vector<std::string> stack{};               // Includes being read, for cycles.
    std::vector<std::pair<const key_value_t *, std::string>> classes{}; // Records and their files.
    std::vector<std::pair<const key_value_t *, std::string>> settings{};

    void Problem( const std::string &text ) { out.problems.push_back( text ); }

    // The file's root object, kept alive in the profile; null when unusable.
    const key_value_t *Read( const std::string &path, bool root, map_status_t *pStatus )
    {
        std::string text;
        if ( !read( path, text ) ) { Problem( path + ": cannot be read" ); if ( pStatus ) { *pStatus = map_status_t::ROOT_UNREADABLE; } return nullptr; }
        if ( text.size() > MAP_GAME_FILE_MAX ) { Problem( path + ": larger than 16 MiB" ); if ( pStatus ) { *pStatus = map_status_t::ROOT_UNREADABLE; } return nullptr; }
        key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
        std::unique_ptr<key_value_document_t, map_game_document_delete_t> document( KeyValue_CreateDocument( desc ) );
        if ( document == nullptr ) { if ( pStatus ) { *pStatus = map_status_t::OUT_OF_MEMORY; } return nullptr; }
        key_value_parse_options_t options{}; options.cbMaxInput = options.cbMaxStringData = MAP_GAME_FILE_MAX;
        const auto parsed = KeyValue_ParseText( SV( text ), options, document.get() );
        if ( parsed.status != key_value_parse_status_t::OK ) {
            Problem( path + ":" + std::to_string( parsed.errorLocation.nLine ) + ": " + KeyValue_ParseStatusName( parsed.status ) );
            if ( pStatus ) { *pStatus = parsed.status == key_value_parse_status_t::OUT_OF_MEMORY ? map_status_t::OUT_OF_MEMORY : map_status_t::ROOT_UNREADABLE; }
            return nullptr;
        }
        const auto header = KeyValue_DocumentHeader( document.get() );
        if ( !StringView_Equals( header.schemaId, SV( MAP_GAME_SCHEMA_ID ) ) || header.nSchemaVersion != MAP_GAME_SCHEMA_VERSION ) {
            Problem( path + ": not a cypher.game V1 document" );
            if ( pStatus ) { *pStatus = map_status_t::ROOT_UNREADABLE; }
            return nullptr;
        }
        const key_value_t *object = KeyValue_Root( document.get() );
        if ( KeyValue_Type( object ) != key_value_type_t::OBJECT ) { Problem( path + ": the root is not an object" ); if ( pStatus ) { *pStatus = map_status_t::ROOT_INVALID; } return nullptr; }
        ( void )root;
        out.files.push_back( path );
        out.documents.push_back( std::move( document ) );
        return object;
    }

    // Classes and map settings of a file, then its includes.
    void Collect( const key_value_t *object, const std::string &path )
    {
        const key_value_t *list = KeyValue_Find( object, SV( "classes" ) );
        if ( list != nullptr && KeyValue_Type( list ) != key_value_type_t::ARRAY ) { Problem( path + ": classes is not an array" ); }
        for ( usize i = 0; KeyValue_Type( list ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( list ); ++i ) { classes.emplace_back( KeyValue_ChildAt( list, i ), path ); }
        const key_value_t *mapSettings = KeyValue_Find( object, SV( "map_settings" ) );
        for ( usize i = 0; KeyValue_Type( mapSettings ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( mapSettings ); ++i ) { settings.emplace_back( KeyValue_ChildAt( mapSettings, i ), path ); }
        std::vector<std::string> includes;
        if ( !Strings( KeyValue_Find( object, SV( "includes" ) ), includes ) ) { Problem( path + ": includes is not a list of paths" ); }
        stack.push_back( path );
        for ( const auto &include : includes ) {
            if ( std::find( stack.begin(), stack.end(), include ) != stack.end() ) { Problem( path + ": includes " + include + " in a cycle" ); continue; }
            if ( stack.size() >= MAP_GAME_DEPTH_MAX ) { Problem( path + ": includes nest more than 16 deep" ); continue; }
            if ( std::find( out.files.begin(), out.files.end(), include ) != out.files.end() ) { continue; } // Already read through another file.
            if ( const key_value_t *included = Read( include, false, nullptr ); included != nullptr ) { Collect( included, include ); }
        }
        stack.pop_back();
    }
};

bool Property( const key_value_t *record, const std::string &where, map_game_property_t &out, std::vector<std::string> &problems )
{
    out = {};
    out.key = Str( record, "key" );
    if ( KeyValue_Type( record ) != key_value_type_t::OBJECT || !Identifier( out.key ) ) { problems.push_back( where + ": a property without a lower-case key" ); return false; }
    const std::string type = Str( record, "type" );
    const auto *found = std::find_if( std::begin( kTypes ), std::end( kTypes ), [&]( const type_name_t &t ) { return type == t.name; } );
    if ( found == std::end( kTypes ) ) { problems.push_back( where + "." + out.key + ": unknown type \"" + type + "\"" ); return false; }
    out.type = found->type;
    out.label = Str( record, "label" ); out.description = Str( record, "description" ); out.group = Str( record, "group" ); out.unit = Str( record, "unit" );
    out.bMin = Number( KeyValue_Find( record, SV( "min" ) ), out.min );
    out.bMax = Number( KeyValue_Find( record, SV( "max" ) ), out.max );
    if ( out.bMin && out.bMax && out.min > out.max ) { problems.push_back( where + "." + out.key + ": min is above max" ); return false; }
    if ( !Strings( KeyValue_Find( record, SV( "choices" ) ), out.choices ) || !Strings( KeyValue_Find( record, SV( "extensions" ) ), out.extensions ) ||
         !Strings( KeyValue_Find( record, SV( "target_classes" ) ), out.targetClasses ) ) {
        problems.push_back( where + "." + out.key + ": choices, extensions and target_classes are lists of strings" ); return false;
    }
    if ( ( out.type == map_game_type_t::CHOICE || out.type == map_game_type_t::FLAGS ) && out.choices.empty() ) {
        problems.push_back( where + "." + out.key + ": a " + type + " needs choices" ); return false;
    }
    out.bRequired = Flag( record, "required" ); out.bReadOnly = Flag( record, "readonly" ); out.bHidden = Flag( record, "hidden" );
    // A bad default leaves the property without one: the property still edits.
    if ( const key_value_t *value = KeyValue_Find( record, SV( "default" ) ); value != nullptr ) {
        std::string why;
        if ( MapGame_CheckValue( out, value, &why ) ) { out.pDefault = value; }
        else { problems.push_back( where + "." + out.key + ": default " + why ); }
    }
    return true;
}

void Io( const key_value_t *list, const std::string &where, std::vector<map_game_io_t> &out, std::vector<std::string> &problems )
{
    for ( usize i = 0; KeyValue_Type( list ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( list ); ++i ) {
        const key_value_t *record = KeyValue_ChildAt( list, i );
        map_game_io_t io{};
        io.name = Str( record, "name" );
        if ( !Identifier( io.name ) ) { problems.push_back( where + ": an input or output without a lower-case name" ); continue; }
        if ( const std::string type = Str( record, "type" ); !type.empty() ) {
            if ( std::none_of( std::begin( kIoTypes ), std::end( kIoTypes ), [&]( const char *t ) { return type == t; } ) ) { problems.push_back( where + "." + io.name + ": unknown type \"" + type + "\"" ); continue; }
            io.type = type;
        }
        io.description = Str( record, "description" );
        if ( out.size() >= MAP_GAME_IO_MAX ) { problems.push_back( where + ": more than 128 inputs or outputs" ); break; }
        out.push_back( std::move( io ) );
    }
}

// Later definitions replace earlier ones with the same key, in place.
template <typename T, typename Key>
void Merge( std::vector<T> &into, const std::vector<T> &from, Key key )
{
    for ( const T &item : from ) {
        auto it = std::find_if( into.begin(), into.end(), [&]( const T &existing ) { return key( existing ) == key( item ); } );
        if ( it != into.end() ) { *it = item; } else { into.push_back( item ); }
    }
}

struct resolver_t {
    std::vector<map_game_class_t> &classes;
    std::unordered_map<std::string, usize> byName{};
    std::vector<map_game_class_t> own{}; // Before bases were folded in.
    std::vector<u8> state{};             // 0 not yet, 1 resolving, 2 done.
    std::vector<std::string> &problems;

    void Resolve( usize index, u32 depth )
    {
        if ( state[index] == 2 ) { return; }
        state[index] = 1;
        map_game_class_t &cls = classes[index];
        std::vector<map_game_property_t> properties;
        std::vector<map_game_io_t> inputs, outputs;
        for ( const auto &base : own[index].bases ) {
            const auto it = byName.find( base );
            if ( it == byName.end() ) { problems.push_back( cls.name + ": base " + base + " is not defined" ); continue; }
            if ( state[it->second] == 1 ) { problems.push_back( cls.name + ": base " + base + " is in a cycle" ); continue; }
            if ( depth + 1 >= MAP_GAME_DEPTH_MAX ) { problems.push_back( cls.name + ": bases nest more than 16 deep" ); continue; }
            Resolve( it->second, depth + 1 );
            const map_game_class_t &resolved = classes[it->second];
            Merge( properties, resolved.properties, []( const map_game_property_t &p ) { return p.key; } );
            Merge( inputs, resolved.inputs, []( const map_game_io_t &io ) { return io.name; } );
            Merge( outputs, resolved.outputs, []( const map_game_io_t &io ) { return io.name; } );
        }
        Merge( properties, own[index].properties, []( const map_game_property_t &p ) { return p.key; } );
        Merge( inputs, own[index].inputs, []( const map_game_io_t &io ) { return io.name; } );
        Merge( outputs, own[index].outputs, []( const map_game_io_t &io ) { return io.name; } );
        if ( properties.size() > MAP_GAME_PROPERTIES_MAX ) { problems.push_back( cls.name + ": more than 256 properties" ); properties.resize( MAP_GAME_PROPERTIES_MAX ); }
        if ( inputs.size() > MAP_GAME_IO_MAX ) { inputs.resize( MAP_GAME_IO_MAX ); }
        if ( outputs.size() > MAP_GAME_IO_MAX ) { outputs.resize( MAP_GAME_IO_MAX ); }
        cls.properties = std::move( properties ); cls.inputs = std::move( inputs ); cls.outputs = std::move( outputs );
        state[index] = 2;
    }
};

bool Class( const key_value_t *record, const std::string &file, map_game_class_t &out, std::vector<std::string> &problems )
{
    out = {};
    out.name = Str( record, "name" );
    if ( KeyValue_Type( record ) != key_value_type_t::OBJECT || !Identifier( out.name ) ) { problems.push_back( file + ": a class without a lower-case name" ); return false; }
    const std::string kind = Str( record, "kind" );
    if ( kind == "point" ) { out.kind = map_game_kind_t::POINT; }
    else if ( kind == "solid" ) { out.kind = map_game_kind_t::SOLID; }
    else if ( kind == "abstract" ) { out.kind = map_game_kind_t::ABSTRACT; }
    else { problems.push_back( out.name + ": kind is point, solid or abstract" ); return false; }
    if ( !Strings( KeyValue_Find( record, SV( "base" ) ), out.bases ) ) { problems.push_back( out.name + ": base is a list of class names" ); out.bases.clear(); }
    out.label = Str( record, "label" ); out.category = Str( record, "category" ); out.description = Str( record, "description" );
    if ( const key_value_t *editor = KeyValue_Find( record, SV( "editor" ) ); editor != nullptr ) {
        if ( const key_value_t *size = KeyValue_Find( editor, SV( "size" ) ); size != nullptr ) {
            out.bSize = KeyValue_Type( size ) == key_value_type_t::ARRAY && KeyValue_ChildCount( size ) == 6;
            for ( usize i = 0; out.bSize && i < 6; ++i ) { out.bSize = Number( KeyValue_ChildAt( size, i ), out.size[i] ); }
            for ( usize i = 0; out.bSize && i < 3; ++i ) { out.bSize = out.size[i] <= out.size[i + 3]; }
            if ( !out.bSize ) { problems.push_back( out.name + ": editor.size is [minx, miny, minz, maxx, maxy, maxz]" ); }
        }
        out.color = Str( editor, "color" ); out.colorFrom = Str( editor, "color_from" );
        if ( !out.color.empty() && !HexColor( SV( out.color ) ) ) { problems.push_back( out.name + ": editor.color is \"#rrggbb\"" ); out.color.clear(); }
        out.model = Str( editor, "model" ); out.modelFrom = Str( editor, "model_from" ); out.sizeFrom = Str( editor, "size_from" );
        out.icon = Str( editor, "icon" ); out.material = Str( editor, "material" );
        const key_value_t *helpers = KeyValue_Find( editor, SV( "helpers" ) );
        for ( usize i = 0; KeyValue_Type( helpers ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( helpers ); ++i ) {
            const key_value_t *h = KeyValue_ChildAt( helpers, i );
            map_game_helper_t helper{ Str( h, "type" ), Str( h, "property" ), Str( h, "angle" ), Str( h, "range" ), Str( h, "distance" ) };
            if ( helper.type.empty() ) { problems.push_back( out.name + ": a helper without a type" ); continue; }
            out.helpers.push_back( std::move( helper ) );
        }
    }
    const key_value_t *properties = KeyValue_Find( record, SV( "properties" ) );
    for ( usize i = 0; KeyValue_Type( properties ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( properties ); ++i ) {
        map_game_property_t property{};
        if ( !Property( KeyValue_ChildAt( properties, i ), out.name, property, problems ) ) { continue; }
        if ( std::any_of( out.properties.begin(), out.properties.end(), [&]( const auto &p ) { return p.key == property.key; } ) ) {
            problems.push_back( out.name + "." + property.key + ": defined twice; the first is kept" ); continue;
        }
        out.properties.push_back( std::move( property ) );
    }
    Io( KeyValue_Find( record, SV( "inputs" ) ), out.name, out.inputs, problems );
    Io( KeyValue_Find( record, SV( "outputs" ) ), out.name, out.outputs, problems );
    return true;
}

void Migrations( const key_value_t *list, map_game_profile_t &out )
{
    for ( usize i = 0; KeyValue_Type( list ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( list ); ++i ) {
        const key_value_t *record = KeyValue_ChildAt( list, i );
        map_game_migration_t migration{};
        if ( !KeyValue_GetU64( KeyValue_Find( record, SV( "version" ) ), &migration.version ) ) { out.problems.push_back( "migrations: an entry without a version" ); continue; }
        const key_value_t *renameClass = KeyValue_Find( record, SV( "rename_class" ) );
        const key_value_t *renameProperty = KeyValue_Find( record, SV( "rename_property" ) );
        const key_value_t *rename = renameClass != nullptr ? renameClass : renameProperty;
        migration.kind = renameClass != nullptr ? map_game_migration_kind_t::RENAME_CLASS : map_game_migration_kind_t::RENAME_PROPERTY;
        migration.className = Str( rename, "class" ); migration.from = Str( rename, "from" ); migration.to = Str( rename, "to" );
        if ( rename == nullptr || migration.from.empty() || migration.to.empty() || ( renameProperty != nullptr && renameClass == nullptr && migration.className.empty() ) ) {
            out.problems.push_back( "migrations: an entry needs rename_class { from to } or rename_property { class from to }" ); continue;
        }
        out.migrations.push_back( std::move( migration ) );
    }
}
} // namespace

map_status_t MapGame_Load( const std::string &path, const map_game_read_fn &read, map_game_profile_t &out )
{
    out = map_game_profile_t{};
    if ( !read ) { return map_status_t::INVALID_ARGUMENT; }
    try {
        loader_t loader{ read, out };
        map_status_t status = map_status_t::OK;
        const key_value_t *root = loader.Read( path, true, &status );
        if ( root == nullptr ) { return status == map_status_t::OK ? map_status_t::ROOT_UNREADABLE : status; }
        out.id = Str( root, "id" ); out.name = Str( root, "name" );
        if ( out.id.empty() || out.name.empty() || !KeyValue_GetU64( KeyValue_Find( root, SV( "version" ) ), &out.version ) ) {
            out.problems.push_back( path + ": a profile needs id, name and version" );
            return map_status_t::ROOT_INVALID;
        }
        f64 units = 0.0;
        if ( Number( KeyValue_Find( root, SV( "units_per_meter" ) ), units ) && units > 0.0 ) { out.unitsPerMeter = units; }
        if ( const key_value_t *editor = KeyValue_Find( root, SV( "editor" ) ); editor != nullptr ) {
            out.defaultPointClass = Str( editor, "default_point_class" ); out.defaultSolidClass = Str( editor, "default_solid_class" );
            out.sprites = Str( editor, "sprites" );
            if ( !Strings( KeyValue_Find( editor, SV( "categories" ) ), out.categories ) ) { out.problems.push_back( path + ": editor.categories is a list of names" ); }
        }
        Migrations( KeyValue_Find( root, SV( "migrations" ) ), out );
        loader.Collect( root, path );

        for ( const auto &[record, file] : loader.settings ) {
            map_game_property_t setting{};
            if ( Property( record, "map_settings", setting, out.problems ) ) { Merge( out.mapSettings, { setting }, []( const map_game_property_t &p ) { return p.key; } ); }
        }
        resolver_t resolver{ out.classes, {}, {}, {}, out.problems };
        for ( const auto &[record, file] : loader.classes ) {
            map_game_class_t cls{};
            if ( !Class( record, file, cls, out.problems ) ) { continue; }
            if ( resolver.byName.count( cls.name ) != 0 ) { out.problems.push_back( file + ": class " + cls.name + " is defined twice; the first is kept" ); continue; }
            if ( out.classes.size() >= MAP_GAME_CLASSES_MAX ) { out.problems.push_back( "more than 4,096 classes" ); break; }
            resolver.byName.emplace( cls.name, out.classes.size() );
            resolver.own.push_back( cls );
            out.classes.push_back( std::move( cls ) );
        }
        resolver.state.assign( out.classes.size(), 0 );
        for ( usize i = 0; i < out.classes.size(); ++i ) { resolver.Resolve( i, 0 ); }
        const std::pair<const char *, const std::string *> defaults[]{ { "default_point_class", &out.defaultPointClass }, { "default_solid_class", &out.defaultSolidClass } };
        for ( const auto &[which, name] : defaults ) {
            if ( !name->empty() && MapGame_FindClass( out, SV( *name ) ) == nullptr ) { out.problems.push_back( std::string( "editor." ) + which + ": " + *name + " is not defined" ); }
        }
    } catch ( ... ) {
        out = map_game_profile_t{};
        return map_status_t::OUT_OF_MEMORY;
    }
    return map_status_t::OK;
}

const map_game_class_t *MapGame_FindClass( const map_game_profile_t &profile, string_view_t name ) noexcept
{
    for ( const auto &cls : profile.classes ) { if ( StringView_Equals( SV( cls.name ), name ) ) { return &cls; } }
    return nullptr;
}

const map_game_property_t *MapGame_FindProperty( const map_game_class_t &cls, string_view_t key ) noexcept
{
    for ( const auto &property : cls.properties ) { if ( StringView_Equals( SV( property.key ), key ) ) { return &property; } }
    return nullptr;
}

const char *MapGame_TypeName( map_game_type_t type ) noexcept
{
    for ( const auto &t : kTypes ) { if ( t.type == type ) { return t.name; } }
    return "string";
}

bool MapGame_CheckValue( const map_game_property_t &property, const key_value_t *value, std::string *pWhyOut )
{
    const auto fail = [&]( const std::string &why ) { if ( pWhyOut != nullptr ) { *pWhyOut = why; } return false; };
    if ( value == nullptr ) { return fail( "is missing" ); }
    const key_value_type_t type = KeyValue_Type( value );
    const auto inRange = [&]( f64 number ) {
        if ( property.bMin && number < property.min ) { return fail( "is below " + std::to_string( property.min ) ); }
        if ( property.bMax && number > property.max ) { return fail( "is above " + std::to_string( property.max ) ); }
        return true;
    };
    const auto numbers = [&]( usize count ) {
        f64 n = 0.0;
        if ( type != key_value_type_t::ARRAY || KeyValue_ChildCount( value ) != count ) { return false; }
        for ( usize i = 0; i < count; ++i ) { if ( !Number( KeyValue_ChildAt( value, i ), n ) ) { return false; } }
        return true;
    };
    string_view_t text{};
    f64 number = 0.0;
    switch ( property.type ) {
        case map_game_type_t::BOOL: return type == key_value_type_t::BOOL || fail( "is not true or false" );
        case map_game_type_t::INTEGER:
            if ( type != key_value_type_t::I64 && type != key_value_type_t::U64 ) { return fail( "is not a whole number" ); }
            ( void )Number( value, number ); return inRange( number );
        case map_game_type_t::REAL:
            if ( !Number( value, number ) ) { return fail( "is not a number" ); }
            return inRange( number );
        case map_game_type_t::STRING:
        case map_game_type_t::TARGET: return KeyValue_GetString( value, &text ) || fail( "is not text" );
        case map_game_type_t::ASSET: {
            if ( !KeyValue_GetString( value, &text ) ) { return fail( "is not an asset path" ); }
            if ( text.cchLength == 0 || property.extensions.empty() ) { return true; }
            const std::string path( text.pData, text.cchLength );
            for ( const auto &extension : property.extensions ) {
                if ( path.size() >= extension.size() && path.compare( path.size() - extension.size(), extension.size(), extension ) == 0 ) { return true; }
            }
            return fail( "is not a " + property.extensions.front() + " asset" );
        }
        case map_game_type_t::CHOICE:
            if ( !KeyValue_GetString( value, &text ) ) { return fail( "is not text" ); }
            return Contains( property.choices, text ) || fail( "\"" + std::string( text.pData, text.cchLength ) + "\" is not one of the choices" );
        case map_game_type_t::FLAGS:
            if ( type != key_value_type_t::ARRAY ) { return fail( "is not a list of flags" ); }
            for ( usize i = 0; i < KeyValue_ChildCount( value ); ++i ) {
                if ( !KeyValue_GetString( KeyValue_ChildAt( value, i ), &text ) || !Contains( property.choices, text ) ) { return fail( "names a flag that is not one of the choices" ); }
            }
            return true;
        case map_game_type_t::COLOR: return ( KeyValue_GetString( value, &text ) && HexColor( text ) ) || fail( "is not \"#rrggbb\"" );
        case map_game_type_t::VECTOR:
        case map_game_type_t::ANGLES: return numbers( 3 ) || fail( "is not three numbers" );
        case map_game_type_t::OBJECT:
        case map_game_type_t::SHAPE: return Id( value ) || fail( "is not an object ID" );
        case map_game_type_t::OBJECTS:
        case map_game_type_t::FACES:
            if ( type != key_value_type_t::ARRAY ) { return fail( "is not a list of IDs" ); }
            for ( usize i = 0; i < KeyValue_ChildCount( value ); ++i ) { if ( !Id( KeyValue_ChildAt( value, i ) ) ) { return fail( "is not a list of IDs" ); } }
            return true;
    }
    return fail( "has an unknown type" );
}

} // namespace cypher::editor::map
