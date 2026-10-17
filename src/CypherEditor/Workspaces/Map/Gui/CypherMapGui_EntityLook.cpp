//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_EntityLook.cpp
//  Purpose: Implements entity boxes, colours and helpers from the game
//           profile.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_EntityLook.h"

#include "CypherMapGui_Game.h"
#include "CypherMap_EntityEdit.h"

#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QHash>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace cypher::editor::map
{
using namespace common;

namespace
{
using vec3 = math::vec3d_t;

QHash<const map_workspace_t *, QHash<u64, QColor>> &Colours()
{
    static QHash<const map_workspace_t *, QHash<u64, QColor>> colours;
    return colours;
}
// Helper lines worked out once per document (Refresh clears them): drawn
// for every entity, they must not read records on every repaint.
QHash<const map_workspace_t *, QHash<u64, std::vector<map_helper_line_t>>> &HelperCache()
{
    static QHash<const map_workspace_t *, QHash<u64, std::vector<map_helper_line_t>>> cache;
    return cache;
}
void Helpers( const map_workspace_t *ws, u64 entity, std::vector<map_helper_line_t> &out );
QHash<const map_workspace_t *, QSet<QByteArray>> &HiddenClasses()
{
    static QHash<const map_workspace_t *, QSet<QByteArray>> hidden;
    return hidden;
}

vec3 V( f64 x, f64 y, f64 z ) noexcept { return math::Vec3d_Make( x, y, z ); }
vec3 Plus( vec3 a, vec3 b ) noexcept { return V( a.x + b.x, a.y + b.y, a.z + b.z ); }
vec3 Minus( vec3 a, vec3 b ) noexcept { return V( a.x - b.x, a.y - b.y, a.z - b.z ); }
vec3 Times( vec3 a, f64 s ) noexcept { return V( a.x * s, a.y * s, a.z * s ); }
vec3 Cross( vec3 a, vec3 b ) noexcept { return V( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x ); }
f64 Length( vec3 a ) noexcept { return std::sqrt( a.x * a.x + a.y * a.y + a.z * a.z ); }
vec3 Unit( vec3 a ) noexcept { const f64 l = Length( a ); return l > 1e-12 ? Times( a, 1.0 / l ) : V( 1, 0, 0 ); }

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
    *pOut = V( c[0], c[1], c[2] );
    return true;
}

QString Text( const key_value_t *pValue )
{
    string_view_t text{};
    return KeyValue_GetString( pValue, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

// The entity's value of a class property: written, else the class default.
const key_value_t *Value( const key_value_t *pRecord, const map_game_class_t &cls, const std::string &key ) noexcept
{
    if ( key.empty() ) { return nullptr; }
    if ( const key_value_t *pWritten = KeyValue_Find( Member( pRecord, "properties" ), { key.data(), key.size() } ) ) { return pWritten; }
    const map_game_property_t *pProperty = MapGame_FindProperty( cls, { key.data(), key.size() } );
    return pProperty != nullptr ? pProperty->pDefault : nullptr;
}

f64 Real( const key_value_t *pRecord, const map_game_class_t &cls, const std::string &key, f64 fallback ) noexcept
{
    f64 value = fallback;
    return Number( Value( pRecord, cls, key ), &value ) ? value : fallback;
}

QColor ColourOf( const key_value_t *pRecord, const map_game_class_t *pClass )
{
    const QColor own( Text( Member( Member( pRecord, "editor" ), "color" ) ) );
    if ( own.isValid() ) { return own; }
    if ( pClass == nullptr ) { return QColor(); }
    if ( !pClass->colorFrom.empty() ) {
        const QColor fromProperty( Text( Value( pRecord, *pClass, pClass->colorFrom ) ) );
        if ( fromProperty.isValid() ) { return fromProperty; }
    }
    return pClass->color.empty() ? QColor() : QColor( QString::fromStdString( pClass->color ) );
}

map_wire_object_t *FindObject( map_wireframe_t &wire, u64 id ) noexcept
{
    map_wire_object_t *pBegin = wire.objects.pData;
    map_wire_object_t *pEnd = pBegin + wire.objects.nCount;
    map_wire_object_t *pAt = std::lower_bound( pBegin, pEnd, id, []( const map_wire_object_t &object, u64 value ) { return object.id < value; } );
    return pAt != pEnd && pAt->id == id ? pAt : nullptr;
}

void Segment( std::vector<map_helper_line_t> &out, vec3 a, vec3 b ) { out.push_back( { a, b } ); }

// An arrow from `from` along `direction` (unit) for `length`, its head
// flared in the plane holding the world's up (or X when it points up).
void Arrow( std::vector<map_helper_line_t> &out, vec3 from, vec3 direction, f64 length )
{
    const vec3 tip = Plus( from, Times( direction, length ) );
    Segment( out, from, tip );
    vec3 side = Cross( direction, V( 0, 0, 1 ) );
    if ( Length( side ) < 1e-6 ) { side = Cross( direction, V( 1, 0, 0 ) ); }
    side = Unit( side );
    const f64 head = std::min( 12.0, length * 0.3 );
    const vec3 back = Minus( tip, Times( direction, head ) );
    Segment( out, tip, Plus( back, Times( side, head * 0.5 ) ) );
    Segment( out, tip, Minus( back, Times( side, head * 0.5 ) ) );
}

void Circle( std::vector<map_helper_line_t> &out, vec3 centre, vec3 u, vec3 v, f64 radius )
{
    constexpr int kSegments = 48;
    vec3 previous = Plus( centre, Times( u, radius ) );
    for ( int i = 1; i <= kSegments; ++i ) {
        const f64 t = 2.0 * std::numbers::pi * static_cast<f64>( i ) / kSegments;
        const vec3 point = Plus( centre, Plus( Times( u, radius * std::cos( t ) ), Times( v, radius * std::sin( t ) ) ) );
        Segment( out, previous, point );
        previous = point;
    }
}

void Box( std::vector<map_helper_line_t> &out, vec3 lo, vec3 hi )
{
    vec3 c[8];
    for ( u32 i = 0; i < 8u; ++i ) { c[i] = V( ( i & 1u ) ? hi.x : lo.x, ( i & 2u ) ? hi.y : lo.y, ( i & 4u ) ? hi.z : lo.z ); }
    constexpr u32 kEdges[12][2]{ { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for ( const auto &edge : kEdges ) { Segment( out, c[edge[0]], c[edge[1]] ); }
}
} // namespace

vec3 MapEntityLook_Forward( vec3 angles ) noexcept
{
    const f64 pitch = angles.x * std::numbers::pi / 180.0, yaw = angles.y * std::numbers::pi / 180.0;
    return V( std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), -std::sin( pitch ) );
}

void MapEntityLook_Refresh( map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return; }
    try {
        QHash<u64, QColor> &colours = Colours()[ws];
        colours.clear();
        HelperCache().remove( ws );
        const map_game_profile_t *pProfile = MapGame_Profile( ws );
        const map_document_t &map = *ws->pDocument;
        for ( usize iStore = 0u; iStore < map.layerStores.nCount; ++iStore ) {
            const key_value_t *pRoot = KeyValue_Root( static_cast<const key_value_document_t *>( map.layerStores.pData[iStore]->store.pDocument ) );
            const key_value_t *pEntities = Member( pRoot, "entities" );
            for ( const key_value_t *pRecord = KeyValue_FirstChild( pEntities ); pRecord != nullptr; pRecord = KeyValue_NextSibling( pRecord ) ) {
                u64 id = 0u;
                if ( !KeyValue_GetU64( Member( pRecord, "id" ), &id ) || id == 0u ) { continue; }
                string_view_t className{};
                ( void )KeyValue_GetString( Member( pRecord, "class" ), &className );
                const map_game_class_t *pClass = pProfile != nullptr ? MapGame_FindClass( *pProfile, className ) : nullptr;
                if ( const QColor colour = ColourOf( pRecord, pClass ); colour.isValid() ) { colours.insert( id, colour ); }
                // The class box, else the default cube (so a reload that drops
                // a size puts the cube back).
                vec3 origin{};
                map_wire_object_t *pObject = FindObject( ws->wire, id );
                if ( pObject == nullptr || pObject->kind != map_wire_kind_t::ENTITY || !Vector( Member( pRecord, "origin" ), &origin ) ) { continue; }
                const bool sized = pClass != nullptr && pClass->bSize;
                const f64 h = MAP_WIRE_POINT_ENTITY_HALF;
                const vec3 lo = sized ? V( pClass->size[0], pClass->size[1], pClass->size[2] ) : V( -h, -h, -h );
                const vec3 hi = sized ? V( pClass->size[3], pClass->size[4], pClass->size[5] ) : V( h, h, h );
                pObject->bounds = {};
                MapBounds_AddPoint( pObject->bounds, Plus( origin, lo ) );
                MapBounds_AddPoint( pObject->bounds, Plus( origin, hi ) );
                MapBounds_AddBounds( ws->wire.bounds, pObject->bounds );
            }
        }
    } catch ( ... ) {
        CY_LOG_WRITE( Warning, Editor, "Entity looks could not be worked out; views use the default boxes" );
    }
}

void MapEntityLook_Forget( const map_workspace_t *ws ) noexcept { Colours().remove( ws ); HelperCache().remove( ws ); HiddenClasses().remove( ws ); }

void MapEntityLook_SetClassHidden( map_workspace_t *ws, const QString &className, bool hidden )
{
    if ( ws == nullptr || className.isEmpty() ) { return; }
    QSet<QByteArray> &set = HiddenClasses()[ws];
    if ( hidden ) { set.insert( className.toUtf8() ); } else { set.remove( className.toUtf8() ); }
    if ( set.isEmpty() ) { HiddenClasses().remove( ws ); }
    if ( hidden ) {
        // What can no longer be seen leaves the selection.
        std::vector<u64> kept;
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
            const u64 id = ws->selection.ids.pData[i];
            const map_wire_object_t *object = MapWireframe_FindObject( ws->wire, id );
            if ( object == nullptr || MapWorkspace_IsVisible( ws, *object ) ) { kept.push_back( id ); }
        }
        if ( kept.size() != ws->selection.ids.nCount ) { MapWorkspace_SetSelection( ws, kept.data(), kept.size() ); }
    }
    MapWorkspace_Notify( ws, MAP_CHANGE_VIEW );
}

bool MapEntityLook_IsClassHidden( const map_workspace_t *ws, const char *pClassName ) noexcept
{
    const auto it = HiddenClasses().constFind( ws );
    return it != HiddenClasses().constEnd() && pClassName != nullptr &&
        it.value().contains( QByteArray::fromRawData( pClassName, static_cast<qsizetype>( std::strlen( pClassName ) ) ) );
}

bool MapEntityLook_AnyClassHidden( const map_workspace_t *ws ) noexcept { return HiddenClasses().contains( ws ); }

QColor MapEntityLook_Colour( const map_workspace_t *ws, u64 entity ) noexcept
{
    const auto it = Colours().constFind( ws );
    return it != Colours().constEnd() ? it.value().value( entity ) : QColor();
}

void MapEntityLook_Helpers( const map_workspace_t *ws, u64 entity, std::vector<map_helper_line_t> &out )
{
    if ( ws == nullptr || ws->pDocument == nullptr ) { return; }
    auto &cache = HelperCache()[ws];
    auto it = cache.find( entity );
    if ( it == cache.end() ) {
        std::vector<map_helper_line_t> lines;
        Helpers( ws, entity, lines );
        it = cache.insert( entity, std::move( lines ) );
    }
    out.insert( out.end(), it.value().begin(), it.value().end() );
}

namespace
{
void Helpers( const map_workspace_t *ws, u64 entity, std::vector<map_helper_line_t> &out )
{
    const key_value_t *pRecord = MapEntityEdit_FindEntity( ws->pDocument, entity );
    if ( pRecord == nullptr ) { return; }
    string_view_t className{};
    ( void )KeyValue_GetString( Member( pRecord, "class" ), &className );
    const map_game_class_t *pClass = MapGame_Class( ws, className );
    if ( pClass == nullptr || pClass->helpers.empty() ) { return; }
    // Where helpers start: the origin, else the middle of the geometry the
    // entity owns (brush entities such as doors).
    map_bounds_t owned{};
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = ws->wire.objects.pData[i];
        if ( object.owner == entity && object.bounds.bHas ) { MapBounds_AddBounds( owned, object.bounds ); }
    }
    vec3 origin{};
    const bool hasOrigin = Vector( Member( pRecord, "origin" ), &origin );
    if ( !hasOrigin ) {
        if ( !owned.bHas ) { return; }
        origin = Times( Plus( owned.box.minimum, owned.box.maximum ), 0.5 );
    }
    const map_wire_object_t *pObject = MapWireframe_FindObject( ws->wire, entity );
    const map_bounds_t &body = owned.bHas ? owned : ( pObject != nullptr ? pObject->bounds : owned );
    const f64 bodySize = body.bHas ? std::max( { body.box.maximum.x - body.box.minimum.x, body.box.maximum.y - body.box.minimum.y, 16.0 } ) : 16.0;
    vec3 angles{};
    ( void )Vector( Member( pRecord, "angles" ), &angles );
    for ( const map_game_helper_t &helper : pClass->helpers ) {
        if ( helper.type == "arrow" ) {
            Arrow( out, origin, MapEntityLook_Forward( angles ), std::max( 32.0, bodySize * 1.5 ) );
        } else if ( helper.type == "radius" ) {
            const f64 radius = Real( pRecord, *pClass, helper.property, 0.0 );
            if ( radius <= 0.0 ) { continue; }
            Circle( out, origin, V( 1, 0, 0 ), V( 0, 1, 0 ), radius );
            Circle( out, origin, V( 1, 0, 0 ), V( 0, 0, 1 ), radius );
            Circle( out, origin, V( 0, 1, 0 ), V( 0, 0, 1 ), radius );
        } else if ( helper.type == "cone" ) {
            const f64 range = Real( pRecord, *pClass, helper.range, 256.0 );
            const f64 angle = std::clamp( Real( pRecord, *pClass, helper.angle, 45.0 ), 1.0, 179.0 );
            const vec3 forward = MapEntityLook_Forward( angles );
            vec3 u = Cross( forward, V( 0, 0, 1 ) );
            if ( Length( u ) < 1e-6 ) { u = Cross( forward, V( 1, 0, 0 ) ); }
            u = Unit( u );
            const vec3 v = Unit( Cross( u, forward ) );
            // `angle` is the cone's full opening.
            const f64 spread = range * std::tan( angle * 0.5 * std::numbers::pi / 180.0 );
            const vec3 centre = Plus( origin, Times( forward, range ) );
            Circle( out, centre, u, v, spread );
            for ( int i = 0; i < 4; ++i ) {
                const f64 t = 0.5 * std::numbers::pi * i;
                Segment( out, origin, Plus( centre, Plus( Times( u, spread * std::cos( t ) ), Times( v, spread * std::sin( t ) ) ) ) );
            }
        } else if ( helper.type == "line_to" ) {
            const QString target = Text( Value( pRecord, *pClass, helper.property ) );
            if ( target.isEmpty() ) { continue; }
            const QByteArray utf8 = target.toUtf8();
            for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
                const map_wire_entity_t &other = ws->wire.entities.pData[i];
                if ( other.id != entity && other.bHasOrigin && std::strcmp( other.name, utf8.constData() ) == 0 ) { Segment( out, origin, other.origin ); }
            }
        } else if ( helper.type == "move_direction" ) {
            vec3 moveAngles{};
            if ( !Vector( Value( pRecord, *pClass, helper.property ), &moveAngles ) ) { continue; }
            const vec3 direction = MapEntityLook_Forward( moveAngles );
            // A door travels its own depth along the direction, less the lip.
            const vec3 size = body.bHas ? Minus( body.box.maximum, body.box.minimum ) : V( 16, 16, 16 );
            const f64 travel = std::max( 0.0, std::fabs( direction.x ) * size.x + std::fabs( direction.y ) * size.y + std::fabs( direction.z ) * size.z -
                                                  Real( pRecord, *pClass, helper.distance, 0.0 ) );
            if ( travel <= 0.0 ) { continue; }
            Arrow( out, origin, direction, travel );
            if ( body.bHas ) { Box( out, Plus( body.box.minimum, Times( direction, travel ) ), Plus( body.box.maximum, Times( direction, travel ) ) ); }
        } else if ( helper.type == "box" ) {
            vec3 size{};
            if ( !Vector( Value( pRecord, *pClass, helper.property ), &size ) ) { continue; }
            const vec3 half = Times( size, 0.5 );
            Box( out, Minus( origin, half ), Plus( origin, half ) );
        }
    }
}
} // namespace

} // namespace cypher::editor::map
