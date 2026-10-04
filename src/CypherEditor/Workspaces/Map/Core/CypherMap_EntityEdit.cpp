//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Entity property edits preserve typed retained source data and explicit ownership.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_EntityEdit.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_Unicode.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace cypher::editor::map
{
using namespace common;
namespace
{
string_view_t SV( const char *text ) noexcept { return StringView_FromCString( text ); }
bool Text( string_view_t text, bool allowEmpty ) noexcept
{
    return StringView_IsValid( text ) && ( allowEmpty || text.cchLength != 0u ) &&
        ( text.cchLength == 0u || std::memchr( text.pData, '\0', text.cchLength ) == nullptr ) &&
        Unicode_ValidateUtf8( text ).status == unicode_status_t::OK;
}
bool Key( string_view_t key ) noexcept { return key.cchLength <= MAP_ENTITY_PROPERTY_KEY_MAX && Text( key, false ); }
template <usize N>
string_view_t CopyText( std::array<char, N> &buffer, string_view_t text ) noexcept
{
    if ( text.cchLength != 0u ) { std::memcpy( buffer.data(), text.pData, text.cchLength ); }
    return { buffer.data(), text.cchLength };
}
const key_value_t *Member( const key_value_t *object, string_view_t name ) noexcept
{
    if ( KeyValue_Type( object ) != key_value_type_t::OBJECT ) { return nullptr; }
    for ( const auto *child = KeyValue_FirstChild( object ); child != nullptr; child = KeyValue_NextSibling( child ) ) {
        if ( StringView_Equals( KeyValue_Name( child ), name ) ) { return child; }
    }
    return nullptr;
}
key_value_t *Member( key_value_t *object, string_view_t name ) noexcept
{
    return const_cast<key_value_t *>( Member( static_cast<const key_value_t *>( object ), name ) );
}
bool UniqueNames( const key_value_t *object ) noexcept
{
    if ( KeyValue_ChildCount( object ) > MAP_ENTITY_PROPERTIES_MAX ) { return false; }
    std::array<string_view_t, MAP_ENTITY_PROPERTIES_MAX> names{};
    usize i = 0;
    for ( const auto *child = KeyValue_FirstChild( object ); child != nullptr; child = KeyValue_NextSibling( child ), ++i ) {
        const auto name = KeyValue_Name( child );
        for ( usize j = 0; j < i; ++j ) {
            if ( StringView_Equals( name, names[j] ) ) { return false; }
        }
        names[i] = name;
    }
    return true;
}
u64 Id( const key_value_t *node ) noexcept
{
    u64 unsignedId = 0; i64 signedId = 0;
    if ( KeyValue_GetU64( node, &unsignedId ) ) { return unsignedId; }
    return KeyValue_GetI64( node, &signedId ) && signedId > 0 ? static_cast<u64>( signedId ) : 0u;
}
const key_value_t *Entity( const map_document_t *map, u64 id, map_chunk_t **chunkOut = nullptr ) noexcept
{
    for ( usize chunk = 0; chunk < map->chunks.nCount; ++chunk ) {
        auto *store = map->chunks.pData[chunk];
        if ( store == nullptr || store->bDamaged ) { continue; }
        const auto *entities = Member( SettingsDocument_Root( &store->store ), SV( "entities" ) );
        if ( KeyValue_Type( entities ) != key_value_type_t::ARRAY ) { continue; }
        for ( const auto *entity = KeyValue_FirstChild( entities ); entity != nullptr; entity = KeyValue_NextSibling( entity ) ) {
            if ( KeyValue_Type( entity ) != key_value_type_t::OBJECT || Id( Member( entity, SV( "id" ) ) ) != id ) { continue; }
            string_view_t className{};
            if ( KeyValue_ChildCount( entity ) > MAP_ENTITY_RECORD_MEMBERS_MAX || !UniqueNames( entity ) || !KeyValue_GetString( Member( entity, SV( "class" ) ), &className ) ||
                !Text( className, false ) ) { return nullptr; }
            for ( const char *section : { "brushes", "meshes", "patches" } ) {
                const auto *owned = Member( entity, SV( section ) );
                if ( owned != nullptr && KeyValue_Type( owned ) != key_value_type_t::ARRAY ) { return nullptr; }
            }
            if ( chunkOut != nullptr ) { *chunkOut = store; }
            return entity;
        }
    }
    return nullptr;
}
map_status_t Check( const map_document_t *map, span_t<const u64> ids ) noexcept
{
    if ( map == nullptr || map->pAllocator == nullptr || !Span_IsValid( ids ) || ids.nCount == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    return map_status_t::OK;
}
struct value_budget_t { usize nodes{}, bytes{}; };
map_status_t ValueValid( const key_value_t *value, usize depth, value_budget_t &budget ) noexcept
{
    if ( value == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( depth > MAP_ENTITY_PROPERTY_VALUE_DEPTH_MAX || ++budget.nodes > MAP_ENTITY_PROPERTY_VALUE_NODES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    const auto type = KeyValue_Type( value );
    const auto name = KeyValue_Name( value );
    if ( !Text( name, true ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( name.cchLength > MAP_ENTITY_PROPERTY_VALUE_BYTES_MAX - budget.bytes ) { return map_status_t::LIMIT_EXCEEDED; }
    budget.bytes += name.cchLength;
    if ( type == key_value_type_t::STRING || type == key_value_type_t::BINARY ) {
        binary_block_t bytes{}; string_view_t text{};
        if ( type == key_value_type_t::STRING ) {
            if ( !KeyValue_GetString( value, &text ) || !StringView_IsValid( text ) ||
                Unicode_ValidateUtf8( text ).status != unicode_status_t::OK ) { return map_status_t::INVALID_ARGUMENT; }
            bytes = BinaryBlock_FromData( text.pData, text.cchLength );
        } else if ( !KeyValue_GetBinary( value, &bytes ) || !BinaryBlock_IsValid( bytes ) ) { return map_status_t::INVALID_ARGUMENT; }
        if ( bytes.cbSize > MAP_ENTITY_PROPERTY_VALUE_BYTES_MAX - budget.bytes ) { return map_status_t::LIMIT_EXCEEDED; }
        budget.bytes += bytes.cbSize;
    } else if ( type == key_value_type_t::F64 ) {
        f64 number{};
        if ( !KeyValue_GetF64( value, &number ) || !std::isfinite( number ) ) { return map_status_t::INVALID_ARGUMENT; }
    } else if ( type == key_value_type_t::OBJECT || type == key_value_type_t::ARRAY ) {
        if ( KeyValue_ChildCount( value ) > MAP_ENTITY_PROPERTY_VALUE_NODES_MAX - budget.nodes ) { return map_status_t::LIMIT_EXCEEDED; }
        if ( type == key_value_type_t::OBJECT && KeyValue_ChildCount( value ) > MAP_ENTITY_PROPERTIES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
        if ( type == key_value_type_t::OBJECT && !UniqueNames( value ) ) { return map_status_t::INVALID_ARGUMENT; }
        for ( const auto *child = KeyValue_FirstChild( value ); child != nullptr; child = KeyValue_NextSibling( child ) ) {
            if ( type == key_value_type_t::OBJECT && !Text( KeyValue_Name( child ), false ) ) { return map_status_t::INVALID_ARGUMENT; }
            const auto status = ValueValid( child, depth + 1u, budget );
            if ( status != map_status_t::OK ) { return status; }
        }
    }
    return map_status_t::OK;
}
map_status_t PropertiesValid( const key_value_t *entity ) noexcept
{
    const auto *properties = Member( entity, SV( "properties" ) );
    if ( properties == nullptr ) { return map_status_t::OK; }
    if ( KeyValue_Type( properties ) != key_value_type_t::OBJECT ) { return map_status_t::INVALID_ARGUMENT; }
    if ( KeyValue_ChildCount( properties ) > MAP_ENTITY_PROPERTIES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( const auto *property = KeyValue_FirstChild( properties ); property != nullptr; property = KeyValue_NextSibling( property ) ) {
        if ( !Key( KeyValue_Name( property ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    value_budget_t budget{};
    return ValueValid( properties, 0, budget );
}
map_status_t Preflight( const map_document_t *map, span_t<const u64> ids, bool *changed ) noexcept
{
    if ( changed == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *changed = false;
    const auto status = Check( map, ids );
    if ( status != map_status_t::OK ) { return status; }
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const auto *entity = ids.pData[i] != 0u ? Entity( map, ids.pData[i] ) : nullptr;
        if ( entity == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        const auto valid = PropertiesValid( entity );
        if ( valid != map_status_t::OK ) { return valid; }
    }
    return map_status_t::OK;
}
bool Equal( const key_value_t *left, const key_value_t *right ) noexcept
{
    if ( left == nullptr || right == nullptr ) { return left == right; }
    if ( KeyValue_Type( left ) != KeyValue_Type( right ) ) { return false; }
    switch ( KeyValue_Type( left ) ) {
        case key_value_type_t::NULL_VALUE: return true;
        case key_value_type_t::BOOL: { bool_t a{}, b{}; return KeyValue_GetBool( left, &a ) && KeyValue_GetBool( right, &b ) && a == b; }
        case key_value_type_t::I64: { i64 a{}, b{}; return KeyValue_GetI64( left, &a ) && KeyValue_GetI64( right, &b ) && a == b; }
        case key_value_type_t::U64: { u64 a{}, b{}; return KeyValue_GetU64( left, &a ) && KeyValue_GetU64( right, &b ) && a == b; }
        case key_value_type_t::F64: { f64 a{}, b{}; return KeyValue_GetF64( left, &a ) && KeyValue_GetF64( right, &b ) && a == b; }
        case key_value_type_t::STRING: { string_view_t a{}, b{}; return KeyValue_GetString( left, &a ) && KeyValue_GetString( right, &b ) && StringView_Equals( a, b ); }
        case key_value_type_t::BINARY: { binary_block_t a{}, b{}; return KeyValue_GetBinary( left, &a ) && KeyValue_GetBinary( right, &b ) && a.cbSize == b.cbSize && ( a.cbSize == 0 || std::memcmp( a.pData, b.pData, a.cbSize ) == 0 ); }
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY: {
            if ( KeyValue_ChildCount( left ) != KeyValue_ChildCount( right ) ) { return false; }
            const auto *other = KeyValue_FirstChild( right );
            for ( const auto *child = KeyValue_FirstChild( left ); child != nullptr; child = KeyValue_NextSibling( child ) ) {
                if ( KeyValue_Type( left ) == key_value_type_t::OBJECT ) { other = Member( right, KeyValue_Name( child ) ); }
                if ( !Equal( child, other ) ) { return false; }
                if ( KeyValue_Type( left ) == key_value_type_t::ARRAY ) { other = KeyValue_NextSibling( other ); }
            }
            return true;
        }
    }
    return false;
}
bool CopyValue( key_value_document_t *document, key_value_t *dest, const key_value_t *source ) noexcept
{
    switch ( KeyValue_Type( source ) ) {
        case key_value_type_t::NULL_VALUE: return KeyValue_SetNull( document, dest );
        case key_value_type_t::BOOL: { bool_t value{}; return KeyValue_GetBool( source, &value ) && KeyValue_SetBool( document, dest, value ); }
        case key_value_type_t::I64: { i64 value{}; return KeyValue_GetI64( source, &value ) && KeyValue_SetI64( document, dest, value ); }
        case key_value_type_t::U64: { u64 value{}; return KeyValue_GetU64( source, &value ) && KeyValue_SetU64( document, dest, value ); }
        case key_value_type_t::F64: { f64 value{}; return KeyValue_GetF64( source, &value ) && KeyValue_SetF64( document, dest, value ); }
        case key_value_type_t::STRING: { string_view_t value{}; return KeyValue_GetString( source, &value ) && KeyValue_SetString( document, dest, value ); }
        case key_value_type_t::BINARY: { binary_block_t value{}; return KeyValue_GetBinary( source, &value ) && KeyValue_SetBinary( document, dest, value ); }
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY:
            if ( !KeyValue_SetContainerType( document, dest, KeyValue_Type( source ) ) ) { return false; }
            for ( const auto *child = KeyValue_FirstChild( source ); child != nullptr; child = KeyValue_NextSibling( child ) ) {
                if ( KeyValue_CloneInto( document, dest, child ) == nullptr ) { return false; }
            }
            return true;
    }
    return false;
}
} // namespace

bool MapEntityEdit_ValuesEqual( const key_value_t *left, const key_value_t *right ) noexcept
{
    if ( left == nullptr || right == nullptr ) { return left == right; }
    value_budget_t leftBudget{}, rightBudget{};
    return ValueValid( left, 0, leftBudget ) == map_status_t::OK &&
        ValueValid( right, 0, rightBudget ) == map_status_t::OK && Equal( left, right );
}

const key_value_t *MapEntityEdit_FindEntity( const map_document_t *map, u64 id, map_chunk_t **chunkOut ) noexcept
{
    if ( chunkOut != nullptr ) { *chunkOut = nullptr; }
    if ( map == nullptr || map->pAllocator == nullptr || id == 0u ) { return nullptr; }
    map_chunk_t *chunk{};
    const auto *entity = Entity( map, id, &chunk );
    if ( entity == nullptr || PropertiesValid( entity ) != map_status_t::OK ) { return nullptr; }
    if ( chunkOut != nullptr ) { *chunkOut = chunk; }
    return entity;
}

map_status_t MapEntityEdit_ResolveOwners( const map_document_t *map, span_t<const u64> selection, vector_t<u64> *owners ) noexcept
{
    if ( owners == nullptr || owners->pAllocator == nullptr || owners->nCount != 0u ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map == nullptr || map->pAllocator == nullptr || !Span_IsValid( selection ) || selection.nCount == 0u ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> resolved{};
    if ( !Vector_Init( &resolved, owners->pAllocator, selection.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < selection.nCount; ++i ) {
        const u64 selected = selection.pData[i]; u64 owner = selected;
        if ( selected == 0u ) { return map_status_t::UNKNOWN_OBJECT; }
        const auto *entity = Entity( map, selected );
        if ( entity == nullptr ) {
            if ( geometry::GeometryDocument_FindBrush( &map->geometry, { selected } ) == nullptr &&
                geometry::GeometryDocument_FindMesh( &map->geometry, { selected } ) == nullptr &&
                geometry::GeometryDocument_FindPatch( &map->geometry, { selected } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
            owner = 0;
            for ( usize k = 0; k < map->geometryRecords.nCount; ++k ) {
                if ( map->geometryRecords.pData[k].id == selected ) { owner = map->geometryRecords.pData[k].owner; break; }
            }
            if ( owner == 0u ) { return map_status_t::INVALID_ARGUMENT; }
            entity = Entity( map, owner );
            if ( entity == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        }
        const auto valid = PropertiesValid( entity );
        if ( valid != map_status_t::OK ) { return valid; }
        bool found = false;
        for ( usize k = 0; k < resolved.nCount; ++k ) { found |= resolved.pData[k] == owner; }
        if ( !found && !Vector_PushBack( &resolved, owner ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    Vector_Shutdown( owners ); Vector_Move( owners, &resolved );
    return map_status_t::OK;
}

map_status_t MapEntityEdit_SetProperty( map_document_t *map, span_t<const u64> ids, string_view_t key,
    const key_value_t *value, bool *changed ) noexcept
{
    auto status = Preflight( map, ids, changed );
    if ( status != map_status_t::OK ) { return status; }
    if ( !Key( key ) || value == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    // Names can borrow payload data from a property that this batch replaces.
    std::array<char, MAP_ENTITY_PROPERTY_KEY_MAX> keyBuffer{};
    key = CopyText( keyBuffer, key );
    value_budget_t budget{}; status = ValueValid( value, 0, budget );
    if ( status != map_status_t::OK ) { return status; }
    bool any = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const auto *entity = Entity( map, ids.pData[i] );
        const auto *properties = Member( entity, SV( "properties" ) );
        if ( properties == nullptr && KeyValue_ChildCount( entity ) >= MAP_ENTITY_RECORD_MEMBERS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
        const auto *old = Member( properties, key );
        if ( old == nullptr && KeyValue_ChildCount( properties ) >= MAP_ENTITY_PROPERTIES_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
        any |= !Equal( old, value );
    }
    if ( !any ) { return map_status_t::OK; }
    // A source may be an existing property or a descendant of the property
    // being replaced. Stage it so resetting any destination cannot invalidate it.
    std::unique_ptr<key_value_document_t, decltype( &KeyValue_DestroyDocument )> staged(
        KeyValue_CreateDocument( { map->pAllocator } ), &KeyValue_DestroyDocument );
    if ( staged == nullptr || KeyValue_CloneInto( staged.get(), nullptr, value ) == nullptr ) { return map_status_t::OUT_OF_MEMORY; }
    value = KeyValue_Root( staged.get() );
    for ( usize i = 0; i < ids.nCount; ++i ) {
        map_chunk_t *chunk{};
        auto *entity = const_cast<key_value_t *>( Entity( map, ids.pData[i], &chunk ) );
        auto *properties = Member( entity, SV( "properties" ) );
        if ( Equal( Member( properties, key ), value ) ) { continue; }
        auto *document = chunk->store.pDocument;
        if ( properties == nullptr ) { properties = KeyValue_ObjectInsert( document, entity, SV( "properties" ), key_value_type_t::OBJECT ); }
        auto *dest = properties != nullptr ? Member( properties, key ) : nullptr;
        if ( dest == nullptr && properties != nullptr ) { dest = KeyValue_ObjectInsert( document, properties, key, key_value_type_t::NULL_VALUE ); }
        if ( dest == nullptr || !CopyValue( document, dest, value ) ) { return map_status_t::OUT_OF_MEMORY; }
        if ( PropertiesValid( entity ) != map_status_t::OK ) { return map_status_t::LIMIT_EXCEEDED; }
    }
    *changed = true;
    return map_status_t::OK;
}

map_status_t MapEntityEdit_RemoveProperty( map_document_t *map, span_t<const u64> ids, string_view_t key, bool *changed ) noexcept
{
    const auto status = Preflight( map, ids, changed );
    if ( status != map_status_t::OK ) { return status; }
    if ( !Key( key ) ) { return map_status_t::INVALID_ARGUMENT; }
    std::array<char, MAP_ENTITY_PROPERTY_KEY_MAX> keyBuffer{};
    key = CopyText( keyBuffer, key );
    bool any = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        map_chunk_t *chunk{}; auto *entity = const_cast<key_value_t *>( Entity( map, ids.pData[i], &chunk ) );
        auto *properties = Member( entity, SV( "properties" ) ); auto *value = Member( properties, key );
        if ( value == nullptr ) { continue; }
        if ( !KeyValue_Remove( chunk->store.pDocument, properties, value ) ) { return map_status_t::INVALID_ARGUMENT; }
        any = true;
    }
    *changed = any;
    return map_status_t::OK;
}

map_status_t MapEntityEdit_RenameProperty( map_document_t *map, span_t<const u64> ids, string_view_t oldKey,
    string_view_t newKey, bool *changed ) noexcept
{
    const auto status = Preflight( map, ids, changed );
    if ( status != map_status_t::OK ) { return status; }
    if ( !Key( oldKey ) || !Key( newKey ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( StringView_Equals( oldKey, newKey ) ) { return map_status_t::OK; }
    std::array<char, MAP_ENTITY_PROPERTY_KEY_MAX> oldKeyBuffer{}, newKeyBuffer{};
    oldKey = CopyText( oldKeyBuffer, oldKey ); newKey = CopyText( newKeyBuffer, newKey );
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const auto *properties = Member( Entity( map, ids.pData[i] ), SV( "properties" ) );
        if ( Member( properties, newKey ) != nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    }
    bool any = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        map_chunk_t *chunk{}; auto *entity = const_cast<key_value_t *>( Entity( map, ids.pData[i], &chunk ) );
        auto *properties = Member( entity, SV( "properties" ) ); auto *source = Member( properties, oldKey );
        if ( source == nullptr ) { continue; }
        auto *dest = KeyValue_ObjectInsert( chunk->store.pDocument, properties, newKey, key_value_type_t::NULL_VALUE );
        if ( dest == nullptr || !CopyValue( chunk->store.pDocument, dest, source ) ) { return map_status_t::OUT_OF_MEMORY; }
        if ( !KeyValue_Remove( chunk->store.pDocument, properties, source ) ) { return map_status_t::INVALID_ARGUMENT; }
        const auto valid = PropertiesValid( entity );
        if ( valid != map_status_t::OK ) { return valid; }
        any = true;
    }
    *changed = any;
    return map_status_t::OK;
}

map_status_t MapEntityEdit_SetIdentityField( map_document_t *map, span_t<const u64> ids, string_view_t field,
    string_view_t value, bool *changed ) noexcept
{
    const auto status = Preflight( map, ids, changed );
    if ( status != map_status_t::OK ) { return status; }
    if ( !StringView_IsValid( field ) ) { return map_status_t::INVALID_ARGUMENT; }
    const bool isClass = StringView_Equals( field, SV( "class" ) );
    if ( ( !isClass && !StringView_Equals( field, SV( "name" ) ) ) || !Text( value, !isClass ) ||
        value.cchLength > MAP_NAME_MAX_LENGTH ) { return map_status_t::INVALID_ARGUMENT; }
    field = isClass ? SV( "class" ) : SV( "name" );
    std::array<char, MAP_NAME_MAX_LENGTH> valueBuffer{};
    value = CopyText( valueBuffer, value );
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const auto *entity = Entity( map, ids.pData[i] );
        if ( Member( entity, field ) == nullptr && KeyValue_ChildCount( entity ) >= MAP_ENTITY_RECORD_MEMBERS_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    }
    bool any = false;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        map_chunk_t *chunk{}; auto *entity = const_cast<key_value_t *>( Entity( map, ids.pData[i], &chunk ) );
        auto *member = Member( entity, field ); string_view_t old{};
        if ( KeyValue_GetString( member, &old ) && StringView_Equals( old, value ) ) { continue; }
        if ( member == nullptr ) { member = KeyValue_ObjectInsert( chunk->store.pDocument, entity, field, key_value_type_t::STRING ); }
        if ( member == nullptr || !KeyValue_SetString( chunk->store.pDocument, member, value ) ) { return map_status_t::OUT_OF_MEMORY; }
        any = true;
    }
    *changed = any;
    return map_status_t::OK;
}
} // namespace cypher::editor::map
